#include "Database.h"
#include "FolderIdentity.h"
#include "WdxApi.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
constexpr int kFieldHeat = 0;
constexpr int kFieldVisits = 1;
constexpr int kFieldLastVisit = 2;
constexpr int kFieldHeatLevel = 3;

fhm::Database g_database;

void CopyAnsi(char* destination, int maxlen, const char* source) {
    if (destination && maxlen > 0) {
        strncpy_s(destination, static_cast<size_t>(maxlen), source, _TRUNCATE);
    }
}

std::wstring AnsiToWide(const char* text) {
    if (!text || !*text) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (n <= 1) {
        return {};
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, text, -1, out.data(), n)) {
        return {};
    }
    out.resize(static_cast<size_t>(n - 1));
    return out;
}

std::wstring GetDatabasePath(const ContentDefaultParamStruct* dps) {
    if (dps != nullptr && dps->DefaultIniName[0] != '\0') {
        const std::wstring iniPath = AnsiToWide(dps->DefaultIniName);
        if (!iniPath.empty()) {
            const std::filesystem::path path(iniPath);
            if (path.has_parent_path()) {
                return (path.parent_path() / L"FolderHeatMap.db").wstring();
            }
        }
    }

    wchar_t localAppData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return (std::filesystem::path(localAppData) / L"FolderHeatMap" / L"FolderHeatMap.db").wstring();
    }

    return L"FolderHeatMap.db";
}

void RecordDirectoryVisit(const std::wstring& path) {
    if (!fhm::IsDirectory(path)) {
        return;
    }
    const auto identity = fhm::ResolveFolderIdentity(path);
    if (!identity) {
        return;
    }
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    g_database.RecordVisit(*identity, now);
}

double DaysAgo(const FILETIME& time) {
    ULARGE_INTEGER then{}, now{};
    then.LowPart = time.dwLowDateTime;
    then.HighPart = time.dwHighDateTime;
    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);
    now.LowPart = nowFileTime.dwLowDateTime;
    now.HighPart = nowFileTime.dwHighDateTime;
    if (!then.QuadPart || now.QuadPart <= then.QuadPart) {
        return 0.0;
    }
    return static_cast<double>(now.QuadPart - then.QuadPart) /
        (10000000.0 * 60.0 * 60.0 * 24.0);
}

double ComputeHeat(const fhm::StoredActivity& activity) {
    if (!activity.visits) {
        return 0.0;
    }
    const double visitHeat = std::min(7.0, std::log2(static_cast<double>(activity.visits) + 1.0));
    const double recency = std::exp(-std::log(2.0) * DaysAgo(activity.lastVisit) / 30.0);
    return std::clamp(visitHeat * recency, 0.0, 7.0);
}

int HeatToLevel(double heat) {
    return heat <= 0.0 ? 0 : std::clamp(static_cast<int>(std::ceil(heat)), 1, 7);
}

int GetValueForDirectory(const std::wstring& path, int fieldIndex, void* fieldValue) {
    if (!fhm::IsDirectory(path)) {
        return ft_fieldempty;
    }

    const auto identity = fhm::ResolveFolderIdentity(path);
    if (!identity) {
        return ft_fieldempty;
    }

    const auto activity = g_database.GetActivity(*identity);

    switch (fieldIndex) {
        case kFieldHeat: {
            *static_cast<double*>(fieldValue) = activity ? ComputeHeat(*activity) : 0.0;
            return ft_numeric_floating;
        }
        case kFieldVisits: {
            *static_cast<__int64*>(fieldValue) = activity
                ? static_cast<__int64>(activity->visits)
                : 0;
            return ft_numeric_64;
        }
        case kFieldLastVisit: {
            if (!activity) {
                return ft_fieldempty;
            }
            *static_cast<FILETIME*>(fieldValue) = activity->lastVisit;
            return ft_datetime;
        }
        case kFieldHeatLevel: {
            *static_cast<int*>(fieldValue) = HeatToLevel(activity ? ComputeHeat(*activity) : 0.0);
            return ft_numeric_32;
        }
        default:
            return ft_nosuchfield;
    }
}
} // namespace

extern "C" __declspec(dllexport) void __stdcall ContentSetDefaultParams(ContentDefaultParamStruct* dps) {
    g_database.Open(GetDatabasePath(dps));
}

extern "C" __declspec(dllexport) int __stdcall ContentGetSupportedField(
    int fieldIndex, char* fieldName, char* units, int maxlen) {
    if (units && maxlen > 0) {
        units[0] = '\0';
    }
    switch (fieldIndex) {
        case kFieldHeat: CopyAnsi(fieldName, maxlen, "Heat"); return ft_numeric_floating;
        case kFieldVisits: CopyAnsi(fieldName, maxlen, "Visits"); return ft_numeric_64;
        case kFieldLastVisit: CopyAnsi(fieldName, maxlen, "Last Visit"); return ft_datetime;
        case kFieldHeatLevel: CopyAnsi(fieldName, maxlen, "Heat Level"); return ft_numeric_32;
        default: return ft_nomorefields;
    }
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValueW(
    WCHAR* fileName, int fieldIndex, int, void* fieldValue, int, int) {
    if (!fileName || !fieldValue) {
        return ft_fileerror;
    }
    return GetValueForDirectory(fileName, fieldIndex, fieldValue);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValue(
    char* fileName, int fieldIndex, int unitIndex, void* fieldValue, int maxlen, int flags) {
    const auto wide = AnsiToWide(fileName);
    if (wide.empty()) {
        return ft_fileerror;
    }
    return ContentGetValueW(
        const_cast<WCHAR*>(wide.c_str()), fieldIndex, unitIndex, fieldValue, maxlen, flags);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetDefaultSortOrder(int fieldIndex) {
    return (fieldIndex == kFieldHeat || fieldIndex == kFieldVisits ||
            fieldIndex == kFieldLastVisit || fieldIndex == kFieldHeatLevel) ? -1 : 1;
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformationW(int state, WCHAR* path) {
    if (state == contst_readnewdir && path && *path) {
        RecordDirectoryVisit(path);
    }
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformation(int state, char* path) {
    const auto wide = AnsiToWide(path);
    if (!wide.empty()) {
        ContentSendStateInformationW(state, const_cast<WCHAR*>(wide.c_str()));
    }
}

extern "C" __declspec(dllexport) void __stdcall ContentPluginUnloading() {
    g_database.Close();
}
