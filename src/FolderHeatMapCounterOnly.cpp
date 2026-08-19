#include "RuntimeShared.h"
#include "WdxApi.h"

#include <windows.h>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
constexpr int kFieldHeat = 0;
constexpr int kFieldVisits = 1;
constexpr int kFieldLastVisit = 2;
constexpr int kFieldHeatLevel = 3;
constexpr int kFieldColorStep = 4;
constexpr int kFieldWrites = 5;
constexpr int kFieldLastWrite = 6;

HMODULE g_module = nullptr;
HANDLE g_mapping = nullptr;
fhm::runtime::SharedState* g_shared = nullptr;
bool g_clientRegistered = false;

void CopyAnsi(char* destination, int maxlen, const char* source) {
    if (destination && maxlen > 0) strncpy_s(destination, static_cast<size_t>(maxlen), source, _TRUNCATE);
}

std::wstring AnsiToWide(const char* text) {
    if (!text || !*text) return {};
    const int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, text, -1, out.data(), n)) return {};
    out.resize(static_cast<size_t>(n - 1));
    return out;
}

std::wstring DefaultIniPath(const ContentDefaultParamStruct* dps) {
    if (!dps || dps->DefaultIniName[0] == '\0') return {};
    return AnsiToWide(dps->DefaultIniName);
}

std::wstring DatabasePath(const std::wstring& defaultIni) {
    const std::filesystem::path ini(defaultIni);
    if (ini.has_parent_path()) return (ini.parent_path() / L"FolderHeatMap.db").wstring();
    return L"FolderHeatMap.db";
}

std::wstring EnginePath() {
    wchar_t modulePath[32768]{};
    if (!g_module || !GetModuleFileNameW(g_module, modulePath, static_cast<DWORD>(std::size(modulePath)))) return {};
    return (std::filesystem::path(modulePath).parent_path() / L"FolderHeatMapEngine.exe").wstring();
}

void OpenSharedMemory() {
    if (g_shared) return;
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(sizeof(fhm::runtime::SharedState)),
                                   fhm::runtime::kMappingName);
    if (!g_mapping) return;

    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
    g_shared = static_cast<fhm::runtime::SharedState*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(fhm::runtime::SharedState)));
    if (!g_shared) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return;
    }

    if (created || g_shared->magic != fhm::runtime::kMagic || g_shared->version != fhm::runtime::kVersion) {
        std::memset(g_shared, 0, sizeof(*g_shared));
        g_shared->magic = fhm::runtime::kMagic;
        g_shared->version = fhm::runtime::kVersion;
    }
}

void LaunchEngine(const ContentDefaultParamStruct* dps) {
    const std::wstring engine = EnginePath();
    if (engine.empty() || !std::filesystem::exists(engine)) return;

    HANDLE existing = OpenMutexW(SYNCHRONIZE, FALSE, fhm::runtime::kEngineMutexName);
    if (existing) {
        CloseHandle(existing);
        return;
    }

    const std::wstring db = DatabasePath(DefaultIniPath(dps));
    std::wstring command = L"\"" + engine + L"\" --db \"" + db + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(engine.c_str(), command.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

int StableValue(int fieldIndex, void* fieldValue, const fhm::runtime::CacheEntry* entry = nullptr) {
    switch (fieldIndex) {
        case kFieldHeat:
            *static_cast<double*>(fieldValue) = 0.0;
            return ft_numeric_floating;
        case kFieldVisits:
            *static_cast<__int64*>(fieldValue) = entry ? entry->visits : 0;
            return ft_numeric_64;
        case kFieldHeatLevel:
        case kFieldColorStep:
            *static_cast<int*>(fieldValue) = 0;
            return ft_numeric_32;
        case kFieldWrites:
            *static_cast<__int64*>(fieldValue) = 0;
            return ft_numeric_64;
        case kFieldLastVisit:
        case kFieldLastWrite:
            return ft_fieldempty;
        default:
            return ft_nosuchfield;
    }
}

int ReadVisitsOnly(const wchar_t* fileName, int fieldIndex, void* fieldValue) {
    if (fieldIndex != kFieldVisits)
        return StableValue(fieldIndex, fieldValue);

    if (!g_shared || g_shared->magic != fhm::runtime::kMagic || g_shared->version != fhm::runtime::kVersion)
        return StableValue(fieldIndex, fieldValue);

    std::uint32_t pathLength = 0;
    const std::uint64_t pathHash = fhm::runtime::HashNormalizedPath(fileName, pathLength);
    if (!pathHash) return StableValue(fieldIndex, fieldValue);

    const LONG active = InterlockedCompareExchange(&g_shared->activeBuffer, 0, 0) & 1;
    auto& buffer = g_shared->buffers[active];
    InterlockedIncrement(&buffer.readers);
    MemoryBarrier();
    const auto* entry = fhm::runtime::FindEntry(buffer, pathHash, pathLength);
    const int result = StableValue(fieldIndex, fieldValue, entry);
    InterlockedDecrement(&buffer.readers);
    return result;
}

void PublishNavigation(const wchar_t* path) {
    if (!g_shared || !path || !*path) return;
    const std::wstring normalized = fhm::runtime::NormalizePath(path);
    wcsncpy_s(g_shared->currentDirectory, normalized.c_str(), _TRUNCATE);
    MemoryBarrier();
    InterlockedIncrement(&g_shared->navigationSeq);
}

void CloseRuntime() {
    if (g_shared && g_clientRegistered) {
        const LONG clients = InterlockedDecrement(&g_shared->clientCount);
        g_clientRegistered = false;
        if (clients <= 0) InterlockedExchange(&g_shared->shutdownRequested, 1);
    }
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

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void __stdcall ContentSetDefaultParams(ContentDefaultParamStruct* dps) {
    OpenSharedMemory();
    if (g_shared && !g_clientRegistered) {
        InterlockedIncrement(&g_shared->clientCount);
        InterlockedExchange(&g_shared->shutdownRequested, 0);
        g_clientRegistered = true;
    }
    LaunchEngine(dps);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetSupportedField(int fieldIndex, char* fieldName, char* units, int maxlen) {
    if (units && maxlen > 0) units[0] = '\0';
    switch (fieldIndex) {
        case kFieldHeat: CopyAnsi(fieldName, maxlen, "Heat"); return ft_numeric_floating;
        case kFieldVisits: CopyAnsi(fieldName, maxlen, "Visits"); return ft_numeric_64;
        case kFieldLastVisit: CopyAnsi(fieldName, maxlen, "Last Visit"); return ft_datetime;
        case kFieldHeatLevel: CopyAnsi(fieldName, maxlen, "Heat Level"); return ft_numeric_32;
        case kFieldColorStep: CopyAnsi(fieldName, maxlen, "Heat Color Step"); return ft_numeric_32;
        case kFieldWrites: CopyAnsi(fieldName, maxlen, "Writes"); return ft_numeric_64;
        case kFieldLastWrite: CopyAnsi(fieldName, maxlen, "Last Write"); return ft_datetime;
        default: return ft_nomorefields;
    }
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValueW(WCHAR* fileName, int fieldIndex, int, void* fieldValue, int, int) {
    if (!fileName || !fieldValue) return ft_fileerror;
    return ReadVisitsOnly(fileName, fieldIndex, fieldValue);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValue(char* fileName, int fieldIndex, int unitIndex, void* fieldValue, int maxlen, int flags) {
    const auto wide = AnsiToWide(fileName);
    if (wide.empty()) return ft_fileerror;
    return ContentGetValueW(const_cast<WCHAR*>(wide.c_str()), fieldIndex, unitIndex, fieldValue, maxlen, flags);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetDefaultSortOrder(int fieldIndex) {
    return (fieldIndex >= kFieldHeat && fieldIndex <= kFieldLastWrite) ? -1 : 1;
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformationW(int state, WCHAR* path) {
    if (state == contst_readnewdir || state == contst_refreshpressed) PublishNavigation(path);
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformation(int state, char* path) {
    const auto wide = AnsiToWide(path);
    ContentSendStateInformationW(state, wide.empty() ? nullptr : const_cast<WCHAR*>(wide.c_str()));
}

extern "C" __declspec(dllexport) void __stdcall ContentPluginUnloading() {
    CloseRuntime();
}
