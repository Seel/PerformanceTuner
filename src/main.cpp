#include "pch.h"
#include "Config.h"
#include "Tuner.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
	std::unique_ptr<PerfTuner::Tuner> g_tuner;

	// F4SE 0.6.x (the OG 1.10.163 generation) has no AddTaskPermanent, and its task queue is
	// drained in a while loop, so a task must never re-queue itself. A small pacer thread adds
	// at most one task per frame instead; the task itself still runs on the main thread.
	std::atomic_bool g_tickerStop{ false };
	std::atomic_bool g_tickPending{ false };

	void OnFrame()
	{
		if (g_tuner) {
			g_tuner->Update();
		}
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

	bool RegisterFrameCallback()
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

		REX::INFO(
			"F4SE {} detected (OG generation): using the paced task tick fallback.",
			F4SE::GetF4SEVersion().string());

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

			if (!RegisterFrameCallback()) {
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
