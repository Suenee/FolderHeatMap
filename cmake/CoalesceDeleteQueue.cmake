if(NOT DEFINED INPUT)
    message(FATAL_ERROR "CoalesceDeleteQueue.cmake requires INPUT")
endif()

file(READ "${INPUT}" ENGINE)

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
    message(FATAL_ERROR "1.17 delete coalescing anchor not found")
endif()
string(REPLACE "${OLD}" "${NEW}" ENGINE "${ENGINE}")
file(WRITE "${INPUT}" "${ENGINE}")
message(STATUS "Injected FolderHeatMap 1.17 delete queue coalescing: ${INPUT}")
