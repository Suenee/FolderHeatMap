if(NOT DEFINED INPUT)
    message(FATAL_ERROR "InjectTcNavigationMonitor.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "1.44 TC navigation monitor anchor not found: ${LABEL}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" PATCHED "${ENGINE}")
    set(ENGINE "${PATCHED}" PARENT_SCOPE)
endfunction()

fhm_replace_once([=[#include "Settings.h"]=]
[=[#include "Settings.h"
#include "TotalCommanderNavigationMonitor.h"]=]
"monitor include")

fhm_replace_once([=[    InterlockedExchange(&g_shared->shutdownRequested, 0);
    std::thread fast(FastWorker);
    std::thread slow(SlowWorker);
    std::thread diagnostics(fhm::RunDeletionDiagnostics, g_shared, &g_log, &g_stopping,
                                            &HandleObservedRemoval, &HandleObservedModification);

    LONG seenSettings = InterlockedCompareExchange(&g_shared->settingsSeq, 0, 0);
    while (true) {]=]
[=[    InterlockedExchange(&g_shared->shutdownRequested, 0);
    std::thread fast(FastWorker);
    std::thread slow(SlowWorker);
    std::thread diagnostics(fhm::RunDeletionDiagnostics, g_shared, &g_log, &g_stopping,
                                            &HandleObservedRemoval, &HandleObservedModification);

    fhm::TotalCommanderNavigationMonitor tcNavigation([&](int panel, const std::wstring& rawPath) {
        const auto path = fhm::runtime::NormalizePath(rawPath);
        if (path.empty()) return;
        g_log.WriteWide("NAV-TC", (panel == 0 ? L"LEFT accepted " : L"RIGHT accepted ") + path);
        wcsncpy_s(g_shared->currentDirectory, path.c_str(), _TRUNCATE);
        MemoryBarrier();
        InterlockedIncrement(&g_shared->navigationSeq);
    });
    auto nextTcPoll = std::chrono::steady_clock::now();

    LONG seenSettings = InterlockedCompareExchange(&g_shared->settingsSeq, 0, 0);
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextTcPoll) {
            tcNavigation.Poll();
            nextTcPoll = now + std::chrono::milliseconds(100);
        }]=]
"monitor startup and polling")

string(FIND "${ENGINE}" "FolderHeatMap 1.20 canonical lifecycle engine starting" VERSION_POS)
if(VERSION_POS EQUAL -1)
    message(FATAL_ERROR "1.44 engine version banner anchor not found")
endif()
string(REPLACE "FolderHeatMap 1.20 canonical lifecycle engine starting"
               "FolderHeatMap 1.44 engine starting (destructive lifecycle tracing)" ENGINE "${ENGINE}")

file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.44 independent Total Commander navigation monitor: ${INPUT}")
