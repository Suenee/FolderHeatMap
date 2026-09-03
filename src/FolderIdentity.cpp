#include "FolderIdentity.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fhm {
namespace {

constexpr ULONG kFileFsObjectIdInformation = 8;

struct IoStatusBlockCompat {
    union {
        LONG status;
        void* pointer;
    };
    ULONG_PTR information;
};

struct FsObjectIdInformationCompat {
    unsigned char objectId[16];
    unsigned char extendedInfo[48];
};

using NtQueryVolumeInformationFileFn = LONG(NTAPI*)(HANDLE, IoStatusBlockCompat*, void*, ULONG, ULONG);

struct RemoteRootIdentity {
    std::wstring volumeId;
    std::wstring shareRoot;
};

std::mutex g_remoteRootCacheMutex;
std::unordered_map<std::wstring, RemoteRootIdentity> g_remoteRootCache;

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

std::wstring EncodeBytes(const unsigned char* bytes, size_t count) {
    static constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring out;
    out.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        const unsigned char value = bytes[i];
        out.push_back(hex[(value >> 4) & 0x0f]);
        out.push_back(hex[value & 0x0f]);
    }
    return out;
}

std::wstring EncodeBytes(const std::array<unsigned char, 16>& bytes) {
    return EncodeBytes(bytes.data(), bytes.size());
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

std::wstring GetFinalPath(HANDLE handle) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                   FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= buffer.size()) return {};
    std::wstring path(buffer.data(), length);
    if (path.rfind(L"\\\\?\\UNC\\", 0) == 0) return L"\\\\" + path.substr(8);
    if (path.rfind(L"\\\\?\\", 0) == 0) return path.substr(4);
    return path;
}

std::optional<std::array<unsigned char, 16>> QueryVolumeObjectId(HANDLE handle) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto query = ntdll ? reinterpret_cast<NtQueryVolumeInformationFileFn>(
                                   GetProcAddress(ntdll, "NtQueryVolumeInformationFile"))
                             : nullptr;
    if (!query) return std::nullopt;

    IoStatusBlockCompat iosb{};
    FsObjectIdInformationCompat info{};
    const LONG status = query(handle, &iosb, &info, sizeof(info), kFileFsObjectIdInformation);
    if (status < 0) return std::nullopt;

    std::array<unsigned char, 16> id{};
    bool any = false;
    for (size_t i = 0; i < id.size(); ++i) {
        id[i] = info.objectId[i];
        any = any || id[i] != 0;
    }
    return any ? std::optional{id} : std::nullopt;
}

std::optional<std::uint64_t> QueryLegacyFileIndex(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) return std::nullopt;
    const std::uint64_t index = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) |
                                static_cast<std::uint64_t>(info.nFileIndexLow);
    return index ? std::optional{index} : std::nullopt;
}

std::wstring EncodeUint64(std::uint64_t value) {
    std::wostringstream out;
    out << std::hex << std::setw(16) << std::setfill(L'0') << value;
    return out.str();
}

bool SplitUncPath(const std::wstring& originalPath,
                  std::wstring& shareRoot,
                  std::wstring& shareName,
                  std::wstring& relative) {
    std::wstring path = originalPath;
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (!path.starts_with(L"\\\\")) return false;
    const size_t serverEnd = path.find(L'\\', 2);
    if (serverEnd == std::wstring::npos) return false;
    const size_t shareEnd = path.find(L'\\', serverEnd + 1);
    shareRoot = shareEnd == std::wstring::npos ? path : path.substr(0, shareEnd);
    shareName = shareEnd == std::wstring::npos ? path.substr(serverEnd + 1)
                                                : path.substr(serverEnd + 1, shareEnd - serverEnd - 1);
    relative = shareEnd == std::wstring::npos ? L"" : path.substr(shareEnd + 1);
    return !shareName.empty();
}

std::optional<FolderIdentity> ResolveUncIdentity(const std::wstring& originalPath) {
    std::wstring shareRoot;
    std::wstring shareName;
    std::wstring relative;
    if (!SplitUncPath(originalPath, shareRoot, shareName, relative)) return std::nullopt;
    FolderIdentity result;
    result.volumeId = L"UNC:" + NormalizeForKey(shareRoot);
    result.relativePath = NormalizeForKey(relative);
    result.mountPoint = shareRoot;
    result.storageKey = result.volumeId + L"|" + result.relativePath;
    return result;
}

std::optional<RemoteRootIdentity> ResolveRemoteRootUncached(const std::wstring& accessRoot) {
    HANDLE handle = CreateFileW(accessRoot.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

    FILE_REMOTE_PROTOCOL_INFO remote{};
    if (!GetFileInformationByHandleEx(handle, FileRemoteProtocolInfo, &remote, sizeof(remote))) {
        CloseHandle(handle);
        return std::nullopt;
    }

    const std::wstring finalRoot = GetFinalPath(handle);
    std::wstring shareRoot;
    std::wstring shareName;
    std::wstring ignoredRelative;
    if (finalRoot.empty() || !SplitUncPath(finalRoot, shareRoot, shareName, ignoredRelative)) {
        CloseHandle(handle);
        return std::nullopt;
    }

    const auto volumeObjectId = QueryVolumeObjectId(handle);
    const auto rootIndex = QueryLegacyFileIndex(handle);
    CloseHandle(handle);

    RemoteRootIdentity result;
    result.shareRoot = shareRoot;
    if (volumeObjectId) {
        // The volume Object ID identifies the underlying SMB filesystem independently
        // of drive letter, IP address and server name. The exported root directory
        // file index keeps distinct shares on the same volume from colliding.
        result.volumeId = L"SMB:" + EncodeBytes(volumeObjectId->data(), volumeObjectId->size());
        if (rootIndex)
            result.volumeId += L":root:" + EncodeUint64(*rootIndex);
        else
            result.volumeId += L":share:" + NormalizeForKey(shareName);
    } else {
        // Compatibility fallback for SMB servers that do not expose a volume Object ID.
        // It still makes mapped-drive access usable, but remains server/share-name based.
        result.volumeId = L"UNC:" + NormalizeForKey(shareRoot);
    }
    return result;
}

std::optional<RemoteRootIdentity> ResolveRemoteRoot(const std::wstring& accessRoot) {
    const std::wstring cacheKey = NormalizeForKey(accessRoot);
    {
        std::scoped_lock lock(g_remoteRootCacheMutex);
        const auto it = g_remoteRootCache.find(cacheKey);
        if (it != g_remoteRootCache.end()) return it->second;
    }

    const auto resolved = ResolveRemoteRootUncached(accessRoot);
    if (!resolved) return std::nullopt;
    {
        std::scoped_lock lock(g_remoteRootCacheMutex);
        g_remoteRootCache[cacheKey] = *resolved;
    }
    return resolved;
}

std::optional<FolderIdentity> ResolveRemoteIdentity(const std::wstring& originalPath) {
    std::wstring path = originalPath;
    std::replace(path.begin(), path.end(), L'/', L'\\');

    std::wstring accessRoot;
    std::wstring relative;
    if (path.starts_with(L"\\\\")) {
        std::wstring shareName;
        if (!SplitUncPath(path, accessRoot, shareName, relative)) return std::nullopt;
    } else if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\') {
        accessRoot = path.substr(0, 3);
        if (GetDriveTypeW(accessRoot.c_str()) != DRIVE_REMOTE) return std::nullopt;
        relative = path.substr(3);
    } else {
        return std::nullopt;
    }

    const auto root = ResolveRemoteRoot(accessRoot);
    if (!root) return std::nullopt;

    FolderIdentity result;
    result.volumeId = root->volumeId;
    result.relativePath = NormalizeForKey(relative);
    result.mountPoint = root->shareRoot;
    result.storageKey = result.volumeId + L"|" + result.relativePath;
    return result;
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

    if (const auto remote = ResolveRemoteIdentity(originalPath)) return remote;
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
    std::wstring volumeId = volumeIdentity.volumeId;
    std::transform(volumeId.begin(), volumeId.end(), volumeId.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    if (volumeId.starts_with(L"unc:") || volumeId.starts_with(L"smb:")) return std::nullopt;
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
