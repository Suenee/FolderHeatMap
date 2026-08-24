#include "DiagnosticWatcher.h"

#include "EngineLog.h"
#include "RuntimeShared.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <string>
#include <unordered_map>

namespace fhm {
namespace {

std::string ActionName(DWORD action) {
    switch (action) {
    case FILE_ACTION_ADDED: return "ADDED";
    case FILE_ACTION_REMOVED: return "REMOVED";
    case FILE_ACTION_MODIFIED: return "MODIFIED";
    case FILE_ACTION_RENAMED_OLD_NAME: return "RENAMED_OLD_NAME";
    case FILE_ACTION_RENAMED_NEW_NAME: return "RENAMED_NEW_NAME";
    default: return "UNKNOWN(" + std::to_string(action) + ")";
    }
}

std::string StateName(LONG state) {
    switch (state) {
    case 1: return "READ_NEW_DIR";
    case 2: return "REFRESH_PRESSED";
    case 4: return "SHOW_HINT";
    default: return "UNKNOWN(" + std::to_string(state) + ")";
    }
}

std::wstring Join(const std::wstring& root, const std::wstring& name) {
    std::wstring out = root;
    if (!out.empty() && out.back() != L'\\') out += L'\\';
    out += name;
    return out;
}

std::string SlowState(runtime::SharedState* shared) {
    if (!shared) return "slow=unavailable";
    const LONG busy = InterlockedCompareExchange(&shared->slowBusy, 0, 0);
    const LONG queued = InterlockedCompareExchange(&shared->slowQueueDepth, 0, 0);
    const LONG pending = InterlockedCompareExchange(&shared->slowPendingCount, 0, 0);
    std::wstring task(shared->slowCurrentPath);
    std::string text = "slow_busy=" + std::to_string(busy) +
                       " queue=" + std::to_string(queued) +
                       " pending=" + std::to_string(pending);
    if (!task.empty()) {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, task.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (bytes > 1) {
            std::string utf8(static_cast<size_t>(bytes - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, task.c_str(), -1, utf8.data(), bytes - 1, nullptr, nullptr);
            text += " task=" + utf8;
        }
    }
    return text;
}

} // namespace

void RunDeletionDiagnostics(runtime::SharedState* shared, EngineLogger* log, std::atomic<bool>* stopping) {
    if (!shared || !log || !stopping) return;

    using Clock = std::chrono::steady_clock;
    std::unordered_map<std::wstring, Clock::time_point> removed;
    LONG seenState = InterlockedCompareExchange(&shared->stateEventSeq, 0, 0);
    std::wstring watched;
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE eventHandle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::array<unsigned char, 64 * 1024> buffer{};
    OVERLAPPED ov{};
    ov.hEvent = eventHandle;
    bool readPending = false;

    auto closeWatch = [&] {
        if (directory != INVALID_HANDLE_VALUE) {
            if (readPending) CancelIoEx(directory, &ov);
            CloseHandle(directory);
            directory = INVALID_HANDLE_VALUE;
        }
        readPending = false;
        if (eventHandle) ResetEvent(eventHandle);
    };

    auto openWatch = [&](const std::wstring& path) {
        closeWatch();
        if (path.empty()) return;
        directory = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (directory == INVALID_HANDLE_VALUE) {
            log->WritePath("DIAG", "WATCH_OPEN_FAILED", path);
            return;
        }
        watched = path;
        log->WritePath("DIAG", "WATCH_START", watched);
    };

    auto armRead = [&] {
        if (directory == INVALID_HANDLE_VALUE || readPending) return;
        DWORD ignored = 0;
        ResetEvent(eventHandle);
        std::memset(&ov, 0, sizeof(ov));
        ov.hEvent = eventHandle;
        readPending = ReadDirectoryChangesW(directory, buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
                                            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                            FILE_NOTIFY_CHANGE_CREATION,
                                            &ignored, &ov, nullptr) != FALSE;
        if (!readPending) log->WritePath("DIAG", "WATCH_READ_FAILED", watched);
    };

    log->Write("DIAG", "delete diagnostics active; read-only mode, no lifecycle repair is performed");

    while (!stopping->load()) {
        wchar_t currentBuf[runtime::kDirectoryChars]{};
        wcsncpy_s(currentBuf, shared->currentDirectory, _TRUNCATE);
        const std::wstring current = runtime::NormalizePath(currentBuf);
        if (current != watched) openWatch(current);
        armRead();

        const LONG stateSeq = InterlockedCompareExchange(&shared->stateEventSeq, 0, 0);
        if (stateSeq != seenState) {
            seenState = stateSeq;
            const LONG code = InterlockedCompareExchange(&shared->stateCode, 0, 0);
            std::wstring statePath(shared->statePath);
            log->Write("DIAG_TC", "state=" + StateName(code) + " code=" + std::to_string(code) + " " + SlowState(shared));
            if (!statePath.empty()) log->WritePath("DIAG_TC", "path", statePath);
        }

        if (directory == INVALID_HANDLE_VALUE || !readPending) {
            Sleep(50);
            continue;
        }

        const DWORD wait = WaitForSingleObject(eventHandle, 50);
        if (wait != WAIT_OBJECT_0) continue;

        DWORD bytes = 0;
        if (!GetOverlappedResult(directory, &ov, &bytes, FALSE)) {
            readPending = false;
            continue;
        }
        readPending = false;

        size_t offset = 0;
        while (offset < bytes) {
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
            std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
            const std::wstring full = runtime::NormalizePath(Join(watched, name));
            const bool existsNow = GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES;
            const auto now = Clock::now();

            std::string timing = "timing=";
            if (info->Action == FILE_ACTION_REMOVED || info->Action == FILE_ACTION_RENAMED_OLD_NAME)
                timing += existsNow ? "PRE_OR_RACE(path_still_exists)" : "POST_CHANGE(path_already_missing)";
            else
                timing += existsNow ? "POST_CHANGE(path_exists)" : "RACE(path_not_visible_yet)";

            log->Write("DIAG_FS", "action=" + ActionName(info->Action) + " " + timing + " " + SlowState(shared));
            log->WritePath("DIAG_FS", "path", full);

            if (info->Action == FILE_ACTION_REMOVED || info->Action == FILE_ACTION_RENAMED_OLD_NAME) {
                removed[full] = now;
            } else if (info->Action == FILE_ACTION_ADDED || info->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                const auto it = removed.find(full);
                if (it != removed.end()) {
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
                    log->Write("DIAG_RECREATE", "same_path=yes after_delete_ms=" + std::to_string(ms) +
                               " priority_candidate=" + std::string(ms <= 5000 ? "yes" : "no") + " " + SlowState(shared));
                    log->WritePath("DIAG_RECREATE", "path", full);
                    removed.erase(it);
                }
            }

            if (!info->NextEntryOffset) break;
            offset += info->NextEntryOffset;
        }
    }

    closeWatch();
    if (eventHandle) CloseHandle(eventHandle);
    log->Write("DIAG", "delete diagnostics stopped");
}

} // namespace fhm
