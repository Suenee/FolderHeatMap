#include "HeatModelFixtures.h"
#include "HeatModel.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

constexpr double kTolerance = 1e-9;

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
    const fhm::DualTimescaleActivityModel model;

    std::cout << "============================================================\n";
    std::cout << "FolderHeatMap heat model golden reference tests\n";
    std::cout << "Runtime model: " << model.DisplayName() << "\n";
    std::cout << "Model id: " << model.Id() << "\n";
    std::cout << "Reference data: pre-refactor Dual-Timescale Activity golden master\n";
    std::cout << "Tolerance: " << std::setprecision(2) << std::scientific << kTolerance << std::defaultfloat << "\n";
    std::cout << "============================================================\n";

    for (const auto& fixture : fhm::tests::kCoolingFixtures) {
        fhm::CoolingInput input{};
        input.automatic = true;
        input.configuredHalfLifeDays = 30.0;
        input.activeDaysIn60 = fixture.activeDaysIn60;
        const double actual = model.EffectiveHalfLifeDays(input);
        const bool pass = Equal(actual, fixture.expectedHalfLifeDays);
        PrintCase("cooling", fixture.name, actual, fixture.expectedHalfLifeDays, pass);
        pass ? ++passCount : ++errorCount;
    }

    for (const auto& fixture : fhm::tests::kFolderHeatFixtures) {
        fhm::FolderHeatInput input{};
        input.visits = static_cast<std::uint64_t>(fixture.visits);
        input.recentVisits = static_cast<std::uint64_t>(fixture.recentVisits);
        input.activeDays = static_cast<std::uint64_t>(fixture.activeDays);
        input.spanDays = fixture.spanDays;
        input.daysSinceLastEffectiveVisit = fixture.daysSinceLastEffectiveVisit;
        input.halfLifeDays = fixture.halfLifeDays;
        input.hasLastEffectiveVisit = fixture.visits != 0;
        const double actual = model.CalculateFolderHeat(input);
        const bool pass = Equal(actual, fixture.expectedHeat);
        PrintCase("folder", fixture.name, actual, fixture.expectedHeat, pass);
        pass ? ++passCount : ++errorCount;
    }

    for (const auto& fixture : fhm::tests::kFileHeatFixtures) {
        fhm::FileHeatInput input{};
        input.writeEvents = static_cast<std::uint64_t>(fixture.writeEvents);
        input.activeDays = static_cast<std::uint64_t>(fixture.activeDays);
        input.spanDays = fixture.spanDays;
        input.daysSinceLastWrite = fixture.daysSinceLastWrite;
        input.halfLifeDays = fixture.halfLifeDays;
        input.hasLastWrite = fixture.writeEvents != 0;
        const double actual = model.CalculateFileHeat(input);
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
