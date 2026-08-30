#pragma once

#include <array>
#include <cstdint>

namespace fhm::tests {

// Stable, date-independent inputs derived from the values consumed by the
// current Dual-Timescale Activity math in EngineApp.cpp.
//
// These fixtures are intentionally model-facing rather than filesystem-facing.
// They are the shared reference dataset for:
//   1) protecting the current model during refactoring, and
//   2) comparing future heat models against the same activity scenarios.

struct CoolingFixture {
    const char* name;
    int activeDaysIn60;
    double expectedHalfLifeDays;
};

inline constexpr std::array<CoolingFixture, 6> kCoolingFixtures{{
    {"bootstrap_no_history", 0, 30.0},
    {"bootstrap_six_days", 6, 30.0},
    {"seven_days", 7, 102.85714285714286},
    {"quarter_active", 15, 48.0},
    {"half_active", 30, 24.0},
    {"daily_user", 60, 12.0},
}};

struct FolderHeatFixture {
    const char* name;
    std::int64_t visits;
    std::int64_t recentVisits;
    std::int64_t activeDays;
    double spanDays;
    double daysSinceLastEffectiveVisit;
    double halfLifeDays;
    double expectedHeat;
};

inline constexpr std::array<FolderHeatFixture, 10> kFolderHeatFixtures{{
    {"new_object", 0, 0, 0, 1.0, 0.0, 30.0, 0.0},
    {"one_visit_now", 1, 1, 1, 1.0, 0.0, 30.0, 1.488450527583482},
    {"three_visits_now", 3, 3, 1, 1.0, 0.0, 30.0, 2.737412545996321},
    {"burst_24_now", 24, 24, 1, 1.0, 0.0, 30.0, 6.039488509170642},
    {"burst_24_after_one_day", 24, 24, 1, 2.0, 1.0, 30.0, 5.300677407536031},
    {"burst_24_after_three_days", 24, 24, 1, 4.0, 3.0, 30.0, 4.115954473950316},
    {"habit_week", 20, 4, 5, 7.0, 0.2, 30.0, 3.632165261392874},
    {"habit_month", 80, 3, 18, 30.0, 0.5, 30.0, 5.014705576046191},
    {"habit_month_idle_week", 80, 3, 18, 37.0, 7.0, 30.0, 4.207934910735419},
    {"occasional_long_term", 30, 2, 10, 120.0, 2.0, 60.0, 3.070332505455759},
}};

struct FileHeatFixture {
    const char* name;
    std::int64_t writeEvents;
    std::int64_t activeDays;
    double spanDays;
    double daysSinceLastWrite;
    double halfLifeDays;
    double expectedHeat;
};

inline constexpr std::array<FileHeatFixture, 8> kFileHeatFixtures{{
    {"new_file", 0, 0, 1.0, 0.0, 30.0, 0.0},
    {"one_write_now", 1, 1, 1.0, 0.0, 30.0, 1.789219555747494},
    {"three_writes_now", 3, 1, 1.0, 0.0, 30.0, 3.653657093029103},
    {"twelve_writes_now", 12, 1, 1.0, 0.0, 30.0, 6.952010004976320},
    {"twelve_writes_after_one_day", 12, 1, 2.0, 1.0, 30.0, 5.880378373373498},
    {"habit_week", 20, 5, 7.0, 0.2, 30.0, 6.809689992423496},
    {"habit_month", 80, 18, 30.0, 0.5, 30.0, 6.552003781005750},
    {"habit_month_idle_week", 80, 18, 37.0, 7.0, 30.0, 3.812457551295095},
}};

} // namespace fhm::tests
