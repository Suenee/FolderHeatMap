if(NOT DEFINED INPUT)
    message(FATAL_ERROR "InjectDeletionDiagnostics.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_diag_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "1.16 diagnostic patch anchor not found: ${LABEL}")
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

fhm_diag_replace_once([=[    g_slowQueue.push_back(key);
    g_slowCv.notify_one();]=]
[=[    g_slowQueue.push_back(key);
    if (g_shared) {
        InterlockedExchange(&g_shared->slowQueueDepth, static_cast<LONG>(g_slowQueue.size()));
        InterlockedExchange(&g_shared->slowPendingCount, static_cast<LONG>(g_slowPending.size()));
    }
    g_slowCv.notify_one();]=]
"queue telemetry")

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

fhm_diag_replace_once([=[            task = std::move(g_slowQueue.front());
            g_slowQueue.pop_front();
        }
        ProcessSlowTask(task);]=]
[=[            task = std::move(g_slowQueue.front());
            g_slowQueue.pop_front();
            if (g_shared) {
                InterlockedExchange(&g_shared->slowQueueDepth, static_cast<LONG>(g_slowQueue.size()));
                InterlockedExchange(&g_shared->slowPendingCount, static_cast<LONG>(g_slowPending.size()));
            }
        }
        ProcessSlowTask(task);]=]
"slow dequeue telemetry")

fhm_diag_replace_once([=[    std::thread fast(FastWorker);
    std::thread slow(SlowWorker);]=]
[=[    std::thread fast(FastWorker);
    std::thread slow(SlowWorker);
    std::thread diagnostics(fhm::RunDeletionDiagnostics, g_shared, &g_log, &g_stopping);]=]
"diagnostic thread start")

fhm_diag_replace_once([=[    if (fast.joinable()) fast.join();
    if (slow.joinable()) slow.join();]=]
[=[    if (fast.joinable()) fast.join();
    if (slow.joinable()) slow.join();
    if (diagnostics.joinable()) diagnostics.join();]=]
"diagnostic thread join")

fhm_diag_replace_once([=[g_log.Write("ENGINE", "FolderHeatMap 1.11 engine starting");]=]
[=[g_log.Write("ENGINE", "FolderHeatMap 1.16 diagnostic engine starting");]=]
"diagnostic engine version")

file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.16 deletion diagnostics: ${INPUT}")
