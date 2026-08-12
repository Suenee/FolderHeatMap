#include "FolderIdentity.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <vector>

namespace fhm {
namespace {

std::wstring TrimTrailingSeparators(std::wstring value) {
    while (value.size() > 3 && (value.back() == L'\\' || value.back() == L'/')) {
        value.pop_back();
    }
    return value;
}

std::wstring NormalizeForKey(std::wstring value) {
    std::replace(value.begin(), value.end(), L'/', L'\\');
    value = TrimTrailingSeparators(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::optional<FolderIdentity> ResolveUncIdentity(const std::wstring& originalPath) {
    std::wstring path = originalPath;
    std::replace(path.begin(), path.end(), L'/', L'\\');

    if (!path.starts_with(L"\\\\")) {
        return std::nullopt;
    }

    const size_t serverEnd = path.find(L'\\', 2);
    if (serverEnd == std::wstring::npos) {
        return std::nullopt;
    }
    const size_t shareEnd = path.find(L'\\', serverEnd + 1);

    const std::wstring shareRoot = shareEnd == std::wstring::npos
        ? path
        : path.substr(0, shareEnd);
    const std::wstring relative = shareEnd == std::wstring::npos
        ? L""
        : path.substr(shareEnd + 1);

    FolderIdentity result;
    result.volumeId = L"UNC:" + NormalizeForKey(shareRoot);
    result.relativePath = NormalizeForKey(relative);
    result.mountPoint = shareRoot;
    result.storageKey = result.volumeId + L"|" + result.relativePath;
    return result;
}

} // namespace

bool IsDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::optional<FolderIdentity> ResolveFolderIdentity(const std::wstring& originalPath) {
    if (originalPath.empty()) {
        return std::nullopt;
    }

    if (originalPath.starts_with(L"\\\\")) {
        return ResolveUncIdentity(originalPath);
    }

    std::vector<wchar_t> volumePath(MAX_PATH + 1, L'\0');
    if (!GetVolumePathNameW(originalPath.c_str(), volumePath.data(), static_cast<DWORD>(volumePath.size()))) {
        return std::nullopt;
    }

    std::vector<wchar_t> volumeName(MAX_PATH + 1, L'\0');
    if (!GetVolumeNameForVolumeMountPointW(volumePath.data(), volumeName.data(), static_cast<DWORD>(volumeName.size()))) {
        return std::nullopt;
    }

    std::wstring normalizedOriginal = originalPath;
    std::replace(normalizedOriginal.begin(), normalizedOriginal.end(), L'/', L'\\');

    std::wstring mountPoint = volumePath.data();
    std::wstring relative;
    if (normalizedOriginal.size() >= mountPoint.size()) {
        relative = normalizedOriginal.substr(mountPoint.size());
    }

    FolderIdentity result;
    result.volumeId = NormalizeForKey(volumeName.data());
    result.relativePath = NormalizeForKey(relative);
    result.mountPoint = mountPoint;
    result.storageKey = result.volumeId + L"|" + result.relativePath;
    return result;
}

} // namespace fhm
