#pragma once

#include "Config.h"

#include <array>
#include <chrono>

namespace PerfTuner
{
	struct FrameSample
	{
		float                                 seconds{ 0.0f };
		std::chrono::steady_clock::time_point at{};
	};

	class Tuner
	{
	public:
		explicit Tuner(Config a_config);

		void Update();

	private:
		void ResetSamples();
		void AddSample(std::chrono::steady_clock::time_point a_now, float a_frameTime);
		[[nodiscard]] float MedianFrameTime(std::chrono::steady_clock::time_point a_now) const noexcept;
		void ApplyAdjustment();
		void ApplyGodrayQuality(float a_shadowDistance);
		void LogDiagnostics(float a_frameTime, bool a_paused, bool a_grace);

		Config                                _config;
		std::array<FrameSample, 512>          _samples{};
		std::size_t                           _sampleIndex{ 0 };
		float                                 _momentum{ 1.0f };
		float                                 _load{ 0.0f };
		bool                                  _wasPaused{ true };
		bool                                  _warnedMissingSetting{ false };
		int                                   _lastGodrayQuality{ -1 };
		std::chrono::steady_clock::time_point _resumeAt{};
		std::chrono::steady_clock::time_point _lastDiagnostic{};
	};
}
