#include "Database.h"
#include "FolderIdentity.h"
#include "RuntimeShared.h"
#include "Settings.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
constexpr double kTicksPerDay = 10000000.0 * 60.0 * 60.0 * 24.0;
constexpr int kPredictionCount = 3;

struct Snapshot {
    bool isDirectory = false;
    bool fileHeatAvailable = false;
    double heat = 0.0;
    std::int64_t visits = 0;
    std::int64_t writes = 0;
    FILETIME lastVisit{};
    FILETIME lastWrite{};
    bool hasLastVisit = false;
    bool hasLastWrite = false;
    int heatLevel = 0;
    int colorStep = 0;
};

using Batch = std::unordered_map<std::wstring, Snapshot>;

struct SlowTask {
    std::wstring directory;
    bool essential = true;
};

fhm::Database g_database;
fhm::Settings g_settings = fhm::DefaultSettings();
std::wstring g_settingsPath;

HANDLE g_mapping = nullptr;
fhm::runtime::SharedState* g_shared = nullptr;
HANDLE g_stoppedEvent = nullptr;
HANDLE g_engineMutex = nullptr;

std::mutex g_stateMutex;
std::unordered_map<std::wstring, Snapshot> g_ram;
std::unordered_map<std::wstring, Batch> g_ready;
std::wstring g_currentDirectory;
std::deque<std::wstring> g_predictions;
std::unordered_set<std::wstring> g_predictionPending;

std::mutex g_slowMutex;
std::condition_variable g_slowCv;
std::deque<SlowTask> g_slowQueue;
std::unordered_set<std::wstring> g_slowPending;

std::atomic<bool> g_stopping{false};
std::atomic<bool> g_fastBusy{false};
std::atomic<bool> g_slowBusy{false};

ULONGLONG FileTimeTicks(const FILETIME& time) {
    ULARGE_INTEGER v{};
    v.LowPart = time.dwLowDateTime;
    v.HighPart = time.dwHighDateTime;
    return v.QuadPart;
}

double DaysAgo(const FILETIME& time) {
    const ULONGLONG then = FileTimeTicks(time);
    FILETIME nowFt{};
    GetSystemTimeAsFileTime(&nowFt);
    const ULONGLONG now = FileTimeTicks(nowFt);
    if (!then || now <= then) return 0.0;
    return static_cast<double>(now - then) / kTicksPerDay;
}

std::int64_t CurrentDayKey() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return static_cast<std::int64_t>(FileTimeTicks(now) / static_cast<ULONGLONG>(kTicksPerDay));
}

double EffectiveHalfLifeDays() {
    if (!g_settings.coolingAuto) return std::clamp(g_settings.coolingHalfLifeDays, 1.0, 365.0);
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
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
            else if (g_settings.includePathHeat && IsDescendantOf(parent, id.relativePath))
                distance = std::max(1, ComponentDepth(parent) - baseDepth);
            if (distance < 0) continue;
            const double pathFactor = distance == 0 ? 1.0 : std::pow(g_settings.pathDecay, distance);
            result = CombineHeat(result, FileHeat(activity, halfLife) * g_settings.fileContribution * pathFactor);
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

std::optional<Snapshot> BuildSnapshot(const std::wstring& path, bool isDirectory) {
    Snapshot result{};
    result.isDirectory = isDirectory;
    const auto id = fhm::ResolveFolderIdentity(path);
    if (!id) return std::nullopt;

    if (isDirectory) {
        const auto activity = g_database.GetActivity(*id);
        result.heat = HeatForIdentity(*id, activity);
        result.visits = activity ? static_cast<std::int64_t>(activity->visits) : 0;
        if (activity) {
            result.lastVisit = activity->lastVisit;
            result.hasLastVisit = FileTimeTicks(activity->lastVisit) != 0;
        }
    } else if (g_settings.fileHeatEnabled) {
        const auto activity = g_database.GetFileActivity(*id);
        if (activity) {
            result.fileHeatAvailable = true;
            result.heat = FileHeat(*activity, EffectiveHalfLifeDays());
            result.writes = static_cast<std::int64_t>(activity->writeEvents);
            result.lastWrite = activity->lastWrite;
            result.hasLastWrite = FileTimeTicks(activity->lastWrite) != 0;
        }
    }

    result.heatLevel = HeatToLevel(result.heat);
    result.colorStep = HeatToColorStep(result.heat);
    return result;
}

Batch BuildBatch(const std::wstring& directory) {
    Batch batch;
    std::wstring pattern = directory;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return batch;

    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
        std::wstring full = directory;
        if (!full.empty() && full.back() != L'\\') full += L'\\';
        full += data.cFileName;
        const bool isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (auto snapshot = BuildSnapshot(full, isDirectory))
            batch.emplace(fhm::runtime::NormalizePath(full), std::move(*snapshot));
    } while (FindNextFileW(find, &data));

    FindClose(find);
    return batch;
}

fhm::runtime::CacheEntry ToCacheEntry(const std::wstring& path, const Snapshot& snapshot) {
    fhm::runtime::CacheEntry entry{};
    std::uint32_t length = 0;
    entry.pathHash = fhm::runtime::HashNormalizedPath(path.c_str(), length);
    entry.pathLength = length;
    entry.flags = snapshot.isDirectory ? fhm::runtime::kFlagDirectory : 0;
    if (snapshot.fileHeatAvailable) entry.flags |= fhm::runtime::kFlagFileHeat;
    if (snapshot.hasLastVisit) entry.flags |= fhm::runtime::kFlagLastVisit;
    if (snapshot.hasLastWrite) entry.flags |= fhm::runtime::kFlagLastWrite;
    entry.heat = snapshot.heat;
    entry.visits = snapshot.visits;
    entry.writes = snapshot.writes;
    entry.lastVisit = snapshot.lastVisit;
    entry.lastWrite = snapshot.lastWrite;
    entry.heatLevel = snapshot.heatLevel;
    entry.colorStep = snapshot.colorStep;
    return entry;
}

void PublishRamLocked() {
    if (!g_shared) return;
    const LONG active = InterlockedCompareExchange(&g_shared->activeBuffer, 0, 0) & 1;
    const LONG inactive = active ^ 1;
    auto& buffer = g_shared->buffers[inactive];

    while (InterlockedCompareExchange(&buffer.readers, 0, 0) != 0 && !g_stopping.load()) Sleep(1);
    std::memset(buffer.entries, 0, sizeof(buffer.entries));
    InterlockedExchange(&buffer.count, 0);

    for (const auto& [path, snapshot] : g_ram) {
        if (!fhm::runtime::InsertEntry(buffer, ToCacheEntry(path, snapshot))) break;
    }

    MemoryBarrier();
    InterlockedExchange(&g_shared->activeBuffer, inactive);
    InterlockedIncrement(&g_shared->generation);
}

void MergeAndPublishLocked(const Batch& batch) {
    for (const auto& [path, snapshot] : batch) g_ram[path] = snapshot;
    PublishRamLocked();
}

void StoreReady(const std::wstring& directory, Batch batch) {
    std::scoped_lock lock(g_stateMutex);
    g_ready[fhm::runtime::NormalizePath(directory)] = std::move(batch);
}

void PromoteReady(const std::wstring& directory) {
    if (directory.empty()) return;
    std::scoped_lock lock(g_stateMutex);
    const auto key = fhm::runtime::NormalizePath(directory);
    const auto it = g_ready.find(key);
    if (it == g_ready.end()) return;
    MergeAndPublishLocked(it->second);
    g_ready.erase(it);
}

void QueueSlow(const std::wstring& directory) {
    const auto key = fhm::runtime::NormalizePath(directory);
    std::scoped_lock lock(g_slowMutex);
    if (!g_slowPending.insert(key).second) return;
    g_slowQueue.push_back({key, true});
    g_slowCv.notify_one();
}

void QueuePrediction(const std::wstring& directory) {
    const auto key = fhm::runtime::NormalizePath(directory);
    std::scoped_lock lock(g_stateMutex);
    if (key.empty() || key == g_currentDirectory || !g_predictionPending.insert(key).second) return;
    g_predictions.push_back(key);
}

void QueueHotPredictions(const std::wstring& directory, const Batch& batch) {
    std::vector<std::pair<double, std::wstring>> hot;
    for (const auto& [path, snapshot] : batch) {
        if (snapshot.isDirectory) hot.emplace_back(snapshot.heat, path);
    }
    std::sort(hot.begin(), hot.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const int count = std::min<int>(kPredictionCount, static_cast<int>(hot.size()));
    for (int i = 0; i < count; ++i) QueuePrediction(hot[i].second);
}

void PersistDirectory(const std::wstring& directory) {
    const auto id = fhm::ResolveFolderIdentity(directory);
    if (id) {
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        g_database.RecordVisit(*id, now, g_settings.repeatVisitCooldownSeconds, g_settings.sessionResetHours);
    }

    if (g_settings.fileHeatEnabled) {
        std::wstring pattern = directory;
        if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
        pattern += L'*';
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
                std::wstring full = directory;
                if (!full.empty() && full.back() != L'\\') full += L'\\';
                full += data.cFileName;
                if (const auto fileId = fhm::ResolveFolderIdentity(full))
                    g_database.ObserveFileWrite(*fileId, data.ftLastWriteTime);
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
    }
}

void HandleNavigation(const std::wstring& nextDirectory) {
    const auto next = fhm::runtime::NormalizePath(nextDirectory);
    if (next.empty()) return;

    std::wstring previous;
    {
        std::scoped_lock lock(g_stateMutex);
        previous = g_currentDirectory;
        g_currentDirectory = next;
    }

    // A completed batch becomes public only after the user has left that directory.
    // Therefore no background calculation can progressively repaint the current view.
    if (!previous.empty() && previous != next) PromoteReady(previous);

    g_fastBusy.store(true);
    Batch batch = BuildBatch(next);
    StoreReady(next, batch);
    QueueSlow(next);
    QueueHotPredictions(next, batch);
    g_fastBusy.store(false);
}

void HandlePrediction(const std::wstring& directory) {
    {
        std::scoped_lock lock(g_stateMutex);
        if (directory == g_currentDirectory) {
            g_predictionPending.erase(directory);
            return;
        }
    }

    g_fastBusy.store(true);
    Batch batch = BuildBatch(directory);
    {
        std::scoped_lock lock(g_stateMutex);
        if (directory != g_currentDirectory) MergeAndPublishLocked(batch);
        g_predictionPending.erase(directory);
    }
    g_fastBusy.store(false);
}

std::wstring ReadNavigationStable(LONG sequence) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        wchar_t buffer[fhm::runtime::kDirectoryChars]{};
        wcsncpy_s(buffer, g_shared->currentDirectory, _TRUNCATE);
        MemoryBarrier();
        if (InterlockedCompareExchange(&g_shared->navigationSeq, 0, 0) == sequence)
            return fhm::runtime::NormalizePath(buffer);
    }
    return {};
}

void FastWorker() {
    LONG seen = g_shared ? InterlockedCompareExchange(&g_shared->navigationSeq, 0, 0) : 0;
    while (!g_stopping.load()) {
        if (!g_shared) break;
        const LONG current = InterlockedCompareExchange(&g_shared->navigationSeq, 0, 0);
        if (current != seen) {
            seen = current;
            const auto path = ReadNavigationStable(current);
            if (!path.empty()) HandleNavigation(path);
            continue;
        }

        std::wstring prediction;
        {
            std::scoped_lock lock(g_stateMutex);
            if (!g_predictions.empty()) {
                prediction = std::move(g_predictions.front());
                g_predictions.pop_front();
            }
        }
        if (!prediction.empty()) {
            HandlePrediction(prediction);
            continue;
        }
        Sleep(10);
    }
}

void SlowWorker() {
    for (;;) {
        SlowTask task;
        {
            std::unique_lock lock(g_slowMutex);
            g_slowCv.wait_for(lock, std::chrono::milliseconds(50), [] {
                return g_stopping.load() || !g_slowQueue.empty();
            });
            if (g_slowQueue.empty()) {
                if (g_stopping.load()) break;
                continue;
            }
            task = std::move(g_slowQueue.front());
            g_slowQueue.pop_front();
        }

        g_slowBusy.store(true);
        PersistDirectory(task.directory);
        Batch refreshed = BuildBatch(task.directory);
        StoreReady(task.directory, refreshed);
        QueueHotPredictions(task.directory, refreshed);
        g_slowBusy.store(false);

        std::scoped_lock lock(g_slowMutex);
        g_slowPending.erase(task.directory);
    }
}

std::wstring ArgValue(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], name) == 0) return argv[i + 1];
    }
    return {};
}

bool OpenRuntime() {
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(sizeof(fhm::runtime::SharedState)),
                                   fhm::runtime::kMappingName);
    if (!g_mapping) return false;
    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
    g_shared = static_cast<fhm::runtime::SharedState*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(fhm::runtime::SharedState)));
    if (!g_shared) return false;
    if (created || g_shared->magic != fhm::runtime::kMagic || g_shared->version != fhm::runtime::kVersion) {
        std::memset(g_shared, 0, sizeof(*g_shared));
        g_shared->magic = fhm::runtime::kMagic;
        g_shared->version = fhm::runtime::kVersion;
    }
    return true;
}

void CloseRuntime() {
    if (g_shared) {
        UnmapViewOfFile(g_shared);
        g_shared = nullptr;
    }
    if (g_mapping) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    g_engineMutex = CreateMutexW(nullptr, TRUE, fhm::runtime::kEngineMutexName);
    if (!g_engineMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_engineMutex) CloseHandle(g_engineMutex);
        return 0;
    }

    g_stoppedEvent = CreateEventW(nullptr, TRUE, FALSE, fhm::runtime::kEngineStoppedEventName);
    if (!OpenRuntime()) return 2;

    const std::wstring databasePath = ArgValue(argc, argv, L"--db");
    g_settingsPath = ArgValue(argc, argv, L"--settings");
    if (!g_settingsPath.empty()) fhm::LoadSettings(g_settingsPath, g_settings);
    if (!databasePath.empty()) g_database.Open(databasePath);

    InterlockedExchange(&g_shared->shutdownRequested, 0);
    std::thread fast(FastWorker);
    std::thread slow(SlowWorker);

    LONG seenSettings = InterlockedCompareExchange(&g_shared->settingsSeq, 0, 0);
    while (true) {
        const LONG settingsSeq = InterlockedCompareExchange(&g_shared->settingsSeq, 0, 0);
        if (settingsSeq != seenSettings) {
            seenSettings = settingsSeq;
            if (!g_settingsPath.empty()) fhm::LoadSettings(g_settingsPath, g_settings);
        }

        const LONG clients = InterlockedCompareExchange(&g_shared->clientCount, 0, 0);
        const LONG shutdown = InterlockedCompareExchange(&g_shared->shutdownRequested, 0, 0);
        if (shutdown != 0 && clients <= 0) break;
        Sleep(20);
    }

    // Graceful drain: stop accepting predictions. FAST finishes its current task,
    // SLOW drains all persistence tasks already queued. This keeps the database a
    // durable backup of the RAM activity state when Total Commander exits.
    g_stopping.store(true);
    {
        std::scoped_lock lock(g_stateMutex);
        g_predictions.clear();
        g_predictionPending.clear();
    }
    g_slowCv.notify_all();

    if (fast.joinable()) fast.join();
    if (slow.joinable()) slow.join();

    // Anything already calculated is safe to expose for the next TC process.
    {
        std::scoped_lock lock(g_stateMutex);
        for (const auto& [directory, batch] : g_ready) {
            for (const auto& [path, snapshot] : batch) g_ram[path] = snapshot;
        }
        PublishRamLocked();
    }

    g_database.Close();
    if (g_stoppedEvent) SetEvent(g_stoppedEvent);
    CloseRuntime();
    if (g_stoppedEvent) CloseHandle(g_stoppedEvent);
    if (g_engineMutex) {
        ReleaseMutex(g_engineMutex);
        CloseHandle(g_engineMutex);
    }
    return 0;
}
