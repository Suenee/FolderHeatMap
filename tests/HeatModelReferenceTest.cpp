#include "HeatModelFixtures.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

constexpr double kTolerance = 1e-9;

// Frozen reference implementation of the current heat mathematics from
// EngineApp.cpp. This test code must not be used by the runtime. Its purpose is
// to preserve the pre-refactor behavior as a golden master.

double EffectiveHalfLifeDays(int activeDaysIn60) {
    constexpr int window = 60;
    if (activeDaysIn60 < 7) return 30.0;
    const double activeFraction = static_cast<double>(activeDaysIn60) / window;
    return std::clamp(12.0 / std::max(activeFraction, 1.0 / window), 7.0, 180.0);
}

double RecentHeat(std::int64_t recentVisits, double daysAgo, double halfLifeDays) {
    if (!recentVisits) return 0.0;
    constexpr double targetVisits = 24.0;
    const double activity = std::log1p(static_cast<double>(recentVisits)) / std::log1p(targetVisits);
    const double base = 5.8 * std::clamp(activity, 0.0, 1.0);
    const double recentHalfLife = std::clamp(halfLifeDays * 0.18, 0.5, 14.0);
    const double recency = std::exp(-std::log(2.0) * daysAgo / recentHalfLife);
    return std::clamp(base * recency, 0.0, 5.8);
}

double HabitHeat(std::int64_t activeDays, double spanDays, double daysAgo, double halfLifeDays) {
    if (!activeDays) return 0.0;
    const double frequency = std::clamp(static_cast<double>(activeDays) / std::max(1.0, spanDays), 0.0, 1.0);
    const double maturity = 1.0 - std::exp(-static_cast<double>(activeDays) / 6.0);
    const double regularity = 0.45 + 0.55 * std::sqrt(frequency);
    const double base = 5.2 * maturity * regularity;
    const double habitHalfLife = std::clamp(halfLifeDays * 3.0, 14.0, 540.0);
    const double recency = std::exp(-std::log(2.0) * daysAgo / habitHalfLife);
    return std::clamp(base * recency, 0.0, 5.2);
}

double DirectHeat(const fhm::tests::FolderHeatFixture& a) {
    if (!a.visits) return 0.0;
    const double recent = RecentHeat(a.recentVisits, a.daysSinceLastEffectiveVisit, a.halfLifeDays);
    const double habit = HabitHeat(a.activeDays, a.spanDays, a.daysSinceLastEffectiveVisit, a.halfLifeDays);
    const double high = std::max(recent, habit);
    const double low = std::min(recent, habit);
    return std::clamp(high + 0.30 * low, 0.0, 7.0);
}

double FileHeat(const fhm::tests::FileHeatFixture& a) {
    if (!a.writeEvents) return 0.0;
    constexpr double targetWrites = 12.0;
    const double activity = std::log1p(static_cast<double>(a.writeEvents)) / std::log1p(targetWrites);
    const double recentBase = 6.2 * std::clamp(activity, 0.0, 1.0);
    const double writeHalfLife = std::clamp(a.halfLifeDays * 0.14, 0.5, 21.0);
    const double recency = std::exp(-std::log(2.0) * a.daysSinceLastWrite / writeHalfLife);
    const double recent = recentBase * recency;
    const double frequency = std::clamp(static_cast<double>(a.activeDays) / std::max(1.0, a.spanDays), 0.0, 1.0);
    const double maturity = 1.0 - std::exp(-static_cast<double>(a.writeEvents) / 8.0);
    const double habitBase = 4.4 * maturity * (0.45 + 0.55 * std::sqrt(frequency));
    const double habitHalfLife = std::clamp(a.halfLifeDays * 2.0, 10.0, 365.0);
    const double habit = habitBase * std::exp(-std::log(2.0) * a.daysSinceLastWrite / habitHalfLife);
    const double high = std::max(recent, habit);
    const double low = std::min(recent, habit);
    return std::clamp(high + 0.22 * low, 0.0, 7.0);
}

bool Equal(double actual, double expected) {
    return std::abs(actual - expected) <= kTolerance;
}

void PrintCase(const char* group, std::string_view name, double actual, double expected, bool pass) {
    std::cout << (pass ? "[PASS] " : "[ERROR] ") << group << " / " << name
              << " actual=" << std::setprecision(15) << actual
              << " expected=" << expected << '\n';
}

} // namespace

int main() {
    int passCount = 0;
    int errorCount = 0;

    std::cout << "============================================================\n";
    std::cout << "FolderHeatMap heat model golden reference tests\n";
    std::cout << "Reference model: Dual-Timescale Activity (pre-refactor)\n";
    std::cout << "Tolerance: " << std::setprecision(2) << std::scientific << kTolerance << std::defaultfloat << "\n";
    std::cout << "============================================================\n";

    for (const auto& fixture : fhm::tests::kCoolingFixtures) {
        const double actual = EffectiveHalfLifeDays(fixture.activeDaysIn60);
        const bool pass = Equal(actual, fixture.expectedHalfLifeDays);
        PrintCase("cooling", fixture.name, actual, fixture.expectedHalfLifeDays, pass);
        pass ? ++passCount : ++errorCount;
    }

    for (const auto& fixture : fhm::tests::kFolderHeatFixtures) {
        const double actual = DirectHeat(fixture);
        const bool pass = Equal(actual, fixture.expectedHeat);
        PrintCase("folder", fixture.name, actual, fixture.expectedHeat, pass);
        pass ? ++passCount : ++errorCount;
    }

    for (const auto& fixture : fhm::tests::kFileHeatFixtures) {
        const double actual = FileHeat(fixture);
        const bool pass = Equal(actual, fixture.expectedHeat);
        PrintCase("file", fixture.name, actual, fixture.expectedHeat, pass);
        pass ? ++passCount : ++errorCount;
    }

    std::cout << "============================================================\n";
    std::cout << "PASS:  " << passCount << '\n';
    std::cout << "ERROR: " << errorCount << '\n';
    std::cout << "RESULT: " << (errorCount == 0 ? "PASS" : "FAIL") << '\n';
    std::cout << "============================================================\n";

    return errorCount == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
