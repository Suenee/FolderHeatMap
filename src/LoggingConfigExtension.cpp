#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <string>

namespace {
constexpr wchar_t kConfigWindowClass[] = L"FolderHeatMapConfigWindow";
constexpr int kIdSave = 1009;
constexpr int kIdCancel = 1011;
constexpr int kIdHelp = 1012;
constexpr int kIdLoggingCombo = 1601;
constexpr int kIdLoggingLabel = 1602;

HHOOK g_cbtHook = nullptr;
WNDPROC g_originalProc = nullptr;
HWND g_loggingCombo = nullptr;
bool g_loggingDirty = false;
std::wstring g_settingsIni;

std::wstring ExpandEnvironment(const std::wstring& value) {
    if (value.empty()) return {};
    const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (!needed) return value;
    std::wstring out(needed, L'\0');
    if (!ExpandEnvironmentStringsW(value.c_str(), out.data(), needed)) return value;
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::wstring QueryRegString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    wchar_t buffer[2048]{};
    DWORD type = 0;
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, subkey, value,
                     RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                     &type, buffer, &size) == ERROR_SUCCESS)
        return ExpandEnvironment(buffer);
    return {};
}

std::wstring FindWincmdIni() {
    wchar_t env[2048]{};
    const DWORD n = GetEnvironmentVariableW(L"COMMANDER_INI", env, static_cast<DWORD>(std::size(env)));
    if (n > 0 && n < std::size(env)) return ExpandEnvironment(env);

    auto path = QueryRegString(HKEY_CURRENT_USER, L"Software\\Ghisler\\Total Commander", L"IniFileName");
    if (!path.empty()) return path;
    path = QueryRegString(HKEY_LOCAL_MACHINE, L"Software\\Ghisler\\Total Commander", L"IniFileName");
    if (!path.empty()) return path;

    wchar_t appData[2048]{};
    const DWORD m = GetEnvironmentVariableW(L"APPDATA", appData, static_cast<DWORD>(std::size(appData)));
    if (m > 0 && m < std::size(appData))
        return (std::filesystem::path(appData) / L"GHISLER" / L"wincmd.ini").wstring();
    return {};
}

std::wstring SettingsIni() {
    if (!g_settingsIni.empty()) return g_settingsIni;
    const auto wincmd = FindWincmdIni();
    if (wincmd.empty()) return {};
    g_settingsIni = (std::filesystem::path(wincmd).parent_path() / L"FolderHeatMap.ini").wstring();
    return g_settingsIni;
}

int ReadLoggingModeIndex() {
    const auto ini = SettingsIni();
    if (ini.empty()) return 0;
    wchar_t mode[32]{};
    GetPrivateProfileStringW(L"Logging", L"Mode", L"off", mode, 32, ini.c_str());
    if (_wcsicmp(mode, L"single") == 0) return 1;
    if (_wcsicmp(mode, L"all") == 0) return 2;
    return 0;
}

bool SaveLoggingMode() {
    if (!g_loggingCombo) return true;
    const auto ini = SettingsIni();
    if (ini.empty()) return false;
    const LRESULT index = SendMessageW(g_loggingCombo, CB_GETCURSEL, 0, 0);
    const wchar_t* mode = index == 1 ? L"single" : index == 2 ? L"all" : L"off";
    const bool ok = WritePrivateProfileStringW(L"Logging", L"Mode", mode, ini.c_str()) != FALSE;
    if (ok) g_loggingDirty = false;
    return ok;
}

void MoveControlDown(HWND parent, int id, int dy) {
    HWND control = GetDlgItem(parent, id);
    if (!control) return;
    RECT r{};
    GetWindowRect(control, &r);
    POINT p{r.left, r.top};
    ScreenToClient(parent, &p);
    SetWindowPos(control, nullptr, p.x, p.y + dy, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void AddLoggingControls(HWND hwnd) {
    RECT outer{};
    GetWindowRect(hwnd, &outer);
    SetWindowPos(hwnd, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top + 58,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    MoveControlDown(hwnd, kIdHelp, 52);
    MoveControlDown(hwnd, kIdSave, 52);
    MoveControlDown(hwnd, kIdCancel, 52);

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND label = CreateWindowExW(0, L"STATIC", L"Logging:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        300, 468, 66, 22, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLoggingLabel)),
        GetModuleHandleW(nullptr), nullptr);
    if (label) SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    g_loggingCombo = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        370, 464, 150, 200, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLoggingCombo)),
        GetModuleHandleW(nullptr), nullptr);
    if (!g_loggingCombo) return;
    SendMessageW(g_loggingCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(g_loggingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"off"));
    SendMessageW(g_loggingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"single"));
    SendMessageW(g_loggingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"all"));
    SendMessageW(g_loggingCombo, CB_SETCURSEL, ReadLoggingModeIndex(), 0);
    g_loggingDirty = false;
}

LRESULT CALLBACK LoggingWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        const LRESULT result = CallWindowProcW(g_originalProc, hwnd, msg, wp, lp);
        AddLoggingControls(hwnd);
        return result;
    }

    if (msg == WM_COMMAND) {
        const int id = LOWORD(wp);
        const int code = HIWORD(wp);
        if (id == kIdLoggingCombo && code == CBN_SELCHANGE) {
            g_loggingDirty = true;
            HWND save = GetDlgItem(hwnd, kIdSave);
            if (save) EnableWindow(save, TRUE);
            return 0;
        }
        if (id == kIdSave && code == BN_CLICKED) {
            const LRESULT result = CallWindowProcW(g_originalProc, hwnd, msg, wp, lp);
            if (g_loggingDirty && !SaveLoggingMode()) {
                MessageBoxW(hwnd, L"Could not save the Logging mode to FolderHeatMap.ini.",
                            L"FolderHeatMap", MB_OK | MB_ICONERROR);
            }
            return result;
        }
    }

    if (msg == WM_DESTROY) {
        g_loggingCombo = nullptr;
        g_loggingDirty = false;
    }
    return CallWindowProcW(g_originalProc, hwnd, msg, wp, lp);
}

bool IsTargetClass(const CREATESTRUCTW* cs) {
    if (!cs || !cs->lpszClass || IS_INTRESOURCE(cs->lpszClass)) return false;
    return wcscmp(cs->lpszClass, kConfigWindowClass) == 0;
}

LRESULT CALLBACK CbtHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND) {
        const auto* create = reinterpret_cast<const CBT_CREATEWNDW*>(lp);
        if (create && IsTargetClass(create->lpcs)) {
            HWND hwnd = reinterpret_cast<HWND>(wp);
            g_originalProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(LoggingWndProc)));
            if (g_cbtHook) {
                UnhookWindowsHookEx(g_cbtHook);
                g_cbtHook = nullptr;
            }
        }
    }
    return CallNextHookEx(g_cbtHook, code, wp, lp);
}

struct LoggingUiInstaller {
    LoggingUiInstaller() {
        g_cbtHook = SetWindowsHookExW(WH_CBT, CbtHookProc, nullptr, GetCurrentThreadId());
    }
    ~LoggingUiInstaller() {
        if (g_cbtHook) UnhookWindowsHookEx(g_cbtHook);
    }
};

LoggingUiInstaller g_loggingUiInstaller;
} // namespace
