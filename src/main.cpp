#include "pch.h"
#include "Config.h"
#include "Tuner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
	std::unique_ptr<PerfTuner::Tuner> g_tuner;

	// F4SE 0.6.x (OG 1.10.163) has no AddTaskPermanent, and its task queue is drained in a
	// while loop, so a task must never re-queue itself. The main loop calls Main::OnIdle once
	// per frame; hooking that call site is the cheap way to get a frame tick.
	using OnIdleFn = void (*)(void*);

	OnIdleFn          g_onIdleOriginal{ nullptr };
	REL::Trampoline*  g_ogTrampoline{ nullptr };

	std::atomic_bool g_tickerStop{ false };
	std::atomic_bool g_tickPending{ false };

	void OnFrame()
	{
		if (g_tuner) {
			g_tuner->Update();
		}
	}

	void OnIdleHook(void* a_this)
	{
		if (g_onIdleOriginal) {
			g_onIdleOriginal(a_this);
		}

		OnFrame();
	}

	[[nodiscard]] std::uintptr_t ScanForCall(
		const std::uintptr_t a_begin,
		const std::uintptr_t a_end,
		const std::uintptr_t a_target,
		int*                 a_count = nullptr)
	{
		std::uintptr_t first = 0;
		int            count = 0;

		for (auto address = a_begin; address + 5 <= a_end; ++address) {
			if (*reinterpret_cast<const std::uint8_t*>(address) != 0xE8) {
				continue;
			}

			std::int32_t relative{};
			std::memcpy(&relative, reinterpret_cast<const void*>(address + 1), sizeof(relative));
			if (address + 5 + static_cast<std::intptr_t>(relative) != a_target) {
				continue;
			}

			if (!first) {
				first = address;
			}
			++count;
		}

		if (a_count) {
			*a_count = count;
		}

		return first;
	}

	[[nodiscard]] std::uintptr_t FindOnIdleCallSite()
	{
		const auto onIdle = REL::Relocation<std::uintptr_t>{ RE::ID::Main::OnIdle }.address();

		const auto module = REX::FModule::GetExecutingModule();
		const auto text = module.GetSection(".text");
		const auto textBegin = reinterpret_cast<std::uintptr_t>(text.GetPointer<std::byte>());
		const auto textEnd = textBegin + text.GetSize();

		// Prefer the known main-loop functions.
		const auto run = REL::Relocation<std::uintptr_t>{ RE::ID::Main::Run }.address();
		if (const auto callsite = ScanForCall(run, std::min(run + 0x4000, textEnd), onIdle)) {
			return callsite;
		}

		const auto loop = REL::Relocation<std::uintptr_t>{ RE::ID::Main::Run_WindowsMessageLoop }.address();
		if (const auto callsite = ScanForCall(loop, std::min(loop + 0x4000, textEnd), onIdle)) {
			return callsite;
		}

		// Last resort: the whole text section, but only when the call is unambiguous.
		int            count = 0;
		const auto     callsite = ScanForCall(textBegin, textEnd, onIdle, &count);
		if (callsite && count == 1) {
			return callsite;
		}

		REX::WARN("Found {} call(s) to Main::OnIdle outside the main loop; not hooking.", count);
		return 0;
	}

	bool InstallOnIdleHook()
	{
		const auto callsite = FindOnIdleCallSite();
		if (!callsite) {
			return false;
		}

		g_ogTrampoline = new REL::Trampoline("PerformanceTuner"sv);
		g_ogTrampoline->create(0x100);

		g_onIdleOriginal = reinterpret_cast<OnIdleFn>(g_ogTrampoline->write_call<5>(callsite, &OnIdleHook));

		REX::INFO("Hooked the Main::OnIdle call at 0x{:X}.", callsite);
		return true;
	}

	void LegacyTick()
	{
		g_tickPending.store(false, std::memory_order_release);
		OnFrame();
	}

	void RunLegacyTicker(const F4SE::TaskInterface* a_tasks)
	{
		using namespace std::chrono_literals;

		while (!g_tickerStop.load(std::memory_order_acquire)) {
			if (!g_tickPending.exchange(true, std::memory_order_acq_rel)) {
				a_tasks->AddTask(LegacyTick);
			}

			std::this_thread::sleep_for(2ms);
		}
	}

	bool RegisterFrameCallback(const bool a_useOnIdleHook)
	{
		const auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			REX::ERROR("The F4SE task interface is unavailable; tuning disabled."sv);
			return false;
		}

		if (F4SE::GetF4SEVersion() >= REL::Version{ 0, 7, 0, 0 }) {
			tasks->AddTaskPermanent(OnFrame);
			REX::INFO("Using the F4SE permanent task tick."sv);
			return true;
		}

		REX::INFO("F4SE {} detected (OG generation).", F4SE::GetF4SEVersion().string());

		if (a_useOnIdleHook && InstallOnIdleHook()) {
			return true;
		}

		REX::WARN("Falling back to the paced task tick for this session."sv);
		std::thread(RunLegacyTicker, tasks).detach();

		return true;
	}

	bool Initialize(const F4SE::LoadInterface* a_f4se)
	{
		static std::once_flag once;
		static bool         initialized = false;

		std::call_once(once, [&] {
			F4SE::Init(a_f4se);

			const auto modulePath = std::filesystem::path(REX::FModule::GetCurrentModule().GetFileName());
			const auto configPath = modulePath.parent_path() / "PerformanceTuner.ini";

			auto config = PerfTuner::Config::Load(configPath);
			REX::INFO(
				"Target band {:.0f}-{:.0f} FPS ({:.2f} ms at max), distance {:.0f}..{:.0f}, godrays {}.",
				config.minFPS,
				config.maxFPS,
				config.TargetFrameTime(),
				config.minDistance,
				config.maxDistance,
				config.adjustGodrayQuality ? "on" : "off");

			g_tuner = std::make_unique<PerfTuner::Tuner>(std::move(config));

			if (!RegisterFrameCallback(config.useOnIdleHook)) {
				return;
			}

			initialized = true;
			REX::INFO("{} v{}.{}.{} initialized on runtime {}.",
				PLUGIN_NAME,
				PLUGIN_VERSION_MAJOR,
				PLUGIN_VERSION_MINOR,
				PLUGIN_VERSION_PATCH,
				F4SE::GetRuntimeVersion().string());
		});

		return initialized;
	}
}

F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	if (const auto* data = F4SE::PluginVersionData::GetSingleton()) {
		a_info->infoVersion = F4SE::PluginInfo::kVersion;
		a_info->name = data->GetPluginName().data();
		a_info->version = data->GetPluginVersion().pack();
	}

	return a_f4se->RuntimeVersion() >= REL::Version(F4SE::RUNTIME_1_10_163);
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	return Initialize(a_f4se);
}
