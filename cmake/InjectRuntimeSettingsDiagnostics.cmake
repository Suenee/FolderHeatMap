if(NOT DEFINED INPUT)
    message(FATAL_ERROR "InjectRuntimeSettingsDiagnostics.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "runtime settings patch anchor not found: ${LABEL}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" PATCHED "${ENGINE}")
    set(ENGINE "${PATCHED}" PARENT_SCOPE)
endfunction()

fhm_replace_once([=[    ReloadSettings();
    g_log.Initialize(g_settingsPath);
    g_log.Write("ENGINE", "FolderHeatMap 1.11 engine starting");]=]
[=[    ReloadSettings();
    g_log.Initialize(g_settingsPath);
    g_log.Write("ENGINE", "FolderHeatMap 1.11 engine starting");
    g_log.WriteWide("ENGINE", L"settings=" + g_settingsPath);
    g_log.WriteWide("ENGINE", L"database=" + databasePath);]=]
"startup logging and resolved runtime paths")

fhm_replace_once([=[        if (settingsSeq != seenSettings) {
            seenSettings = settingsSeq;
            ReloadSettings();
            g_log.Write("ENGINE", "settings reloaded");
        }]=]
[=[        if (settingsSeq != seenSettings) {
            seenSettings = settingsSeq;
            ReloadSettings();
            g_log.Initialize(g_settingsPath);
            g_log.Write("ENGINE", "settings reloaded; logger reinitialized");
            g_log.WriteWide("ENGINE", L"settings=" + g_settingsPath);
            g_log.WriteWide("ENGINE", L"database=" + databasePath);
        }]=]
"live settings and logger reload")

file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap runtime settings/logging diagnostics")
