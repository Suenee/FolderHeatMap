#pragma once

#include <windows.h>
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
        mode_ = Mode::Off;
        if (settingsPath.empty()) return;

        wchar_t modeText[32]{};
        GetPrivateProfileStringW(L"Logging", L"Mode", L"off", modeText, 32, settingsPath.c_str());
        std::wstring mode(modeText);
        for (auto& ch : mode) ch = static_cast<wchar_t>(towlower(ch));
        if (mode == L"single") mode_ = Mode::Single;
        else if (mode == L"all") mode_ = Mode::All;
        else return;

        const std::filesystem::path settings(settingsPath);
        path_ = settings.has_parent_path()
            ? settings.parent_path() / L"FolderHeatMap.log"
            : std::filesystem::path(L"FolderHeatMap.log");

        const auto openMode = std::ios::out | (mode_ == Mode::All ? std::ios::app : std::ios::trunc);
        stream_.open(path_, openMode);
        if (!stream_) mode_ = Mode::Off;
    }

    void Write(const char* category, const std::string& message) {
        std::scoped_lock lock(mutex_);
        if (mode_ == Mode::Off || !stream_) return;

        SYSTEMTIME st{};
        GetLocalTime(&st);
        char stamp[64]{};
        sprintf_s(stamp, "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        stream_ << stamp << " [" << category << "] " << message << "\n";
        stream_.flush();
    }

    void WritePath(const char* category, const char* action, const std::wstring& path) {
        if (mode_ == Mode::Off) return;
        Write(category, std::string(action) + " " + Utf8(path));
    }

    Mode GetMode() const { return mode_; }

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
