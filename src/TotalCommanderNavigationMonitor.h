#pragma once

#include <windows.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace fhm {

class TotalCommanderNavigationMonitor {
public:
    using Callback = std::function<void(int panel, const std::wstring& path)>;

    explicit TotalCommanderNavigationMonitor(Callback callback) : callback_(std::move(callback)) {}

    void Poll() {
        HWND tc = FindWindowW(L"TTOTAL_CMD", nullptr);
        if (!tc) {
            tcWindow_ = nullptr;
            lastObserved_.fill({});
            return;
        }
        if (tc != tcWindow_) {
            tcWindow_ = tc;
            lastObserved_.fill({});
        }

        PollPanel(0, 1); // documented TC left path control
        PollPanel(1, 2); // documented TC right path control
    }

private:
    static std::wstring ReadPanelPath(HWND tc, WPARAM selector) {
        // Total Commander exposes the left/right path controls through WM_USER+50.
        // SendMessageTimeout avoids letting an unresponsive TC block the engine.
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(tc, WM_USER + 50, selector, 0,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result) || !result)
            return {};
        HWND pathWindow = reinterpret_cast<HWND>(result);
        if (!IsWindow(pathWindow)) return {};
        const int length = GetWindowTextLengthW(pathWindow);
        if (length <= 0 || length >= 32767) return {};
        std::wstring path(static_cast<std::size_t>(length + 1), L'\0');
        const int copied = GetWindowTextW(pathWindow, path.data(), length + 1);
        if (copied <= 0) return {};
        path.resize(static_cast<std::size_t>(copied));
        return path;
    }

    void PollPanel(int panel, WPARAM selector) {
        std::wstring path = ReadPanelPath(tcWindow_, selector);
        if (path.empty()) return;
        for (auto& ch : path) if (ch == L'/') ch = L'\\';
        while (path.size() > 3 && path.back() == L'\\') path.pop_back();

        // A path change is a navigation. Re-reading an unchanged panel is not.
        if (_wcsicmp(path.c_str(), lastObserved_[panel].c_str()) == 0) return;
        lastObserved_[panel] = path;

        // One accepted occurrence of the same path per whole second protects
        // against rapid panel oscillation / repeated Enter without hiding a
        // genuine later revisit.
        const auto second = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto& last = lastAcceptedSecond_[path];
        if (last == second) return;
        last = second;
        if (callback_) callback_(panel, path);
    }

    Callback callback_;
    HWND tcWindow_ = nullptr;
    std::array<std::wstring, 2> lastObserved_{};
    std::unordered_map<std::wstring, std::int64_t> lastAcceptedSecond_;
};

} // namespace fhm
