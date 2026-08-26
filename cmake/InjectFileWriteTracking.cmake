if(NOT DEFINED INPUT)
    message(FATAL_ERROR "InjectFileWriteTracking.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_write_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "1.28 file write patch anchor not found: ${LABEL}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" PATCHED "${ENGINE}")
    set(ENGINE "${PATCHED}" PARENT_SCOPE)
endfunction()

fhm_write_replace_once([=[void HandleObservedRemoval(const std::wstring& path);]=]
[=[void HandleObservedRemoval(const std::wstring& path);
void HandleObservedModification(const std::wstring& path);]=]
"modification callback declaration")

fhm_write_replace_once([=[void QueuePrediction(const std::wstring& directory) {]=]
[=[void HandleObservedModification(const std::wstring& path) {
    const auto key = fhm::runtime::NormalizePath(path);
    if (key.empty() || IsTombstoned(key)) return;

    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(key.c_str(), GetFileExInfoStandard, &data)) return;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return;

    const auto settings = SettingsSnapshot();
    if (!settings.fileHeatEnabled) return;

    const auto identity = fhm::ResolveFolderIdentity(key);
    if (!identity) return;
    if (!g_writeDatabase.ObserveFileWrite(*identity, data.ftLastWriteTime)) return;

    const double halfLife = EffectiveHalfLifeDays(settings);
    const auto fileSnapshot = BuildSnapshot(key, false, settings, halfLife);
    const auto parent = ExistingParent(key);
    const auto parentSnapshot = parent.empty() ? std::optional<Snapshot>{} : BuildSnapshot(parent, true, settings, halfLife);

    {
        std::scoped_lock lock(g_stateMutex);
        bool changed = false;
        if (fileSnapshot) {
            g_ram[key] = *fileSnapshot;
            changed = true;
        }
        if (parentSnapshot) {
            g_ram[fhm::runtime::NormalizePath(parent)] = *parentSnapshot;
            changed = true;
        }
        if (changed) {
            g_runtimeCacheDirty = true;
            PublishRamLocked();
        }
    }

    g_log.WritePath("FILE_WRITE", "persisted", key);
    MaybeSaveRamPersistence(false);
}

void QueuePrediction(const std::wstring& directory) {]=]
"file write persistence callback")

fhm_write_replace_once([=[std::thread diagnostics(fhm::RunDeletionDiagnostics, g_shared, &g_log, &g_stopping, &HandleObservedRemoval);]=]
[=[std::thread diagnostics(fhm::RunDeletionDiagnostics, g_shared, &g_log, &g_stopping,
                                            &HandleObservedRemoval, &HandleObservedModification);]=]
"watcher callback wiring")

file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.28 file write tracking: ${INPUT}")
