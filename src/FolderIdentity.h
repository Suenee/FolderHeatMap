#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace fhm {

struct FolderIdentity {
    std::wstring storageKey;
    std::wstring volumeId;
    std::wstring relativePath;
    std::wstring mountPoint;
};

// Canonical native Win32 filesystem identity. FILE_ID_INFO returns both parts
// from one handle/query, so callers never compare IDs produced by different
// APIs or representations.
struct FilesystemIdentity {
    std::uint64_t volumeSerial = 0;
    std::array<unsigned char, 16> fileId{};
    bool valid = false;
};

bool IsDirectory(const std::wstring& path);
std::optional<FolderIdentity> ResolveFolderIdentity(const std::wstring& path);

// Resolve canonical identity through CreateFileW + GetFileInformationByHandleEx(FileIdInfo).
// Directories are opened with FILE_FLAG_BACKUP_SEMANTICS. A zero 128-bit File ID
// is treated as unsupported/invalid and is never eligible for lifecycle decisions.
std::optional<FilesystemIdentity> ResolveFilesystemIdentity(const std::wstring& path, bool isDirectory);
std::wstring EncodeFilesystemFileId(const FilesystemIdentity& identity);
std::wstring DescribeFilesystemIdentity(const FilesystemIdentity& identity);

// Compatibility wrapper used by the existing tracked_objects schema. It returns
// only the canonical 128-bit File ID; volume identity remains stored separately.
std::optional<std::wstring> ResolveFilesystemObjectId(const std::wstring& path, bool isDirectory);

// Resolve a previously known per-volume File ID back to its current path.
// This is used to distinguish a same-volume move/rename from a true delete.
std::optional<std::wstring> ResolveFilesystemPathByObjectId(const FolderIdentity& volumeIdentity,
                                                            const std::wstring& objectId,
                                                            bool isDirectory);

} // namespace fhm
