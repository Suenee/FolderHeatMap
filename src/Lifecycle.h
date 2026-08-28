#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fhm {
class Database;

enum class LifecycleChangeKind {
    Moved,
    Deleted
};

struct LifecycleChange {
    LifecycleChangeKind kind = LifecycleChangeKind::Deleted;
    std::wstring objectId;
    std::wstring oldPath;
    std::wstring newPath;
    bool isDirectory = false;
};

struct LifecycleResult {
    std::vector<LifecycleChange> changes;
    std::size_t observed = 0;
};

// SLOW-worker only. Reconciles the immediate contents of one directory using
// stable volume-local file IDs. Same-volume rename/move preserves history;
// delete, recycle-bin move and cross-volume move remove the old history.
LifecycleResult ReconcileDirectoryLifecycle(Database& database, const std::wstring& directory);

} // namespace fhm
