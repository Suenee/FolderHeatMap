if(NOT DEFINED INPUT)
    message(FATAL_ERROR "ProtectSameVolumeMoves.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

set(OLD [=[void ProcessDeleteTask(const std::wstring& path) {
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
}]=])

set(NEW [=[void ProcessDeleteTask(const std::wstring& path) {
    const auto identity = DeletedPathIdentity(path);

    // A FILE_ACTION_REMOVED from the watched directory can also mean that the
    // object was moved elsewhere on the SAME volume. Before purging history,
    // resolve the previously tracked File ID. If it still exists outside the
    // recycle bin, this is a move and history must be preserved.
    if (identity) {
        const auto tracked = g_readDatabase.GetTrackedObjectAtPath(identity->volumeId, identity->relativePath);
        if (tracked) {
            const auto currentPath = fhm::ResolveFilesystemPathByObjectId(*identity, tracked->objectId, tracked->isDirectory);
            if (currentPath) {
                const auto currentIdentity = fhm::ResolveFolderIdentity(*currentPath);
                bool recycle = false;
                if (currentIdentity) {
                    std::wstring relative = currentIdentity->relativePath;
                    std::transform(relative.begin(), relative.end(), relative.begin(), [](wchar_t c) {
                        return static_cast<wchar_t>(std::towlower(c));
                    });
                    recycle = relative == L"$recycle.bin" || relative.starts_with(L"$recycle.bin\\");
                }
                const auto normalizedCurrent = fhm::runtime::NormalizePath(*currentPath);
                if (currentIdentity && currentIdentity->volumeId == identity->volumeId && !recycle &&
                    _wcsicmp(normalizedCurrent.c_str(), path.c_str()) != 0) {
                    {
                        std::scoped_lock lock(g_deleteMutex);
                        g_deletePending.erase(path);
                        g_tombstones.erase(path);
                    }
                    g_log.WritePath("LIFECYCLE", "move_preserved old", path);
                    g_log.WritePath("LIFECYCLE", "move_preserved new", normalizedCurrent);
                    const auto oldParent = ExistingParent(path);
                    const auto newParent = ExistingParent(normalizedCurrent);
                    if (!oldParent.empty()) QueueSlow(oldParent);
                    if (!newParent.empty()) QueueSlow(newParent);
                    return;
                }
            }
        }
    }

    const bool ok = identity && g_writeDatabase.ResetRecursiveActivity(*identity);
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
}]=])

# 1.18 may already contain the same-volume move protection directly in the
# primary lifecycle injection. In that case this compatibility stage must be a
# no-op rather than failing configuration.
string(FIND "${ENGINE}" "move_preserved old" ALREADY_POS)
if(NOT ALREADY_POS EQUAL -1)
    message(STATUS "FolderHeatMap same-volume move protection stage not required: ${INPUT}")
    return()
endif()

string(FIND "${ENGINE}" "${OLD}" POS)
if(POS EQUAL -1)
    message(STATUS "FolderHeatMap same-volume move protection anchor not present; compatibility stage skipped: ${INPUT}")
    return()
endif()

string(REPLACE "${OLD}" "${NEW}" ENGINE "${ENGINE}")
file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap same-volume move protection: ${INPUT}")
