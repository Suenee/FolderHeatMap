#include "FolderIdentity.h"
#include "WdxApi.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

constexpr int kFieldHeat = 0;
constexpr int kFieldVisits = 1;
constexpr int kFieldLastVisit = 2;
constexpr int kFieldHeatLevel = 3;

struct Activity {
    std::uint64_t visits = 0;
    FILETIME lastVisit{};
};

std::mutex g_activityMutex;
std::unordered_map<std::wstring, Activity> g_activity;

void CopyAnsi(char* destination, int maxlen, const char* source) {
    if (destination == nullptr || maxlen <= 0) {
        return;
    }
    strncpy_s(destination, static_cast<size_t>(maxlen), source, _TRUNCATE);
}

std::wstring AnsiToWide(const char* text) {
    if (text == nullptr || *text == '\0') {
        return {};
    }

    const int lengthWithNull = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (lengthWithNull <= 1) {
        return {};
    }

    std::wstring result(static_cast<size_t>(lengthWithNull), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), lengthWithNull) <= 0) {
        return {};
    }
    result.resize(static_cast<size_t>(lengthWithNull - 1));
    return result;
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

    std::scoped_lock lock(g_activityMutex);
    auto& activity = g_activity[identity->storageKey];
    ++activity.visits;
    activity.lastVisit = now;
}

bool TryGetActivity(const std::wstring& path, Activity& activity) {
    const auto identity = fhm::ResolveFolderIdentity(path);
    if (!identity) {
        return false;
    }

    std::scoped_lock lock(g_activityMutex);
    const auto it = g_activity.find(identity->storageKey);
    if (it == g_activity.end()) {
        return false;
    }

    activity = it->second;
    return true;
}

double FileTimeToDaysAgo(const FILETIME& time) {
    ULARGE_INTEGER then{};
    then.LowPart = time.dwLowDateTime;
    then.HighPart = time.dwHighDateTime;

    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);
    ULARGE_INTEGER now{};
    now.LowPart = nowFileTime.dwLowDateTime;
    now.HighPart = nowFileTime.dwHighDateTime;

    if (then.QuadPart == 0 || now.QuadPart <= then.QuadPart) {
        return 0.0;
    }

    constexpr double kTicksPerDay = 10'000'000.0 * 60.0 * 60.0 * 24.0;
    return static_cast<double>(now.QuadPart - then.QuadPart) / kTicksPerDay;
}

double ComputeHeat(const Activity& activity) {
    if (activity.visits == 0) {
        return 0.0;
    }

    // Prototype scoring only. Raw visit data is kept separate so this formula can
    // evolve without changing the database format later.
    const double visitHeat = std::min(7.0, std::log2(static_cast<double>(activity.visits) + 1.0));
    const double ageDays = FileTimeToDaysAgo(activity.lastVisit);
    constexpr double kHalfLifeDays = 30.0;
    const double recencyFactor = std::exp(-std::log(2.0) * ageDays / kHalfLifeDays);
    return std::clamp(visitHeat * recencyFactor, 0.0, 7.0);
}

int HeatToLevel(double heat) {
    if (heat <= 0.0) {
        return 0;
    }
    return std::clamp(static_cast<int>(std::ceil(heat)), 1, 7);
}

int GetValueForDirectory(const std::wstring& path, int fieldIndex, void* fieldValue) {
    if (!fhm::IsDirectory(path)) {
        return ft_fieldempty;
    }

    Activity activity{};
    const bool found = TryGetActivity(path, activity);

    switch (fieldIndex) {
        case kFieldHeat: {
            const double heat = found ? ComputeHeat(activity) : 0.0;
            *static_cast<double*>(fieldValue) = heat;
            return ft_numeric_floating;
        }
        case kFieldVisits: {
            *static_cast<__int64*>(fieldValue) = found
                ? static_cast<__int64>(activity.visits)
                : 0;
            return ft_numeric_64;
        }
        case kFieldLastVisit: {
            if (!found) {
                return ft_fieldempty;
            }
            *static_cast<FILETIME*>(fieldValue) = activity.lastVisit;
            return ft_datetime;
        }
        case kFieldHeatLevel: {
            const double heat = found ? ComputeHeat(activity) : 0.0;
            *static_cast<int*>(fieldValue) = HeatToLevel(heat);
            return ft_numeric_32;
        }
        default:
            return ft_nosuchfield;
    }
}

} // namespace

extern "C" __declspec(dllexport) int __stdcall ContentGetSupportedField(
    int fieldIndex,
    char* fieldName,
    char* units,
    int maxlen) {

    if (units != nullptr && maxlen > 0) {
        units[0] = '\0';
    }

    switch (fieldIndex) {
        case kFieldHeat:
            CopyAnsi(fieldName, maxlen, "Heat");
            return ft_numeric_floating;
        case kFieldVisits:
            CopyAnsi(fieldName, maxlen, "Visits");
            return ft_numeric_64;
        case kFieldLastVisit:
            CopyAnsi(fieldName, maxlen, "Last Visit");
            return ft_datetime;
        case kFieldHeatLevel:
            CopyAnsi(fieldName, maxlen, "Heat Level");
            return ft_numeric_32;
        default:
            return ft_nomorefields;
    }
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValueW(
    WCHAR* fileName,
    int fieldIndex,
    int /*unitIndex*/,
    void* fieldValue,
    int /*maxlen*/,
    int /*flags*/) {

    if (fileName == nullptr || fieldValue == nullptr) {
        return ft_fileerror;
    }
    return GetValueForDirectory(fileName, fieldIndex, fieldValue);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValue(
    char* fileName,
    int fieldIndex,
    int unitIndex,
    void* fieldValue,
    int maxlen,
    int flags) {

    const std::wstring wideName = AnsiToWide(fileName);
    if (wideName.empty()) {
        return ft_fileerror;
    }
    return ContentGetValueW(
        const_cast<WCHAR*>(wideName.c_str()),
        fieldIndex,
        unitIndex,
        fieldValue,
        maxlen,
        flags);
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformationW(int state, WCHAR* path) {
    if (state == contst_readnewdir && path != nullptr && *path != L'\0') {
        RecordDirectoryVisit(path);
    }
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformation(int state, char* path) {
    const std::wstring widePath = AnsiToWide(path);
    if (!widePath.empty()) {
        ContentSendStateInformationW(state, const_cast<WCHAR*>(widePath.c_str()));
    }
}

extern "C" __declspec(dllexport) void __stdcall ContentPluginUnloading(void) {
    std::scoped_lock lock(g_activityMutex);
    g_activity.clear();
}
