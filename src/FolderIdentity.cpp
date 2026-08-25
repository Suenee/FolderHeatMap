#include "FolderIdentity.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <sstream>
#include <vector>

namespace fhm {
namespace {

std::wstring TrimTrailingSeparators(std::wstring value) {
    while (value.size() > 3 && (value.back() == L'\\' || value.back() == L'/')) value.pop_back();
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
    if (!path.starts_with(L"\\\\")) return std::nullopt;
    const size_t serverEnd = path.find(L'\\', 2);
    if (serverEnd == std::wstring::npos) return std::nullopt;
    const size_t shareEnd = path.find(L'\\', serverEnd + 1);
    const std::wstring shareRoot = shareEnd == std::wstring::npos ? path : path.substr(0, shareEnd);
    const std::wstring relative = shareEnd == std::wstring::npos ? L"" : path.substr(shareEnd + 1);
    FolderIdentity result;
    result.volumeId = L"UNC:" + NormalizeForKey(shareRoot);
    result.relativePath = NormalizeForKey(relative);
    result.mountPoint = shareRoot;
    result.storageKey = result.volumeId + L"|" + result.relativePath;
    return result;
}

std::wstring EncodeBytes(const std::array<unsigned char, 16>& bytes) {
    static constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring out;
    out.reserve(32);
    for (unsigned char value : bytes) {
        out.push_back(hex[(value >> 4) & 0x0f]);
        out.push_back(hex[value & 0x0f]);
    }
    return out;
}

std::optional<std::array<unsigned char, 16>> DecodeObjectId(const std::wstring& text) {
    if (text.size() != 32) return std::nullopt;
    auto nibble = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return 10 + c - L'a';
        if (c >= L'A' && c <= L'F') return 10 + c - L'A';
        return -1;
    };
    std::array<unsigned char, 16> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i) {
        const int hi = nibble(text[i * 2]);
        const int lo = nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        bytes[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return bytes;
}

std::wstring VolumeHandlePath(const FolderIdentity& identity) {
    if (identity.mountPoint.size() >= 2 && identity.mountPoint[1] == L':')
        return L"\\\\.\\" + identity.mountPoint.substr(0, 2);
    std::wstring out = identity.volumeId;
    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) out.pop_back();
    return out;
}

} // namespace

bool IsDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::optional<FolderIdentity> ResolveFolderIdentity(const std::wstring& originalPath) {
    if (originalPath.empty()) return std::nullopt;
    if (originalPath.starts_with(L"\\\\")) return ResolveUncIdentity(originalPath);
    std::vector<wchar_t> volumePath(MAX_PATH + 1, L'\0');
    if (!GetVolumePathNameW(originalPath.c_str(), volumePath.data(), static_cast<DWORD>(volumePath.size()))) return std::nullopt;
    std::vector<wchar_t> volumeName(MAX_PATH + 1, L'\0');
    if (!GetVolumeNameForVolumeMountPointW(volumePath.data(), volumeName.data(), static_cast<DWORD>(volumeName.size()))) return std::nullopt;
    std::wstring normalizedOriginal = originalPath;
    std::replace(normalizedOriginal.begin(), normalizedOriginal.end(), L'/', L'\\');
    std::wstring mountPoint = volumePath.data();
    std::wstring relative;
    if (normalizedOriginal.size() >= mountPoint.size()) relative = normalizedOriginal.substr(mountPoint.size());
    FolderIdentity result;
    result.volumeId = NormalizeForKey(volumeName.data());
    result.relativePath = NormalizeForKey(relative);
    result.mountPoint = mountPoint;
    result.storageKey = result.volumeId + L"|" + result.relativePath;
    return result;
}

std::optional<FilesystemIdentity> ResolveFilesystemIdentity(const std::wstring& path, bool isDirectory) {
    if (path.empty()) return std::nullopt;
    const DWORD flags = isDirectory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    FILE_ID_INFO info{};
    const BOOL ok = GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info));
    CloseHandle(handle);
    if (!ok) return std::nullopt;

    FilesystemIdentity result{};
    result.volumeSerial = static_cast<std::uint64_t>(info.VolumeSerialNumber);
    bool any = false;
    for (size_t i = 0; i < result.fileId.size(); ++i) {
        result.fileId[i] = info.FileId.Identifier[i];
        any = any || result.fileId[i] != 0;
    }
    // Microsoft documents a zero FILE_ID_128 for filesystems that do not support
    // 128-bit IDs. Never turn that condition into an identity mismatch/delete.
    result.valid = any;
    if (!result.valid) return std::nullopt;
    return result;
}

std::wstring EncodeFilesystemFileId(const FilesystemIdentity& identity) {
    return identity.valid ? EncodeBytes(identity.fileId) : std::wstring{};
}

std::wstring DescribeFilesystemIdentity(const FilesystemIdentity& identity) {
    if (!identity.valid) return L"valid=0";
    std::wostringstream out;
    out << L"valid=1 volume_serial=" << identity.volumeSerial << L" file_id=" << EncodeBytes(identity.fileId);
    return out.str();
}

std::optional<std::wstring> ResolveFilesystemObjectId(const std::wstring& path, bool isDirectory) {
    const auto identity = ResolveFilesystemIdentity(path, isDirectory);
    if (!identity) return std::nullopt;
    const auto encoded = EncodeFilesystemFileId(*identity);
    if (encoded.empty()) return std::nullopt;
    return encoded;
}

std::optional<std::wstring> ResolveFilesystemPathByObjectId(const FolderIdentity& volumeIdentity,
                                                            const std::wstring& objectId,
                                                            bool isDirectory) {
    if (volumeIdentity.volumeId.starts_with(L"unc:")) return std::nullopt;
    const auto bytes = DecodeObjectId(objectId);
    if (!bytes) return std::nullopt;
    const std::wstring volumePath = VolumeHandlePath(volumeIdentity);
    HANDLE volume = CreateFileW(volumePath.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (volume == INVALID_HANDLE_VALUE) return std::nullopt;
    FILE_ID_DESCRIPTOR descriptor{};
    descriptor.dwSize = sizeof(descriptor);
    descriptor.Type = ExtendedFileIdType;
    for (size_t i = 0; i < bytes->size(); ++i) descriptor.ExtendedFileId.Identifier[i] = (*bytes)[i];
    HANDLE item = OpenFileById(volume, &descriptor, FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, isDirectory ? FILE_FLAG_BACKUP_SEMANTICS : 0);
    CloseHandle(volume);
    if (item == INVALID_HANDLE_VALUE) return std::nullopt;
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(item, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                   FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(item);
    if (length == 0 || length >= buffer.size()) return std::nullopt;
    std::wstring path(buffer.data(), length);
    if (path.starts_with(L"\\\\?\\UNC\\")) path = L"\\\\" + path.substr(8);
    else if (path.starts_with(L"\\\\?\\")) path = path.substr(4);
    return path;
}

} // namespace fhm
