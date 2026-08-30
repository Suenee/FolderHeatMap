#include "HeatModel.h"

#include <algorithm>
#include <cmath>

namespace fhm {

const char* DualTimescaleActivityModel::Id() const noexcept {
    return "dual_timescale_activity";
}

const char* DualTimescaleActivityModel::DisplayName() const noexcept {
    return "Dual-Timescale Activity";
}

double DualTimescaleActivityModel::EffectiveHalfLifeDays(const CoolingInput& input) const {
    if (!input.automatic) return std::clamp(input.configuredHalfLifeDays, 1.0, 365.0);
    constexpr int window = 60;
    if (input.activeDaysIn60 < 7) return 30.0;
    const double activeFraction = static_cast<double>(input.activeDaysIn60) / window;
    return std::clamp(12.0 / std::max(activeFraction, 1.0 / window), 7.0, 180.0);
}

double DualTimescaleActivityModel::RecentFolderHeat(const FolderHeatInput& input) {
    if (!input.recentVisits || !input.hasLastEffectiveVisit) return 0.0;
    constexpr double targetVisits = 24.0;
    const double activity = std::log1p(static_cast<double>(input.recentVisits)) / std::log1p(targetVisits);
    const double base = 5.8 * std::clamp(activity, 0.0, 1.0);
    const double recentHalfLife = std::clamp(input.halfLifeDays * 0.18, 0.5, 14.0);
    const double recency = std::exp(-std::log(2.0) * input.daysSinceLastEffectiveVisit / recentHalfLife);
    return std::clamp(base * recency, 0.0, 5.8);
}

double DualTimescaleActivityModel::HabitFolderHeat(const FolderHeatInput& input) {
    if (!input.activeDays || !input.hasLastEffectiveVisit) return 0.0;
    const double frequency = std::clamp(static_cast<double>(input.activeDays) / std::max(1.0, input.spanDays), 0.0, 1.0);
    const double maturity = 1.0 - std::exp(-static_cast<double>(input.activeDays) / 6.0);
    const double regularity = 0.45 + 0.55 * std::sqrt(frequency);
    const double base = 5.2 * maturity * regularity;
    const double habitHalfLife = std::clamp(input.halfLifeDays * 3.0, 14.0, 540.0);
    const double recency = std::exp(-std::log(2.0) * input.daysSinceLastEffectiveVisit / habitHalfLife);
    return std::clamp(base * recency, 0.0, 5.2);
}

double DualTimescaleActivityModel::CalculateFolderHeat(const FolderHeatInput& input) const {
    if (!input.visits) return 0.0;
    const double recent = RecentFolderHeat(input);
    const double habit = HabitFolderHeat(input);
    const double high = std::max(recent, habit);
    const double low = std::min(recent, habit);
    return std::clamp(high + 0.30 * low, 0.0, 7.0);
}

double DualTimescaleActivityModel::CalculateFileHeat(const FileHeatInput& input) const {
    if (!input.writeEvents || !input.hasLastWrite) return 0.0;
    constexpr double targetWrites = 12.0;
    const double activity = std::log1p(static_cast<double>(input.writeEvents)) / std::log1p(targetWrites);
    const double recentBase = 6.2 * std::clamp(activity, 0.0, 1.0);
    const double writeHalfLife = std::clamp(input.halfLifeDays * 0.14, 0.5, 21.0);
    const double recency = std::exp(-std::log(2.0) * input.daysSinceLastWrite / writeHalfLife);
    const double recent = recentBase * recency;
    const double frequency = std::clamp(static_cast<double>(input.activeDays) / std::max(1.0, input.spanDays), 0.0, 1.0);
    const double maturity = 1.0 - std::exp(-static_cast<double>(input.writeEvents) / 8.0);
    const double habitBase = 4.4 * maturity * (0.45 + 0.55 * std::sqrt(frequency));
    const double habitHalfLife = std::clamp(input.halfLifeDays * 2.0, 10.0, 365.0);
    const double habit = habitBase * std::exp(-std::log(2.0) * input.daysSinceLastWrite / habitHalfLife);
    const double high = std::max(recent, habit);
    const double low = std::min(recent, habit);
    return std::clamp(high + 0.22 * low, 0.0, 7.0);
}

} // namespace fhm
