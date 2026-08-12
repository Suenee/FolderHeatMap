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
constexpr int IDC_AUTO_FHM = 1001;
constexpr int IDC_MANUAL_FHM = 1002;
constexpr int IDC_HALF = 1003;
constexpr int IDC_PATH = 1004;
constexpr int IDC_DECAY = 1005;
constexpr int IDC_SMOOTH = 1007;
constexpr int IDC_STEPS = 1008;
constexpr int IDC_SAVE = 1009;
constexpr int IDC_CANCEL = 1011;
constexpr int IDC_HELP_BUTTON = 1012;
constexpr int IDC_COOLDOWN = 1013;
constexpr int IDC_SESSION = 1014;
constexpr int IDC_HALF_SPIN = 1015;
constexpr int IDC_STEPS_SPIN = 1016;
constexpr int IDC_COOLDOWN_SPIN = 1017;
constexpr int IDC_SESSION_SPIN = 1018;
constexpr int IDC_DECAY_SPIN = 1019;
constexpr int IDC_SWATCH_BASE = 1200;
constexpr int MAX_TC_COLOR_FILTERS = 999;
constexpr int MAX_MANAGED_SEARCHES = 128;
constexpr wchar_t WINDOW_CLASS[] = L"FolderHeatMapConfigWindow";
constexpr wchar_t SINGLE_INSTANCE_MUTEX[] = L"Local\\FolderHeatMapConfig.SingleInstance";

fhm::Settings g_settings;
fhm::Settings g_savedSettings;
std::wstring g_wincmdIni;
std::wstring g_settingsIni;
std::array<HWND, 8> g_colorSwatches{};
std::array<std::wstring, 8> g_swatchTooltips{};
HWND g_halfEdit{};
HWND g_decayEdit{};
HWND g_stepsEdit{};
HWND g_cooldownEdit{};
HWND g_sessionEdit{};
HWND g_saveButton{};
HWND g_tooltip{};
HFONT g_boldFont{};
HBRUSH g_windowBrush{};
HINSTANCE g_instance{};
HANDLE g_singleInstanceMutex{};
COLORREF g_level0Color = RGB(0, 0, 0);
COLORREF g_savedLevel0Color = RGB(0, 0, 0);
bool g_initializing = true;

std::wstring ExpandEnvironment(const std::wstring& value) {
    if (value.empty()) return {};
    const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (!needed) return value;
    std::vector<wchar_t> buffer(needed);
    if (!ExpandEnvironmentStringsW(value.c_str(), buffer.data(), needed)) return value;
    return buffer.data();
}

std::wstring QueryRegString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    wchar_t buffer[2048]{};
    DWORD type = 0;
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, buffer, &size) == ERROR_SUCCESS)
        return ExpandEnvironment(buffer);
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
        L"FolderHeatMap needs read/write access to Total Commander's wincmd.ini.\n\n"
        L"Please select the wincmd.ini used by Total Commander. Its location is shown under Help > About Total Commander > INI files.",
        L"FolderHeatMap - Total Commander configuration required", MB_OK | MB_ICONINFORMATION);
    wchar_t file[MAX_PATH] = L"wincmd.ini";
    wchar_t appData[MAX_PATH]{};
    std::wstring initialDir;
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (n > 0 && n < MAX_PATH) initialDir = (std::filesystem::path(appData) / L"GHISLER").wstring();
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
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
    const std::wstring value = ReadIniString(L"Colors", L"ForeColor");
    if (value.empty()) return GetSysColor(COLOR_WINDOWTEXT);
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(value.c_str(), &end, 10);
    return end != value.c_str() ? static_cast<COLORREF>(parsed) : GetSysColor(COLOR_WINDOWTEXT);
}

bool WriteTcBaseColor(COLORREF color) {
    const std::wstring value = std::to_wstring(static_cast<unsigned long>(color));
    return WritePrivateProfileStringW(L"Colors", L"ForeColor", value.c_str(), g_wincmdIni.c_str()) != FALSE;
}

COLORREF Interpolate(COLORREF a, COLORREF b, double t) {
    auto mix = [t](int x, int y) { return std::clamp(static_cast<int>(x + (y - x) * t + 0.5), 0, 255); };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
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

int EditInt(HWND edit, int minValue, int maxValue, bool normalize = false) {
    wchar_t buf[32]{};
    GetWindowTextW(edit, buf, 32);
    const int value = std::clamp(_wtoi(buf), minValue, maxValue);
    if (normalize) SetWindowTextW(edit, std::to_wstring(value).c_str());
    return value;
}

bool SettingsEqual(const fhm::Settings& a, const fhm::Settings& b) {
    if (a.coolingAuto != b.coolingAuto ||
        std::abs(a.coolingHalfLifeDays - b.coolingHalfLifeDays) > 0.0001 ||
        a.includePathHeat != b.includePathHeat ||
        std::abs(a.pathDecay - b.pathDecay) > 0.0001 ||
        a.repeatVisitCooldownSeconds != b.repeatVisitCooldownSeconds ||
        a.sessionResetHours != b.sessionResetHours ||
        a.smoothColors != b.smoothColors ||
        a.stepsPerLevel != b.stepsPerLevel) return false;
    for (size_t i = 0; i < a.colors.size(); ++i) if (a.colors[i] != b.colors[i]) return false;
    return true;
}

fhm::Settings ReadUiSettings(HWND hwnd, bool normalize) {
    fhm::Settings s = g_settings;
    s.coolingAuto = SendDlgItemMessageW(hwnd, IDC_AUTO_FHM, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.coolingHalfLifeDays = EditInt(g_halfEdit, 1, 365, normalize);
    s.includePathHeat = SendDlgItemMessageW(hwnd, IDC_PATH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.pathDecay = EditInt(g_decayEdit, 0, 100, normalize) / 100.0;
    s.repeatVisitCooldownSeconds = EditInt(g_cooldownEdit, 0, 600, normalize);
    s.sessionResetHours = EditInt(g_sessionEdit, 1, 24, normalize);
    s.smoothColors = SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.stepsPerLevel = EditInt(g_stepsEdit, 1, 16, normalize);
    return s;
}

void UpdateSaveState(HWND hwnd) {
    if (g_initializing || !g_saveButton) return;
    const auto current = ReadUiSettings(hwnd, false);
    const bool dirty = !SettingsEqual(current, g_savedSettings) || g_level0Color != g_savedLevel0Color;
    EnableWindow(g_saveButton, dirty ? TRUE : FALSE);
}

void ChooseColor(HWND hwnd, int level) {
    if (level < 0 || level > 7) return;
    static COLORREF custom[16]{};
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = hwnd;
    cc.rgbResult = level == 0 ? g_level0Color : static_cast<COLORREF>(g_settings.colors[level]);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return;
    if (level == 0) g_level0Color = cc.rgbResult;
    else g_settings.colors[level] = cc.rgbResult;
    InvalidateRect(g_colorSwatches[level], nullptr, TRUE);
    UpdateSaveState(hwnd);
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
    for (int i = 0; i < 50; ++i) { Sleep(100); if (!IsTcRunning()) return true; }
    for (DWORD pid : TcProcessIds()) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!process) continue;
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 3000);
        CloseHandle(process);
    }
    for (int i = 0; i < 30; ++i) { if (!IsTcRunning()) return true; Sleep(100); }
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
    for (const auto* suffix : suffixes) WritePrivateProfileStringW(L"searches", (name + suffix).c_str(), nullptr, g_wincmdIni.c_str());
}

void CleanupManagedSearches() { for (int i = 1; i <= MAX_MANAGED_SEARCHES; ++i) DeleteSearch(ManagedSearchName(i)); }

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
    if (!CanUseIni(g_wincmdIni) || !WriteTcBaseColor(g_level0Color)) return false;
    const int expected = WriteManagedColorRules();
    if (expected <= 0) return false;
    const std::wstring firstSearch = ManagedSearchName(1);
    return ReadIniString(L"Colors", L"ColorFilter1") == (L">" + firstSearch) &&
        ReadIniString(L"searches", firstSearch + L"_plugin").rfind(L"folderheatmap.Heat > ", 0) == 0 &&
        GetPrivateProfileIntW(L"FolderHeatMap", L"ManagedColorRuleCount", 0, g_wincmdIni.c_str()) == expected;
}

void UpdateEnabledState(HWND hwnd) {
    const bool manual = SendDlgItemMessageW(hwnd, IDC_MANUAL_FHM, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_halfEdit, manual);
    EnableWindow(GetDlgItem(hwnd, IDC_HALF_SPIN), manual);
    const bool path = SendDlgItemMessageW(hwnd, IDC_PATH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_decayEdit, path);
    EnableWindow(GetDlgItem(hwnd, IDC_DECAY_SPIN), path);
    const bool smooth = SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_stepsEdit, smooth);
    EnableWindow(GetDlgItem(hwnd, IDC_STEPS_SPIN), smooth);
    UpdateSaveState(hwnd);
}

void Save(HWND hwnd) {
    if (!CanUseIni(g_wincmdIni)) {
        MessageBoxW(hwnd, L"Total Commander's wincmd.ini is no longer accessible. Reopen FolderHeatMap Settings and select a writable INI file.",
            L"FolderHeatMap", MB_ICONERROR);
        return;
    }
    g_settings = ReadUiSettings(hwnd, true);
    if (!fhm::SaveSettings(g_settingsIni, g_settings)) {
        MessageBoxW(hwnd, L"Settings could not be saved.", L"FolderHeatMap", MB_ICONERROR);
        return;
    }
    const bool wasRunning = IsTcRunning();
    if (wasRunning && !StopTc()) {
        MessageBoxW(hwnd, L"Total Commander could not be closed even after a forced attempt. Settings were saved, but Total Commander colors were not updated.",
            L"FolderHeatMap", MB_ICONWARNING);
        return;
    }
    if (!ApplyTcColorRules()) {
        if (wasRunning) StartTc();
        const std::wstring msg = L"Could not write or verify Total Commander color rules.\n\nwincmd.ini used:\n" + g_wincmdIni;
        MessageBoxW(hwnd, msg.c_str(), L"FolderHeatMap", MB_ICONWARNING);
        return;
    }
    if (wasRunning) StartTc();
    g_savedSettings = g_settings;
    g_savedLevel0Color = g_level0Color;
    UpdateSaveState(hwnd);
}

void ShowHelp(HWND hwnd) {
    MessageBoxW(hwnd,
        L"Cooling\nAutomatic learns cooling speed from your Total Commander usage rhythm. Manual uses a fixed half-life from 1 to 365 days.\n\n"
        L"Activity tuning\nRepeat cooldown prevents rapid re-entry from inflating Heat. Session reset defines when recent work starts a new session.\n\n"
        L"Path heat\nA hot descendant can keep its parent path warm. Contribution is reduced for every level upward.\n\n"
        L"Color heat map\nColors 0-7 are shown left to right. Level 0 is Total Commander's base text color and is editable. Levels 1-7 are FolderHeatMap heat anchors.\n\n"
        L"Save is enabled only when something has changed. Save writes settings and refreshes Total Commander; Cancel closes without saving current changes.",
        L"FolderHeatMap - Help", MB_OK | MB_ICONINFORMATION);
}

void BringExistingWindowToFront() {
    HWND hwnd = nullptr;
    for (int i = 0; i < 30 && !hwnd; ++i) {
        hwnd = FindWindowW(WINDOW_CLASS, nullptr);
        if (!hwnd) Sleep(50);
    }
    if (!hwnd) return;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
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

            // Two-column settings area, based on the approved layout.
            header(L"Cooling", 28, 18, 190);
            HWND autoBtn = add(L"BUTTON", L"Automatic", BS_AUTORADIOBUTTON | WS_GROUP, 36, 44, 92, 22, IDC_AUTO_FHM);
            HWND manualBtn = add(L"BUTTON", L"Manual", BS_AUTORADIOBUTTON, 132, 44, 78, 22, IDC_MANUAL_FHM);
            add(L"STATIC", L"Half-life:", SS_LEFT, 36, 76, 62, 20);
            g_halfEdit = add(L"EDIT", L"30", WS_BORDER | ES_NUMBER | ES_CENTER, 100, 72, 58, 22, IDC_HALF);
            AddSpin(hwnd, g_halfEdit, IDC_HALF_SPIN, 1, 365);
            add(L"STATIC", L"days", SS_LEFT, 166, 76, 34, 20);

            header(L"Activity tuning", 292, 18, 190);
            add(L"STATIC", L"Repeat cooldown:", SS_LEFT, 300, 48, 108, 20);
            g_cooldownEdit = add(L"EDIT", L"90", WS_BORDER | ES_NUMBER | ES_CENTER, 412, 44, 58, 22, IDC_COOLDOWN);
            AddSpin(hwnd, g_cooldownEdit, IDC_COOLDOWN_SPIN, 0, 600);
            add(L"STATIC", L"sec", SS_LEFT, 478, 48, 28, 20);
            add(L"STATIC", L"Session reset:", SS_LEFT, 300, 80, 108, 20);
            g_sessionEdit = add(L"EDIT", L"8", WS_BORDER | ES_NUMBER | ES_CENTER, 412, 76, 58, 22, IDC_SESSION);
            AddSpin(hwnd, g_sessionEdit, IDC_SESSION_SPIN, 1, 24);
            add(L"STATIC", L"hours", SS_LEFT, 478, 80, 42, 20);

            header(L"Path heat", 28, 122, 190);
            HWND pathBtn = add(L"BUTTON", L"Include hot descendants", BS_AUTOCHECKBOX, 36, 148, 182, 22, IDC_PATH);
            add(L"STATIC", L"Contribution:", SS_LEFT, 36, 180, 82, 20);
            g_decayEdit = add(L"EDIT", L"50", WS_BORDER | ES_NUMBER | ES_CENTER, 120, 176, 58, 22, IDC_DECAY);
            AddSpin(hwnd, g_decayEdit, IDC_DECAY_SPIN, 0, 100);
            add(L"STATIC", L"%", SS_LEFT, 186, 180, 20, 20);

            header(L"Color behavior", 292, 122, 190);
            HWND smoothBtn = add(L"BUTTON", L"Smooth transitions", BS_AUTOCHECKBOX, 300, 148, 154, 22, IDC_SMOOTH);
            add(L"STATIC", L"Steps per level:", SS_LEFT, 300, 180, 108, 20);
            g_stepsEdit = add(L"EDIT", L"4", WS_BORDER | ES_NUMBER | ES_CENTER, 412, 176, 58, 22, IDC_STEPS);
            AddSpin(hwnd, g_stepsEdit, IDC_STEPS_SPIN, 1, 16);

            // Centered title and a full-width, contiguous heat strip.
            HWND heatTitle = header(L"Color heat map", 0, 218, 540);
            if (heatTitle) SetWindowLongPtrW(heatTitle, GWL_STYLE, GetWindowLongPtrW(heatTitle, GWL_STYLE) | SS_CENTER);
            const int stripX = 28;
            const int stripY = 246;
            const int stripWidth = 484;
            const int gap = 2;
            const int swatchWidth = (stripWidth - gap * 7) / 8;
            int x = stripX;
            for (int i = 0; i <= 7; ++i) {
                const int w = (i == 7) ? (stripX + stripWidth - x) : swatchWidth;
                g_colorSwatches[i] = add(L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY, x, stripY, w, 34, IDC_SWATCH_BASE + i);
                g_swatchTooltips[i] = i == 0
                    ? L"Heat level 0 color - Total Commander base text color (editable)."
                    : L"Heat level " + std::to_wstring(i) + L" color.";
                AddTooltip(g_colorSwatches[i], g_swatchTooltips[i].c_str());
                x += w + gap;
            }

            HWND helpBtn = add(L"BUTTON", L"Help", BS_PUSHBUTTON, 28, 304, 78, 30, IDC_HELP_BUTTON);
            g_saveButton = add(L"BUTTON", L"Save", BS_DEFPUSHBUTTON, 356, 304, 78, 30, IDC_SAVE);
            add(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 442, 304, 78, 30, IDC_CANCEL);

            AddTooltip(autoBtn, L"Learns cooling speed from your Total Commander usage rhythm.");
            AddTooltip(manualBtn, L"Use a fixed cooling half-life instead of automatic learning.");
            AddTooltip(g_halfEdit, L"Manual cooling half-life. Range: 1-365 days.");
            AddTooltip(pathBtn, L"Let the hottest descendant contribute heat to its parent path.");
            AddTooltip(g_decayEdit, L"Descendant heat retained per directory level. Range: 0-100%.");
            AddTooltip(g_cooldownEdit, L"Repeated entries inside this interval do not increase Heat. Range: 0-600 seconds.");
            AddTooltip(g_sessionEdit, L"Idle gap after which recent work starts a new session. Range: 1-24 hours.");
            AddTooltip(smoothBtn, L"Generate intermediate Total Commander colors between heat anchors.");
            AddTooltip(g_stepsEdit, L"Intermediate color steps per heat level. Range: 1-16.");
            AddTooltip(helpBtn, L"Show help for FolderHeatMap settings.");

            SendDlgItemMessageW(hwnd, g_settings.coolingAuto ? IDC_AUTO_FHM : IDC_MANUAL_FHM, BM_SETCHECK, BST_CHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_PATH, BM_SETCHECK, g_settings.includePathHeat ? BST_CHECKED : BST_UNCHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_SETCHECK, g_settings.smoothColors ? BST_CHECKED : BST_UNCHECKED, 0);
            SetWindowTextW(g_halfEdit, std::to_wstring(static_cast<int>(g_settings.coolingHalfLifeDays)).c_str());
            SetWindowTextW(g_cooldownEdit, std::to_wstring(g_settings.repeatVisitCooldownSeconds).c_str());
            SetWindowTextW(g_sessionEdit, std::to_wstring(g_settings.sessionResetHours).c_str());
            SetWindowTextW(g_decayEdit, std::to_wstring(static_cast<int>(std::lround(g_settings.pathDecay * 100.0))).c_str());
            SetWindowTextW(g_stepsEdit, std::to_wstring(g_settings.stepsPerLevel).c_str());
            g_initializing = false;
            UpdateEnabledState(hwnd);
            UpdateSaveState(hwnd);
            return 0;
        }

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
            const int code = HIWORD(wp);
            if (id >= IDC_SWATCH_BASE && id <= IDC_SWATCH_BASE + 7 && code == STN_CLICKED) {
                ChooseColor(hwnd, id - IDC_SWATCH_BASE);
                return 0;
            }
            if (id == IDC_AUTO_FHM || id == IDC_MANUAL_FHM || id == IDC_PATH || id == IDC_SMOOTH) {
                UpdateEnabledState(hwnd);
                return 0;
            }
            if ((id == IDC_HALF || id == IDC_DECAY || id == IDC_STEPS || id == IDC_COOLDOWN || id == IDC_SESSION) && code == EN_CHANGE) {
                UpdateSaveState(hwnd);
                return 0;
            }
            if (id == IDC_HELP_BUTTON) { ShowHelp(hwnd); return 0; }
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
    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, SINGLE_INSTANCE_MUTEX);
    if (!g_singleInstanceMutex) return 4;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        BringExistingWindowToFront();
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return 0;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_UPDOWN_CLASS | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    g_wincmdIni = FindWincmdIni();
    if (g_wincmdIni.empty()) g_wincmdIni = PromptForWincmdIni();
    if (g_wincmdIni.empty()) { CloseHandle(g_singleInstanceMutex); return 1; }

    g_settingsIni = fhm::SettingsPathFromDefaultIni(g_wincmdIni);
    fhm::LoadSettings(g_settingsIni, g_settings);
    g_savedSettings = g_settings;
    g_level0Color = TcBaseColor();
    g_savedLevel0Color = g_level0Color;
    g_windowBrush = GetSysColorBrush(COLOR_WINDOW);

    HICON appIcon = static_cast<HICON>(LoadImageW(instance, L"IDI_FHM_CONFIG", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    HICON smallIcon = static_cast<HICON>(LoadImageW(instance, L"IDI_FHM_CONFIG", IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = WINDOW_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_windowBrush;
    wc.hIcon = appIcon;
    wc.hIconSm = smallIcon ? smallIcon : appIcon;
    const ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        const DWORD err = GetLastError();
        MessageBoxW(nullptr, (L"Could not register the settings window. Windows error: " + std::to_wstring(err)).c_str(),
            L"FolderHeatMap", MB_ICONERROR);
        CloseHandle(g_singleInstanceMutex);
        return 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS, L"FolderHeatMap - Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 556, 386, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        const DWORD err = GetLastError();
        MessageBoxW(nullptr, (L"Could not create the settings window. Windows error: " + std::to_wstring(err)).c_str(),
            L"FolderHeatMap", MB_ICONERROR);
        CloseHandle(g_singleInstanceMutex);
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

    ReleaseMutex(g_singleInstanceMutex);
    CloseHandle(g_singleInstanceMutex);
    g_singleInstanceMutex = nullptr;
    return 0;
}
