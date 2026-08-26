#pragma once

#include <windows.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cwctype>
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
            ResetPanelState();
            return;
        }
        if (tc != tcWindow_) {
            tcWindow_ = tc;
            ResetPanelState();
        }

        // Total Commander WM_USER+50 control IDs: 9=leftpath, 10=rightpath.
        PollPanel(0, 9);
        PollPanel(1, 10);
    }

private:
    void ResetPanelState() {
        lastObserved_[0].clear();
        lastObserved_[1].clear();
        initialized_[0] = false;
        initialized_[1] = false;
    }

    static std::wstring ReadControlText(HWND tc, WPARAM selector) {
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(tc, WM_USER + 50, selector, 0,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result) || !result)
            return {};
        HWND control = reinterpret_cast<HWND>(result);
        if (!IsWindow(control)) return {};
        const int length = GetWindowTextLengthW(control);
        if (length <= 0 || length >= 32767) return {};
        std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
        const int copied = GetWindowTextW(control, text.data(), length + 1);
        if (copied <= 0) return {};
        text.resize(static_cast<std::size_t>(copied));
        return text;
    }

    static bool IsDirectoryPath(const std::wstring& path) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    static std::wstring NormalizePanelPath(std::wstring path) {
        if (path.empty()) return {};
        for (auto& ch : path) if (ch == L'/') ch = L'\\';
        while (path.size() > 3 && path.back() == L'\\') path.pop_back();

        // Path controls may append a display filter (for example C:\Dir\*.*).
        // Prefer the text unchanged when it is already a real directory; only
        // strip the final component when the full text is not a directory.
        if (!IsDirectoryPath(path)) {
            const auto slash = path.find_last_of(L'\\');
            if (slash != std::wstring::npos) {
                std::wstring parent;
                if (slash == 2 && path.size() >= 3 && path[1] == L':') parent = path.substr(0, 3);
                else parent = path.substr(0, slash);
                if (IsDirectoryPath(parent)) path = std::move(parent);
                else return {};
            } else {
                return {};
            }
        }

        for (auto& ch : path) ch = static_cast<wchar_t>(std::towlower(ch));
        return path;
    }

    void PollPanel(int panel, WPARAM selector) {
        std::wstring path = NormalizePanelPath(ReadControlText(tcWindow_, selector));
        if (path.empty()) return;

        // The first successful read after engine/TC startup is only a baseline.
        // Starting FolderHeatMap must not fabricate visits for already-open panels.
        if (!initialized_[panel]) {
            lastObserved_[panel] = std::move(path);
            initialized_[panel] = true;
            return;
        }

        // Sampling the same panel path is not a navigation. A later return to the
        // path after visiting something else is observed as a change and accepted.
        if (path == lastObserved_[panel]) return;
        lastObserved_[panel] = path;

        // Maximum one accepted occurrence of a path per whole second protects
        // against rapid repeated Enter or technical oscillation.
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
    std::array<bool, 2> initialized_{false, false};
    std::unordered_map<std::wstring, std::int64_t> lastAcceptedSecond_;
};

} // namespace fhm
