if(NOT DEFINED INPUT)
    message(FATAL_ERROR "ProtectRapidMoveRoundTrips.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_roundtrip_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "1.50 rapid MOVE round-trip anchor not found: ${LABEL}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" PATCHED "${ENGINE}")
    set(ENGINE "${PATCHED}" PARENT_SCOPE)
endfunction()

# Durable path -> File ID memory survives individual delete tasks. This is
# required when the watcher sees a rapid round trip only from one endpoint.
fhm_roundtrip_replace_once([=[std::unordered_map<std::wstring, DeleteIdentityHint> g_deleteIdentityHints;]=]
[=[std::unordered_map<std::wstring, DeleteIdentityHint> g_deleteIdentityHints;
std::unordered_map<std::wstring, DeleteIdentityHint> g_pathIdentityMemory;]=]
"path identity memory state")

fhm_roundtrip_replace_once([=[            hint.objectId = tracked->objectId;
            hint.isDirectory = tracked->isDirectory;
            identityHint = std::move(hint);
        }
    }

    bool newlyQueued = false;]=]
[=[            hint.objectId = tracked->objectId;
            hint.isDirectory = tracked->isDirectory;
            identityHint = hint;
            {
                std::scoped_lock lock(g_deleteMutex);
                g_pathIdentityMemory[key] = hint;
            }
        }
    }
    if (!identityHint) {
        std::scoped_lock lock(g_deleteMutex);
        const auto remembered = g_pathIdentityMemory.find(key);
        if (remembered != g_pathIdentityMemory.end()) identityHint = remembered->second;
    }

    bool newlyQueued = false;]=]
"fallback to last confirmed path identity")

fhm_roundtrip_replace_once([=[                    {
                        std::scoped_lock lock(g_deleteMutex);
                        g_deletePending.erase(path); g_tombstones.erase(path); g_deleteIdentityHints.erase(path);
                    }
                    if (moved) {]=]
[=[                    {
                        std::scoped_lock lock(g_deleteMutex);
                        DeleteIdentityHint remembered;
                        remembered.volumeId = identity->volumeId;
                        remembered.relativePath = currentIdentity->relativePath;
                        remembered.objectId = tracked->objectId;
                        remembered.isDirectory = tracked->isDirectory;
                        g_pathIdentityMemory[path] = remembered;
                        g_pathIdentityMemory[normalizedCurrent] = remembered;
                        g_deletePending.erase(path); g_tombstones.erase(path); g_deleteIdentityHints.erase(path);
                    }
                    if (moved) {]=]
"remember migrated identity at both endpoints")

# A watcher can miss the departure from the unwatched destination during a
# rapid DST -> SRC return. The subsequent ADDED event at the watched endpoint
# is therefore authoritative positive evidence: resolve the arriving object's
# canonical File ID and, if that File ID is tracked elsewhere, migrate the DB
# row to the path where the object physically exists now.
fhm_roundtrip_replace_once([=[void ProcessDeleteTask(const std::wstring& path) {]=]
[=[void HandleObservedArrival(const std::wstring& path) {
    const auto key = fhm::runtime::NormalizePath(path);
    if (key.empty()) return;

    const DWORD attrs = GetFileAttributesW(key.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return;
    const bool isDirectory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

    const auto identity = fhm::ResolveFolderIdentity(key);
    const auto objectId = fhm::ResolveFilesystemObjectId(key, isDirectory);
    if (!identity || !objectId || objectId->empty()) return;

    std::wstring relativeLower = identity->relativePath;
    std::transform(relativeLower.begin(), relativeLower.end(), relativeLower.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (relativeLower == L"$recycle.bin" || relativeLower.starts_with(L"$recycle.bin\\")) return;

    DeleteIdentityHint remembered;
    remembered.volumeId = identity->volumeId;
    remembered.relativePath = identity->relativePath;
    remembered.objectId = *objectId;
    remembered.isDirectory = isDirectory;
    {
        std::scoped_lock lock(g_deleteMutex);
        g_pathIdentityMemory[key] = remembered;
    }

    const auto tracked = g_readDatabase.GetTrackedObjectById(identity->volumeId, *objectId);
    if (!tracked) return;
    if (_wcsicmp(tracked->relativePath.c_str(), identity->relativePath.c_str()) == 0) {
        g_log.WriteWide("LIFECYCLE", L"arrival_identity_current object_id=" + *objectId + L" current=" + key);
        return;
    }

    const std::wstring databasePath = tracked->relativePath;
    const bool moved = g_writeDatabase.MoveTrackedObject(identity->volumeId, *objectId,
                                                          databasePath, identity->relativePath,
                                                          tracked->isDirectory);
    if (moved) {
        g_log.WriteWide("LIFECYCLE", L"arrival_identity_reconciled object_id=" + *objectId +
                                      L" db=" + databasePath + L" current=" + key);
        const auto parent = ExistingParent(key);
        if (!parent.empty()) QueueSlow(parent);
    } else {
        g_log.WriteWide("LIFECYCLE", L"arrival_identity_reconcile_FAILED object_id=" + *objectId +
                                      L" db=" + databasePath + L" current=" + key);
    }
}

void ProcessDeleteTask(const std::wstring& path) {]=]
"arrival-side File ID reconciliation")

file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.50 rapid MOVE arrival-side identity reconciliation: ${INPUT}")
