#pragma once

#include <optional>
#include <string>

namespace fhm {

struct FolderIdentity {
    std::wstring storageKey;
    std::wstring volumeId;
    std::wstring relativePath;
    std::wstring mountPoint;
};

bool IsDirectory(const std::wstring& path);
std::optional<FolderIdentity> ResolveFolderIdentity(const std::wstring& path);

// Stable only inside one filesystem volume. This is intentionally separate
// from the path identity: same path + different object ID means the previous
// filesystem object no longer exists and its cached/history data is stale.
std::optional<std::wstring> ResolveFilesystemObjectId(const std::wstring& path, bool isDirectory);

// Resolve a previously known per-volume File ID back to its current path.
// This is used to distinguish a same-volume move/rename from a true delete.
std::optional<std::wstring> ResolveFilesystemPathByObjectId(const FolderIdentity& volumeIdentity,
                                                            const std::wstring& objectId,
                                                            bool isDirectory);

} // namespace fhm
