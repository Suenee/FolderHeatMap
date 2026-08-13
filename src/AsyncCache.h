#pragma once

#include "Database.h"

#include <windows.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fhm {

class AsyncDatabase {
public:
    AsyncDatabase() = default;
    ~AsyncDatabase();

    AsyncDatabase(const AsyncDatabase&) = delete;
    AsyncDatabase& operator=(const AsyncDatabase&) = delete;

    bool Open(const std::wstring& databasePath);
    void Close();
    bool IsOpen() const;

    bool RecordVisit(const FolderIdentity& identity, const FILETIME& now, int cooldownSeconds, int sessionResetHours);
    std::optional<StoredActivity> GetActivity(const FolderIdentity& identity);
    std::vector<std::pair<std::wstring, StoredActivity>> GetVolumeActivities(const std::wstring& volumeId);

    bool ObserveFileWrite(const FolderIdentity& identity, const FILETIME& lastWrite);
    std::optional<StoredFileActivity> GetFileActivity(const FolderIdentity& identity);
    std::vector<std::pair<std::wstring, StoredFileActivity>> GetVolumeFileActivities(const std::wstring& volumeId);

    bool ResetDirectActivity(const FolderIdentity& identity, bool isDirectory, const FILETIME* currentLastWrite = nullptr);
    bool ResetRecursiveActivity(const FolderIdentity& identity);
    int GetRecentActiveDays(const FILETIME& now, int windowDays);

private:
    enum class JobType { Visit, FileWrite, RefreshFolders, RefreshFiles };
    struct Job {
        JobType type{};
        FolderIdentity identity{};
        FILETIME time{};
        int cooldownSeconds = 0;
        int sessionResetHours = 0;
        std::wstring volumeId;
    };

    void StartWorker();
    void StopWorker();
    void Queue(Job job);
    void QueueRefresh(JobType type, const std::wstring& volumeId);
    void WorkerLoop();
    void InvalidateVolume(const std::wstring& volumeId);

    Database database_;
    mutable std::mutex stateMutex_;
    bool open_ = false;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<Job> queue_;
    bool stopping_ = false;
    std::thread worker_;

    std::mutex cacheMutex_;
    std::unordered_map<std::wstring, std::vector<std::pair<std::wstring, StoredActivity>>> folderCache_;
    std::unordered_map<std::wstring, std::vector<std::pair<std::wstring, StoredFileActivity>>> fileCache_;
    std::unordered_map<std::wstring, bool> folderRefreshPending_;
    std::unordered_map<std::wstring, bool> fileRefreshPending_;
};

} // namespace fhm
