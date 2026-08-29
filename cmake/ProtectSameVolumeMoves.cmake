if(NOT DEFINED INPUT)
    message(FATAL_ERROR "ProtectSameVolumeMoves.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_move_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "1.48 rapid MOVE reconciliation anchor not found: ${LABEL}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" PATCHED "${ENGINE}")
    set(ENGINE "${PATCHED}" PARENT_SCOPE)
endfunction()

# A delete task must retain the identity that caused it to be queued. Looking
# the object up only later by its old path is unsafe because an earlier MOVE
# task may already have migrated tracked_objects away from that path.
fhm_move_replace_once([=[std::unordered_set<std::wstring> g_deletePending;
std::unordered_set<std::wstring> g_tombstones;]=]
[=[std::unordered_set<std::wstring> g_deletePending;
std::unordered_set<std::wstring> g_tombstones;

struct DeleteIdentityHint {
    std::wstring volumeId;
    std::wstring relativePath;
    std::wstring objectId;
    bool isDirectory = false;
};
std::unordered_map<std::wstring, DeleteIdentityHint> g_deleteIdentityHints;]=]
"queued delete identity state")

fhm_move_replace_once([=[void HandleObservedRemoval(const std::wstring& path);

bool g_runtimeCacheDirty]=]
[=[void HandleObservedRemoval(const std::wstring& path);
std::optional<fhm::FolderIdentity> DeletedPathIdentity(const std::wstring& deletedPath);

bool g_runtimeCacheDirty]=]
"deleted path identity forward declaration")

fhm_move_replace_once([=[    if (IsVolumeRoot(key)) {
        g_log.WritePath("LIFECYCLE", "ROOT_DELETE_BLOCKED", key);
        return;
    }

    bool newlyQueued = false;]=]
[=[    if (IsVolumeRoot(key)) {
        g_log.WritePath("LIFECYCLE", "ROOT_DELETE_BLOCKED", key);
        return;
    }

    std::optional<DeleteIdentityHint> identityHint;
    if (const auto identity = DeletedPathIdentity(key)) {
        const auto tracked = g_readDatabase.GetTrackedObjectAtPath(identity->volumeId, identity->relativePath);
        if (tracked && !tracked->objectId.empty()) {
            DeleteIdentityHint hint;
            hint.volumeId = identity->volumeId;
            hint.relativePath = identity->relativePath;
            hint.objectId = tracked->objectId;
            hint.isDirectory = tracked->isDirectory;
            identityHint = std::move(hint);
        }
    }

    bool newlyQueued = false;]=]
"capture File ID when removal is observed")

fhm_move_replace_once([=[        g_tombstones.insert(key);
        if (g_deletePending.insert(key).second) { g_deleteQueue.push_back(key); newlyQueued = true; }]=]
[=[        g_tombstones.insert(key);
        if (identityHint) g_deleteIdentityHints[key] = *identityHint;
        if (g_deletePending.insert(key).second) { g_deleteQueue.push_back(key); newlyQueued = true; }]=]
"store queued File ID")

string(FIND "${ENGINE}" "coveredDescendants" HAS_COALESCING)
if(NOT HAS_COALESCING EQUAL -1)
    fhm_move_replace_once([=[        for (const auto& descendant : coveredDescendants) {
            g_deletePending.erase(descendant);
            g_deleteQueue.erase(std::remove(g_deleteQueue.begin(), g_deleteQueue.end(), descendant), g_deleteQueue.end());
        }]=]
[=[        for (const auto& descendant : coveredDescendants) {
            g_deletePending.erase(descendant);
            g_deleteIdentityHints.erase(descendant);
            g_deleteQueue.erase(std::remove(g_deleteQueue.begin(), g_deleteQueue.end(), descendant), g_deleteQueue.end());
        }]=]
"coalesced delete identity cleanup")
endif()

set(OLD [=[void ProcessDeleteTask(const std::wstring& path) {
    if (IsVolumeRoot(path)) { g_log.WritePath("LIFECYCLE", "ROOT_PURGE_BLOCKED", path); return; }
    const auto identity = DeletedPathIdentity(path);
    bool ok = identity && g_writeDatabase.ResetRecursiveActivity(*identity);
    if (ok) {
        std::scoped_lock lock(g_deleteMutex);
        g_deletePending.erase(path); g_tombstones.erase(path);
    }
    if (ok) {
        g_log.WritePath("LIFECYCLE", "purge_subtree completed", path);
        const auto parent = ExistingParent(path); if (!parent.empty()) QueueSlow(parent);
    } else g_log.WritePath("LIFECYCLE", "purge_subtree FAILED", path);
}]=])

set(NEW [=[void ProcessDeleteTask(const std::wstring& path) {
    if (IsVolumeRoot(path)) { g_log.WritePath("LIFECYCLE", "ROOT_PURGE_BLOCKED", path); return; }
    const auto identity = DeletedPathIdentity(path);

    std::optional<DeleteIdentityHint> queuedHint;
    {
        std::scoped_lock lock(g_deleteMutex);
        const auto it = g_deleteIdentityHints.find(path);
        if (it != g_deleteIdentityHints.end()) queuedHint = it->second;
    }

    std::wstring purgeObjectId = queuedHint ? queuedHint->objectId : L"";
    bool hadTrackedAtOldPath = false;

    if (identity) {
        const auto tracked = g_readDatabase.GetTrackedObjectAtPath(identity->volumeId, identity->relativePath);
        if (tracked && !tracked->objectId.empty()) {
            hadTrackedAtOldPath = true;
            purgeObjectId = tracked->objectId;
            std::optional<std::wstring> currentPath;
            for (int attempt = 0; attempt < 8 && !currentPath; ++attempt) {
                currentPath = fhm::ResolveFilesystemPathByObjectId(*identity, tracked->objectId, tracked->isDirectory);
                if (!currentPath && attempt != 7) Sleep(150);
            }

            if (currentPath) {
                const auto normalizedCurrent = fhm::runtime::NormalizePath(*currentPath);
                const auto currentIdentity = fhm::ResolveFolderIdentity(normalizedCurrent);
                bool recycle = false;
                if (currentIdentity) {
                    std::wstring relative = currentIdentity->relativePath;
                    std::transform(relative.begin(), relative.end(), relative.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
                    recycle = relative == L"$recycle.bin" || relative.starts_with(L"$recycle.bin\\");
                }

                if (currentIdentity && currentIdentity->volumeId == identity->volumeId && !recycle) {
                    if (_wcsicmp(normalizedCurrent.c_str(), path.c_str()) == 0) {
                        {
                            std::scoped_lock lock(g_deleteMutex);
                            g_deletePending.erase(path); g_tombstones.erase(path); g_deleteIdentityHints.erase(path);
                        }
                        g_log.WritePath("LIFECYCLE", "stale_removal_same_identity", path);
                        const auto parent = ExistingParent(path); if (!parent.empty()) QueueSlow(parent);
                        return;
                    }

                    const bool moved = g_writeDatabase.MoveTrackedObject(identity->volumeId, tracked->objectId, tracked->relativePath, currentIdentity->relativePath, tracked->isDirectory);
                    {
                        std::scoped_lock lock(g_deleteMutex);
                        g_deletePending.erase(path); g_tombstones.erase(path); g_deleteIdentityHints.erase(path);
                    }
                    if (moved) {
                        g_log.WritePath("LIFECYCLE", "move_migrated old", path);
                        g_log.WritePath("LIFECYCLE", "move_migrated new", normalizedCurrent);
                    } else {
                        g_log.WritePath("LIFECYCLE", "move_migration FAILED old", path);
                        g_log.WritePath("LIFECYCLE", "move_migration FAILED new", normalizedCurrent);
                    }
                    const auto oldParent = ExistingParent(path); const auto newParent = ExistingParent(normalizedCurrent);
                    if (!oldParent.empty()) QueueSlow(oldParent); if (!newParent.empty()) QueueSlow(newParent);
                    return;
                }
            }
        }
    }

    // If the queued File ID survived but the tracked row already moved away
    // from this stale old path, do not merely cancel DELETE. Reconcile the
    // database row for that exact File ID to its current filesystem path.
    if (!hadTrackedAtOldPath && identity && queuedHint && !queuedHint->objectId.empty() && queuedHint->volumeId == identity->volumeId) {
        std::optional<std::wstring> currentPath;
        for (int attempt = 0; attempt < 8 && !currentPath; ++attempt) {
            currentPath = fhm::ResolveFilesystemPathByObjectId(*identity, queuedHint->objectId, queuedHint->isDirectory);
            if (!currentPath && attempt != 7) Sleep(150);
        }
        if (currentPath) {
            const auto normalizedCurrent = fhm::runtime::NormalizePath(*currentPath);
            const auto currentIdentity = fhm::ResolveFolderIdentity(normalizedCurrent);
            bool recycle = false;
            if (currentIdentity) {
                std::wstring relative = currentIdentity->relativePath;
                std::transform(relative.begin(), relative.end(), relative.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
                recycle = relative == L"$recycle.bin" || relative.starts_with(L"$recycle.bin\\");
            }
            if (currentIdentity && currentIdentity->volumeId == identity->volumeId && !recycle) {
                bool reconciled = false;
                std::wstring databasePath;
                if (const auto trackedCurrent = g_readDatabase.GetTrackedObjectById(queuedHint->volumeId, queuedHint->objectId)) {
                    databasePath = trackedCurrent->relativePath;
                    if (_wcsicmp(databasePath.c_str(), currentIdentity->relativePath.c_str()) == 0) {
                        reconciled = true;
                    } else {
                        reconciled = g_writeDatabase.MoveTrackedObject(queuedHint->volumeId, queuedHint->objectId, databasePath, currentIdentity->relativePath, queuedHint->isDirectory);
                    }
                }

                {
                    std::scoped_lock lock(g_deleteMutex);
                    g_deletePending.erase(path); g_tombstones.erase(path); g_deleteIdentityHints.erase(path);
                }
                if (reconciled) {
                    g_log.WriteWide("LIFECYCLE", L"queued_identity_reconciled object_id=" + queuedHint->objectId + L" db=" + databasePath + L" current=" + normalizedCurrent);
                } else {
                    g_log.WriteWide("LIFECYCLE", L"queued_identity_survived_unreconciled object_id=" + queuedHint->objectId + L" current=" + normalizedCurrent);
                }
                const auto oldParent = ExistingParent(path); const auto newParent = ExistingParent(normalizedCurrent);
                if (!oldParent.empty()) QueueSlow(oldParent); if (!newParent.empty()) QueueSlow(newParent);
                return;
            }
        }
    }

    if (identity) {
        g_log.WriteWide("DB_DELETE_TRACE", L"source=watcher_purge action=RESET_RECURSIVE object_id=" + purgeObjectId + L" volume=" + identity->volumeId + L" relative=" + identity->relativePath + L" path=" + path);
    }
    const bool ok = identity && g_writeDatabase.ResetRecursiveActivity(*identity);
    if (ok) {
        std::scoped_lock lock(g_deleteMutex);
        g_deletePending.erase(path); g_tombstones.erase(path); g_deleteIdentityHints.erase(path);
    }
    if (ok) {
        g_log.WritePath("LIFECYCLE", "purge_subtree completed", path);
        const auto parent = ExistingParent(path); if (!parent.empty()) QueueSlow(parent);
    } else g_log.WritePath("LIFECYCLE", "purge_subtree FAILED", path);
}]=])

string(FIND "${ENGINE}" "${OLD}" POS)
if(POS EQUAL -1)
    message(FATAL_ERROR "FolderHeatMap 1.48 identity-first move anchor not found: ${INPUT}")
endif()
string(REPLACE "${OLD}" "${NEW}" ENGINE "${ENGINE}")
file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.48 surviving File ID path reconciliation: ${INPUT}")
