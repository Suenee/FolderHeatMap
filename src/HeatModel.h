#pragma once

#include <cstdint>

namespace fhm {

struct CoolingInput {
    bool automatic = true;
    double configuredHalfLifeDays = 30.0;
    int activeDaysIn60 = 0;
};

struct FolderHeatInput {
    std::uint64_t visits = 0;
    std::uint64_t recentVisits = 0;
    std::uint64_t activeDays = 0;
    double spanDays = 1.0;
    double daysSinceLastEffectiveVisit = 0.0;
    double halfLifeDays = 30.0;
    bool hasLastEffectiveVisit = false;
};

struct FileHeatInput {
    std::uint64_t writeEvents = 0;
    std::uint64_t activeDays = 0;
    double spanDays = 1.0;
    double daysSinceLastWrite = 0.0;
    double halfLifeDays = 30.0;
    bool hasLastWrite = false;
};

class IHeatModel {
public:
    virtual ~IHeatModel() = default;
    virtual const char* Id() const noexcept = 0;
    virtual const char* DisplayName() const noexcept = 0;
    virtual double EffectiveHalfLifeDays(const CoolingInput& input) const = 0;
    virtual double CalculateFolderHeat(const FolderHeatInput& input) const = 0;
    virtual double CalculateFileHeat(const FileHeatInput& input) const = 0;
};

class DualTimescaleActivityModel final : public IHeatModel {
public:
    const char* Id() const noexcept override;
    const char* DisplayName() const noexcept override;
    double EffectiveHalfLifeDays(const CoolingInput& input) const override;
    double CalculateFolderHeat(const FolderHeatInput& input) const override;
    double CalculateFileHeat(const FileHeatInput& input) const override;

private:
    static double RecentFolderHeat(const FolderHeatInput& input);
    static double HabitFolderHeat(const FolderHeatInput& input);
};

} // namespace fhm
