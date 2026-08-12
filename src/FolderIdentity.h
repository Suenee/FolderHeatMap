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

} // namespace fhm
