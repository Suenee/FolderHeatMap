if(NOT DEFINED INPUT)
    message(FATAL_ERROR "ProtectSameVolumeMoves.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

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

    // Identity-first lifecycle: FILE_ACTION_REMOVED/RENAMED_OLD_NAME is only
    // a hint. The object may have moved elsewhere on the same volume, or a
    // queued removal may have become stale because a rapid round trip already
    // brought the same File ID back to this exact path.
    if (identity) {
        const auto tracked = g_readDatabase.GetTrackedObjectAtPath(identity->volumeId, identity->relativePath);
        if (tracked && !tracked->objectId.empty()) {
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
                    std::transform(relative.begin(), relative.end(), relative.begin(), [](wchar_t c) {
                        return static_cast<wchar_t>(std::towlower(c));
                    });
                    recycle = relative == L"$recycle.bin" || relative.starts_with(L"$recycle.bin\\");
                }

                if (currentIdentity && currentIdentity->volumeId == identity->volumeId && !recycle) {
                    if (_wcsicmp(normalizedCurrent.c_str(), path.c_str()) == 0) {
                        // The exact tracked File ID is alive again at the old
                        // path. This is a stale removal task from a rapid move
                        // round trip, not a deletion. Never purge its history.
                        {
                            std::scoped_lock lock(g_deleteMutex);
                            g_deletePending.erase(path);
                            g_tombstones.erase(path);
                        }
                        g_log.WritePath("LIFECYCLE", "stale_removal_same_identity", path);
                        const auto parent = ExistingParent(path);
                        if (!parent.empty()) QueueSlow(parent);
                        return;
                    }

                    const bool moved = g_writeDatabase.MoveTrackedObject(
                        identity->volumeId, tracked->objectId, tracked->relativePath,
                        currentIdentity->relativePath, tracked->isDirectory);

                    {
                        std::scoped_lock lock(g_deleteMutex);
                        g_deletePending.erase(path);
                        g_tombstones.erase(path);
                    }

                    if (moved) {
                        g_log.WritePath("LIFECYCLE", "move_migrated old", path);
                        g_log.WritePath("LIFECYCLE", "move_migrated new", normalizedCurrent);
                    } else {
                        // Never destroy history merely because migration failed.
                        // Reconciliation of the destination parent gets another
                        // chance to pair the same File ID with the new path.
                        g_log.WritePath("LIFECYCLE", "move_migration FAILED old", path);
                        g_log.WritePath("LIFECYCLE", "move_migration FAILED new", normalizedCurrent);
                    }

                    const auto oldParent = ExistingParent(path);
                    const auto newParent = ExistingParent(normalizedCurrent);
                    if (!oldParent.empty()) QueueSlow(oldParent);
                    if (!newParent.empty()) QueueSlow(newParent);
                    return;
                }
            }
        }
    }

    // No same-volume object with the tracked File ID survived: this is a real
    // deletion (or a recycle-bin move, which is intentionally treated as one).
    const bool ok = identity && g_writeDatabase.ResetRecursiveActivity(*identity);
    if (ok) {
        std::scoped_lock lock(g_deleteMutex);
        g_deletePending.erase(path); g_tombstones.erase(path);
    }
    if (ok) {
        g_log.WritePath("LIFECYCLE", "purge_subtree completed", path);
        const auto parent = ExistingParent(path); if (!parent.empty()) QueueSlow(parent);
    } else g_log.WritePath("LIFECYCLE", "purge_subtree FAILED", path);
}]=])

string(FIND "${ENGINE}" "move_migrated old" ALREADY_POS)
if(NOT ALREADY_POS EQUAL -1)
    message(STATUS "FolderHeatMap identity-first same-volume move handling already present: ${INPUT}")
    return()
endif()

string(FIND "${ENGINE}" "${OLD}" POS)
if(POS EQUAL -1)
    message(FATAL_ERROR "FolderHeatMap identity-first move anchor not found: ${INPUT}")
endif()

string(REPLACE "${OLD}" "${NEW}" ENGINE "${ENGINE}")
file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.40 identity-first same-volume move/rename handling: ${INPUT}")
