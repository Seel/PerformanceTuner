#include "pch.h"
#include "Config.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace
{
	[[nodiscard]] std::string_view Trim(std::string_view a_text) noexcept
	{
		const auto first = a_text.find_first_not_of(" \t\r\n"sv);
		if (first == std::string_view::npos) {
			return {};
		}

		const auto last = a_text.find_last_not_of(" \t\r\n"sv);
		return a_text.substr(first, last - first + 1);
	}

	[[nodiscard]] bool IEquals(std::string_view a_lhs, std::string_view a_rhs) noexcept
	{
		if (a_lhs.size() != a_rhs.size()) {
			return false;
		}

		for (std::size_t i = 0; i < a_lhs.size(); ++i) {
			const auto lhs = static_cast<unsigned char>(a_lhs[i]);
			const auto rhs = static_cast<unsigned char>(a_rhs[i]);
			if (std::tolower(lhs) != std::tolower(rhs)) {
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] std::optional<float> ParseFloat(std::string_view a_text) noexcept
	{
		float      value{};
		const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
		if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size()) {
			return std::nullopt;
		}

		return value;
	}

	[[nodiscard]] bool ParseBool(std::string_view a_text) noexcept
	{
		if (const auto value = ParseFloat(a_text)) {
			return *value != 0.0f;
		}

		return IEquals(a_text, "true"sv) || IEquals(a_text, "yes"sv) || IEquals(a_text, "on"sv);
	}

	void Apply(PerfTuner::Config& a_config, std::string_view a_key, std::string_view a_value)
	{
		if (IEquals(a_key, "fMaxFPS"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.maxFPS = *value;
			}
		} else if (IEquals(a_key, "fMinFPS"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.minFPS = *value;
			}
		} else if (IEquals(a_key, "fMomentumDownAcceleration"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.momentumDownAcceleration = std::abs(*value);
			}
		} else if (IEquals(a_key, "fMinMomentum"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.minMomentum = -std::abs(*value);
			}
		} else if (IEquals(a_key, "fMomentumUpAcceleration"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.momentumUpAcceleration = std::abs(*value);
			}
		} else if (IEquals(a_key, "fMaxMomentum"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.maxMomentum = std::abs(*value);
			}
		} else if (IEquals(a_key, "fShadowDirDistanceMin"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.minDistance = *value;
			}
		} else if (IEquals(a_key, "fShadowDirDistanceMax"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.maxDistance = *value;
			}
		} else if (IEquals(a_key, "bAdjustGRQuality"sv)) {
			a_config.adjustGodrayQuality = ParseBool(a_value);
		} else if (IEquals(a_key, "iMinGRQuality"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.minGodrayQuality = static_cast<int>(*value);
			}
		} else if (IEquals(a_key, "iMaxGRQuality"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.maxGodrayQuality = static_cast<int>(*value);
			}
		} else if (IEquals(a_key, "bAdjustGRGrid"sv)) {
			a_config.adjustGodrayGrid = ParseBool(a_value);
		} else if (IEquals(a_key, "iGRGridMin"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.minGodrayGrid = static_cast<int>(*value);
			}
		} else if (IEquals(a_key, "iGRGridMax"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.maxGodrayGrid = static_cast<int>(*value);
			}
		} else if (IEquals(a_key, "iGRGridStep"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.godrayGridStep = static_cast<int>(*value);
			}
		} else if (IEquals(a_key, "fGRQualityShadowDist1"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.godrayQualityDistances[0] = *value;
			}
		} else if (IEquals(a_key, "fGRQualityShadowDist2"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.godrayQualityDistances[1] = *value;
			}
		} else if (IEquals(a_key, "fGRQualityShadowDist3"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.godrayQualityDistances[2] = *value;
			}
		} else if (IEquals(a_key, "bShowDiagnostics"sv)) {
			a_config.showDiagnostics = ParseBool(a_value);
		} else if (IEquals(a_key, "fDebugMessageFrequency"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.diagnosticInterval = 1.0f / std::max(*value, 0.01f);
			}
		} else if (IEquals(a_key, "fResumeGraceTime"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.resumeGraceTime = *value;
			}
		} else if (IEquals(a_key, "fLoadWindowTime"sv)) {
			if (const auto value = ParseFloat(a_value)) {
				a_config.loadWindowTime = *value;
			}
		}
	}

	void Validate(PerfTuner::Config& a_config)
	{
		a_config.maxFPS = std::clamp(a_config.maxFPS, 10.0f, 1000.0f);
		// Keep a minimum band, otherwise every frame flips the direction.
		a_config.minFPS = std::clamp(a_config.minFPS, 1.0f, a_config.maxFPS * 0.95f);

		a_config.momentumDownAcceleration = std::max(a_config.momentumDownAcceleration, 1.0f);
		a_config.momentumUpAcceleration = std::max(a_config.momentumUpAcceleration, 1.0f);
		a_config.minMomentum = std::min(a_config.minMomentum, 0.0f);
		a_config.maxMomentum = std::max(a_config.maxMomentum, 0.0f);

		a_config.minDistance = std::max(a_config.minDistance, 0.0f);
		a_config.maxDistance = std::max(a_config.maxDistance, a_config.minDistance);

		std::ranges::sort(a_config.godrayQualityDistances);
		a_config.minGodrayQuality = std::clamp(a_config.minGodrayQuality, 0, 3);
		a_config.maxGodrayQuality = std::clamp(a_config.maxGodrayQuality, a_config.minGodrayQuality, 3);
		a_config.minGodrayGrid = std::clamp(a_config.minGodrayGrid, 1, 64);
		a_config.maxGodrayGrid = std::clamp(a_config.maxGodrayGrid, a_config.minGodrayGrid, 64);
		a_config.godrayGridStep = std::clamp(a_config.godrayGridStep, 1, 64);
		a_config.diagnosticInterval = std::max(a_config.diagnosticInterval, 0.1f);
		a_config.resumeGraceTime = std::clamp(a_config.resumeGraceTime, 0.0f, 10.0f);
		a_config.loadWindowTime = std::clamp(a_config.loadWindowTime, 0.1f, 5.0f);
	}
}

int PerfTuner::Config::GodrayQualityFor(float a_shadowDistance) const noexcept
{
	for (std::size_t i = 0; i < godrayQualityDistances.size(); ++i) {
		if (a_shadowDistance < godrayQualityDistances[i]) {
			return static_cast<int>(i);
		}
	}

	return static_cast<int>(godrayQualityDistances.size());
}

PerfTuner::Config PerfTuner::Config::Load(const std::filesystem::path& a_path)
{
	Config config;

	std::ifstream file(a_path);
	if (!file) {
		REX::WARN("Configuration file not found, using defaults: {}", a_path.string());
		return config;
	}

	std::string line;
	while (std::getline(file, line)) {
		const auto text = Trim(line);
		if (text.empty() || text.front() == ';' || text.front() == '#' || text.front() == '[') {
			continue;
		}

		const auto equals = text.find('=');
		if (equals == std::string_view::npos) {
			continue;
		}

		Apply(config, Trim(text.substr(0, equals)), Trim(text.substr(equals + 1)));
	}

	Validate(config);
	REX::INFO("Configuration loaded from {}.", a_path.string());

	return config;
}
