#pragma once

#include <array>
#include <filesystem>

namespace PerfTuner
{
	struct Config
	{
		// [Simple]
		float              maxFPS{ 70.0f };
		float              minFPS{ 64.0f };
		float              momentumDownAcceleration{ 1.1f };
		float              minMomentum{ -64.0f };
		float              momentumUpAcceleration{ 1.05f };
		float              maxMomentum{ 1.0f };
		float              minDistance{ 3000.0f };
		float              maxDistance{ 8000.0f };
		bool               adjustGodrayQuality{ false };
		int                minGodrayQuality{ 0 };
		int                maxGodrayQuality{ 3 };
		std::array<float, 3> godrayQualityDistances{ 3000.0f, 6000.0f, 10000.0f };
		bool               adjustGodrayGrid{ false };
		int                minGodrayGrid{ 6 };
		int                maxGodrayGrid{ 16 };

		// [Advanced]
		bool  showDiagnostics{ false };
		float diagnosticInterval{ 1.0f };
		float resumeGraceTime{ 1.0f };
		float loadWindowTime{ 2.0f };

		// 100% load is one frame at maxFPS.
		[[nodiscard]] float TargetFrameTime() const noexcept
		{
			return 1000.0f / maxFPS;
		}

		// Load above this means the frame rate dropped below minFPS.
		[[nodiscard]] float ShrinkLoadThreshold() const noexcept
		{
			return maxFPS / minFPS;
		}

		[[nodiscard]] int GodrayQualityFor(float a_shadowDistance) const noexcept;

		static Config Load(const std::filesystem::path& a_path);
	};
}
