#include "pch.h"
#include "Tuner.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <vector>

namespace
{
	constexpr float kMaximumFrameTime = 0.5f;  // seconds; longer frames are treated as a hitch
	constexpr float kGodrayGridHysteresis = 150.0f;  // world units between grid changes

	// The `gr grid` console command only stores an int into an engine global that the
	// renderer reads every frame, but it also echoes in the console. Find that global once
	// by watching which int in .data follows the value across two command invocations; after
	// that the grid is written directly, silently and without command parsing.
	constexpr int kGridCalibrationLow = 6;
	constexpr int kGridCalibrationHigh = 13;

	std::int32_t* g_godrayGridGlobal{ nullptr };
	bool          g_godrayGridCalibrationDone{ false };

	void SetGodrayGrid(int a_value)
	{
		if (g_godrayGridGlobal) {
			*g_godrayGridGlobal = a_value;
			return;
		}

		RE::Console::ExecuteCommand(std::format("gr grid {}", a_value).c_str());
	}

	void CalibrateGodrayGrid()
	{
		if (g_godrayGridCalibrationDone) {
			return;
		}

		g_godrayGridCalibrationDone = true;

		const auto module = REX::FModule::GetExecutingModule();
		const auto data = module.GetSection(".data");
		const auto begin = reinterpret_cast<std::uintptr_t>(data.GetPointer<std::byte>());
		const auto size = data.GetSize();

		if (!begin || size < sizeof(std::int32_t)) {
			REX::WARN("Cannot locate the godray grid global: no usable .data section."sv);
			return;
		}

		SetGodrayGrid(kGridCalibrationLow);

		std::vector<std::int32_t*> candidates;
		candidates.reserve(65536);
		for (std::size_t offset = 0; offset + sizeof(std::int32_t) <= size; offset += sizeof(std::int32_t)) {
			auto* value = reinterpret_cast<std::int32_t*>(begin + offset);
			if (*value == kGridCalibrationLow) {
				candidates.push_back(value);
			}
		}

		SetGodrayGrid(kGridCalibrationHigh);
		candidates.erase(
			std::remove_if(candidates.begin(), candidates.end(), [](const std::int32_t* a_value) {
				return *a_value != kGridCalibrationHigh;
			}),
			candidates.end());

		SetGodrayGrid(kGridCalibrationLow);
		candidates.erase(
			std::remove_if(candidates.begin(), candidates.end(), [](const std::int32_t* a_value) {
				return *a_value != kGridCalibrationLow;
			}),
			candidates.end());

		if (candidates.size() == 1) {
			g_godrayGridGlobal = candidates.front();
			REX::INFO(
				"Godray grid global located at 0x{:X}.",
				reinterpret_cast<std::uintptr_t>(g_godrayGridGlobal));
		} else {
			REX::WARN(
				"Godray grid calibration found {} candidate(s); falling back to console commands.",
				candidates.size());
		}
	}

	// The tuner must not fight the game while it is not rendering the world.
	// These menus either pause the simulation or replace the rendered scene.
	constexpr std::array kPauseMenus{
		"MainMenu",
		"LoadingMenu",
		"PauseMenu",
		"PipboyMenu",
		"SleepWaitMenu",
	};

	[[nodiscard]] bool IsPaused()
	{
		static const std::array<RE::BSFixedString, kPauseMenus.size()> kMenus{
			RE::BSFixedString(kPauseMenus[0]),
			RE::BSFixedString(kPauseMenus[1]),
			RE::BSFixedString(kPauseMenus[2]),
			RE::BSFixedString(kPauseMenus[3]),
			RE::BSFixedString(kPauseMenus[4]),
		};

		const auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return true;
		}

		for (const auto& menu : kMenus) {
			if (ui->GetMenuOpen(menu)) {
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] RE::Setting* GetDirectionalShadowDistanceSetting()
	{
		auto* setting = RE::ShadowSettings::GetDirShadowDistance();
		if (!setting || setting->GetType() != RE::Setting::SETTING_TYPE::kFloat) {
			return nullptr;
		}

		return setting;
	}

	// The renderer does not read the INI setting while the game is running:
	// BSShaderManager caches the value during initialization and the shadow code uses the cache.
	// Writing the Setting object alone therefore has no visible effect, which is exactly what the
	// original mod worked around by patching this cached value directly.
	[[nodiscard]] float& CachedShadowDistance()
	{
		return RE::BSShaderManager::GetCachedDirectionalShadowDistance();
	}

	[[nodiscard]] float GetFrameTime()
	{
		const auto* timer = RE::BSTimer::GetSingleton();
		return timer ? timer->realTimeDelta : 0.0f;
	}
}

namespace PerfTuner
{
	Tuner::Tuner(Config a_config) :
		_config(std::move(a_config))
	{
	}

	void Tuner::Update()
	{
		const auto  now = std::chrono::steady_clock::now();
		const float frameTime = GetFrameTime();
		const bool  paused = IsPaused();

		if (paused) {
			_wasPaused = true;
			LogDiagnostics(frameTime, true, false);
			return;
		}

		if (_wasPaused) {
			_wasPaused = false;
			_resumeAt = now;
			ResetSamples();

			// The engine may recreate godray volumes when a world loads, so re-apply the
			// current quality/grid choices instead of assuming they survived.
			_lastGodrayQuality = -1;
			_lastGodrayGrid = -1;
		}

		// Streaming and physics settle right after a load; adjusting then would chase
		// hitching that has nothing to do with shadow load.
		if (now - _resumeAt < std::chrono::duration<float>(_config.resumeGraceTime)) {
			LogDiagnostics(frameTime, false, true);
			return;
		}

		if (frameTime <= 0.0f || frameTime > kMaximumFrameTime) {
			LogDiagnostics(frameTime, false, false);
			return;
		}

		AddSample(now, frameTime);
		_load = MedianFrameTime(now) / _config.TargetFrameTime();
		ApplyAdjustment();
		LogDiagnostics(frameTime, false, false);
	}

	void Tuner::ResetSamples()
	{
		_samples = {};
		_sampleIndex = 0;
		_momentum = 1.0f;
	}

	void Tuner::AddSample(std::chrono::steady_clock::time_point a_now, float a_frameTime)
	{
		_samples[_sampleIndex] = { a_frameTime, a_now };
		_sampleIndex = (_sampleIndex + 1) % _samples.size();
	}

	float Tuner::MedianFrameTime(std::chrono::steady_clock::time_point a_now) const noexcept
	{
		// Only samples inside the time window count, so a burst of hitching shorter than the
		// window cannot move the median. This keeps camera turns and streaming spikes from
		// being mistaken for sustained load.
		const auto cutoff = a_now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
		                              std::chrono::duration<float>(_config.loadWindowTime));

		std::array<float, 512> scratch{};
		std::size_t            count = 0;
		for (const auto& sample : _samples) {
			if (sample.at >= cutoff && sample.seconds > 0.0f) {
				scratch[count++] = sample.seconds;
			}
		}

		if (count == 0) {
			return 0.0f;
		}

		std::sort(scratch.begin(), scratch.begin() + count);

		const auto middle = count / 2;
		const auto median = count % 2 == 0 ?
		                        (scratch[middle - 1] + scratch[middle]) * 0.5f :
		                        scratch[middle];

		return median * 1000.0f;
	}

	void Tuner::ApplyAdjustment()
	{
		// Grow while the frame rate is above maxFPS (load below 1), pull in while it is
		// below minFPS (load above maxFPS / minFPS). In between, hold.
		constexpr float lower = 1.0f;
		const float     upper = _config.ShrinkLoadThreshold();

		if (_load > upper) {
			// Over budget: react quickly, every extra millisecond is felt.
			_momentum = _momentum >= 0.0f ?
			                -2.0f :
			                std::max(_momentum * _config.momentumDownAcceleration, _config.minMomentum);
		} else if (_load < lower) {
			// Under budget: recover slowly so a single fast frame does not overshoot.
			_momentum = _momentum <= 0.0f ?
			                1.0f :
			                std::min(_momentum * _config.momentumUpAcceleration, _config.maxMomentum);
		} else {
			_momentum = 0.0f;  // inside the tolerance band: hold the current distance
		}

		const float current = CachedShadowDistance();
		const float next = std::clamp(current + _momentum, _config.minDistance, _config.maxDistance);
		if (next != current) {
			CachedShadowDistance() = next;
		}

		if (_config.adjustGodrayQuality) {
			ApplyGodrayQuality(next);
		}

		if (_config.adjustGodrayGrid) {
			ApplyGodrayGrid(next);
		}
	}

	void Tuner::ApplyGodrayGrid(float a_shadowDistance)
	{
		if (!g_godrayGridCalibrationDone) {
			CalibrateGodrayGrid();
		}

		const auto minGrid = static_cast<float>(_config.minGodrayGrid);
		const auto maxGrid = static_cast<float>(_config.maxGodrayGrid);

		const float span = _config.maxDistance - _config.minDistance;
		const float progress = span > 0.0f ?
		                           std::clamp((a_shadowDistance - _config.minDistance) / span, 0.0f, 1.0f) :
		                           1.0f;

		const int target = std::clamp(
			[&] {
				int value = static_cast<int>(std::lround(minGrid + progress * (maxGrid - minGrid)));
				if (_config.godrayGridStep > 1) {
					value = _config.minGodrayGrid +
					        ((value - _config.minGodrayGrid) / _config.godrayGridStep) * _config.godrayGridStep;
				}
				return value;
			}(),
			_config.minGodrayGrid,
			_config.maxGodrayGrid);

		if (target == _lastGodrayGrid) {
			return;
		}

		// A grid step spans a wide distance range; require some movement away from the last
		// change so the beam quality does not flicker while the distance hovers on a boundary.
		if (_lastGodrayGrid >= 0 && std::abs(a_shadowDistance - _lastGodrayGridDistance) < kGodrayGridHysteresis) {
			return;
		}

		_lastGodrayGrid = target;
		_lastGodrayGridDistance = a_shadowDistance;

		REX::INFO("Applying godray grid {}.", target);
		SetGodrayGrid(target);
	}

	void Tuner::ApplyGodrayQuality(float a_shadowDistance)
	{
		const int quality = std::clamp(
			_config.GodrayQualityFor(a_shadowDistance),
			_config.minGodrayQuality,
			_config.maxGodrayQuality);

		if (quality == _lastGodrayQuality) {
			return;
		}

		_lastGodrayQuality = quality;

		// The renderer keeps its own copy of the godray settings, so apply the change the same
		// way the console command does instead of editing the INI setting.
		REX::INFO("Applying godray quality {}.", quality);
		RE::Console::ExecuteCommand(std::format("gr quality {}", quality).c_str());
	}

	void Tuner::LogDiagnostics(float a_frameTime, bool a_paused, bool a_grace)
	{
		if (!_config.showDiagnostics) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now - _lastDiagnostic < std::chrono::duration<float>(_config.diagnosticInterval)) {
			return;
		}

		_lastDiagnostic = now;

		const auto* setting = GetDirectionalShadowDistanceSetting();
		const auto* timer = RE::BSTimer::GetSingleton();
		const auto* godray = RE::GetINISetting("iVolumetricLightingQuality"sv);

		const float median = MedianFrameTime(now);
		const float fps = median > 0.0f ? 1000.0f / median : 0.0f;

		REX::INFO(
			"cached={:.0f} setting={:.0f} momentum={:+.2f} load={:.1f}% fps={:.1f} frame={:.2f}ms median={:.2f}ms gameDelta={:.2f}ms paused={} grace={} gr={} grSetting={} grid={}",
			CachedShadowDistance(),
			setting ? setting->GetFloat() : 0.0f,
			_momentum,
			_load * 100.0f,
			fps,
			a_frameTime * 1000.0f,
			median,
			timer ? timer->delta * 1000.0f : 0.0f,
			a_paused,
			a_grace,
			_config.adjustGodrayQuality ? _config.GodrayQualityFor(CachedShadowDistance()) : -1,
			godray && godray->GetType() == RE::Setting::SETTING_TYPE::kInt ? godray->GetInt() : -1,
			_lastGodrayGrid);
	}
}


