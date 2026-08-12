#include "Database.h"
#include "FolderIdentity.h"
#include "Settings.h"
#include "WdxApi.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
constexpr int kFieldHeat = 0;
constexpr int kFieldVisits = 1;
constexpr int kFieldLastVisit = 2;
constexpr int kFieldHeatLevel = 3;
constexpr int kFieldColorStep = 4;

fhm::Database g_database;
fhm::Settings g_settings = fhm::DefaultSettings();
std::wstring g_settingsPath;

void CopyAnsi(char* destination, int maxlen, const char* source) {
    if (destination && maxlen > 0) strncpy_s(destination, static_cast<size_t>(maxlen), source, _TRUNCATE);
}

std::wstring AnsiToWide(const char* text) {
    if (!text || !*text) return {};
    const int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, text, -1, out.data(), n)) return {};
    out.resize(static_cast<size_t>(n - 1));
    return out;
}

std::wstring GetDatabasePath(const ContentDefaultParamStruct* dps) {
    if (dps && dps->DefaultIniName[0] != '\0') {
        const auto ini = AnsiToWide(dps->DefaultIniName);
        if (!ini.empty()) {
            const std::filesystem::path p(ini);
            if (p.has_parent_path()) return (p.parent_path() / L"FolderHeatMap.db").wstring();
        }
    }
    wchar_t localAppData[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return (std::filesystem::path(localAppData) / L"FolderHeatMap" / L"FolderHeatMap.db").wstring();
    return L"FolderHeatMap.db";
}

void RecordDirectoryVisit(const std::wstring& path) {
    if (!fhm::IsDirectory(path)) return;
    const auto id = fhm::ResolveFolderIdentity(path);
    if (!id) return;
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    g_database.RecordVisit(*id, now);
}

double DaysAgo(const FILETIME& time) {
    ULARGE_INTEGER then{}, now{};
    then.LowPart = time.dwLowDateTime; then.HighPart = time.dwHighDateTime;
    FILETIME nft{}; GetSystemTimeAsFileTime(&nft);
    now.LowPart = nft.dwLowDateTime; now.HighPart = nft.dwHighDateTime;
    if (!then.QuadPart || now.QuadPart <= then.QuadPart) return 0.0;
    return static_cast<double>(now.QuadPart - then.QuadPart) / (10000000.0 * 60.0 * 60.0 * 24.0);
}

double EffectiveHalfLifeDays() {
    if (!g_settings.coolingAuto) return std::clamp(g_settings.coolingHalfLifeDays, 1.0, 3650.0);
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    constexpr int window = 60;
    const int activeDays = g_database.GetRecentActiveDays(now, window);
    if (activeDays < 7) return 30.0; // Bootstrap until enough behavior has been observed.
    const double activeFraction = static_cast<double>(activeDays) / window;
    return std::clamp(10.0 / std::max(activeFraction, 1.0 / window), 7.0, 180.0);
}

double DirectHeat(const fhm::StoredActivity& a, double halfLifeDays) {
    if (!a.visits) return 0.0;
    const double visitHeat = std::min(7.0, std::log2(static_cast<double>(a.visits) + 1.0));
    const double recency = std::exp(-std::log(2.0) * DaysAgo(a.lastVisit) / halfLifeDays);
    return std::clamp(visitHeat * recency, 0.0, 7.0);
}

int ComponentDepth(const std::wstring& relative) {
    if (relative.empty()) return 0;
    int depth = 1;
    for (wchar_t c : relative) if (c == L'\\') ++depth;
    return depth;
}

bool IsDescendantOf(const std::wstring& child, const std::wstring& parent) {
    if (parent.empty()) return !child.empty();
    if (child.size() <= parent.size()) return false;
    if (_wcsnicmp(child.c_str(), parent.c_str(), parent.size()) != 0) return false;
    return child[parent.size()] == L'\\';
}

double HeatForIdentity(const fhm::FolderIdentity& id, const std::optional<fhm::StoredActivity>& direct) {
    const double halfLife = EffectiveHalfLifeDays();
    double result = direct ? DirectHeat(*direct, halfLife) : 0.0;
    if (!g_settings.includePathHeat || g_settings.pathDecay <= 0.0) return result;

    const int baseDepth = ComponentDepth(id.relativePath);
    const auto activities = g_database.GetVolumeActivities(id.volumeId);
    for (const auto& [relative, activity] : activities) {
        if (!IsDescendantOf(relative, id.relativePath)) continue;
        const int distance = std::max(1, ComponentDepth(relative) - baseDepth);
        const double inherited = DirectHeat(activity, halfLife) * std::pow(g_settings.pathDecay, distance);
        result = std::max(result, inherited);
    }
    return std::clamp(result, 0.0, 7.0);
}

int HeatToLevel(double heat) {
    return heat <= 0.0 ? 0 : std::clamp(static_cast<int>(std::ceil(heat)), 1, 7);
}

int HeatToColorStep(double heat) {
    if (heat <= 0.0) return 0;
    const int steps = std::clamp(g_settings.stepsPerLevel, 1, 16);
    return std::clamp(static_cast<int>(std::ceil(heat * steps)), 1, 7 * steps);
}

int GetValueForDirectory(const std::wstring& path, int fieldIndex, void* fieldValue) {
    if (!fhm::IsDirectory(path)) return ft_fieldempty;
    const auto id = fhm::ResolveFolderIdentity(path);
    if (!id) return ft_fieldempty;
    const auto activity = g_database.GetActivity(*id);
    const double heat = HeatForIdentity(*id, activity);

    switch (fieldIndex) {
        case kFieldHeat: *static_cast<double*>(fieldValue) = heat; return ft_numeric_floating;
        case kFieldVisits: *static_cast<__int64*>(fieldValue) = activity ? static_cast<__int64>(activity->visits) : 0; return ft_numeric_64;
        case kFieldLastVisit:
            if (!activity) return ft_fieldempty;
            *static_cast<FILETIME*>(fieldValue) = activity->lastVisit; return ft_datetime;
        case kFieldHeatLevel: *static_cast<int*>(fieldValue) = HeatToLevel(heat); return ft_numeric_32;
        case kFieldColorStep: *static_cast<int*>(fieldValue) = HeatToColorStep(heat); return ft_numeric_32;
        default: return ft_nosuchfield;
    }
}
} // namespace

extern "C" __declspec(dllexport) void __stdcall ContentSetDefaultParams(ContentDefaultParamStruct* dps) {
    std::wstring defaultIni;
    if (dps && dps->DefaultIniName[0] != '\0') defaultIni = AnsiToWide(dps->DefaultIniName);
    g_settingsPath = fhm::SettingsPathFromDefaultIni(defaultIni);
    fhm::LoadSettings(g_settingsPath, g_settings);
    g_database.Open(GetDatabasePath(dps));
}

extern "C" __declspec(dllexport) int __stdcall ContentGetSupportedField(int fieldIndex, char* fieldName, char* units, int maxlen) {
    if (units && maxlen > 0) units[0] = '\0';
    switch (fieldIndex) {
        case kFieldHeat: CopyAnsi(fieldName, maxlen, "Heat"); return ft_numeric_floating;
        case kFieldVisits: CopyAnsi(fieldName, maxlen, "Visits"); return ft_numeric_64;
        case kFieldLastVisit: CopyAnsi(fieldName, maxlen, "Last Visit"); return ft_datetime;
        case kFieldHeatLevel: CopyAnsi(fieldName, maxlen, "Heat Level"); return ft_numeric_32;
        case kFieldColorStep: CopyAnsi(fieldName, maxlen, "Heat Color Step"); return ft_numeric_32;
        default: return ft_nomorefields;
    }
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValueW(WCHAR* fileName, int fieldIndex, int, void* fieldValue, int, int) {
    if (!fileName || !fieldValue) return ft_fileerror;
    if (!g_settingsPath.empty()) fhm::LoadSettings(g_settingsPath, g_settings); // Cheap INI refresh for live config changes.
    return GetValueForDirectory(fileName, fieldIndex, fieldValue);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValue(char* fileName, int fieldIndex, int unitIndex, void* fieldValue, int maxlen, int flags) {
    const auto wide = AnsiToWide(fileName);
    if (wide.empty()) return ft_fileerror;
    return ContentGetValueW(const_cast<WCHAR*>(wide.c_str()), fieldIndex, unitIndex, fieldValue, maxlen, flags);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetDefaultSortOrder(int fieldIndex) {
    return (fieldIndex >= kFieldHeat && fieldIndex <= kFieldColorStep) ? -1 : 1;
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformationW(int state, WCHAR* path) {
    if (state == contst_readnewdir && path && *path) RecordDirectoryVisit(path);
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformation(int state, char* path) {
    const auto wide = AnsiToWide(path);
    if (!wide.empty()) ContentSendStateInformationW(state, const_cast<WCHAR*>(wide.c_str()));
}

extern "C" __declspec(dllexport) void __stdcall ContentPluginUnloading() { g_database.Close(); }
