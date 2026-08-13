#include "WdxApi.h"

#include <windows.h>
#include <cstring>

namespace {
constexpr int kFieldHeat = 0;
constexpr int kFieldVisits = 1;
constexpr int kFieldLastVisit = 2;
constexpr int kFieldHeatLevel = 3;
constexpr int kFieldColorStep = 4;
constexpr int kFieldWrites = 5;
constexpr int kFieldLastWrite = 6;

void CopyAnsi(char* destination, int maxlen, const char* source) {
    if (destination && maxlen > 0) strncpy_s(destination, static_cast<size_t>(maxlen), source, _TRUNCATE);
}

int GetDiagnosticValue(int fieldIndex, void* fieldValue) {
    if (!fieldValue) return ft_fileerror;
    switch (fieldIndex) {
        case kFieldHeat:
            *static_cast<double*>(fieldValue) = 0.0;
            return ft_numeric_floating;
        case kFieldVisits:
        case kFieldWrites:
            *static_cast<__int64*>(fieldValue) = 0;
            return ft_numeric_64;
        case kFieldHeatLevel:
        case kFieldColorStep:
            *static_cast<int*>(fieldValue) = 0;
            return ft_numeric_32;
        case kFieldLastVisit:
        case kFieldLastWrite:
            return ft_fieldempty;
        default:
            return ft_nosuchfield;
    }
}
} // namespace

extern "C" __declspec(dllexport) void __stdcall ContentSetDefaultParams(ContentDefaultParamStruct*) {
    // Diagnostic build: intentionally no settings, database or filesystem initialization.
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

extern "C" __declspec(dllexport) int __stdcall ContentGetValueW(WCHAR*, int fieldIndex, int, void* fieldValue, int, int) {
    // Deliberately zero-work hot path for A/B performance diagnosis.
    // No settings load, filesystem metadata, identity resolution, SQLite, heat math or logging.
    return GetDiagnosticValue(fieldIndex, fieldValue);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetValue(char*, int fieldIndex, int, void* fieldValue, int, int) {
    return GetDiagnosticValue(fieldIndex, fieldValue);
}

extern "C" __declspec(dllexport) int __stdcall ContentGetDefaultSortOrder(int fieldIndex) {
    return (fieldIndex >= kFieldHeat && fieldIndex <= kFieldLastWrite) ? -1 : 1;
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformationW(int, WCHAR*) {
    // Diagnostic build: deliberately ignore directory-change notifications.
}

extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformation(int, char*) {
    // Diagnostic build: deliberately ignore directory-change notifications.
}

extern "C" __declspec(dllexport) void __stdcall ContentPluginUnloading() {
    // Diagnostic build: nothing to close.
}
