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
constexpr int kFieldWrites = 5;
constexpr int kFieldLastWrite = 6;
constexpr double kTicksPerDay = 10000000.0 * 60.0 * 60.0 * 24.0;

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
    if (!g_settingsPath.empty()) fhm::LoadSettings(g_settingsPath, g_settings);
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    g_database.RecordVisit(*id, now, g_settings.repeatVisitCooldownSeconds, g_settings.sessionResetHours);
}

ULONGLONG FileTimeTicks(const FILETIME& time) {
    ULARGE_INTEGER v{};
    v.LowPart = time.dwLowDateTime;
    v.HighPart = time.dwHighDateTime;
    return v.QuadPart;
}

double DaysAgo(const FILETIME& time) {
    const ULONGLONG then = FileTimeTicks(time);
    FILETIME nft{}; GetSystemTimeAsFileTime(&nft);
    const ULONGLONG now = FileTimeTicks(nft);
    if (!then || now <= then) return 0.0;
    return static_cast<double>(now - then) / kTicksPerDay;
}

std::int64_t CurrentDayKey() {
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    return static_cast<std::int64_t>(FileTimeTicks(now) / static_cast<ULONGLONG>(kTicksPerDay));
}

double EffectiveHalfLifeDays() {
    if (!g_settings.coolingAuto) return std::clamp(g_settings.coolingHalfLifeDays, 1.0, 365.0);

    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    constexpr int window = 60;
    const int activeDays = g_database.GetRecentActiveDays(now, window);
    if (activeDays < 7) return 30.0;

    const double activeFraction = static_cast<double>(activeDays) / window;
    return std::clamp(12.0 / std::max(activeFraction, 1.0 / window), 7.0, 180.0);
}

double RecentHeat(const fhm::StoredActivity& a, double halfLifeDays) {
    if (!a.recentVisits || !FileTimeTicks(a.lastEffectiveVisit)) return 0.0;
    constexpr double targetVisits = 24.0;
    const double activity = std::log1p(static_cast<double>(a.recentVisits)) / std::log1p(targetVisits);
    const double base = 5.8 * std::clamp(activity, 0.0, 1.0);
    const double recentHalfLife = std::clamp(halfLifeDays * 0.18, 0.5, 14.0);
    const double recency = std::exp(-std::log(2.0) * DaysAgo(a.lastEffectiveVisit) / recentHalfLife);
    return std::clamp(base * recency, 0.0, 5.8);
}

double HabitHeat(const fhm::StoredActivity& a, double halfLifeDays) {
    if (!a.activeDays || !FileTimeTicks(a.lastEffectiveVisit)) return 0.0;

    const std::int64_t today = CurrentDayKey();
    const std::int64_t first = a.firstActiveDay > 0 ? a.firstActiveDay : today;
    const double spanDays = static_cast<double>(std::max<std::int64_t>(1, today - first + 1));
    const double frequency = std::clamp(static_cast<double>(a.activeDays) / spanDays, 0.0, 1.0);
    const double maturity = 1.0 - std::exp(-static_cast<double>(a.activeDays) / 6.0);
    const double regularity = 0.45 + 0.55 * std::sqrt(frequency);
    const double base = 5.2 * maturity * regularity;
    const double habitHalfLife = std::clamp(halfLifeDays * 3.0, 14.0, 540.0);
    const double recency = std::exp(-std::log(2.0) * DaysAgo(a.lastEffectiveVisit) / habitHalfLife);
    return std::clamp(base * recency, 0.0, 5.2);
}

double DirectHeat(const fhm::StoredActivity& a, double halfLifeDays) {
    if (!a.visits) return 0.0;
    const double recent = RecentHeat(a, halfLifeDays);
    const double habit = HabitHeat(a, halfLifeDays);
    const double high = std::max(recent, habit);
    const double low = std::min(recent, habit);
    return std::clamp(high + 0.30 * low, 0.0, 7.0);
}

double FileHeat(const fhm::StoredFileActivity& a, double halfLifeDays) {
    if (!FileTimeTicks(a.lastWrite)) return 0.0;

    const double writeHalfLife = std::clamp(halfLifeDays * 0.14, 0.5, 21.0);
    const double recent = 6.4 * std::exp(-std::log(2.0) * DaysAgo(a.lastWrite) / writeHalfLife);

    if (!a.activeDays || !a.writeEvents) return std::clamp(recent, 0.0, 7.0);
    const std::int64_t today = CurrentDayKey();
    const std::int64_t first = a.firstActiveDay > 0 ? a.firstActiveDay : today;
    const double spanDays = static_cast<double>(std::max<std::int64_t>(1, today - first + 1));
    const double frequency = std::clamp(static_cast<double>(a.activeDays) / spanDays, 0.0, 1.0);
    const double maturity = 1.0 - std::exp(-static_cast<double>(a.writeEvents) / 8.0);
    const double habitBase = 4.4 * maturity * (0.45 + 0.55 * std::sqrt(frequency));
    const double habitHalfLife = std::clamp(halfLifeDays * 2.0, 10.0, 365.0);
    const double habit = habitBase * std::exp(-std::log(2.0) * DaysAgo(a.lastWrite) / habitHalfLife);

    const double high = std::max(recent, habit);
    const double low = std::min(recent, habit);
    return std::clamp(high + 0.22 * low, 0.0, 7.0);
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

std::wstring ParentRelativePath(const std::wstring& path) {
    const size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? L"" : path.substr(0, pos);
}

bool GetLastWriteTime(const std::wstring& path, FILETIME& lastWrite) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;
    lastWrite = data.ftLastWriteTime;
    return FileTimeTicks(lastWrite) != 0;
}

double HeatForIdentity(const fhm::FolderIdentity& id, const std::optional<fhm::StoredActivity>& direct) {
    const double halfLife = EffectiveHalfLifeDays();
    double result = direct ? DirectHeat(*direct, halfLife) : 0.0;

    const int baseDepth = ComponentDepth(id.relativePath);

    if (g_settings.includePathHeat && g_settings.pathDecay > 0.0) {
        const auto activities = g_database.GetVolumeActivities(id.volumeId);
        for (const auto& [relative, activity] : activities) {
            if (!IsDescendantOf(relative, id.relativePath)) continue;
            const int distance = std::max(1, ComponentDepth(relative) - baseDepth);
            const double inherited = DirectHeat(activity, halfLife) * std::pow(g_settings.pathDecay, distance);
            result = std::max(result, inherited);
        }
    }

    if (g_settings.fileHeatEnabled && g_settings.fileContribution > 0.0) {
        const auto files = g_database.GetVolumeFileActivities(id.volumeId);
        for (const auto& [relative, activity] : files) {
            const std::wstring parent = ParentRelativePath(relative);
            int distance = -1;
            if (_wcsicmp(parent.c_str(), id.relativePath.c_str()) == 0) {
                distance = 0;
            } else if (g_settings.includePathHeat && IsDescendantOf(parent, id.relativePath)) {
                distance = std::max(1, ComponentDepth(parent) - baseDepth);
            }
            if (distance < 0) continue;
            const double pathFactor = distance == 0 ? 1.0 : std::pow(g_settings.pathDecay, distance);
            const double inherited = FileHeat(activity, halfLife) * g_settings.fileContribution * pathFactor;
            result = std::max(result, inherited);
        }
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
        case kFieldWrites:
        case kFieldLastWrite:
            return ft_fieldempty;
        default: return ft_nosuchfield;
    }
}

int GetValueForFile(const std::wstring& path, int fieldIndex, void* fieldValue) {
    if (!g_settings.fileHeatEnabled) return ft_fieldempty;

    FILETIME lastWrite{};
    if (!GetLastWriteTime(path, lastWrite)) return ft_fieldempty;
    const auto id = fhm::ResolveFolderIdentity(path);
    if (!id) return ft_fieldempty;

    g_database.ObserveFileWrite(*id, lastWrite);
    auto activity = g_database.GetFileActivity(*id);
    if (!activity) {
        fhm::StoredFileActivity fallback{};
        fallback.lastWrite = lastWrite;
        fallback.writeEvents = 1;
        fallback.activeDays = 1;
        const auto day = static_cast<std::int64_t>(FileTimeTicks(lastWrite) / static_cast<ULONGLONG>(kTicksPerDay));
        fallback.firstActiveDay = day;
        fallback.lastActiveDay = day;
        activity = fallback;
    }

    const double heat = FileHeat(*activity, EffectiveHalfLifeDays());
    switch (fieldIndex) {
        case kFieldHeat: *static_cast<double*>(fieldValue) = heat; return ft_numeric_floating;
        case kFieldHeatLevel: *static_cast<int*>(fieldValue) = HeatToLevel(heat); return ft_numeric_32;
        case kFieldColorStep: *static_cast<int*>(fieldValue) = HeatToColorStep(heat); return ft_numeric_32;
        case kFieldWrites: *static_cast<__int64*>(fieldValue) = static_cast<__int64>(activity->writeEvents); return ft_numeric_64;
        case kFieldLastWrite: *static_cast<FILETIME*>(fieldValue) = activity->lastWrite; return ft_datetime;
        case kFieldVisits:
        case kFieldLastVisit:
            return ft_fieldempty;
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
        case kFieldWrites: CopyAnsi(fieldName, maxlen, "Writes"); return ft_numeric_64;
        case kFieldLastWrite: CopyAnsi(fieldName, maxlen, "Last Write"); return ft_datetime;
        default: return ft_nomorefields;
    }
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValueW(WCHAR* fileName, int fieldIndex, int, void* fieldValue, int, int) {
    if (!fileName || !fieldValue) return ft_fileerror;
    if (!g_settingsPath.empty()) fhm::LoadSettings(g_settingsPath, g_settings);
    return fhm::IsDirectory(fileName)
        ? GetValueForDirectory(fileName, fieldIndex, fieldValue)
        : GetValueForFile(fileName, fieldIndex, fieldValue);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValue(char* fileName, int fieldIndex, int unitIndex, void* fieldValue, int maxlen, int flags) {
    const auto wide = AnsiToWide(fileName);
    if (wide.empty()) return ft_fileerror;
    return ContentGetValueW(const_cast<WCHAR*>(wide.c_str()), fieldIndex, unitIndex, fieldValue, maxlen, flags);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetDefaultSortOrder(int fieldIndex) {
    return (fieldIndex >= kFieldHeat && fieldIndex <= kFieldLastWrite) ? -1 : 1;
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformationW(int state, WCHAR* path) {
    if (state == contst_readnewdir && path && *path) RecordDirectoryVisit(path);
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformation(int state, char* path) {
    const auto wide = AnsiToWide(path);
    if (!wide.empty()) ContentSendStateInformationW(state, const_cast<WCHAR*>(wide.c_str()));
}

extern "C" __declspec(dllexport) void __stdcall ContentPluginUnloading() { g_database.Close(); }
