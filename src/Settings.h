#pragma once

#include <array>
#include <string>

namespace fhm {

struct Settings {
    bool coolingAuto = true;
    double coolingHalfLifeDays = 30.0;
    bool includePathHeat = true;
    double pathDecay = 0.50;
    bool fileHeatEnabled = true;
    double fileContribution = 0.50;
    int repeatVisitCooldownSeconds = 90;
    int sessionResetHours = 8;
    bool smoothColors = true;
    int stepsPerLevel = 4;
    std::array<unsigned long, 8> colors{}; // Win32 COLORREF; index 0 is unused/default.

    // Optional custom source artwork for folder heat levels 1-7.
    // Empty = use the automatically generated folder icon based on the heat color.
    // Level 0 deliberately stays empty/read-only and uses Total Commander's normal folder icon.
    std::array<std::wstring, 8> folderIconSources{};
};

Settings DefaultSettings();
bool LoadSettings(const std::wstring& iniPath, Settings& settings);
bool SaveSettings(const std::wstring& iniPath, const Settings& settings);
std::wstring SettingsPathFromDefaultIni(const std::wstring& defaultIniName);

} // namespace fhm
