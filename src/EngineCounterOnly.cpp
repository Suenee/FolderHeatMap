#include "Database.h"
#include "FolderIdentity.h"
#include "RuntimeShared.h"

#include <windows.h>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace {
fhm::Database g_database;
HANDLE g_mapping = nullptr;
fhm::runtime::SharedState* g_shared = nullptr;
HANDLE g_engineMutex = nullptr;
std::unordered_map<std::wstring, std::int64_t> g_visits;

std::wstring ArgValue(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], name) == 0) return argv[i + 1];
    }
    return {};
}

bool OpenRuntime() {
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(sizeof(fhm::runtime::SharedState)),
                                   fhm::runtime::kMappingName);
    if (!g_mapping) return false;

    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
    g_shared = static_cast<fhm::runtime::SharedState*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(fhm::runtime::SharedState)));
    if (!g_shared) return false;

    if (created || g_shared->magic != fhm::runtime::kMagic || g_shared->version != fhm::runtime::kVersion) {
        std::memset(g_shared, 0, sizeof(*g_shared));
        g_shared->magic = fhm::runtime::kMagic;
        g_shared->version = fhm::runtime::kVersion;
    }
    return true;
}

void PublishVisits() {
    if (!g_shared) return;

    const LONG active = InterlockedCompareExchange(&g_shared->activeBuffer, 0, 0) & 1;
    const LONG inactive = active ^ 1;
    auto& buffer = g_shared->buffers[inactive];

    while (InterlockedCompareExchange(&buffer.readers, 0, 0) != 0) Sleep(1);
    std::memset(buffer.entries, 0, sizeof(buffer.entries));
    InterlockedExchange(&buffer.count, 0);

    for (const auto& [path, visits] : g_visits) {
        fhm::runtime::CacheEntry entry{};
        std::uint32_t length = 0;
        entry.pathHash = fhm::runtime::HashNormalizedPath(path.c_str(), length);
        entry.pathLength = length;
        entry.flags = fhm::runtime::kFlagDirectory;
        entry.visits = visits;
        if (!fhm::runtime::InsertEntry(buffer, entry)) break;
    }

    MemoryBarrier();
    InterlockedExchange(&g_shared->activeBuffer, inactive);
    InterlockedIncrement(&g_shared->generation);
}

void RecordPath(const std::wstring& path) {
    if (path.empty()) return;
    const auto identity = fhm::ResolveFolderIdentity(path);
    if (!identity) return;

    FILETIME now{};
    GetSystemTimeAsFileTime(&now);

    // Counter-only baseline: only raw visits matter. Heat math, file scanning,
    // prediction and inherited calculations are intentionally disabled.
    if (!g_database.RecordVisit(*identity, now, 0, 1)) return;

    const auto activity = g_database.GetActivity(*identity);
    if (!activity) return;

    g_visits[fhm::runtime::NormalizePath(path)] = static_cast<std::int64_t>(activity->visits);
    PublishVisits();
}

std::wstring ReadNavigationStable(LONG sequence) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        wchar_t buffer[fhm::runtime::kDirectoryChars]{};
        wcsncpy_s(buffer, g_shared->currentDirectory, _TRUNCATE);
        MemoryBarrier();
        if (InterlockedCompareExchange(&g_shared->navigationSeq, 0, 0) == sequence)
            return fhm::runtime::NormalizePath(buffer);
    }
    return {};
}

void CloseRuntime() {
    if (g_shared) {
        UnmapViewOfFile(g_shared);
        g_shared = nullptr;
    }
    if (g_mapping) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    g_engineMutex = CreateMutexW(nullptr, TRUE, fhm::runtime::kEngineMutexName);
    if (!g_engineMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_engineMutex) CloseHandle(g_engineMutex);
        return 0;
    }

    if (!OpenRuntime()) return 2;

    const std::wstring databasePath = ArgValue(argc, argv, L"--db");
    if (!databasePath.empty() && !g_database.Open(databasePath)) return 3;

    LONG seen = InterlockedCompareExchange(&g_shared->navigationSeq, 0, 0);
    while (true) {
        const LONG current = InterlockedCompareExchange(&g_shared->navigationSeq, 0, 0);
        if (current != seen) {
            seen = current;
            const auto path = ReadNavigationStable(current);
            if (!path.empty()) RecordPath(path);
        }

        const LONG clients = InterlockedCompareExchange(&g_shared->clientCount, 0, 0);
        const LONG shutdown = InterlockedCompareExchange(&g_shared->shutdownRequested, 0, 0);
        if (shutdown != 0 && clients <= 0) break;
        Sleep(15);
    }

    g_database.Close();
    CloseRuntime();
    if (g_engineMutex) {
        ReleaseMutex(g_engineMutex);
        CloseHandle(g_engineMutex);
    }
    return 0;
}
