if(NOT DEFINED INPUT)
    message(FATAL_ERROR "CoalesceDeleteQueue.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

# 1.18-safe injection: the deletion patch already coalesces descendant
# tombstones inside HandleObservedRemoval. This step is therefore idempotent.
# Keep the file as a compatibility build stage so older CMakeLists/upgraders do
# not fail just because there is no longer a separate 1.17 anchor to replace.
string(FIND "${ENGINE}" "coveredDescendants" HAS_COALESCING)
if(NOT HAS_COALESCING EQUAL -1)
    message(STATUS "FolderHeatMap delete queue coalescing already present: ${INPUT}")
    return()
endif()

set(OLD [=[        for (auto it = g_tombstones.begin(); it != g_tombstones.end();) {
            if (CoveredByPath(*it, key)) it = g_tombstones.erase(it);
            else ++it;
        }
        g_tombstones.insert(key);]=])

set(NEW [=[        std::vector<std::wstring> coveredDescendants;
        for (auto it = g_tombstones.begin(); it != g_tombstones.end();) {
            if (CoveredByPath(*it, key)) {
                coveredDescendants.push_back(*it);
                it = g_tombstones.erase(it);
            } else ++it;
        }
        for (const auto& descendant : coveredDescendants) {
            g_deletePending.erase(descendant);
            g_deleteQueue.erase(std::remove(g_deleteQueue.begin(), g_deleteQueue.end(), descendant), g_deleteQueue.end());
        }
        g_tombstones.insert(key);]=])

string(FIND "${ENGINE}" "${OLD}" POS)
if(POS EQUAL -1)
    # Nothing to patch is not an error in 1.18. The lifecycle injector may
    # already contain the finalized coalescing implementation.
    message(STATUS "FolderHeatMap delete queue coalescing stage not required: ${INPUT}")
    return()
endif()

string(REPLACE "${OLD}" "${NEW}" ENGINE "${ENGINE}")
file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap delete queue coalescing: ${INPUT}")
