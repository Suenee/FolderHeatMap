#pragma once

#include <windows.h>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace fhm {

class EngineLogger {
public:
    enum class Mode { Off, Single, All };

    void Initialize(const std::wstring& settingsPath) {
        std::scoped_lock lock(mutex_);
        if (stream_.is_open()) stream_.close();
        stream_.clear(); mode_ = Mode::Off; path_.clear(); bytesWritten_ = 0; capped_ = false;
        if (settingsPath.empty()) return;
        wchar_t modeText[32]{};
        GetPrivateProfileStringW(L"Logging", L"Mode", L"off", modeText, 32, settingsPath.c_str());
        std::wstring mode(modeText);
        for (auto& ch : mode) ch = static_cast<wchar_t>(std::towlower(ch));
        if (mode == L"single") mode_ = Mode::Single; else if (mode == L"all") mode_ = Mode::All; else return;
        wchar_t configuredPath[32768]{};
        GetPrivateProfileStringW(L"Logging", L"Path", L"", configuredPath, static_cast<DWORD>(std::size(configuredPath)), settingsPath.c_str());
        if (configuredPath[0] == L'\0') { mode_ = Mode::Off; return; }
        path_ = std::filesystem::path(configuredPath);
        std::error_code ec;
        if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path(), ec);
        const auto openMode = std::ios::out | (mode_ == Mode::All ? std::ios::app : std::ios::trunc);
        stream_.open(path_, openMode);
        if (!stream_) { mode_ = Mode::Off; return; }
        if (mode_ == Mode::All) { const auto size = std::filesystem::file_size(path_, ec); if (!ec) bytesWritten_ = size; }
        WriteUnlocked("ENGINE", std::string("logging started mode=") + (mode_ == Mode::Single ? "single" : "all"));
    }

    void Write(const char* category, const std::string& message) {
        std::scoped_lock lock(mutex_);
        if (mode_ == Mode::Off || !stream_ || capped_) return;
        if (bytesWritten_ >= kMaxLogBytes) {
            WriteUnlocked("LOGGER", "SAFETY CAP reached (10 MiB); further engine logging disabled for this run");
            capped_ = true; stream_.flush(); return;
        }
        WriteUnlocked(category, message);
    }

    void WriteWide(const char* category, const std::wstring& message) { Write(category, Utf8(message)); }
    void WritePath(const char* category, const char* action, const std::wstring& path) { Write(category, std::string(action) + " " + Utf8(path)); }
    Mode GetMode() const { std::scoped_lock lock(mutex_); return mode_; }
    std::wstring Path() const { std::scoped_lock lock(mutex_); return path_.wstring(); }

private:
    static constexpr std::uintmax_t kMaxLogBytes = 10u * 1024u * 1024u;

    void WriteUnlocked(const char* category, const std::string& message) {
        SYSTEMTIME st{}; GetLocalTime(&st);
        char stamp[64]{};
        sprintf_s(stamp, "%02u.%02u.%04u %02u:%02u:%02u.%03u", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        const std::string line = std::string(stamp) + " [" + category + "] " + message + "\n";
        stream_ << line; stream_.flush(); bytesWritten_ += line.size();
    }

    static std::string Utf8(const std::wstring& text) {
        if (text.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) return {};
        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    mutable std::mutex mutex_;
    Mode mode_ = Mode::Off;
    std::filesystem::path path_;
    std::ofstream stream_;
    std::uintmax_t bytesWritten_ = 0;
    bool capped_ = false;
};

} // namespace fhm
