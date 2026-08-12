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

struct Activity { std::uint64_t visits = 0; FILETIME lastVisit{}; };
std::mutex g_activityMutex;
std::unordered_map<std::wstring, Activity> g_activity;

void CopyAnsi(char* destination, int maxlen, const char* source) {
    if (destination && maxlen > 0) strncpy_s(destination, static_cast<size_t>(maxlen), source, _TRUNCATE);
}
std::wstring AnsiToWide(const char* text) {
    if (!text || !*text) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, text, -1, out.data(), n)) return {};
    out.resize(static_cast<size_t>(n - 1));
    return out;
}
void RecordDirectoryVisit(const std::wstring& path) {
    if (!fhm::IsDirectory(path)) return;
    auto identity = fhm::ResolveFolderIdentity(path);
    if (!identity) return;
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    std::scoped_lock lock(g_activityMutex);
    auto& a = g_activity[identity->storageKey];
    ++a.visits; a.lastVisit = now;
}
bool TryGetActivity(const std::wstring& path, Activity& activity) {
    auto identity = fhm::ResolveFolderIdentity(path);
    if (!identity) return false;
    std::scoped_lock lock(g_activityMutex);
    auto it = g_activity.find(identity->storageKey);
    if (it == g_activity.end()) return false;
    activity = it->second; return true;
}
double DaysAgo(const FILETIME& time) {
    ULARGE_INTEGER then{}, now{}; then.LowPart=time.dwLowDateTime; then.HighPart=time.dwHighDateTime;
    FILETIME nft{}; GetSystemTimeAsFileTime(&nft); now.LowPart=nft.dwLowDateTime; now.HighPart=nft.dwHighDateTime;
    if (!then.QuadPart || now.QuadPart <= then.QuadPart) return 0.0;
    return static_cast<double>(now.QuadPart-then.QuadPart)/(10000000.0*60.0*60.0*24.0);
}
double ComputeHeat(const Activity& a) {
    if (!a.visits) return 0.0;
    double visitHeat=std::min(7.0,std::log2(static_cast<double>(a.visits)+1.0));
    double recency=std::exp(-std::log(2.0)*DaysAgo(a.lastVisit)/30.0);
    return std::clamp(visitHeat*recency,0.0,7.0);
}
int HeatToLevel(double heat) { return heat<=0.0 ? 0 : std::clamp(static_cast<int>(std::ceil(heat)),1,7); }
int GetValueForDirectory(const std::wstring& path,int fieldIndex,void* fieldValue) {
    if (!fhm::IsDirectory(path)) return ft_fieldempty;
    Activity a{}; bool found=TryGetActivity(path,a);
    switch(fieldIndex) {
        case kFieldHeat: *static_cast<double*>(fieldValue)=found?ComputeHeat(a):0.0; return ft_numeric_floating;
        case kFieldVisits: *static_cast<__int64*>(fieldValue)=found?static_cast<__int64>(a.visits):0; return ft_numeric_64;
        case kFieldLastVisit: if(!found)return ft_fieldempty; *static_cast<FILETIME*>(fieldValue)=a.lastVisit; return ft_datetime;
        case kFieldHeatLevel: *static_cast<int*>(fieldValue)=HeatToLevel(found?ComputeHeat(a):0.0); return ft_numeric_32;
        default:return ft_nosuchfield;
    }
}
}

extern "C" __declspec(dllexport) int __stdcall ContentGetSupportedField(int fieldIndex,char* fieldName,char* units,int maxlen) {
    if(units&&maxlen>0) units[0]='\0';
    switch(fieldIndex){
        case kFieldHeat:CopyAnsi(fieldName,maxlen,"Heat");return ft_numeric_floating;
        case kFieldVisits:CopyAnsi(fieldName,maxlen,"Visits");return ft_numeric_64;
        case kFieldLastVisit:CopyAnsi(fieldName,maxlen,"Last Visit");return ft_datetime;
        case kFieldHeatLevel:CopyAnsi(fieldName,maxlen,"Heat Level");return ft_numeric_32;
        default:return ft_nomorefields;
    }
}
extern "C" __declspec(dllexport) int __stdcall ContentGetValueW(WCHAR* fileName,int fieldIndex,int,void* fieldValue,int,int) {
    if(!fileName||!fieldValue)return ft_fileerror; return GetValueForDirectory(fileName,fieldIndex,fieldValue);
}
extern "C" __declspec(dllexport) int __stdcall ContentGetValue(char* fileName,int fieldIndex,int unitIndex,void* fieldValue,int maxlen,int flags) {
    auto wide=AnsiToWide(fileName); if(wide.empty())return ft_fileerror;
    return ContentGetValueW(const_cast<WCHAR*>(wide.c_str()),fieldIndex,unitIndex,fieldValue,maxlen,flags);
}
extern "C" __declspec(dllexport) int __stdcall ContentGetDefaultSortOrder(int fieldIndex) {
    return (fieldIndex==kFieldHeat || fieldIndex==kFieldVisits || fieldIndex==kFieldLastVisit || fieldIndex==kFieldHeatLevel) ? -1 : 1;
}
extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformationW(int state,WCHAR* path) {
    if(state==contst_readnewdir && path && *path) RecordDirectoryVisit(path);
}
extern "C" __declspec(dllexport) void __stdcall ContentSendStateInformation(int state,char* path) {
    auto wide=AnsiToWide(path); if(!wide.empty()) ContentSendStateInformationW(state,const_cast<WCHAR*>(wide.c_str()));
}
extern "C" __declspec(dllexport) void __stdcall ContentPluginUnloading() {
    std::scoped_lock lock(g_activityMutex); g_activity.clear();
}
