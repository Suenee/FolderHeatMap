#include "Settings.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr int IDC_AUTO = 1001;
constexpr int IDC_MANUAL = 1002;
constexpr int IDC_HALF = 1003;
constexpr int IDC_PATH = 1004;
constexpr int IDC_DECAY = 1005;
constexpr int IDC_DECAY_VALUE = 1006;
constexpr int IDC_SMOOTH = 1007;
constexpr int IDC_STEPS = 1008;
constexpr int IDC_SAVE = 1009;
constexpr int IDC_STATUS = 1010;
constexpr int IDC_CANCEL = 1011;
constexpr int IDC_HELP = 1012;
constexpr int IDC_COOLDOWN = 1013;
constexpr int IDC_SESSION = 1014;
constexpr int IDC_HALF_SPIN = 1015;
constexpr int IDC_STEPS_SPIN = 1016;
constexpr int IDC_COOLDOWN_SPIN = 1017;
constexpr int IDC_SESSION_SPIN = 1018;
constexpr int IDC_COLOR_BASE = 1100;
constexpr int IDC_SWATCH_BASE = 1200;
constexpr int MAX_TC_COLOR_FILTERS = 999;
constexpr int MAX_MANAGED_SEARCHES = 128;

fhm::Settings g_settings;
std::wstring g_wincmdIni;
std::wstring g_settingsIni;
std::array<HWND, 8> g_colorButtons{};
std::array<HWND, 8> g_colorSwatches{};
HWND g_halfEdit{};
HWND g_decaySlider{};
HWND g_decayValue{};
HWND g_stepsEdit{};
HWND g_cooldownEdit{};
HWND g_sessionEdit{};
HWND g_status{};
HWND g_tooltip{};
HFONT g_boldFont{};
HBRUSH g_windowBrush{};
HINSTANCE g_instance{};
COLORREF g_level0Color = RGB(0, 0, 0);

std::wstring ExpandEnvironment(const std::wstring& value) {
    if (value.empty()) return {};
    DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (!needed) return value;
    std::vector<wchar_t> buffer(needed);
    if (!ExpandEnvironmentStringsW(value.c_str(), buffer.data(), needed)) return value;
    return buffer.data();
}

std::wstring QueryRegString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    wchar_t buffer[2048]{};
    DWORD type = 0;
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, buffer, &size) == ERROR_SUCCESS) {
        return ExpandEnvironment(buffer);
    }
    return {};
}

bool CanUseIni(const std::wstring& path) {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

std::wstring FindWincmdIni() {
    wchar_t env[2048]{};
    DWORD n = GetEnvironmentVariableW(L"COMMANDER_INI", env, 2048);
    if (n > 0 && n < 2048) {
        const auto expanded = ExpandEnvironment(env);
        if (CanUseIni(expanded)) return expanded;
    }

    auto p = QueryRegString(HKEY_CURRENT_USER, L"Software\\Ghisler\\Total Commander", L"IniFileName");
    if (CanUseIni(p)) return p;

    p = QueryRegString(HKEY_LOCAL_MACHINE, L"Software\\Ghisler\\Total Commander", L"IniFileName");
    if (CanUseIni(p)) return p;

    wchar_t appData[2048]{};
    n = GetEnvironmentVariableW(L"APPDATA", appData, 2048);
    if (n > 0 && n < 2048) {
        const auto fallback = (std::filesystem::path(appData) / L"GHISLER" / L"wincmd.ini").wstring();
        if (CanUseIni(fallback)) return fallback;
    }
    return {};
}

std::wstring PromptForWincmdIni() {
    MessageBoxW(nullptr,
        L"FolderHeatMap needs read/write access to Total Commander's wincmd.ini before settings can be used.\n\n"
        L"Please select the wincmd.ini used by Total Commander. You can find its location in Total Commander under Help > About Total Commander > INI files.",
        L"FolderHeatMap - Total Commander configuration required", MB_OK | MB_ICONINFORMATION);

    wchar_t file[MAX_PATH] = L"wincmd.ini";
    wchar_t initial[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", initial, MAX_PATH);
    std::wstring initialDir;
    if (n > 0 && n < MAX_PATH) initialDir = (std::filesystem::path(initial) / L"GHISLER").wstring();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = L"Total Commander INI (wincmd.ini)\0wincmd.ini\0INI files (*.ini)\0*.ini\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.lpstrTitle = L"Select Total Commander wincmd.ini";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return {};

    const std::wstring selected = file;
    if (!CanUseIni(selected)) {
        MessageBoxW(nullptr, L"The selected file is not writable. FolderHeatMap cannot continue until wincmd.ini is accessible.",
            L"FolderHeatMap", MB_OK | MB_ICONERROR);
        return {};
    }
    return selected;
}

std::wstring ReadIniString(const wchar_t* section, const std::wstring& key) {
    std::vector<wchar_t> buffer(8192);
    GetPrivateProfileStringW(section, key.c_str(), L"", buffer.data(), static_cast<DWORD>(buffer.size()), g_wincmdIni.c_str());
    return buffer.data();
}

COLORREF TcBaseColor() {
    const int value = GetPrivateProfileIntW(L"Colors", L"ForeColor", -1, g_wincmdIni.c_str());
    return value < 0 ? GetSysColor(COLOR_WINDOWTEXT) : static_cast<COLORREF>(value);
}

COLORREF Interpolate(COLORREF a, COLORREF b, double t) {
    auto mix = [t](int x, int y) {
        return std::clamp(static_cast<int>(x + (y - x) * t + 0.5), 0, 255);
    };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

void UpdateColorButton(int level) {
    const std::wstring text = L"Level " + std::to_wstring(level);
    SetWindowTextW(g_colorButtons[level], text.c_str());
    if (g_colorSwatches[level]) InvalidateRect(g_colorSwatches[level], nullptr, TRUE);
}

void ChooseLevelColor(HWND hwnd, int level) {
    if (level < 1 || level > 7) return;
    static COLORREF custom[16]{};
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = hwnd;
    cc.rgbResult = static_cast<COLORREF>(g_settings.colors[level]);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        g_settings.colors[level] = cc.rgbResult;
        UpdateColorButton(level);
    }
}

void AddTooltip(HWND control, const wchar_t* text) {
    if (!g_tooltip || !control || !text) return;
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(control);
    ti.uId = reinterpret_cast<UINT_PTR>(control);
    ti.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(g_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

HWND AddSpin(HWND hwnd, HWND buddy, int id, int minValue, int maxValue) {
    HWND spin = CreateWindowExW(0, UPDOWN_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_SETBUDDYINT | UDS_ARROWKEYS,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(buddy), 0);
    SendMessageW(spin, UDM_SETRANGE32, minValue, maxValue);
    return spin;
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
    if (std::filesystem::exists(p32)) return p32.wstring();
    return {};
}

BOOL CALLBACK CloseTcWindow(HWND hwnd, LPARAM) {
    wchar_t cls[128]{};
    if (GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) > 0 && wcscmp(cls, L"TTOTAL_CMD") == 0)
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

bool StopTc() {
    if (!IsTcRunning()) return true;
    EnumWindows(CloseTcWindow, 0);
    for (int i = 0; i < 50; ++i) {
        Sleep(100);
        if (!IsTcRunning()) return true;
    }
    for (DWORD pid : TcProcessIds()) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!process) continue;
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 3000);
        CloseHandle(process);
    }
    for (int i = 0; i < 30; ++i) {
        if (!IsTcRunning()) return true;
        Sleep(100);
    }
    return !IsTcRunning();
}

void StartTc() {
    const auto exe = FindTcExe();
    if (!exe.empty()) ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring HeatNumber(double value) {
    std::wostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::fixed << std::setprecision(3) << value;
    return ss.str();
}

std::wstring ManagedSearchName(int index) {
    wchar_t number[8]{};
    swprintf_s(number, L"%03d", index);
    return std::wstring(L"FolderHeatMap Heat ") + number;
}

bool IsManagedColorFilter(const std::wstring& value) { return value.rfind(L">FolderHeatMap Heat ", 0) == 0; }

void DeleteSearch(const std::wstring& name) {
    static const wchar_t* suffixes[] = {L"_SearchFor", L"_SearchIn", L"_SearchText", L"_SearchFlags", L"_plugin"};
    for (const auto* suffix : suffixes)
        WritePrivateProfileStringW(L"searches", (name + suffix).c_str(), nullptr, g_wincmdIni.c_str());
}

void CleanupManagedSearches() {
    for (int i = 1; i <= MAX_MANAGED_SEARCHES; ++i) DeleteSearch(ManagedSearchName(i));
}

struct ExistingColorRule { std::wstring filter, color, colorDark; };

void DeleteColorRuleIndex(int index) {
    const std::wstring base = L"ColorFilter" + std::to_wstring(index);
    WritePrivateProfileStringW(L"Colors", base.c_str(), nullptr, g_wincmdIni.c_str());
    WritePrivateProfileStringW(L"Colors", (base + L"Color").c_str(), nullptr, g_wincmdIni.c_str());
    WritePrivateProfileStringW(L"Colors", (base + L"ColorDark").c_str(), nullptr, g_wincmdIni.c_str());
}

void WriteColorRuleIndex(int index, const ExistingColorRule& rule) {
    const std::wstring base = L"ColorFilter" + std::to_wstring(index);
    WritePrivateProfileStringW(L"Colors", base.c_str(), rule.filter.c_str(), g_wincmdIni.c_str());
    if (!rule.color.empty()) WritePrivateProfileStringW(L"Colors", (base + L"Color").c_str(), rule.color.c_str(), g_wincmdIni.c_str());
    if (!rule.colorDark.empty()) WritePrivateProfileStringW(L"Colors", (base + L"ColorDark").c_str(), rule.colorDark.c_str(), g_wincmdIni.c_str());
}

void CreateManagedSearch(const std::wstring& name, double threshold) {
    WritePrivateProfileStringW(L"searches", (name + L"_SearchFor").c_str(), L"", g_wincmdIni.c_str());
    WritePrivateProfileStringW(L"searches", (name + L"_SearchIn").c_str(), L"", g_wincmdIni.c_str());
    WritePrivateProfileStringW(L"searches", (name + L"_SearchText").c_str(), L"", g_wincmdIni.c_str());
    WritePrivateProfileStringW(L"searches", (name + L"_SearchFlags").c_str(), L"0|002002000020|||||||||0000|||", g_wincmdIni.c_str());
    const std::wstring expression = L"folderheatmap.Heat > " + HeatNumber(threshold);
    WritePrivateProfileStringW(L"searches", (name + L"_plugin").c_str(), expression.c_str(), g_wincmdIni.c_str());
}

void RemoveLegacyColorKeys() {
    for (int i = 0; i < MAX_MANAGED_SEARCHES; ++i) {
        const std::wstring name = L"FHM_" + std::to_wstring(i);
        WritePrivateProfileStringW(L"Colors", name.c_str(), nullptr, g_wincmdIni.c_str());
        WritePrivateProfileStringW(L"Colors", (name + L"Color").c_str(), nullptr, g_wincmdIni.c_str());
    }
}

int WriteManagedColorRules() {
    CleanupManagedSearches();
    RemoveLegacyColorKeys();
    std::vector<ExistingColorRule> existing;
    for (int i = 1; i <= MAX_TC_COLOR_FILTERS; ++i) {
        const std::wstring base = L"ColorFilter" + std::to_wstring(i);
        const std::wstring filter = ReadIniString(L"Colors", base);
        if (filter.empty() || IsManagedColorFilter(filter)) continue;
        existing.push_back({filter, ReadIniString(L"Colors", base + L"Color"), ReadIniString(L"Colors", base + L"ColorDark")});
    }
    for (int i = 1; i <= MAX_TC_COLOR_FILTERS; ++i) DeleteColorRuleIndex(i);

    const int steps = g_settings.smoothColors ? std::clamp(g_settings.stepsPerLevel, 1, 16) : 1;
    int tcRuleIndex = 1;
    int managedCount = 0;
    constexpr double EPSILON = 0.001;

    {
        const std::wstring searchName = ManagedSearchName(++managedCount);
        CreateManagedSearch(searchName, 7.0 - EPSILON);
        WriteColorRuleIndex(tcRuleIndex++, {L">" + searchName, std::to_wstring(static_cast<unsigned long>(g_settings.colors[7])), L""});
    }
    for (int level = 6; level >= 1; --level) {
        const COLORREF from = static_cast<COLORREF>(g_settings.colors[level]);
        const COLORREF to = static_cast<COLORREF>(g_settings.colors[level + 1]);
        for (int s = steps - 1; s >= 0; --s) {
            const double position = static_cast<double>(s) / steps;
            const std::wstring searchName = ManagedSearchName(++managedCount);
            CreateManagedSearch(searchName, level + position - EPSILON);
            WriteColorRuleIndex(tcRuleIndex++, {L">" + searchName,
                std::to_wstring(static_cast<unsigned long>(Interpolate(from, to, position))), L""});
        }
    }
    for (const auto& rule : existing) {
        if (tcRuleIndex > MAX_TC_COLOR_FILTERS) break;
        WriteColorRuleIndex(tcRuleIndex++, rule);
    }
    WritePrivateProfileStringW(L"FolderHeatMap", L"ManagedColorRuleCount", std::to_wstring(managedCount).c_str(), g_wincmdIni.c_str());
    WritePrivateProfileStringW(L"FolderHeatMap", L"ManagedColorRuleStart", L"1", g_wincmdIni.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_wincmdIni.c_str());
    return managedCount;
}

bool ApplyTcColorRules() {
    if (!CanUseIni(g_wincmdIni)) return false;
    const int expected = WriteManagedColorRules();
    if (expected <= 0) return false;
    const std::wstring firstSearch = ManagedSearchName(1);
    return ReadIniString(L"Colors", L"ColorFilter1") == (L">" + firstSearch) &&
        ReadIniString(L"searches", firstSearch + L"_plugin").rfind(L"folderheatmap.Heat > ", 0) == 0 &&
        GetPrivateProfileIntW(L"FolderHeatMap", L"ManagedColorRuleCount", 0, g_wincmdIni.c_str()) == expected;
}

int EditInt(HWND edit, int minValue, int maxValue) {
    wchar_t buf[32]{};
    GetWindowTextW(edit, buf, 32);
    return std::clamp(_wtoi(buf), minValue, maxValue);
}

void UpdateEnabledState(HWND hwnd) {
    const bool manual = SendDlgItemMessageW(hwnd, IDC_MANUAL, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_halfEdit, manual);
    EnableWindow(GetDlgItem(hwnd, IDC_HALF_SPIN), manual);
    EnableWindow(g_decaySlider, SendDlgItemMessageW(hwnd, IDC_PATH, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(g_stepsEdit, SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_STEPS_SPIN), IsWindowEnabled(g_stepsEdit));
}

void Save(HWND hwnd) {
    if (!CanUseIni(g_wincmdIni)) {
        MessageBoxW(hwnd, L"Total Commander's wincmd.ini is no longer accessible. Reopen FolderHeatMap Settings and select a writable INI file.", L"FolderHeatMap", MB_ICONERROR);
        return;
    }

    g_settings.coolingAuto = SendDlgItemMessageW(hwnd, IDC_AUTO, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.coolingHalfLifeDays = EditInt(g_halfEdit, 1, 365);
    g_settings.includePathHeat = SendDlgItemMessageW(hwnd, IDC_PATH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.pathDecay = static_cast<double>(SendMessageW(g_decaySlider, TBM_GETPOS, 0, 0)) / 100.0;
    g_settings.repeatVisitCooldownSeconds = EditInt(g_cooldownEdit, 0, 600);
    g_settings.sessionResetHours = EditInt(g_sessionEdit, 1, 24);
    g_settings.smoothColors = SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.stepsPerLevel = EditInt(g_stepsEdit, 1, 16);

    if (!fhm::SaveSettings(g_settingsIni, g_settings)) {
        MessageBoxW(hwnd, L"Settings could not be saved.", L"FolderHeatMap", MB_ICONERROR);
        return;
    }

    const bool wasRunning = IsTcRunning();
    if (wasRunning && !StopTc()) {
        MessageBoxW(hwnd, L"Total Commander could not be closed even after a forced attempt. Settings were saved, but colors were not updated.", L"FolderHeatMap", MB_ICONWARNING);
        return;
    }
    if (!ApplyTcColorRules()) {
        if (wasRunning) StartTc();
        const std::wstring msg = L"Could not write or verify Total Commander color rules.\n\nwincmd.ini used:\n" + g_wincmdIni;
        MessageBoxW(hwnd, msg.c_str(), L"FolderHeatMap", MB_ICONWARNING);
        return;
    }
    if (wasRunning) StartTc();
    SetWindowTextW(g_status, L"Saved. Total Commander color map updated.");
}

void ShowHelp(HWND hwnd) {
    MessageBoxW(hwnd,
        L"Cooling\nAutomatic mode learns your usage rhythm. Manual mode uses a fixed 1-365 day half-life.\n\n"
        L"Path heat\nA hot descendant can keep its parent path warm. Contribution is reduced for every level upward.\n\n"
        L"Activity tuning\nRepeat visit cooldown prevents rapid re-entry from inflating heat. Session reset defines when recent work starts a new session.\n\n"
        L"Color map\nLevel 0 shows Total Commander's base text color and cannot be edited. Levels 1-7 are editable anchors; click either a level button or its color square.\n\n"
        L"Save updates FolderHeatMap settings and Total Commander color rules. Cancel closes without saving current changes.",
        L"FolderHeatMap - Help", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            LOGFONTW lf{};
            GetObjectW(font, sizeof(lf), &lf);
            lf.lfWeight = FW_SEMIBOLD;
            g_boldFont = CreateFontIndirectW(&lf);

            auto add = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h, int id = 0) {
                HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
                if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return c;
            };
            auto header = [&](const wchar_t* txt, int x, int y, int w) {
                HWND c = add(L"STATIC", txt, SS_LEFT, x, y, w, 20);
                if (c && g_boldFont) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_boldFont), TRUE);
                return c;
            };

            g_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, g_instance, nullptr);
            SendMessageW(g_tooltip, TTM_SETMAXTIPWIDTH, 0, 360);

            header(L"Cooling", 20, 16, 250);
            HWND autoBtn = add(L"BUTTON", L"Automatic", BS_AUTORADIOBUTTON | WS_GROUP, 28, 40, 92, 22, IDC_AUTO);
            HWND manualBtn = add(L"BUTTON", L"Manual", BS_AUTORADIOBUTTON, 126, 40, 76, 22, IDC_MANUAL);
            add(L"STATIC", L"Half-life:", SS_LEFT, 28, 70, 70, 20);
            g_halfEdit = add(L"EDIT", L"30", WS_BORDER | ES_NUMBER | ES_CENTER, 98, 67, 55, 24, IDC_HALF);
            AddSpin(hwnd, g_halfEdit, IDC_HALF_SPIN, 1, 365);
            add(L"STATIC", L"days", SS_LEFT, 160, 70, 36, 20);

            header(L"Activity tuning", 310, 16, 250);
            add(L"STATIC", L"Repeat cooldown:", SS_LEFT, 318, 43, 105, 20);
            g_cooldownEdit = add(L"EDIT", L"90", WS_BORDER | ES_NUMBER | ES_CENTER, 430, 40, 58, 24, IDC_COOLDOWN);
            AddSpin(hwnd, g_cooldownEdit, IDC_COOLDOWN_SPIN, 0, 600);
            add(L"STATIC", L"sec", SS_LEFT, 495, 43, 30, 20);
            add(L"STATIC", L"Session reset:", SS_LEFT, 318, 73, 105, 20);
            g_sessionEdit = add(L"EDIT", L"8", WS_BORDER | ES_NUMBER | ES_CENTER, 430, 70, 58, 24, IDC_SESSION);
            AddSpin(hwnd, g_sessionEdit, IDC_SESSION_SPIN, 1, 24);
            add(L"STATIC", L"hours", SS_LEFT, 495, 73, 42, 20);

            header(L"Path heat", 20, 112, 250);
            HWND pathBtn = add(L"BUTTON", L"Include hot descendants", BS_AUTOCHECKBOX, 28, 136, 180, 22, IDC_PATH);
            add(L"STATIC", L"Contribution:", SS_LEFT, 28, 167, 78, 20);
            g_decaySlider = add(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, 105, 160, 125, 28, IDC_DECAY);
            SendMessageW(g_decaySlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            g_decayValue = add(L"STATIC", L"50%", SS_RIGHT, 235, 166, 40, 20, IDC_DECAY_VALUE);

            header(L"Color behavior", 310, 112, 250);
            HWND smoothBtn = add(L"BUTTON", L"Smooth transitions", BS_AUTOCHECKBOX, 318, 136, 150, 22, IDC_SMOOTH);
            add(L"STATIC", L"Steps per level:", SS_LEFT, 318, 167, 105, 20);
            g_stepsEdit = add(L"EDIT", L"4", WS_BORDER | ES_NUMBER | ES_CENTER, 430, 164, 58, 24, IDC_STEPS);
            AddSpin(hwnd, g_stepsEdit, IDC_STEPS_SPIN, 1, 16);

            header(L"Color map", 20, 208, 540);
            for (int i = 0; i <= 7; ++i) {
                const int col = i < 4 ? 0 : 1;
                const int row = i < 4 ? i : i - 4;
                const int x = col == 0 ? 28 : 310;
                const int y = 234 + row * 34;
                g_colorButtons[i] = add(L"BUTTON", L"", BS_PUSHBUTTON, x, y, 105, 24, IDC_COLOR_BASE + i);
                g_colorSwatches[i] = add(L"STATIC", L"", SS_OWNERDRAW | (i == 0 ? 0 : SS_NOTIFY), x + 113, y + 1, 23, 22, IDC_SWATCH_BASE + i);
                UpdateColorButton(i);
                if (i == 0) EnableWindow(g_colorButtons[i], FALSE);
            }

            HWND helpBtn = add(L"BUTTON", L"Help", BS_PUSHBUTTON, 20, 400, 78, 30, IDC_HELP);
            add(L"BUTTON", L"Save", BS_DEFPUSHBUTTON, 404, 400, 78, 30, IDC_SAVE);
            add(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 490, 400, 78, 30, IDC_CANCEL);
            g_status = add(L"STATIC", L"", SS_LEFT, 110, 406, 280, 32, IDC_STATUS);

            AddTooltip(autoBtn, L"Learns cooling speed from how many days you actively use Total Commander.");
            AddTooltip(manualBtn, L"Use a fixed cooling half-life instead of automatic learning.");
            AddTooltip(g_halfEdit, L"Manual cooling half-life, from 1 to 365 days.");
            AddTooltip(pathBtn, L"Let the hottest descendant contribute heat to its parent path.");
            AddTooltip(g_decaySlider, L"Percentage of descendant heat retained for each level upward, from 0% to 100%.");
            AddTooltip(g_cooldownEdit, L"Repeated entries into the same folder inside this interval still increase Visits, but do not increase Heat. Range: 0-600 seconds.");
            AddTooltip(g_sessionEdit, L"After this idle gap, the next effective visit starts a new recent-work session. Range: 1-24 hours.");
            AddTooltip(smoothBtn, L"Generate intermediate Total Commander colors between the seven anchor levels.");
            AddTooltip(g_stepsEdit, L"Number of generated intermediate color steps per heat level. Range: 1-16.");
            AddTooltip(helpBtn, L"Show an explanation of the heat model and settings.");
            for (int i = 1; i <= 7; ++i) {
                AddTooltip(g_colorButtons[i], L"Choose the anchor color for this heat level.");
                AddTooltip(g_colorSwatches[i], L"Click the color square to choose the anchor color.");
            }
            AddTooltip(g_colorButtons[0], L"Read-only preview of Total Commander's base text color.");

            SendDlgItemMessageW(hwnd, g_settings.coolingAuto ? IDC_AUTO : IDC_MANUAL, BM_SETCHECK, BST_CHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_PATH, BM_SETCHECK, g_settings.includePathHeat ? BST_CHECKED : BST_UNCHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_SETCHECK, g_settings.smoothColors ? BST_CHECKED : BST_UNCHECKED, 0);
            SetWindowTextW(g_halfEdit, std::to_wstring(static_cast<int>(g_settings.coolingHalfLifeDays)).c_str());
            SetWindowTextW(g_cooldownEdit, std::to_wstring(g_settings.repeatVisitCooldownSeconds).c_str());
            SetWindowTextW(g_sessionEdit, std::to_wstring(g_settings.sessionResetHours).c_str());
            SetWindowTextW(g_stepsEdit, std::to_wstring(g_settings.stepsPerLevel).c_str());
            SendMessageW(g_decaySlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(g_settings.pathDecay * 100.0)));
            const std::wstring decayText = std::to_wstring(static_cast<int>(std::lround(g_settings.pathDecay * 100.0))) + L"%";
            SetWindowTextW(g_decayValue, decayText.c_str());
            UpdateEnabledState(hwnd);
            return 0;
        }

        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lp) == g_decaySlider) {
                const int value = static_cast<int>(SendMessageW(g_decaySlider, TBM_GETPOS, 0, 0));
                const std::wstring text = std::to_wstring(value) + L"%";
                SetWindowTextW(g_decayValue, text.c_str());
                return 0;
            }
            break;

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(g_windowBrush);
        }

        case WM_DRAWITEM: {
            const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (dis && dis->CtlID >= IDC_SWATCH_BASE && dis->CtlID <= IDC_SWATCH_BASE + 7) {
                const int level = static_cast<int>(dis->CtlID) - IDC_SWATCH_BASE;
                RECT r = dis->rcItem;
                const COLORREF color = level == 0 ? g_level0Color : static_cast<COLORREF>(g_settings.colors[level]);
                HBRUSH brush = CreateSolidBrush(color);
                FillRect(dis->hDC, &r, brush);
                DeleteObject(brush);
                FrameRect(dis->hDC, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id >= IDC_COLOR_BASE + 1 && id <= IDC_COLOR_BASE + 7) {
                ChooseLevelColor(hwnd, id - IDC_COLOR_BASE);
                return 0;
            }
            if (id >= IDC_SWATCH_BASE + 1 && id <= IDC_SWATCH_BASE + 7 && HIWORD(wp) == STN_CLICKED) {
                ChooseLevelColor(hwnd, id - IDC_SWATCH_BASE);
                return 0;
            }
            if (id == IDC_AUTO || id == IDC_MANUAL || id == IDC_PATH || id == IDC_SMOOTH) {
                UpdateEnabledState(hwnd);
                return 0;
            }
            if (id == IDC_HELP) { ShowHelp(hwnd); return 0; }
            if (id == IDC_SAVE) { Save(hwnd); return 0; }
            if (id == IDC_CANCEL) { DestroyWindow(hwnd); return 0; }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_boldFont) { DeleteObject(g_boldFont); g_boldFont = nullptr; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    g_instance = instance;

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_UPDOWN_CLASS | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    g_wincmdIni = FindWincmdIni();
    if (g_wincmdIni.empty()) g_wincmdIni = PromptForWincmdIni();
    if (g_wincmdIni.empty()) return 1;

    g_settingsIni = fhm::SettingsPathFromDefaultIni(g_wincmdIni);
    fhm::LoadSettings(g_settingsIni, g_settings);
    g_level0Color = TcBaseColor();
    g_windowBrush = GetSysColorBrush(COLOR_WINDOW);

    HICON appIcon = static_cast<HICON>(LoadImageW(instance, L"IDI_FHM_CONFIG", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    HICON smallIcon = static_cast<HICON>(LoadImageW(instance, L"IDI_FHM_CONFIG", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"FolderHeatMapConfigWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_windowBrush;
    wc.hIcon = appIcon;
    wc.hIconSm = smallIcon ? smallIcon : appIcon;
    ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        const DWORD err = GetLastError();
        const std::wstring text = L"Could not register the settings window. Windows error: " + std::to_wstring(err);
        MessageBoxW(nullptr, text.c_str(), L"FolderHeatMap", MB_ICONERROR);
        return 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"FolderHeatMap - Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 480, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        const DWORD err = GetLastError();
        const std::wstring text = L"Could not create the settings window. Windows error: " + std::to_wstring(err);
        MessageBoxW(nullptr, text.c_str(), L"FolderHeatMap", MB_ICONERROR);
        return 3;
    }

    if (appIcon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
    if (smallIcon) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));

    ShowWindow(hwnd, show == 0 ? SW_SHOWNORMAL : show);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
