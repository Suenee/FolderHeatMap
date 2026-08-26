# FolderHeatMap guarded engine generator.
# EngineApp.cpp stays as the verified source baseline. This generator patches a
# build-only copy and aborts configuration if any expected anchor changed.

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "GenerateOptimizedEngine.cmake requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "engine patch anchor not found: ${LABEL}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" PATCHED "${ENGINE}")
    set(ENGINE "${PATCHED}" PARENT_SCOPE)
endfunction()

fhm_replace_once([=[#include "FolderIdentity.h"
#include "RuntimeShared.h"]=]
[=[#include "FolderIdentity.h"
#include "Lifecycle.h"
#include "RuntimeShared.h"]=]
"lifecycle include")

fhm_replace_once([=[std::atomic<bool> g_stopping{false};]=]
[=[std::atomic<bool> g_stopping{false};
bool g_runtimeCacheDirty = false;
std::chrono::steady_clock::time_point g_lastRuntimeCacheSave = std::chrono::steady_clock::now();
constexpr auto kRuntimeCacheFlushDelay = std::chrono::seconds(5);
std::unordered_map<std::wstring, std::int64_t> g_lastAcceptedNavigationSecond;]=]
"runtime dirty batching and navigation debounce state")

fhm_replace_once([=[ULONGLONG FileTimeTicks(const FILETIME& time) {
    ULARGE_INTEGER v{};
    v.LowPart = time.dwLowDateTime;
    v.HighPart = time.dwHighDateTime;
    return v.QuadPart;
}]=]
[=[ULONGLONG FileTimeTicks(const FILETIME& time) {
    ULARGE_INTEGER v{};
    v.LowPart = time.dwLowDateTime;
    v.HighPart = time.dwHighDateTime;
    return v.QuadPart;
}

bool SnapshotEquivalent(const Snapshot& a, const Snapshot& b) {
    constexpr double kHeatEpsilon = 0.001;
    return a.isDirectory == b.isDirectory &&
           a.fileHeatAvailable == b.fileHeatAvailable &&
           std::abs(a.heat - b.heat) < kHeatEpsilon &&
           a.visits == b.visits && a.writes == b.writes &&
           a.hasLastVisit == b.hasLastVisit && a.hasLastWrite == b.hasLastWrite &&
           FileTimeTicks(a.lastVisit) == FileTimeTicks(b.lastVisit) &&
           FileTimeTicks(a.lastWrite) == FileTimeTicks(b.lastWrite) &&
           a.heatLevel == b.heatLevel && a.colorStep == b.colorStep;
}]=]
"snapshot equivalence")

fhm_replace_once([=[    FindClose(find);
    return batch;
}

fhm::runtime::CacheEntry ToCacheEntry]=]
[=[    FindClose(find);
    return batch;
}

bool PathAtOrBelow(const std::wstring& path, const std::wstring& root) {
    if (root.empty()) return false;
    if (_wcsicmp(path.c_str(), root.c_str()) == 0) return true;
    if (path.size() <= root.size() || _wcsnicmp(path.c_str(), root.c_str(), root.size()) != 0) return false;
    if (root.back() == L'\\') return true;
    return path[root.size()] == L'\\';
}

std::wstring ParentFsPath(std::wstring path) {
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    const size_t pos = path.find_last_of(L'\\');
    if (pos == std::wstring::npos) return {};
    if (pos == 2 && path.size() >= 3 && path[1] == L':') return path.substr(0, 3);
    if (pos == 0) return {};
    return path.substr(0, pos);
}

Batch BuildLifecycleAncestorBatch(const fhm::LifecycleResult& lifecycle) {
    Batch batch;
    if (lifecycle.changes.empty()) return batch;
    std::unordered_set<std::wstring> paths;
    for (const auto& change : lifecycle.changes) {
        std::wstring candidates[2] = {ParentFsPath(change.oldPath), ParentFsPath(change.newPath)};
        for (auto candidate : candidates) {
            while (!candidate.empty()) {
                const auto normalized = fhm::runtime::NormalizePath(candidate);
                if (!paths.insert(normalized).second) break;
                const auto parent = ParentFsPath(candidate);
                if (parent.empty() || _wcsicmp(parent.c_str(), candidate.c_str()) == 0) break;
                candidate = parent;
            }
        }
    }
    const auto settings = SettingsSnapshot();
    const double halfLife = EffectiveHalfLifeDays(settings);
    for (const auto& path : paths) {
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (auto snapshot = BuildSnapshot(path, true, settings, halfLife)) batch[path] = std::move(*snapshot);
    }
    return batch;
}

bool MergeBatchChangedLocked(const Batch& batch) {
    bool changed = false;
    for (const auto& [path, snapshot] : batch) {
        const auto it = g_ram.find(path);
        if (it != g_ram.end() && SnapshotEquivalent(it->second, snapshot)) continue;
        g_ram[path] = snapshot;
        changed = true;
    }
    return changed;
}

bool ApplyLifecycleStructureLocked(const fhm::LifecycleResult& lifecycle) {
    bool changed = false;
    for (const auto& change : lifecycle.changes) {
        const auto oldPath = fhm::runtime::NormalizePath(change.oldPath);
        const auto newPath = fhm::runtime::NormalizePath(change.newPath);
        if (change.kind == fhm::LifecycleChangeKind::Moved && !newPath.empty()) {
            std::vector<std::pair<std::wstring, Snapshot>> relocated;
            for (auto it = g_ram.begin(); it != g_ram.end();) {
                if (!PathAtOrBelow(it->first, oldPath)) { ++it; continue; }
                std::wstring target = newPath + it->first.substr(oldPath.size());
                relocated.emplace_back(std::move(target), std::move(it->second));
                it = g_ram.erase(it);
                changed = true;
            }
            for (auto& [path, snapshot] : relocated) g_ram[path] = std::move(snapshot);
        } else {
            for (auto it = g_ram.begin(); it != g_ram.end();) {
                if (PathAtOrBelow(it->first, oldPath)) { it = g_ram.erase(it); changed = true; }
                else ++it;
            }
        }

        for (auto it = g_ready.begin(); it != g_ready.end();) {
            const bool oldRelated = PathAtOrBelow(it->first, oldPath) || PathAtOrBelow(oldPath, it->first);
            const bool newRelated = !newPath.empty() && (PathAtOrBelow(it->first, newPath) || PathAtOrBelow(newPath, it->first));
            if (oldRelated || newRelated) it = g_ready.erase(it);
            else ++it;
        }
    }
    return changed;
}

fhm::runtime::CacheEntry ToCacheEntry]=]
"lifecycle RAM and ancestor helpers")

fhm_replace_once([=[void SaveRamPersistence() {
    std::vector<fhm::RuntimeCacheRecord> records;
    {
        std::scoped_lock lock(g_stateMutex);
        records.reserve(g_ram.size());
        for (const auto& [path, snapshot] : g_ram) records.push_back(ToRuntimeRecord(path, snapshot));
    }
    if (g_writeDatabase.SaveRuntimeCache(records))
        g_log.Write("DB", "saved runtime cache records=" + std::to_string(records.size()));
}]=]
[=[void SaveRamPersistence() {
    std::vector<fhm::RuntimeCacheRecord> records;
    {
        std::scoped_lock lock(g_stateMutex);
        if (!g_runtimeCacheDirty) return;
        records.reserve(g_ram.size());
        for (const auto& [path, snapshot] : g_ram) records.push_back(ToRuntimeRecord(path, snapshot));
        g_runtimeCacheDirty = false;
    }
    if (g_writeDatabase.SaveRuntimeCache(records)) {
        g_lastRuntimeCacheSave = std::chrono::steady_clock::now();
        g_log.Write("DB", "batched runtime cache save records=" + std::to_string(records.size()));
    } else {
        std::scoped_lock lock(g_stateMutex);
        g_runtimeCacheDirty = true;
    }
}

void MaybeSaveRamPersistence(bool force) {
    {
        std::scoped_lock lock(g_stateMutex);
        if (!g_runtimeCacheDirty) return;
        if (!force && std::chrono::steady_clock::now() - g_lastRuntimeCacheSave < kRuntimeCacheFlushDelay) return;
    }
    SaveRamPersistence();
}]=]
"batched runtime persistence")

fhm_replace_once([=[        PublishRamLocked();
    }
    g_log.Write("DB", "restored runtime cache records=" + std::to_string(records.size()));]=]
[=[        PublishRamLocked();
        g_runtimeCacheDirty = false;
        g_lastRuntimeCacheSave = std::chrono::steady_clock::now();
    }
    g_log.Write("DB", "restored runtime cache records=" + std::to_string(records.size()));]=]
"restore dirty reset")

fhm_replace_once([=[void MergeAndPublishLocked(const Batch& batch) {
    for (const auto& [path, snapshot] : batch) g_ram[path] = snapshot;
    PublishRamLocked();
}]=]
[=[void MergeAndPublishLocked(const Batch& batch) {
    if (!MergeBatchChangedLocked(batch)) {
        g_log.Write("CACHE", "unchanged batch; publish skipped");
        return;
    }
    g_runtimeCacheDirty = true;
    PublishRamLocked();
}]=]
"no-op batch suppression")

fhm_replace_once([=[    std::wstring previous;
    {
        std::scoped_lock lock(g_stateMutex);
        previous = g_currentDirectory;
        g_currentDirectory = next;
    }]=]
[=[    std::wstring previous;
    {
        std::scoped_lock lock(g_stateMutex);
        const auto nowSecond = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto last = g_lastAcceptedNavigationSecond.find(next);
        if (last != g_lastAcceptedNavigationSecond.end() && last->second == nowSecond) {
            g_log.WritePath("NAV", "debounced same path within second", next);
            return;
        }
        previous = g_currentDirectory;
        g_currentDirectory = next;
        g_lastAcceptedNavigationSecond[next] = nowSecond;
        g_log.WritePath("NAV", "accepted", next);
    }]=]
"one accepted navigation per path per second")

fhm_replace_once([=[void ProcessSlowTask(const std::wstring& directory) {
    g_log.WritePath("SLOW", "persist", directory);
    PersistDirectory(directory);

    const auto settings = SettingsSnapshot();
    const double halfLife = EffectiveHalfLifeDays(settings);
    if (auto ownSnapshot = BuildSnapshot(directory, true, settings, halfLife)) {
        const auto key = fhm::runtime::NormalizePath(directory);
        std::scoped_lock lock(g_stateMutex);
        g_ram[key] = std::move(*ownSnapshot);
        PublishRamLocked();
    }

    Batch refreshed = BuildBatch(directory);
    StoreReady(directory, refreshed);
    QueueHotPredictions(refreshed);
    {
        std::scoped_lock lock(g_slowMutex);
        g_slowPending.erase(directory);
    }
    SaveRamPersistence();
}]=]
[=[void ProcessSlowTask(const std::wstring& directory) {
    g_log.WritePath("SLOW", "persist", directory);

    const auto lifecycle = fhm::ReconcileDirectoryLifecycle(g_writeDatabase, directory);
    if (!lifecycle.changes.empty()) {
        std::size_t moved = 0, deleted = 0;
        for (const auto& change : lifecycle.changes) {
            if (change.kind == fhm::LifecycleChangeKind::Moved) ++moved;
            else ++deleted;
        }
        g_log.Write("LIFECYCLE", "batch observed=" + std::to_string(lifecycle.observed) +
                    " moved=" + std::to_string(moved) + " deleted=" + std::to_string(deleted));
    }

    PersistDirectory(directory);

    Batch publishBatch;
    const auto settings = SettingsSnapshot();
    const double halfLife = EffectiveHalfLifeDays(settings);
    if (auto ownSnapshot = BuildSnapshot(directory, true, settings, halfLife))
        publishBatch[fhm::runtime::NormalizePath(directory)] = std::move(*ownSnapshot);

    if (!lifecycle.changes.empty()) {
        Batch ancestors = BuildLifecycleAncestorBatch(lifecycle);
        for (auto& [path, snapshot] : ancestors) publishBatch[path] = std::move(snapshot);
    }

    {
        std::scoped_lock lock(g_stateMutex);
        const bool structuralChanged = ApplyLifecycleStructureLocked(lifecycle);
        const bool snapshotChanged = MergeBatchChangedLocked(publishBatch);
        if (structuralChanged || snapshotChanged) {
            g_runtimeCacheDirty = true;
            PublishRamLocked();
        }
    }

    Batch refreshed = BuildBatch(directory);
    StoreReady(directory, refreshed);
    {
        std::scoped_lock lock(g_slowMutex);
        g_slowPending.erase(directory);
    }
    MaybeSaveRamPersistence(false);
}]=]
"SLOW lifecycle and coalesced publication")

fhm_replace_once([=[            if (g_slowQueue.empty()) {
                if (g_stopping.load()) break;
                continue;
            }]=]
[=[            if (g_slowQueue.empty()) {
                if (g_stopping.load()) break;
                lock.unlock();
                MaybeSaveRamPersistence(false);
                continue;
            }]=]
"idle batched persistence flush")

fhm_replace_once([=[        for (const auto& [directory, batch] : g_ready)
            for (const auto& [path, snapshot] : batch) g_ram[path] = snapshot;
        PublishRamLocked();
    }
    SaveRamPersistence();]=]
[=[        bool shutdownChanged = false;
        for (const auto& [directory, batch] : g_ready)
            shutdownChanged = MergeBatchChangedLocked(batch) || shutdownChanged;
        if (shutdownChanged) {
            g_runtimeCacheDirty = true;
            PublishRamLocked();
        }
    }
    MaybeSaveRamPersistence(true);]=]
"shutdown final flush")

fhm_replace_once([=[g_log.Write("ENGINE", "FolderHeatMap 1.07 engine starting");]=]
[=[g_log.Write("ENGINE", "FolderHeatMap 1.11 engine starting");]=]
"engine version log")

get_filename_component(OUT_DIR "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUT_DIR}")
file(WRITE "${OUTPUT}" "${ENGINE}")
message(STATUS "Generated optimized FolderHeatMap engine source: ${OUTPUT}")
