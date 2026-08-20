#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kConfigWindowClass[] = L"FolderHeatMapConfigWindow";
constexpr int kIdSave = 1009;
constexpr int kIdCancel = 1011;
constexpr int kIdHelp = 1012;
constexpr int kIdLoggingCombo = 1601;
constexpr int kIdLoggingLabel = 1602;
constexpr int kIdLogPathLabel = 1603;

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

std::wstring LogPath() {
    const auto ini = SettingsIni();
    if (ini.empty()) return {};
    return (std::filesystem::path(ini).parent_path() / L"FolderHeatMap.log").wstring();
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
    if (ok) {
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini.c_str());
        g_loggingDirty = false;
    }
    return ok;
}

std::vector<DWORD> TcProcessIds() {
    std::vector<DWORD> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"TOTALCMD64.EXE") == 0 || _wcsicmp(entry.szExeFile, L"TOTALCMD.EXE") == 0)
                result.push_back(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

bool IsTcRunning() { return !TcProcessIds().empty(); }

BOOL CALLBACK CloseTcWindow(HWND hwnd, LPARAM) {
    wchar_t cls[128]{};
    if (GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) > 0 && wcscmp(cls, L"TTOTAL_CMD") == 0)
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

bool StopTc() {
    if (!IsTcRunning()) return true;
    EnumWindows(CloseTcWindow, 0);
    for (int i = 0; i < 50; ++i) { Sleep(100); if (!IsTcRunning()) return true; }
    return false;
}

std::wstring FindTcExe() {
    wchar_t env[2048]{};
    DWORD n = GetEnvironmentVariableW(L"COMMANDER_PATH", env, 2048);
    std::wstring dir;
    if (n > 0 && n < 2048) dir = ExpandEnvironment(env);
    if (dir.empty()) dir = QueryRegString(HKEY_CURRENT_USER, L"Software\\Ghisler\\Total Commander", L"InstallDir");
    if (dir.empty()) dir = QueryRegString(HKEY_LOCAL_MACHINE, L"Software\\Ghisler\\Total Commander", L"InstallDir");
    if (dir.empty()) return {};
    const auto p64 = std::filesystem::path(dir) / L"TOTALCMD64.EXE";
    if (std::filesystem::exists(p64)) return p64.wstring();
    const auto p32 = std::filesystem::path(dir) / L"TOTALCMD.EXE";
    return std::filesystem::exists(p32) ? p32.wstring() : L"";
}

void StartTc() {
    const auto exe = FindTcExe();
    if (!exe.empty()) ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool RunCleanupScript() {
    wchar_t exePath[32768]{};
    if (!GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)))) return false;
    const auto script = std::filesystem::path(exePath).parent_path() / L"cleanup_tc_integration.ps1";
    if (!std::filesystem::exists(script)) return false;
    const auto wincmd = FindWincmdIni();
    if (wincmd.empty()) return false;

    std::wstring params = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() +
                          L"\" -WincmdIni \"" + wincmd + L"\"";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return false;
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD rc = 1;
    GetExitCodeProcess(sei.hProcess, &rc);
    CloseHandle(sei.hProcess);
    return rc == 0;
}

void EnforceStagedNoColors(HWND hwnd) {
    // The legacy Save() path still installs the color rules. During the staged
    // diagnostic rebuild we immediately remove only FolderHeatMap-managed rules
    // again, while TC is stopped, then restart it. This keeps Heat numeric-only.
    if (!IsTcRunning()) return;
    if (!StopTc()) {
        MessageBoxW(hwnd, L"Total Commander could not be stopped to keep staged Heat colors disabled.",
                    L"FolderHeatMap", MB_OK | MB_ICONWARNING);
        return;
    }
    const bool ok = RunCleanupScript();
    StartTc();
    if (!ok)
        MessageBoxW(hwnd, L"FolderHeatMap settings were saved, but staged color-rule cleanup failed.",
                    L"FolderHeatMap", MB_OK | MB_ICONWARNING);
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
    SetWindowPos(hwnd, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top + 76,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    MoveControlDown(hwnd, kIdHelp, 70);
    MoveControlDown(hwnd, kIdSave, 70);
    MoveControlDown(hwnd, kIdCancel, 70);

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND label = CreateWindowExW(0, L"STATIC", L"Logging:", WS_CHILD | WS_VISIBLE | SS_LEFT,
        300, 468, 66, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLoggingLabel)),
        GetModuleHandleW(nullptr), nullptr);
    if (label) SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    g_loggingCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        370, 464, 150, 200, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLoggingCombo)),
        GetModuleHandleW(nullptr), nullptr);
    if (g_loggingCombo) {
        SendMessageW(g_loggingCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(g_loggingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"off"));
        SendMessageW(g_loggingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"single"));
        SendMessageW(g_loggingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"all"));
        SendMessageW(g_loggingCombo, CB_SETCURSEL, ReadLoggingModeIndex(), 0);
    }

    const std::wstring logText = L"Log: " + LogPath();
    HWND pathLabel = CreateWindowExW(0, L"STATIC", logText.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
        36, 500, 484, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLogPathLabel)),
        GetModuleHandleW(nullptr), nullptr);
    if (pathLabel) SendMessageW(pathLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
            // Critical ordering: persist Logging *before* legacy Save() restarts
            // Total Commander. Otherwise the new engine session starts with the
            // previous logging mode (the bug seen in 1.07).
            if (g_loggingDirty && !SaveLoggingMode()) {
                MessageBoxW(hwnd, L"Could not save the Logging mode to FolderHeatMap.ini.",
                            L"FolderHeatMap", MB_OK | MB_ICONERROR);
                return 0;
            }
            const LRESULT result = CallWindowProcW(g_originalProc, hwnd, msg, wp, lp);
            EnforceStagedNoColors(hwnd);
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
