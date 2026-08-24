if(NOT DEFINED INPUT)
    message(FATAL_ERROR "InjectDeletionDiagnostics.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_diag_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "1.17 lifecycle patch anchor not found: ${LABEL}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" PATCHED "${ENGINE}")
    set(ENGINE "${PATCHED}" PARENT_SCOPE)
endfunction()

fhm_diag_replace_once([=[#include "EngineLog.h"
#include "FolderIdentity.h"]=]
[=[#include "EngineLog.h"
#include "DiagnosticWatcher.h"
#include "FolderIdentity.h"]=]
"diagnostic include")

fhm_diag_replace_once([=[std::atomic<bool> g_stopping{false};
bool g_runtimeCacheDirty]=]
[=[std::atomic<bool> g_stopping{false};

// 1.17 deletion barrier. A tombstone is cheap and immediate: it prevents stale
// RAM/DB history from being returned while SLOW performs physical subtree GC.
std::mutex g_deleteMutex;
std::deque<std::wstring> g_deleteQueue;
std::unordered_set<std::wstring> g_deletePending;
std::unordered_set<std::wstring> g_tombstones;

bool CoveredByPath(const std::wstring& path, const std::wstring& root) {
    if (root.empty()) return false;
    if (_wcsicmp(path.c_str(), root.c_str()) == 0) return true;
    if (path.size() <= root.size() || _wcsnicmp(path.c_str(), root.c_str(), root.size()) != 0) return false;
    if (root.back() == L'\\') return true;
    return path[root.size()] == L'\\';
}

bool IsTombstoned(const std::wstring& path) {
    const auto key = fhm::runtime::NormalizePath(path);
    std::scoped_lock lock(g_deleteMutex);
    for (const auto& root : g_tombstones) if (CoveredByPath(key, root)) return true;
    return false;
}

void HandleObservedRemoval(const std::wstring& path);

bool g_runtimeCacheDirty]=]
"delete barrier state")

fhm_diag_replace_once([=[std::optional<Snapshot> BuildSnapshot(const std::wstring& path, bool isDirectory,
                                      const fhm::Settings& settings, double halfLife) {
    Snapshot result{};
    result.isDirectory = isDirectory;
    const auto id = fhm::ResolveFolderIdentity(path);
    if (!id) return std::nullopt;]=]
[=[std::optional<Snapshot> BuildSnapshot(const std::wstring& path, bool isDirectory,
                                      const fhm::Settings& settings, double halfLife) {
    Snapshot result{};
    result.isDirectory = isDirectory;

    // A known delete wins over every cache/DB value until SLOW finishes purge.
    if (IsTombstoned(path)) return result;

    const auto id = fhm::ResolveFolderIdentity(path);
    if (!id) return std::nullopt;

    // Safety net for deletes performed outside the currently watched TC
    // directory. Same path + different per-volume File ID proves that the old
    // object no longer exists. Never expose its history; schedule stale subtree
    // cleanup and let the current object start cold.
    if (!id->volumeId.starts_with(L"unc:")) {
        const auto tracked = g_readDatabase.GetTrackedObjectAtPath(id->volumeId, id->relativePath);
        if (tracked) {
            const auto currentObjectId = fhm::ResolveFilesystemObjectId(path, isDirectory);
            if (currentObjectId && *currentObjectId != tracked->objectId) {
                g_log.WritePath("LIFECYCLE", "identity_mismatch", path);
                HandleObservedRemoval(path);
                return result;
            }
        }
    }]=]
"identity mismatch guard")

fhm_diag_replace_once([=[    g_slowQueue.push_back(key);
    g_slowCv.notify_one();]=]
[=[    g_slowQueue.push_back(key);
    if (g_shared) {
        InterlockedExchange(&g_shared->slowQueueDepth, static_cast<LONG>(g_slowQueue.size()));
        InterlockedExchange(&g_shared->slowPendingCount, static_cast<LONG>(g_slowPending.size()));
    }
    g_slowCv.notify_one();]=]
"queue telemetry")

fhm_diag_replace_once([=[void QueuePrediction(const std::wstring& directory) {]=]
[=[void HandleObservedRemoval(const std::wstring& path) {
    const auto key = fhm::runtime::NormalizePath(path);
    if (key.empty()) return;

    bool newlyQueued = false;
    {
        std::scoped_lock lock(g_deleteMutex);
        // If an ancestor tombstone already covers this path, no additional work
        // is needed. If a higher tombstone arrives later it replaces descendants.
        for (const auto& root : g_tombstones) if (CoveredByPath(key, root)) return;
        for (auto it = g_tombstones.begin(); it != g_tombstones.end();) {
            if (CoveredByPath(*it, key)) it = g_tombstones.erase(it);
            else ++it;
        }
        g_tombstones.insert(key);
        if (g_deletePending.insert(key).second) {
            g_deleteQueue.push_back(key);
            newlyQueued = true;
        }
    }

    bool ramChanged = false;
    {
        std::scoped_lock lock(g_stateMutex);
        for (auto it = g_ram.begin(); it != g_ram.end();) {
            if (CoveredByPath(it->first, key)) { it = g_ram.erase(it); ramChanged = true; }
            else ++it;
        }
        for (auto it = g_ready.begin(); it != g_ready.end();) {
            if (CoveredByPath(it->first, key) || CoveredByPath(key, it->first)) it = g_ready.erase(it);
            else ++it;
        }
        if (ramChanged) {
            g_runtimeCacheDirty = true;
            PublishRamLocked();
        }
    }

    g_log.WritePath("LIFECYCLE", "tombstone", key);
    if (newlyQueued) g_log.WritePath("LIFECYCLE", "purge_subtree queued", key);
    g_slowCv.notify_one();
}

std::wstring ExistingParent(std::wstring path) {
    path = fhm::runtime::NormalizePath(path);
    while (path.size() > 3) {
        const size_t pos = path.find_last_of(L'\\');
        if (pos == std::wstring::npos) return {};
        path = (pos == 2 && path.size() >= 3 && path[1] == L':') ? path.substr(0, 3) : path.substr(0, pos);
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return path;
    }
    return path;
}

std::optional<fhm::FolderIdentity> DeletedPathIdentity(const std::wstring& deletedPath) {
    const auto parent = ExistingParent(deletedPath);
    if (parent.empty()) return std::nullopt;
    auto id = fhm::ResolveFolderIdentity(parent);
    if (!id || id->volumeId.starts_with(L"unc:")) return std::nullopt;

    const auto normalizedDeleted = fhm::runtime::NormalizePath(deletedPath);
    const auto normalizedParent = fhm::runtime::NormalizePath(parent);
    std::wstring suffix;
    if (normalizedDeleted.size() > normalizedParent.size()) {
        size_t start = normalizedParent.size();
        if (start < normalizedDeleted.size() && normalizedDeleted[start] == L'\\') ++start;
        suffix = normalizedDeleted.substr(start);
    }
    if (!suffix.empty()) {
        if (!id->relativePath.empty()) id->relativePath += L'\\';
        id->relativePath += suffix;
    }
    id->storageKey = id->volumeId + L"|" + id->relativePath;
    return id;
}

void ProcessDeleteTask(const std::wstring& path) {
    const auto identity = DeletedPathIdentity(path);
    bool ok = identity && g_writeDatabase.ResetRecursiveActivity(*identity);
    if (ok) {
        std::scoped_lock lock(g_deleteMutex);
        g_deletePending.erase(path);
        g_tombstones.erase(path);
    }
    if (ok) {
        g_log.WritePath("LIFECYCLE", "purge_subtree completed", path);
        const auto parent = ExistingParent(path);
        if (!parent.empty()) QueueSlow(parent);
    } else {
        g_log.WritePath("LIFECYCLE", "purge_subtree FAILED", path);
    }
}

void QueuePrediction(const std::wstring& directory) {]=]
"delete queue implementation")

fhm_diag_replace_once([=[    directory = std::move(g_slowQueue.front());
    g_slowQueue.pop_front();
    return true;]=]
[=[    directory = std::move(g_slowQueue.front());
    g_slowQueue.pop_front();
    if (g_shared) {
        InterlockedExchange(&g_shared->slowQueueDepth, static_cast<LONG>(g_slowQueue.size()));
        InterlockedExchange(&g_shared->slowPendingCount, static_cast<LONG>(g_slowPending.size()));
    }
    return true;]=]
"take telemetry")

fhm_diag_replace_once([=[void ProcessSlowTask(const std::wstring& directory) {
    g_log.WritePath("SLOW", "persist", directory);]=]
[=[void ProcessSlowTask(const std::wstring& directory) {
    if (g_shared) {
        InterlockedExchange(&g_shared->slowBusy, 1);
        wcsncpy_s(g_shared->slowCurrentPath, directory.c_str(), _TRUNCATE);
    }
    g_log.WritePath("SLOW", "persist", directory);]=]
"slow busy start")

fhm_diag_replace_once([=[        g_slowPending.erase(directory);
    }
    MaybeSaveRamPersistence(false);
}]=]
[=[        g_slowPending.erase(directory);
        if (g_shared) InterlockedExchange(&g_shared->slowPendingCount, static_cast<LONG>(g_slowPending.size()));
    }
    MaybeSaveRamPersistence(false);
    if (g_shared) {
        g_shared->slowCurrentPath[0] = L'\0';
        InterlockedExchange(&g_shared->slowBusy, 0);
    }
}]=]
"slow busy end")

fhm_diag_replace_once([=[void SlowWorker() {
    for (;;) {
        std::wstring task;
        {
            std::unique_lock lock(g_slowMutex);
            g_slowCv.wait_for(lock, std::chrono::milliseconds(50), [] {
                return g_stopping.load() || !g_slowQueue.empty();
            });
            if (g_slowQueue.empty()) {
                if (g_stopping.load()) break;
                lock.unlock();
                MaybeSaveRamPersistence(false);
                continue;
            }
            task = std::move(g_slowQueue.front());
            g_slowQueue.pop_front();
        }
        ProcessSlowTask(task);
    }
}]=]
[=[void SlowWorker() {
    for (;;) {
        std::wstring deleteTask;
        {
            std::scoped_lock deleteLock(g_deleteMutex);
            if (!g_deleteQueue.empty()) {
                deleteTask = std::move(g_deleteQueue.front());
                g_deleteQueue.pop_front();
            }
        }
        if (!deleteTask.empty()) {
            if (g_shared) {
                InterlockedExchange(&g_shared->slowBusy, 1);
                wcsncpy_s(g_shared->slowCurrentPath, deleteTask.c_str(), _TRUNCATE);
            }
            ProcessDeleteTask(deleteTask);
            if (g_shared) {
                g_shared->slowCurrentPath[0] = L'\0';
                InterlockedExchange(&g_shared->slowBusy, 0);
            }
            continue;
        }

        std::wstring task;
        {
            std::unique_lock lock(g_slowMutex);
            g_slowCv.wait_for(lock, std::chrono::milliseconds(50), [] {
                if (g_stopping.load() || !g_slowQueue.empty()) return true;
                std::scoped_lock deleteLock(g_deleteMutex);
                return !g_deleteQueue.empty();
            });
            {
                std::scoped_lock deleteLock(g_deleteMutex);
                if (!g_deleteQueue.empty()) continue;
            }
            if (g_slowQueue.empty()) {
                if (g_stopping.load()) break;
                lock.unlock();
                MaybeSaveRamPersistence(false);
                continue;
            }
            task = std::move(g_slowQueue.front());
            g_slowQueue.pop_front();
            if (g_shared) {
                InterlockedExchange(&g_shared->slowQueueDepth, static_cast<LONG>(g_slowQueue.size()));
                InterlockedExchange(&g_shared->slowPendingCount, static_cast<LONG>(g_slowPending.size()));
            }
        }
        ProcessSlowTask(task);
    }
}]=]
"prioritized delete queue")

fhm_diag_replace_once([=[    std::thread fast(FastWorker);
    std::thread slow(SlowWorker);]=]
[=[    std::thread fast(FastWorker);
    std::thread slow(SlowWorker);
    std::thread diagnostics(fhm::RunDeletionDiagnostics, g_shared, &g_log, &g_stopping, &HandleObservedRemoval);]=]
"diagnostic thread start")

fhm_diag_replace_once([=[    if (fast.joinable()) fast.join();
    if (slow.joinable()) slow.join();]=]
[=[    if (fast.joinable()) fast.join();
    if (slow.joinable()) slow.join();
    if (diagnostics.joinable()) diagnostics.join();]=]
"diagnostic thread join")

fhm_diag_replace_once([=[g_log.Write("ENGINE", "FolderHeatMap 1.11 engine starting");]=]
[=[g_log.Write("ENGINE", "FolderHeatMap 1.17 lifecycle engine starting");]=]
"engine version")

file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.17 deletion lifecycle: ${INPUT}")
