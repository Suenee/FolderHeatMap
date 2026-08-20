#include "Lifecycle.h"

#include "Database.h"
#include "FolderIdentity.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace fhm {
namespace {

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

std::wstring JoinPath(const std::wstring& root, const std::wstring& relative) {
    if (relative.empty()) return root;
    std::wstring out = root;
    if (!out.empty() && out.back() != L'\\') out += L'\\';
    out += relative;
    return out;
}

std::optional<std::array<unsigned char, 16>> ReadObjectIdBytes(const std::wstring& path, bool isDirectory) {
    const DWORD flags = isDirectory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    FILE_ID_INFO info{};
    const BOOL ok = GetFileInformationByHandleEx(h, FileIdInfo, &info, sizeof(info));
    CloseHandle(h);
    if (!ok) return std::nullopt;
    std::array<unsigned char, 16> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = info.FileId.Identifier[i];
    return bytes;
}

std::wstring EncodeObjectId(const std::array<unsigned char, 16>& bytes) {
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
    if (identity.mountPoint.size() >= 2 && identity.mountPoint[1] == L':') {
        return L"\\\\.\\" + identity.mountPoint.substr(0, 2);
    }
    std::wstring out = identity.volumeId;
    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) out.pop_back();
    return out;
}

std::optional<std::wstring> ResolveCurrentPathByObjectId(const FolderIdentity& volumeIdentity,
                                                         const std::wstring& objectId,
                                                         bool isDirectory) {
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

bool IsRecycleBinPath(const FolderIdentity& identity) {
    const std::wstring relative = Lower(identity.relativePath);
    return relative == L"$recycle.bin" || relative.starts_with(L"$recycle.bin\\");
}

} // namespace

LifecycleResult ReconcileDirectoryLifecycle(Database& database, const std::wstring& directory) {
    LifecycleResult result;
    const auto directoryIdentity = ResolveFolderIdentity(directory);
    if (!directoryIdentity || directoryIdentity->volumeId.starts_with(L"unc:")) return result;

    const auto previous = database.GetTrackedChildren(directoryIdentity->volumeId,
                                                       directoryIdentity->relativePath);
    std::unordered_set<std::wstring> observedIds;
    std::vector<TrackedObservation> observations;
    std::vector<TrackedAction> explicitActions;

    std::wstring pattern = directory;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            std::wstring full = directory;
            if (!full.empty() && full.back() != L'\\') full += L'\\';
            full += data.cFileName;
            const bool isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const auto idBytes = ReadObjectIdBytes(full, isDirectory);
            if (!idBytes) continue;
            const auto identity = ResolveFolderIdentity(full);
            if (!identity) continue;
            TrackedObservation observation;
            observation.objectId = EncodeObjectId(*idBytes);
            observation.relativePath = identity->relativePath;
            observation.isDirectory = isDirectory;
            observedIds.insert(observation.objectId);
            observations.push_back(std::move(observation));
            ++result.observed;
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }

    for (const auto& old : previous) {
        if (observedIds.contains(old.objectId)) continue;
        TrackedAction action;
        action.objectId = old.objectId;
        action.oldRelativePath = old.relativePath;
        action.isDirectory = old.isDirectory;

        const auto currentPath = ResolveCurrentPathByObjectId(*directoryIdentity, old.objectId, old.isDirectory);
        if (currentPath) {
            const auto currentIdentity = ResolveFolderIdentity(*currentPath);
            if (currentIdentity && currentIdentity->volumeId == directoryIdentity->volumeId &&
                !IsRecycleBinPath(*currentIdentity) && currentIdentity->relativePath != old.relativePath) {
                action.kind = TrackedActionKind::Move;
                action.newRelativePath = currentIdentity->relativePath;
                explicitActions.push_back(std::move(action));
                continue;
            }
        }
        action.kind = TrackedActionKind::Delete;
        explicitActions.push_back(std::move(action));
    }

    std::vector<TrackedAction> applied;
    if (!database.ApplyTrackedLifecycleBatch(directoryIdentity->volumeId, observations, explicitActions, &applied)) {
        return result;
    }

    result.changes.reserve(applied.size());
    for (const auto& action : applied) {
        LifecycleChange change;
        change.isDirectory = action.isDirectory;
        change.oldPath = JoinPath(directoryIdentity->mountPoint, action.oldRelativePath);
        if (action.kind == TrackedActionKind::Move) {
            change.kind = LifecycleChangeKind::Moved;
            change.newPath = JoinPath(directoryIdentity->mountPoint, action.newRelativePath);
        } else {
            change.kind = LifecycleChangeKind::Deleted;
        }
        result.changes.push_back(std::move(change));
    }
    return result;
}

} // namespace fhm
