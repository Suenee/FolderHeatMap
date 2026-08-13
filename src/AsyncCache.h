#pragma once

#include "Database.h"

#include <windows.h>

#include <chrono>
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
    ~AsyncDatabase() { Close(); }
    AsyncDatabase(const AsyncDatabase&) = delete;
    AsyncDatabase& operator=(const AsyncDatabase&) = delete;

    bool Open(const std::wstring& databasePath) {
        std::scoped_lock lock(stateMutex_);
        if (open_) return true;
        open_ = database_.Open(databasePath);
        if (open_ && !worker_.joinable()) {
            stopping_ = false;
            worker_ = std::thread(&AsyncDatabase::WorkerLoop, this);
        }
        return open_;
    }

    void Close() {
        {
            std::scoped_lock lock(stateMutex_);
            if (!open_) return;
            open_ = false;
        }
        {
            std::scoped_lock lock(queueMutex_);
            stopping_ = true;
        }
        queueCv_.notify_all();
        if (worker_.joinable()) worker_.join();
        database_.Close();
        {
            std::scoped_lock lock(queueMutex_);
            queue_.clear();
        }
        {
            std::scoped_lock lock(cacheMutex_);
            folderCache_.clear();
            fileCache_.clear();
            folderUpdated_.clear();
            fileUpdated_.clear();
            folderRefreshPending_.clear();
            fileRefreshPending_.clear();
        }
    }

    bool IsOpen() const {
        std::scoped_lock lock(stateMutex_);
        return open_;
    }

    bool RecordVisit(const FolderIdentity& identity, const FILETIME& now, int cooldownSeconds, int sessionResetHours) {
        Job job{};
        job.type = JobType::Visit;
        job.identity = identity;
        job.time = now;
        job.cooldownSeconds = cooldownSeconds;
        job.sessionResetHours = sessionResetHours;
        Queue(std::move(job));
        return true;
    }

    std::optional<StoredActivity> GetActivity(const FolderIdentity& identity) {
        return database_.GetActivity(identity);
    }

    std::vector<std::pair<std::wstring, StoredActivity>> GetVolumeActivities(const std::wstring& volumeId) {
        std::vector<std::pair<std::wstring, StoredActivity>> result;
        bool found = false;
        bool stale = true;
        {
            std::scoped_lock lock(cacheMutex_);
            const auto it = folderCache_.find(volumeId);
            if (it != folderCache_.end()) {
                result = it->second;
                found = true;
                const auto t = folderUpdated_.find(volumeId);
                stale = t == folderUpdated_.end() || std::chrono::steady_clock::now() - t->second > cacheLifetime_;
            }
        }
        if (!found || stale) QueueRefresh(JobType::RefreshFolders, volumeId);
        return result;
    }

    bool ObserveFileWrite(const FolderIdentity& identity, const FILETIME& lastWrite) {
        Job job{};
        job.type = JobType::FileWrite;
        job.identity = identity;
        job.time = lastWrite;
        Queue(std::move(job));
        return true;
    }

    std::optional<StoredFileActivity> GetFileActivity(const FolderIdentity& identity) {
        return database_.GetFileActivity(identity);
    }

    std::vector<std::pair<std::wstring, StoredFileActivity>> GetVolumeFileActivities(const std::wstring& volumeId) {
        std::vector<std::pair<std::wstring, StoredFileActivity>> result;
        bool found = false;
        bool stale = true;
        {
            std::scoped_lock lock(cacheMutex_);
            const auto it = fileCache_.find(volumeId);
            if (it != fileCache_.end()) {
                result = it->second;
                found = true;
                const auto t = fileUpdated_.find(volumeId);
                stale = t == fileUpdated_.end() || std::chrono::steady_clock::now() - t->second > cacheLifetime_;
            }
        }
        if (!found || stale) QueueRefresh(JobType::RefreshFiles, volumeId);
        return result;
    }

    bool ResetDirectActivity(const FolderIdentity& identity, bool isDirectory, const FILETIME* currentLastWrite = nullptr) {
        const bool ok = database_.ResetDirectActivity(identity, isDirectory, currentLastWrite);
        if (ok) InvalidateVolume(identity.volumeId);
        return ok;
    }

    bool ResetRecursiveActivity(const FolderIdentity& identity) {
        const bool ok = database_.ResetRecursiveActivity(identity);
        if (ok) InvalidateVolume(identity.volumeId);
        return ok;
    }

    int GetRecentActiveDays(const FILETIME& now, int windowDays) {
        // Auto-cooling changes slowly. Avoid repeating this aggregate SQLite
        // query for every WDX field request in the same panel refresh.
        const auto tick = std::chrono::steady_clock::now();
        {
            std::scoped_lock lock(activeDaysMutex_);
            if (activeDaysValid_ && tick - activeDaysUpdated_ < activeDaysLifetime_ && windowDays == activeDaysWindow_)
                return activeDaysValue_;
        }
        const int value = database_.GetRecentActiveDays(now, windowDays);
        {
            std::scoped_lock lock(activeDaysMutex_);
            activeDaysValue_ = value;
            activeDaysWindow_ = windowDays;
            activeDaysUpdated_ = tick;
            activeDaysValid_ = true;
        }
        return value;
    }

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

    static constexpr auto cacheLifetime_ = std::chrono::seconds(5);
    static constexpr auto activeDaysLifetime_ = std::chrono::seconds(30);

    void Queue(Job job) {
        std::scoped_lock lock(queueMutex_);
        if (stopping_ || queue_.size() >= 4096) return;
        queue_.push_back(std::move(job));
        queueCv_.notify_one();
    }

    void QueueRefresh(JobType type, const std::wstring& volumeId) {
        {
            std::scoped_lock lock(cacheMutex_);
            auto& pending = type == JobType::RefreshFolders ? folderRefreshPending_ : fileRefreshPending_;
            if (pending[volumeId]) return;
            pending[volumeId] = true;
        }
        Job job{};
        job.type = type;
        job.volumeId = volumeId;
        Queue(std::move(job));
    }

    void InvalidateVolume(const std::wstring& volumeId) {
        std::scoped_lock lock(cacheMutex_);
        folderUpdated_.erase(volumeId);
        fileUpdated_.erase(volumeId);
    }

    void WorkerLoop() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(queueMutex_);
                queueCv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) break;
                job = std::move(queue_.front());
                queue_.pop_front();
            }

            switch (job.type) {
                case JobType::Visit:
                    database_.RecordVisit(job.identity, job.time, job.cooldownSeconds, job.sessionResetHours);
                    InvalidateVolume(job.identity.volumeId);
                    break;
                case JobType::FileWrite:
                    database_.ObserveFileWrite(job.identity, job.time);
                    InvalidateVolume(job.identity.volumeId);
                    break;
                case JobType::RefreshFolders: {
                    auto data = database_.GetVolumeActivities(job.volumeId);
                    std::scoped_lock lock(cacheMutex_);
                    folderCache_[job.volumeId] = std::move(data);
                    folderUpdated_[job.volumeId] = std::chrono::steady_clock::now();
                    folderRefreshPending_[job.volumeId] = false;
                    break;
                }
                case JobType::RefreshFiles: {
                    auto data = database_.GetVolumeFileActivities(job.volumeId);
                    std::scoped_lock lock(cacheMutex_);
                    fileCache_[job.volumeId] = std::move(data);
                    fileUpdated_[job.volumeId] = std::chrono::steady_clock::now();
                    fileRefreshPending_[job.volumeId] = false;
                    break;
                }
            }
        }
    }

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
    std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> folderUpdated_;
    std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> fileUpdated_;
    std::unordered_map<std::wstring, bool> folderRefreshPending_;
    std::unordered_map<std::wstring, bool> fileRefreshPending_;

    std::mutex activeDaysMutex_;
    bool activeDaysValid_ = false;
    int activeDaysWindow_ = 0;
    int activeDaysValue_ = 0;
    std::chrono::steady_clock::time_point activeDaysUpdated_{};
};

} // namespace fhm

// FolderHeatMap.cpp historically names the facade fhm::Database. For the WDX
// target only, CMake force-includes this header before FolderHeatMap.cpp and
// enables this alias. Database.cpp and the reset utility keep the synchronous
// class untouched.
#ifdef FHM_USE_ASYNC_DATABASE
#define Database AsyncDatabase
#endif
