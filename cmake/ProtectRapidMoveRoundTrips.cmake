if(NOT DEFINED INPUT)
    message(FATAL_ERROR "ProtectRapidMoveRoundTrips.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_roundtrip_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "1.48 rapid MOVE round-trip anchor not found: ${LABEL}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" PATCHED "${ENGINE}")
    set(ENGINE "${PATCHED}" PARENT_SCOPE)
endfunction()

# 1.49 build-stage integration helper for the 1.48 generated engine. The
# canonical lifecycle logic remains in ProtectSameVolumeMoves.cmake; this stage
# only adds durable path -> File ID memory at stable 1.48 anchors.
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

file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.49 rapid MOVE path identity memory: ${INPUT}")
