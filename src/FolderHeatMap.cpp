#include "Database.h"
#include "FolderIdentity.h"
#include "Settings.h"
#include "WdxApi.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr int kFieldHeat = 0;
constexpr int kFieldVisits = 1;
constexpr int kFieldLastVisit = 2;
constexpr int kFieldHeatLevel = 3;
constexpr int kFieldColorStep = 4;
constexpr int kFieldWrites = 5;
constexpr int kFieldLastWrite = 6;
constexpr double kTicksPerDay = 10000000.0 * 60.0 * 60.0 * 24.0;
constexpr auto kSnapshotLifetime = std::chrono::seconds(5);

fhm::Database g_database;
fhm::Settings g_settings = fhm::DefaultSettings();
std::wstring g_settingsPath;

struct ValueSnapshot {
    bool isDirectory = false;
    bool fileHeatAvailable = false;
    double heat = 0.0;
    __int64 visits = 0;
    __int64 writes = 0;
    FILETIME lastVisit{};
    FILETIME lastWrite{};
    bool hasLastVisit = false;
    bool hasLastWrite = false;
    int heatLevel = 0;
    int colorStep = 0;
    std::chrono::steady_clock::time_point updated{};
};

std::mutex g_snapshotMutex;
std::unordered_map<std::wstring, ValueSnapshot> g_snapshots;
std::mutex g_snapshotQueueMutex;
std::condition_variable g_snapshotCv;
std::deque<std::wstring> g_snapshotQueue;
std::unordered_set<std::wstring> g_snapshotPending;
std::thread g_snapshotWorker;
bool g_snapshotStopping = false;

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

void ReloadSettings() {
    if (!g_settingsPath.empty()) fhm::LoadSettings(g_settingsPath, g_settings);
}

void RecordDirectoryVisit(const std::wstring& path) {
    if (!fhm::IsDirectory(path)) return;
    const auto id = fhm::ResolveFolderIdentity(path);
    if (!id) return;
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
    if (!a.writeEvents || !FileTimeTicks(a.lastWrite)) return 0.0;
    constexpr double targetWrites = 12.0;
    const double activity = std::log1p(static_cast<double>(a.writeEvents)) / std::log1p(targetWrites);
    const double recentBase = 6.2 * std::clamp(activity, 0.0, 1.0);
    const double writeHalfLife = std::clamp(halfLifeDays * 0.14, 0.5, 21.0);
    const double recency = std::exp(-std::log(2.0) * DaysAgo(a.lastWrite) / writeHalfLife);
    const double recent = recentBase * recency;
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

double CombineHeat(double current, double contribution) {
    const double a = std::clamp(current, 0.0, 7.0) / 7.0;
    const double b = std::clamp(contribution, 0.0, 7.0) / 7.0;
    return 7.0 * (1.0 - (1.0 - a) * (1.0 - b));
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
            result = CombineHeat(result, DirectHeat(activity, halfLife) * std::pow(g_settings.pathDecay, distance));
        }
    }
    if (g_settings.fileHeatEnabled && g_settings.fileContribution > 0.0) {
        const auto files = g_database.GetVolumeFileActivities(id.volumeId);
        for (const auto& [relative, activity] : files) {
            const std::wstring parent = ParentRelativePath(relative);
            int distance = -1;
            if (_wcsicmp(parent.c_str(), id.relativePath.c_str()) == 0) distance = 0;
            else if (g_settings.includePathHeat && IsDescendantOf(parent, id.relativePath)) distance = std::max(1, ComponentDepth(parent) - baseDepth);
            if (distance < 0) continue;
            const double pathFactor = distance == 0 ? 1.0 : std::pow(g_settings.pathDecay, distance);
            result = CombineHeat(result, FileHeat(activity, halfLife) * g_settings.fileContribution * pathFactor);
        }
    }
    return std::clamp(result, 0.0, 7.0);
}

int HeatToLevel(double heat) { return heat <= 0.0 ? 0 : std::clamp(static_cast<int>(std::ceil(heat)), 1, 7); }
int HeatToColorStep(double heat) {
    if (heat <= 0.0) return 0;
    const int steps = std::clamp(g_settings.stepsPerLevel, 1, 16);
    return std::clamp(static_cast<int>(std::ceil(heat * steps)), 1, 7 * steps);
}

std::optional<ValueSnapshot> BuildSnapshot(const std::wstring& path) {
    ValueSnapshot snapshot{};
    snapshot.isDirectory = fhm::IsDirectory(path);

    if (snapshot.isDirectory) {
        const auto id = fhm::ResolveFolderIdentity(path);
        if (!id) return std::nullopt;
        const auto activity = g_database.GetActivity(*id);
        snapshot.heat = HeatForIdentity(*id, activity);
        snapshot.visits = activity ? static_cast<__int64>(activity->visits) : 0;
        if (activity) {
            snapshot.lastVisit = activity->lastVisit;
            snapshot.hasLastVisit = true;
        }
    } else {
        if (!g_settings.fileHeatEnabled) {
            snapshot.updated = std::chrono::steady_clock::now();
            return snapshot;
        }
        FILETIME lastWrite{};
        if (!GetLastWriteTime(path, lastWrite)) return std::nullopt;
        const auto id = fhm::ResolveFolderIdentity(path);
        if (!id) return std::nullopt;
        g_database.ObserveFileWrite(*id, lastWrite);
        auto activity = g_database.GetFileActivity(*id);
        if (!activity) {
            fhm::StoredFileActivity fallback{};
            fallback.lastWrite = lastWrite;
            fallback.writeEvents = 0;
            activity = fallback;
        }
        snapshot.fileHeatAvailable = true;
        snapshot.heat = FileHeat(*activity, EffectiveHalfLifeDays());
        snapshot.writes = static_cast<__int64>(activity->writeEvents);
        snapshot.lastWrite = activity->lastWrite;
        snapshot.hasLastWrite = true;
    }

    snapshot.heatLevel = HeatToLevel(snapshot.heat);
    snapshot.colorStep = HeatToColorStep(snapshot.heat);
    snapshot.updated = std::chrono::steady_clock::now();
    return snapshot;
}

int ValueFromSnapshot(const ValueSnapshot& snapshot, int fieldIndex, void* fieldValue) {
    if (!snapshot.isDirectory && !snapshot.fileHeatAvailable) return ft_fieldempty;
    switch (fieldIndex) {
        case kFieldHeat: *static_cast<double*>(fieldValue) = snapshot.heat; return ft_numeric_floating;
        case kFieldVisits:
            if (!snapshot.isDirectory) return ft_fieldempty;
            *static_cast<__int64*>(fieldValue) = snapshot.visits; return ft_numeric_64;
        case kFieldLastVisit:
            if (!snapshot.isDirectory || !snapshot.hasLastVisit) return ft_fieldempty;
            *static_cast<FILETIME*>(fieldValue) = snapshot.lastVisit; return ft_datetime;
        case kFieldHeatLevel: *static_cast<int*>(fieldValue) = snapshot.heatLevel; return ft_numeric_32;
        case kFieldColorStep: *static_cast<int*>(fieldValue) = snapshot.colorStep; return ft_numeric_32;
        case kFieldWrites:
            if (snapshot.isDirectory) return ft_fieldempty;
            *static_cast<__int64*>(fieldValue) = snapshot.writes; return ft_numeric_64;
        case kFieldLastWrite:
            if (snapshot.isDirectory || !snapshot.hasLastWrite) return ft_fieldempty;
            *static_cast<FILETIME*>(fieldValue) = snapshot.lastWrite; return ft_datetime;
        default: return ft_nosuchfield;
    }
}

void StoreSnapshot(const std::wstring& path, ValueSnapshot snapshot) {
    std::scoped_lock lock(g_snapshotMutex);
    g_snapshots[path] = std::move(snapshot);
}

std::optional<ValueSnapshot> FindSnapshot(const std::wstring& path) {
    std::scoped_lock lock(g_snapshotMutex);
    const auto it = g_snapshots.find(path);
    if (it == g_snapshots.end()) return std::nullopt;
    return it->second;
}

void QueueSnapshotRefresh(const std::wstring& path) {
    std::scoped_lock lock(g_snapshotQueueMutex);
    if (g_snapshotStopping || !g_snapshotPending.insert(path).second) return;
    g_snapshotQueue.push_back(path);
    g_snapshotCv.notify_one();
}

void SnapshotWorkerLoop() {
    for (;;) {
        std::wstring path;
        {
            std::unique_lock lock(g_snapshotQueueMutex);
            g_snapshotCv.wait(lock, [] { return g_snapshotStopping || !g_snapshotQueue.empty(); });
            if (g_snapshotStopping && g_snapshotQueue.empty()) break;
            path = std::move(g_snapshotQueue.front());
            g_snapshotQueue.pop_front();
        }
        if (auto snapshot = BuildSnapshot(path)) StoreSnapshot(path, std::move(*snapshot));
        {
            std::scoped_lock lock(g_snapshotQueueMutex);
            g_snapshotPending.erase(path);
        }
    }
}

void StartSnapshotWorker() {
    std::scoped_lock lock(g_snapshotQueueMutex);
    if (g_snapshotWorker.joinable()) return;
    g_snapshotStopping = false;
    g_snapshotWorker = std::thread(SnapshotWorkerLoop);
}

void StopSnapshotWorker() {
    {
        std::scoped_lock lock(g_snapshotQueueMutex);
        g_snapshotStopping = true;
    }
    g_snapshotCv.notify_all();
    if (g_snapshotWorker.joinable()) g_snapshotWorker.join();
    {
        std::scoped_lock lock(g_snapshotQueueMutex);
        g_snapshotQueue.clear();
        g_snapshotPending.clear();
    }
    {
        std::scoped_lock lock(g_snapshotMutex);
        g_snapshots.clear();
    }
}

void ClearSnapshots() {
    std::scoped_lock lock(g_snapshotMutex);
    g_snapshots.clear();
}
} // namespace

extern "C" __declspec(dllexport) void __stdcall ContentSetDefaultParams(ContentDefaultParamStruct* dps) {
    std::wstring defaultIni;
    if (dps && dps->DefaultIniName[0] != '\0') defaultIni = AnsiToWide(dps->DefaultIniName);
    g_settingsPath = fhm::SettingsPathFromDefaultIni(defaultIni);
    ReloadSettings();
    g_database.Open(GetDatabasePath(dps));
    StartSnapshotWorker();
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

extern "C" __declspec(dllexport) int __stdcall ContentGetValueW(WCHAR* fileName, int fieldIndex, int, void* fieldValue, int, int flags) {
    if (!fileName || !fieldValue) return ft_fileerror;
    const std::wstring path(fileName);

    if (auto cached = FindSnapshot(path)) {
        if (std::chrono::steady_clock::now() - cached->updated > kSnapshotLifetime) QueueSnapshotRefresh(path);
        return ValueFromSnapshot(*cached, fieldIndex, fieldValue);
    }

    if ((flags & CONTENT_DELAYIFSLOW) != 0) {
        QueueSnapshotRefresh(path);
        return ft_delayed;
    }

    if (auto snapshot = BuildSnapshot(path)) {
        const int result = ValueFromSnapshot(*snapshot, fieldIndex, fieldValue);
        StoreSnapshot(path, std::move(*snapshot));
        return result;
    }
    return ft_fieldempty;
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValue(char* fileName, int fieldIndex, int unitIndex, void* fieldValue, int maxlen, int flags) {
    const auto wide = AnsiToWide(fileName);
    if (wide.empty()) return ft_fileerror;
    return ContentGetValueW(const_cast<WCHAR*>(wide.c_str()), fieldIndex, unitIndex, fieldValue, maxlen, flags);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetDefaultSortOrder(int fieldIndex) { return (fieldIndex >= kFieldHeat && fieldIndex <= kFieldLastWrite) ? -1 : 1; }

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformationW(int state, WCHAR* path) {
    if (state == contst_refreshpressed) {
        ReloadSettings();
        ClearSnapshots();
    }
    if (state == contst_readnewdir && path && *path) {
        RecordDirectoryVisit(path);
        QueueSnapshotRefresh(path);
    }
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformation(int state, char* path) {
    const auto wide = AnsiToWide(path);
    if (!wide.empty()) ContentSendStateInformationW(state, const_cast<WCHAR*>(wide.c_str()));
}

extern "C" __declspec(dllexport) void __stdcall ContentPluginUnloading() {
    StopSnapshotWorker();
    g_database.Close();
}
