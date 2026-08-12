#include "Settings.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>

namespace fhm {
namespace {
unsigned long ReadColor(const std::wstring& ini, int index, unsigned long fallback) {
    wchar_t key[32]{};
    swprintf_s(key, L"Color%d", index);
    wchar_t value[32]{};
    wchar_t fallbackText[32]{};
    swprintf_s(fallbackText, L"%lu", fallback);
    GetPrivateProfileStringW(L"Colors", key, fallbackText, value, 32, ini.c_str());
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(value, &end, 10);
    return end != value ? parsed : fallback;
}
}

Settings DefaultSettings() {
    Settings s;
    s.colors[0] = RGB(0, 0, 0);
    s.colors[1] = RGB(80, 200, 120);
    s.colors[2] = RGB(130, 220, 90);
    s.colors[3] = RGB(200, 225, 70);
    s.colors[4] = RGB(245, 205, 65);
    s.colors[5] = RGB(250, 155, 55);
    s.colors[6] = RGB(245, 90, 60);
    s.colors[7] = RGB(245, 70, 120);
    return s;
}

bool LoadSettings(const std::wstring& iniPath, Settings& s) {
    s = DefaultSettings();
    s.coolingAuto = GetPrivateProfileIntW(L"Heat", L"CoolingAuto", 1, iniPath.c_str()) != 0;
    s.coolingHalfLifeDays = std::clamp(static_cast<double>(GetPrivateProfileIntW(L"Heat", L"CoolingHalfLifeDays", 30, iniPath.c_str())), 1.0, 365.0);
    s.includePathHeat = GetPrivateProfileIntW(L"Heat", L"IncludePathHeat", 1, iniPath.c_str()) != 0;
    s.pathDecay = std::clamp(GetPrivateProfileIntW(L"Heat", L"PathDecayPercent", 50, iniPath.c_str()), 0u, 100u) / 100.0;
    s.fileHeatEnabled = GetPrivateProfileIntW(L"Heat", L"FileHeatEnabled", 1, iniPath.c_str()) != 0;
    s.fileContribution = std::clamp(GetPrivateProfileIntW(L"Heat", L"FileContributionPercent", 50, iniPath.c_str()), 0u, 100u) / 100.0;
    s.repeatVisitCooldownSeconds = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Heat", L"RepeatVisitCooldownSeconds", 90, iniPath.c_str())), 0, 600);
    s.sessionResetHours = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Heat", L"SessionResetHours", 8, iniPath.c_str())), 1, 24);
    s.smoothColors = GetPrivateProfileIntW(L"Colors", L"Smooth", 1, iniPath.c_str()) != 0;
    s.stepsPerLevel = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Colors", L"StepsPerLevel", 4, iniPath.c_str())), 1, 16);
    const Settings defaults = DefaultSettings();
    for (int i = 1; i <= 7; ++i) s.colors[i] = ReadColor(iniPath, i, defaults.colors[i]);
    return true;
}

bool SaveSettings(const std::wstring& iniPath, const Settings& s) {
    std::error_code ec;
    const std::filesystem::path p(iniPath);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    auto writeInt = [&](const wchar_t* section, const wchar_t* key, long value) {
        wchar_t text[32]{}; swprintf_s(text, L"%ld", value);
        return WritePrivateProfileStringW(section, key, text, iniPath.c_str()) != FALSE;
    };
    bool ok = true;
    ok &= writeInt(L"Heat", L"CoolingAuto", s.coolingAuto ? 1 : 0);
    ok &= writeInt(L"Heat", L"CoolingHalfLifeDays", static_cast<long>(std::clamp(s.coolingHalfLifeDays, 1.0, 365.0)));
    ok &= writeInt(L"Heat", L"IncludePathHeat", s.includePathHeat ? 1 : 0);
    ok &= writeInt(L"Heat", L"PathDecayPercent", static_cast<long>(std::clamp(s.pathDecay, 0.0, 1.0) * 100.0 + 0.5));
    ok &= writeInt(L"Heat", L"FileHeatEnabled", s.fileHeatEnabled ? 1 : 0);
    ok &= writeInt(L"Heat", L"FileContributionPercent", static_cast<long>(std::clamp(s.fileContribution, 0.0, 1.0) * 100.0 + 0.5));
    ok &= writeInt(L"Heat", L"RepeatVisitCooldownSeconds", std::clamp(s.repeatVisitCooldownSeconds, 0, 600));
    ok &= writeInt(L"Heat", L"SessionResetHours", std::clamp(s.sessionResetHours, 1, 24));
    ok &= writeInt(L"Colors", L"Smooth", s.smoothColors ? 1 : 0);
    ok &= writeInt(L"Colors", L"StepsPerLevel", std::clamp(s.stepsPerLevel, 1, 16));
    for (int i = 1; i <= 7; ++i) {
        wchar_t key[32]{}; wchar_t value[32]{};
        swprintf_s(key, L"Color%d", i); swprintf_s(value, L"%lu", s.colors[i]);
        ok &= WritePrivateProfileStringW(L"Colors", key, value, iniPath.c_str()) != FALSE;
    }
    return ok;
}

std::wstring SettingsPathFromDefaultIni(const std::wstring& defaultIniName) {
    std::filesystem::path p(defaultIniName);
    if (p.has_parent_path()) return (p.parent_path() / L"FolderHeatMap.ini").wstring();
    return L"FolderHeatMap.ini";
}

} // namespace fhm
