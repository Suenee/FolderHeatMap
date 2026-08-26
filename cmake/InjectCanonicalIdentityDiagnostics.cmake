if(NOT DEFINED INPUT)
    message(FATAL_ERROR "InjectCanonicalIdentityDiagnostics.cmake requires INPUT")
endif()
file(READ "${INPUT}" ENGINE)
set(OLD [=[            const auto currentObjectId = fhm::ResolveFilesystemObjectId(path, isDirectory);
            if (currentObjectId && *currentObjectId != tracked->objectId)
                g_log.WritePath("LIFECYCLE_DIAG", "identity_mismatch_non_destructive", path);]=])
set(NEW [=[            const auto currentIdentity = fhm::ResolveFilesystemIdentity(path, isDirectory);
            if (!currentIdentity) {
                g_log.WriteWide("IDENTITY_DIAG", L"unavailable path=" + path + L" stored_file_id=" + tracked->objectId);
            } else {
                const auto currentObjectId = fhm::EncodeFilesystemFileId(*currentIdentity);
                if (currentObjectId != tracked->objectId) {
                    g_log.WriteWide("IDENTITY_DIAG",
                        L"mismatch_non_destructive path=" + path +
                        L" stored_file_id=" + tracked->objectId + L" current_" +
                        fhm::DescribeFilesystemIdentity(*currentIdentity));
                } else {
                    g_log.WriteWide("IDENTITY_DIAG",
                        L"match path=" + path + L" current_" +
                        fhm::DescribeFilesystemIdentity(*currentIdentity));
                }
            }]=])
string(FIND "${ENGINE}" "${OLD}" POS)
if(POS EQUAL -1)
    message(FATAL_ERROR "1.20 canonical identity diagnostic anchor not found")
endif()
string(REPLACE "${OLD}" "${NEW}" ENGINE "${ENGINE}")
string(REPLACE "FolderHeatMap 1.18 safe lifecycle engine starting" "FolderHeatMap 1.20 canonical lifecycle engine starting" ENGINE "${ENGINE}")
file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.20 canonical filesystem identity diagnostics: ${INPUT}")
