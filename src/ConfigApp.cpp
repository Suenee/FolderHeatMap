#include "Settings.h"

#include <windows.h>
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
constexpr int IDC_SMOOTH = 1006;
constexpr int IDC_STEPS = 1007;
constexpr int IDC_APPLY = 1008;
constexpr int IDC_STATUS = 1009;
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
HWND g_decayEdit{};
HWND g_stepsEdit{};
HWND g_status{};
HINSTANCE g_instance{};

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

std::wstring FindWincmdIni() {
    wchar_t env[2048]{};
    DWORD n = GetEnvironmentVariableW(L"COMMANDER_INI", env, 2048);
    if (n > 0 && n < 2048) {
        const auto expanded = ExpandEnvironment(env);
        if (std::filesystem::exists(expanded)) return expanded;
    }

    auto p = QueryRegString(HKEY_CURRENT_USER, L"Software\\Ghisler\\Total Commander", L"IniFileName");
    if (!p.empty() && std::filesystem::exists(p)) return p;

    p = QueryRegString(HKEY_LOCAL_MACHINE, L"Software\\Ghisler\\Total Commander", L"IniFileName");
    if (!p.empty() && std::filesystem::exists(p)) return p;

    wchar_t appData[2048]{};
    n = GetEnvironmentVariableW(L"APPDATA", appData, 2048);
    if (n > 0 && n < 2048) {
        std::filesystem::path fallback = std::filesystem::path(appData) / L"GHISLER" / L"wincmd.ini";
        if (std::filesystem::exists(fallback)) return fallback.wstring();
    }
    return {};
}

std::wstring ReadIniString(const wchar_t* section, const std::wstring& key) {
    std::vector<wchar_t> buffer(8192);
    GetPrivateProfileStringW(section, key.c_str(), L"", buffer.data(), static_cast<DWORD>(buffer.size()), g_wincmdIni.c_str());
    return buffer.data();
}

COLORREF Interpolate(COLORREF a, COLORREF b, double t) {
    auto mix = [t](int x, int y) {
        return std::clamp(static_cast<int>(x + (y - x) * t + 0.5), 0, 255);
    };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

void UpdateColorButton(int level) {
    std::wstring text = L"Úroveň " + std::to_wstring(level);
    SetWindowTextW(g_colorButtons[level], text.c_str());
    if (g_colorSwatches[level]) InvalidateRect(g_colorSwatches[level], nullptr, TRUE);
}

std::vector<DWORD> TcProcessIds() {
    std::vector<DWORD> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"TOTALCMD64.EXE") == 0 ||
                _wcsicmp(entry.szExeFile, L"TOTALCMD.EXE") == 0) {
                result.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

bool IsTcRunning() {
    return !TcProcessIds().empty();
}

std::wstring FindTcExe() {
    wchar_t env[2048]{};
    DWORD n = GetEnvironmentVariableW(L"COMMANDER_PATH", env, 2048);
    std::wstring dir;
    if (n > 0 && n < 2048) dir = ExpandEnvironment(env);
    if (dir.empty()) dir = QueryRegString(HKEY_CURRENT_USER, L"Software\\Ghisler\\Total Commander", L"InstallDir");
    if (dir.empty()) dir = QueryRegString(HKEY_LOCAL_MACHINE, L"Software\\Ghisler\\Total Commander", L"InstallDir");
    if (dir.empty()) return {};
    std::filesystem::path p64 = std::filesystem::path(dir) / L"TOTALCMD64.EXE";
    if (std::filesystem::exists(p64)) return p64.wstring();
    std::filesystem::path p32 = std::filesystem::path(dir) / L"TOTALCMD.EXE";
    if (std::filesystem::exists(p32)) return p32.wstring();
    return {};
}

BOOL CALLBACK CloseTcWindow(HWND hwnd, LPARAM) {
    wchar_t cls[128]{};
    if (GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) > 0 && wcscmp(cls, L"TTOTAL_CMD") == 0) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

bool StopTc() {
    if (!IsTcRunning()) return true;

    EnumWindows(CloseTcWindow, 0);
    for (int i = 0; i < 50; ++i) {
        Sleep(100);
        if (!IsTcRunning()) return true;
    }

    const auto remaining = TcProcessIds();
    for (DWORD pid : remaining) {
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
    auto exe = FindTcExe();
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

bool IsManagedColorFilter(const std::wstring& value) {
    return value.rfind(L">FolderHeatMap Heat ", 0) == 0;
}

void DeleteSearch(const std::wstring& name) {
    static const wchar_t* suffixes[] = {L"_SearchFor", L"_SearchIn", L"_SearchText", L"_SearchFlags", L"_plugin"};
    for (const auto* suffix : suffixes) {
        WritePrivateProfileStringW(L"searches", (name + suffix).c_str(), nullptr, g_wincmdIni.c_str());
    }
}

void CleanupManagedSearches() {
    for (int i = 1; i <= MAX_MANAGED_SEARCHES; ++i) DeleteSearch(ManagedSearchName(i));
}

struct ExistingColorRule {
    std::wstring filter;
    std::wstring color;
    std::wstring colorDark;
};

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
    existing.reserve(32);
    for (int i = 1; i <= MAX_TC_COLOR_FILTERS; ++i) {
        const std::wstring base = L"ColorFilter" + std::to_wstring(i);
        const std::wstring filter = ReadIniString(L"Colors", base);
        if (filter.empty()) continue;
        if (IsManagedColorFilter(filter)) continue;
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
        WriteColorRuleIndex(tcRuleIndex++, {L">" + searchName,
            std::to_wstring(static_cast<unsigned long>(static_cast<COLORREF>(g_settings.colors[7]))), L""});
    }

    for (int level = 6; level >= 1; --level) {
        const COLORREF from = static_cast<COLORREF>(g_settings.colors[level]);
        const COLORREF to = static_cast<COLORREF>(g_settings.colors[level + 1]);
        for (int s = steps - 1; s >= 0; --s) {
            const double position = static_cast<double>(s) / steps;
            const double threshold = level + position - EPSILON;
            const COLORREF color = Interpolate(from, to, position);
            const std::wstring searchName = ManagedSearchName(++managedCount);
            CreateManagedSearch(searchName, threshold);
            WriteColorRuleIndex(tcRuleIndex++, {L">" + searchName,
                std::to_wstring(static_cast<unsigned long>(color)), L""});
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
    if (g_wincmdIni.empty() || !std::filesystem::exists(g_wincmdIni)) return false;

    const int expected = WriteManagedColorRules();
    if (expected <= 0) return false;

    // Read back from disk. This catches a wrong INI path immediately instead of
    // reporting success while Total Commander still sees no generated rules.
    const std::wstring firstFilter = ReadIniString(L"Colors", L"ColorFilter1");
    const std::wstring firstSearch = ManagedSearchName(1);
    const std::wstring firstPlugin = ReadIniString(L"searches", firstSearch + L"_plugin");
    const int storedCount = GetPrivateProfileIntW(L"FolderHeatMap", L"ManagedColorRuleCount", 0, g_wincmdIni.c_str());

    return firstFilter == (L">" + firstSearch) &&
           firstPlugin.rfind(L"folderheatmap.Heat > ", 0) == 0 &&
           storedCount == expected;
}

void Apply(HWND hwnd) {
    wchar_t buf[64]{};
    g_settings.coolingAuto = SendDlgItemMessageW(hwnd, IDC_AUTO, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(g_halfEdit, buf, 64);
    g_settings.coolingHalfLifeDays = std::clamp(_wtoi(buf), 1, 3650);
    g_settings.includePathHeat = SendDlgItemMessageW(hwnd, IDC_PATH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(g_decayEdit, buf, 64);
    g_settings.pathDecay = std::clamp(_wtoi(buf) / 100.0, 0.0, 1.0);
    g_settings.smoothColors = SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(g_stepsEdit, buf, 64);
    g_settings.stepsPerLevel = std::clamp(_wtoi(buf), 1, 16);

    if (!fhm::SaveSettings(g_settingsIni, g_settings)) {
        MessageBoxW(hwnd, L"Nastavení se nepodařilo uložit.", L"FolderHeatMap", MB_ICONERROR);
        return;
    }

    const bool wasRunning = IsTcRunning();
    if (wasRunning && !StopTc()) {
        MessageBoxW(hwnd, L"Total Commander se nepodařilo ukončit ani po vynuceném pokusu. Nastavení je uložené, ale barvy nebyly změněny.", L"FolderHeatMap", MB_ICONWARNING);
        return;
    }

    if (!ApplyTcColorRules()) {
        if (wasRunning) StartTc();
        std::wstring msg = L"Nepodařilo se zapsat nebo ověřit barevná pravidla Total Commanderu.\n\nPoužitý wincmd.ini:\n" + g_wincmdIni;
        MessageBoxW(hwnd, msg.c_str(), L"FolderHeatMap", MB_ICONWARNING);
        return;
    }

    if (wasRunning) StartTc();
    std::wstring status = L"Uloženo. Barevná mapa byla aktualizována v: " + g_wincmdIni;
    SetWindowTextW(g_status, status.c_str());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto add = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h, int id = 0) {
                HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
                if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return c;
            };

            add(L"STATIC", L"Chladnutí", SS_LEFT, 20, 15, 200, 22);
            add(L"BUTTON", L"Automaticky podle rytmu používání", BS_AUTORADIOBUTTON | WS_GROUP, 30, 42, 280, 22, IDC_AUTO);
            add(L"BUTTON", L"Ručně", BS_AUTORADIOBUTTON, 30, 68, 100, 22, IDC_MANUAL);
            add(L"STATIC", L"Poločas (dnů):", SS_LEFT, 145, 70, 110, 20);
            g_halfEdit = add(L"EDIT", L"30", WS_BORDER | ES_NUMBER, 260, 67, 60, 24, IDC_HALF);

            add(L"STATIC", L"Teplota cesty", SS_LEFT, 20, 105, 200, 22);
            add(L"BUTTON", L"Zohlednit horké podadresáře", BS_AUTOCHECKBOX, 30, 132, 260, 22, IDC_PATH);
            add(L"STATIC", L"Příspěvek na jednu úroveň (%):", SS_LEFT, 30, 161, 220, 20);
            g_decayEdit = add(L"EDIT", L"50", WS_BORDER | ES_NUMBER, 260, 158, 60, 24, IDC_DECAY);

            add(L"STATIC", L"Barevná mapa", SS_LEFT, 20, 198, 200, 22);
            add(L"BUTTON", L"Plynulé přechody mezi úrovněmi", BS_AUTOCHECKBOX, 30, 225, 270, 22, IDC_SMOOTH);
            add(L"STATIC", L"Mezikroků na úroveň:", SS_LEFT, 320, 227, 150, 20);
            g_stepsEdit = add(L"EDIT", L"4", WS_BORDER | ES_NUMBER, 475, 224, 50, 24, IDC_STEPS);
            for (int i = 1; i <= 7; ++i) {
                const int y = 258 + (i - 1) * 34;
                g_colorButtons[i] = add(L"BUTTON", L"", BS_PUSHBUTTON, 30, y, 180, 28, IDC_COLOR_BASE + i);
                g_colorSwatches[i] = add(L"STATIC", L"", SS_OWNERDRAW, 218, y + 2, 24, 24, IDC_SWATCH_BASE + i);
            }
            add(L"STATIC", L"Úroveň 0 = bez zásahu; používá se původní barva Total Commanderu.", SS_LEFT, 275, 260, 360, 42);
            add(L"STATIC", L"Barvy 1–7 jsou záchytné body. Mezilehlé odstíny vznikají jen mezi nimi.", SS_LEFT, 275, 305, 360, 42);
            add(L"BUTTON", L"Použít a aktualizovat TC", BS_DEFPUSHBUTTON, 390, 500, 210, 34, IDC_APPLY);
            g_status = add(L"STATIC", L"", SS_LEFT, 20, 510, 350, 40, IDC_STATUS);

            SendDlgItemMessageW(hwnd, g_settings.coolingAuto ? IDC_AUTO : IDC_MANUAL, BM_SETCHECK, BST_CHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_PATH, BM_SETCHECK, g_settings.includePathHeat ? BST_CHECKED : BST_UNCHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_SETCHECK, g_settings.smoothColors ? BST_CHECKED : BST_UNCHECKED, 0);
            SetWindowTextW(g_halfEdit, std::to_wstring(static_cast<int>(g_settings.coolingHalfLifeDays)).c_str());
            SetWindowTextW(g_decayEdit, std::to_wstring(static_cast<int>(g_settings.pathDecay * 100 + 0.5)).c_str());
            SetWindowTextW(g_stepsEdit, std::to_wstring(g_settings.stepsPerLevel).c_str());
            for (int i = 1; i <= 7; ++i) UpdateColorButton(i);
            return 0;
        }

        case WM_DRAWITEM: {
            const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (dis && dis->CtlID >= IDC_SWATCH_BASE + 1 && dis->CtlID <= IDC_SWATCH_BASE + 7) {
                const int level = static_cast<int>(dis->CtlID) - IDC_SWATCH_BASE;
                RECT r = dis->rcItem;
                HBRUSH brush = CreateSolidBrush(static_cast<COLORREF>(g_settings.colors[level]));
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
                const int level = id - IDC_COLOR_BASE;
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
                return 0;
            }
            if (id == IDC_APPLY) {
                Apply(hwnd);
                return 0;
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    g_instance = instance;
    g_wincmdIni = FindWincmdIni();
    if (g_wincmdIni.empty()) {
        MessageBoxW(nullptr, L"Nepodařilo se najít wincmd.ini Total Commanderu.", L"FolderHeatMap", MB_ICONERROR);
        return 1;
    }

    g_settingsIni = fhm::SettingsPathFromDefaultIni(g_wincmdIni);
    fhm::LoadSettings(g_settingsIni, g_settings);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"FolderHeatMapConfigWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    ATOM atom = RegisterClassW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        DWORD err = GetLastError();
        std::wstring msg = L"Nepodařilo se zaregistrovat okno nastavení. Windows chyba: " + std::to_wstring(err);
        MessageBoxW(nullptr, msg.c_str(), L"FolderHeatMap", MB_ICONERROR);
        return 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"FolderHeatMap – Nastavení",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 590, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        DWORD err = GetLastError();
        std::wstring msg = L"Nepodařilo se vytvořit okno nastavení. Windows chyba: " + std::to_wstring(err);
        MessageBoxW(nullptr, msg.c_str(), L"FolderHeatMap", MB_ICONERROR);
        return 3;
    }

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