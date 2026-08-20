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
        stream_.clear();
        mode_ = Mode::Off;
        path_.clear();
        if (settingsPath.empty()) return;

        wchar_t modeText[32]{};
        GetPrivateProfileStringW(L"Logging", L"Mode", L"off", modeText, 32, settingsPath.c_str());
        std::wstring mode(modeText);
        for (auto& ch : mode) ch = static_cast<wchar_t>(std::towlower(ch));
        if (mode == L"single") mode_ = Mode::Single;
        else if (mode == L"all") mode_ = Mode::All;
        else return;

        // The log always lives next to FolderHeatMap.ini. For a normal Total
        // Commander installation this is the writable GHISLER configuration
        // directory (typically %APPDATA%\GHISLER), independent of where the
        // WDX/engine binary itself happens to be deployed.
        const std::filesystem::path settings(settingsPath);
        path_ = settings.has_parent_path()
            ? settings.parent_path() / L"FolderHeatMap.log"
            : std::filesystem::path(L"FolderHeatMap.log");

        std::error_code ec;
        if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path(), ec);

        const auto openMode = std::ios::out | (mode_ == Mode::All ? std::ios::app : std::ios::trunc);
        stream_.open(path_, openMode);
        if (!stream_) {
            mode_ = Mode::Off;
            return;
        }

        // Create visible proof immediately at engine startup. A user does not
        // need to perform any navigation/write before FolderHeatMap.log exists.
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char stamp[64]{};
        sprintf_s(stamp, "%02u.%02u.%04u %02u:%02u:%02u.%03u",
                  st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        stream_ << stamp << " [ENGINE] logging started mode="
                << (mode_ == Mode::Single ? "single" : "all") << "\n";
        stream_.flush();
    }

    void Write(const char* category, const std::string& message) {
        std::scoped_lock lock(mutex_);
        if (mode_ == Mode::Off || !stream_) return;

        SYSTEMTIME st{};
        GetLocalTime(&st);
        char stamp[64]{};
        sprintf_s(stamp, "%02u.%02u.%04u %02u:%02u:%02u.%03u",
                  st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        stream_ << stamp << " [" << category << "] " << message << "\n";
        stream_.flush();
    }

    void WritePath(const char* category, const char* action, const std::wstring& path) {
        Write(category, std::string(action) + " " + Utf8(path));
    }

    Mode GetMode() const {
        std::scoped_lock lock(mutex_);
        return mode_;
    }

    std::wstring Path() const {
        std::scoped_lock lock(mutex_);
        return path_.wstring();
    }

private:
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
};

} // namespace fhm
