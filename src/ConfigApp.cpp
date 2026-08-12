#include "Settings.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
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

fhm::Settings g_settings;
std::wstring g_wincmdIni;
std::wstring g_settingsIni;
std::array<HWND, 8> g_colorButtons{};
HWND g_halfEdit{};
HWND g_decayEdit{};
HWND g_stepsEdit{};
HWND g_status{};

std::wstring QueryRegString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    wchar_t buffer[2048]{};
    DWORD type = 0;
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, &type, buffer, &size) == ERROR_SUCCESS) return buffer;
    return {};
}

std::wstring FindWincmdIni() {
    wchar_t env[2048]{};
    DWORD n = GetEnvironmentVariableW(L"COMMANDER_INI", env, 2048);
    if (n > 0 && n < 2048 && std::filesystem::exists(env)) return env;
    auto p = QueryRegString(HKEY_CURRENT_USER, L"Software\\Ghisler\\Total Commander", L"IniFileName");
    if (!p.empty()) return p;
    return QueryRegString(HKEY_LOCAL_MACHINE, L"Software\\Ghisler\\Total Commander", L"IniFileName");
}

std::wstring HexColor(COLORREF c) {
    wchar_t s[32]{};
    swprintf_s(s, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return s;
}

COLORREF Interpolate(COLORREF a, COLORREF b, double t) {
    auto mix = [t](int x, int y) {
        return std::clamp(static_cast<int>(x + (y - x) * t + 0.5), 0, 255);
    };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

void UpdateColorButton(int level) {
    std::wstring text = L"Úroveň " + std::to_wstring(level) + L"   " + HexColor(static_cast<COLORREF>(g_settings.colors[level]));
    SetWindowTextW(g_colorButtons[level], text.c_str());
}

bool IsTcRunning() {
    return system("tasklist /FI \"IMAGENAME eq TOTALCMD64.EXE\" 2>nul | find /I \"TOTALCMD64.EXE\" >nul") == 0 ||
           system("tasklist /FI \"IMAGENAME eq TOTALCMD.EXE\" 2>nul | find /I \"TOTALCMD.EXE\" >nul") == 0;
}

std::wstring FindTcExe() {
    wchar_t env[2048]{};
    DWORD n = GetEnvironmentVariableW(L"COMMANDER_PATH", env, 2048);
    std::wstring dir = (n > 0 && n < 2048) ? env : QueryRegString(HKEY_CURRENT_USER, L"Software\\Ghisler\\Total Commander", L"InstallDir");
    if (dir.empty()) dir = QueryRegString(HKEY_LOCAL_MACHINE, L"Software\\Ghisler\\Total Commander", L"InstallDir");
    if (dir.empty()) return {};
    auto p64 = std::filesystem::path(dir) / L"TOTALCMD64.EXE";
    if (std::filesystem::exists(p64)) return p64.wstring();
    auto p32 = std::filesystem::path(dir) / L"TOTALCMD.EXE";
    return std::filesystem::exists(p32) ? p32.wstring() : L"";
}

void StopTc() {
    system("taskkill /IM TOTALCMD64.EXE /T >nul 2>nul");
    system("taskkill /IM TOTALCMD.EXE /T >nul 2>nul");
    for (int i = 0; i < 30 && IsTcRunning(); ++i) Sleep(100);
    if (IsTcRunning()) {
        system("taskkill /F /IM TOTALCMD64.EXE /T >nul 2>nul");
        system("taskkill /F /IM TOTALCMD.EXE /T >nul 2>nul");
        Sleep(300);
    }
}

void StartTc() {
    const auto exe = FindTcExe();
    if (!exe.empty()) ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void RemoveOldHeatSearches() {
    for (int i = 1; i <= 112; ++i) {
        wchar_t base[64]{};
        swprintf_s(base, L"FolderHeatMap Heat %03d", i);
        const wchar_t* suffixes[] = {L"_SearchFor", L"_SearchIn", L"_SearchText", L"_SearchFlags", L"_plugin"};
        for (const auto* suffix : suffixes) {
            std::wstring key = std::wstring(base) + suffix;
            WritePrivateProfileStringW(L"Searches", key.c_str(), nullptr, g_wincmdIni.c_str());
        }
    }
}

struct ExistingFilter {
    std::wstring value;
    std::wstring color;
};

std::vector<ExistingFilter> ReadNonHeatFilters() {
    std::vector<ExistingFilter> filters;
    for (int i = 1; i <= 256; ++i) {
        wchar_t key[64]{};
        swprintf_s(key, L"ColorFilter%d", i);
        wchar_t value[2048]{};
        GetPrivateProfileStringW(L"Colors", key, L"", value, 2048, g_wincmdIni.c_str());
        if (!*value) continue;
        if (wcsncmp(value, L">FolderHeatMap Heat ", 19) == 0) continue;
        wchar_t ckey[64]{};
        swprintf_s(ckey, L"ColorFilter%dColor", i);
        wchar_t color[64]{};
        GetPrivateProfileStringW(L"Colors", ckey, L"-1", color, 64, g_wincmdIni.c_str());
        filters.push_back({value, color});
    }
    return filters;
}

void ClearColorFilters() {
    for (int i = 1; i <= 256; ++i) {
        wchar_t key[64]{};
        swprintf_s(key, L"ColorFilter%d", i);
        WritePrivateProfileStringW(L"Colors", key, nullptr, g_wincmdIni.c_str());
        swprintf_s(key, L"ColorFilter%dColor", i);
        WritePrivateProfileStringW(L"Colors", key, nullptr, g_wincmdIni.c_str());
    }
}

bool ApplyTcColorRules() {
    if (g_wincmdIni.empty()) return false;

    // Read existing TC rules before deleting/rebuilding ours. This preserves
    // user's file-type colors while keeping ColorFilter indexes continuous.
    const auto existing = ReadNonHeatFilters();
    RemoveOldHeatSearches();
    ClearColorFilters();

    int index = 1;
    for (const auto& f : existing) {
        wchar_t key[64]{};
        swprintf_s(key, L"ColorFilter%d", index);
        WritePrivateProfileStringW(L"Colors", key, f.value.c_str(), g_wincmdIni.c_str());
        swprintf_s(key, L"ColorFilter%dColor", index);
        WritePrivateProfileStringW(L"Colors", key, f.color.c_str(), g_wincmdIni.c_str());
        ++index;
    }

    const int steps = g_settings.smoothColors ? std::clamp(g_settings.stepsPerLevel, 1, 16) : 1;
    const int maxStep = 7 * steps;
    for (int step = 1; step <= maxStep; ++step) {
        wchar_t name[64]{};
        swprintf_s(name, L"FolderHeatMap Heat %03d", step);
        const double heat = static_cast<double>(step) / steps;
        const int upper = std::clamp(static_cast<int>(std::ceil(heat)), 1, 7);
        const int lower = std::max(1, upper - 1);
        const double t = upper == lower ? 0.0 : std::clamp(heat - lower, 0.0, 1.0);
        const COLORREF color = upper == 1
            ? static_cast<COLORREF>(g_settings.colors[1])
            : Interpolate(static_cast<COLORREF>(g_settings.colors[lower]), static_cast<COLORREF>(g_settings.colors[upper]), t);

        std::wstring prefix = name;
        WritePrivateProfileStringW(L"Searches", (prefix + L"_SearchFor").c_str(), L"", g_wincmdIni.c_str());
        WritePrivateProfileStringW(L"Searches", (prefix + L"_SearchIn").c_str(), L"", g_wincmdIni.c_str());
        WritePrivateProfileStringW(L"Searches", (prefix + L"_SearchText").c_str(), L"", g_wincmdIni.c_str());
        WritePrivateProfileStringW(L"Searches", (prefix + L"_SearchFlags").c_str(), L"0|002002010021|||||||||0000|||", g_wincmdIni.c_str());
        const std::wstring plugin = L"folderheatmap.Heat Color Step = " + std::to_wstring(step);
        WritePrivateProfileStringW(L"Searches", (prefix + L"_plugin").c_str(), plugin.c_str(), g_wincmdIni.c_str());

        wchar_t key[64]{};
        swprintf_s(key, L"ColorFilter%d", index);
        const std::wstring filter = L">" + prefix;
        WritePrivateProfileStringW(L"Colors", key, filter.c_str(), g_wincmdIni.c_str());
        swprintf_s(key, L"ColorFilter%dColor", index);
        wchar_t colorText[64]{};
        swprintf_s(colorText, L"%lu", static_cast<unsigned long>(color));
        WritePrivateProfileStringW(L"Colors", key, colorText, g_wincmdIni.c_str());
        ++index;
    }

    WritePrivateProfileStringW(L"Configuration", L"ColorFilters", L"1", g_wincmdIni.c_str());
    return true;
}

int ReadEditInt(HWND h, int fallback, int minV, int maxV) {
    wchar_t b[64]{};
    GetWindowTextW(h, b, 64);
    wchar_t* e = nullptr;
    const long v = wcstol(b, &e, 10);
    return e == b ? fallback : std::clamp(static_cast<int>(v), minV, maxV);
}

void Apply(HWND hwnd) {
    g_settings.coolingAuto = SendDlgItemMessageW(hwnd, IDC_AUTO, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.coolingHalfLifeDays = ReadEditInt(g_halfEdit, 30, 1, 3650);
    g_settings.includePathHeat = SendDlgItemMessageW(hwnd, IDC_PATH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.pathDecay = ReadEditInt(g_decayEdit, 50, 0, 100) / 100.0;
    g_settings.smoothColors = SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.stepsPerLevel = ReadEditInt(g_stepsEdit, 4, 1, 16);

    if (!fhm::SaveSettings(g_settingsIni, g_settings)) {
        MessageBoxW(hwnd, L"Nepodařilo se uložit FolderHeatMap.ini.", L"FolderHeatMap", MB_ICONERROR);
        return;
    }

    // TC can rewrite wincmd.ini while shutting down. Therefore stop it first,
    // then modify its configuration, and only afterwards start it again.
    const bool wasRunning = IsTcRunning();
    if (wasRunning) {
        SetWindowTextW(g_status, L"Zastavuji Total Commander a aktualizuji barevná pravidla…");
        StopTc();
    }

    if (!ApplyTcColorRules()) {
        if (wasRunning) StartTc();
        MessageBoxW(hwnd, L"Nastavení bylo uloženo, ale nepodařilo se upravit barvy Total Commanderu.", L"FolderHeatMap", MB_ICONWARNING);
        return;
    }

    if (wasRunning) StartTc();
    SetWindowTextW(g_status, L"Uloženo. Barevná mapa byla aktualizována.");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto add = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h, int id = 0) {
                HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
                g_colorButtons[i] = add(L"BUTTON", L"", BS_PUSHBUTTON, 30, 258 + (i - 1) * 34, 220, 28, IDC_COLOR_BASE + i);
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
    return DefWindowProcW(hwnd, msg, wp, 0);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
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
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"FolderHeatMap – Nastavení",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 590, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 2;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
