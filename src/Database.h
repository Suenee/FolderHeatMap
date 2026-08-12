#pragma once

#include "FolderIdentity.h"

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct sqlite3;

namespace fhm {

struct StoredActivity {
    std::uint64_t visits = 0;
    FILETIME lastVisit{};
};

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool Open(const std::wstring& databasePath);
    void Close();
    bool IsOpen() const;

    bool RecordVisit(const FolderIdentity& identity, const FILETIME& now);
    std::optional<StoredActivity> GetActivity(const FolderIdentity& identity);

private:
    bool EnsureSchema();

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace fhm
