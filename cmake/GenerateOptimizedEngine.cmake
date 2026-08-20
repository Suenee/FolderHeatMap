# FolderHeatMap 1.10 engine optimization generator.
#
# This deliberately patches a generated build copy of EngineApp.cpp instead of
# rewriting the large, already-tested source file through GitHub's whole-file
# API. Every replacement is guarded: configuration fails immediately if the
# expected 1.09 source anchors no longer match.

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "GenerateOptimizedEngine.cmake requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" ENGINE)

function(fhm_replace_once OLD NEW LABEL)
    string(FIND "${ENGINE}" "${OLD}" POS)
    if(POS EQUAL -1)
        message(FATAL_ERROR "1.10 engine patch anchor not found: ${LABEL}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" PATCHED "${ENGINE}")
    set(ENGINE "${PATCHED}" PARENT_SCOPE)
endfunction()

fhm_replace_once(
"std::atomic<bool> g_stopping{false};"
"std::atomic<bool> g_stopping{false};\nbool g_runtimeCacheDirty = false;"
"runtime-cache dirty flag")

fhm_replace_once(
"ULONGLONG FileTimeTicks(const FILETIME& time) {\n    ULARGE_INTEGER v{};\n    v.LowPart = time.dwLowDateTime;\n    v.HighPart = time.dwHighDateTime;\n    return v.QuadPart;\n}"
"ULONGLONG FileTimeTicks(const FILETIME& time) {\n    ULARGE_INTEGER v{};\n    v.LowPart = time.dwLowDateTime;\n    v.HighPart = time.dwHighDateTime;\n    return v.QuadPart;\n}\n\nbool SnapshotEquivalent(const Snapshot& a, const Snapshot& b) {\n    constexpr double kHeatEpsilon = 0.001;\n    return a.isDirectory == b.isDirectory &&\n           a.fileHeatAvailable == b.fileHeatAvailable &&\n           std::abs(a.heat - b.heat) < kHeatEpsilon &&\n           a.visits == b.visits && a.writes == b.writes &&\n           a.hasLastVisit == b.hasLastVisit && a.hasLastWrite == b.hasLastWrite &&\n           FileTimeTicks(a.lastVisit) == FileTimeTicks(b.lastVisit) &&\n           FileTimeTicks(a.lastWrite) == FileTimeTicks(b.lastWrite) &&\n           a.heatLevel == b.heatLevel && a.colorStep == b.colorStep;\n}"
"snapshot equivalence")

fhm_replace_once(
"void SaveRamPersistence() {\n    std::vector<fhm::RuntimeCacheRecord> records;\n    {\n        std::scoped_lock lock(g_stateMutex);\n        records.reserve(g_ram.size());\n        for (const auto& [path, snapshot] : g_ram) records.push_back(ToRuntimeRecord(path, snapshot));\n    }\n    if (g_writeDatabase.SaveRuntimeCache(records))\n        g_log.Write(\"DB\", \"saved runtime cache records=\" + std::to_string(records.size()));\n}"
"void SaveRamPersistence() {\n    std::vector<fhm::RuntimeCacheRecord> records;\n    {\n        std::scoped_lock lock(g_stateMutex);\n        if (!g_runtimeCacheDirty) {\n            g_log.Write(\"DB\", \"runtime cache unchanged; save skipped\");\n            return;\n        }\n        records.reserve(g_ram.size());\n        for (const auto& [path, snapshot] : g_ram) records.push_back(ToRuntimeRecord(path, snapshot));\n        g_runtimeCacheDirty = false;\n    }\n    if (g_writeDatabase.SaveRuntimeCache(records)) {\n        g_log.Write(\"DB\", \"saved runtime cache records=\" + std::to_string(records.size()));\n    } else {\n        std::scoped_lock lock(g_stateMutex);\n        g_runtimeCacheDirty = true;\n    }\n}"
"dirty-only runtime persistence")

fhm_replace_once(
"        PublishRamLocked();\n    }\n    g_log.Write(\"DB\", \"restored runtime cache records=\" + std::to_string(records.size()));"
"        PublishRamLocked();\n        g_runtimeCacheDirty = false;\n    }\n    g_log.Write(\"DB\", \"restored runtime cache records=\" + std::to_string(records.size()));"
"restore dirty reset")

fhm_replace_once(
"void MergeAndPublishLocked(const Batch& batch) {\n    for (const auto& [path, snapshot] : batch) g_ram[path] = snapshot;\n    PublishRamLocked();\n}"
"void MergeAndPublishLocked(const Batch& batch) {\n    bool changed = false;\n    for (const auto& [path, snapshot] : batch) {\n        const auto it = g_ram.find(path);\n        if (it != g_ram.end() && SnapshotEquivalent(it->second, snapshot)) continue;\n        g_ram[path] = snapshot;\n        changed = true;\n    }\n    if (!changed) {\n        g_log.Write(\"CACHE\", \"unchanged batch; publish skipped\");\n        return;\n    }\n    g_runtimeCacheDirty = true;\n    PublishRamLocked();\n}"
"no-op batch suppression")

fhm_replace_once(
"    std::wstring previous;\n    {\n        std::scoped_lock lock(g_stateMutex);\n        previous = g_currentDirectory;\n        g_currentDirectory = next;\n    }"
"    std::wstring previous;\n    {\n        std::scoped_lock lock(g_stateMutex);\n        if (g_currentDirectory == next) {\n            g_log.WritePath(\"FAST\", \"duplicate navigation suppressed\", next);\n            return;\n        }\n        previous = g_currentDirectory;\n        g_currentDirectory = next;\n    }"
"duplicate navigation suppression")

fhm_replace_once(
"        const auto key = fhm::runtime::NormalizePath(directory);\n        std::scoped_lock lock(g_stateMutex);\n        g_ram[key] = std::move(*ownSnapshot);\n        PublishRamLocked();"
"        const auto key = fhm::runtime::NormalizePath(directory);\n        std::scoped_lock lock(g_stateMutex);\n        const auto it = g_ram.find(key);\n        if (it == g_ram.end() || !SnapshotEquivalent(it->second, *ownSnapshot)) {\n            g_ram[key] = std::move(*ownSnapshot);\n            g_runtimeCacheDirty = true;\n            PublishRamLocked();\n        } else {\n            g_log.Write(\"CACHE\", \"own snapshot unchanged; publish skipped\");\n        }"
"own-snapshot no-op suppression")

fhm_replace_once(
"    StoreReady(directory, refreshed);\n    QueueHotPredictions(refreshed);"
"    StoreReady(directory, refreshed);\n    // Navigation already queued the hot children. Do not schedule the same\n    // prediction set again from the SLOW refresh; the next real navigation\n    // will naturally re-rank candidates from fresh data."
"duplicate prediction suppression")

fhm_replace_once(
"        for (const auto& [directory, batch] : g_ready)\n            for (const auto& [path, snapshot] : batch) g_ram[path] = snapshot;\n        PublishRamLocked();"
"        bool shutdownChanged = false;\n        for (const auto& [directory, batch] : g_ready) {\n            for (const auto& [path, snapshot] : batch) {\n                const auto it = g_ram.find(path);\n                if (it != g_ram.end() && SnapshotEquivalent(it->second, snapshot)) continue;\n                g_ram[path] = snapshot;\n                shutdownChanged = true;\n            }\n        }\n        if (shutdownChanged) {\n            g_runtimeCacheDirty = true;\n            PublishRamLocked();\n        }"
"shutdown coalescing")

fhm_replace_once(
"g_log.Write(\"ENGINE\", \"FolderHeatMap 1.07 engine starting\");"
"g_log.Write(\"ENGINE\", \"FolderHeatMap 1.10 engine starting\");"
"engine version log")

get_filename_component(OUT_DIR "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUT_DIR}")
file(WRITE "${OUTPUT}" "${ENGINE}")
message(STATUS "Generated optimized FolderHeatMap 1.10 engine source: ${OUTPUT}")
