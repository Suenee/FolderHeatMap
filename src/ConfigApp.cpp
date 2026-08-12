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
HINSTANCE g_instance{};

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
    return FindWindowW(L"TTOTAL_CMD", nullptr) != nullptr;
}

std::wstring FindTcExe() {
    wchar_t env[2048]{};
    DWORD n = GetEnvironmentVariableW(L"COMMANDER_PATH", env, 2048);
    std::wstring dir;
    if (n > 0 && n < 2048) dir = env;
    if (dir.empty()) dir = QueryRegString(HKEY_CURRENT_USER, L"Software\\Ghisler\\Total Commander", L"InstallDir");
    if (dir.empty()) dir = QueryRegString(HKEY_LOCAL_MACHINE, L"Software\\Ghisler\\Total Commander", L"InstallDir");
    if (dir.empty()) return {};
    std::filesystem::path p64 = std::filesystem::path(dir) / L"TOTALCMD64.EXE";
    if (std::filesystem::exists(p64)) return p64.wstring();
    std::filesystem::path p32 = std::filesystem::path(dir) / L"TOTALCMD.EXE";
    if (std::filesystem::exists(p32)) return p32.wstring();
    return {};
}

bool StopTc() {
    HWND hwnd = FindWindowW(L"TTOTAL_CMD", nullptr);
    if (!hwnd) return true;
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    for (int i = 0; i < 50; ++i) {
        Sleep(100);
        if (!FindWindowW(L"TTOTAL_CMD", nullptr)) return true;
    }
    return false;
}

void StartTc() {
    auto exe = FindTcExe();
    if (!exe.empty()) ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring ReadIniSection(const wchar_t* section) {
    std::vector<wchar_t> buffer(65536);
    DWORD n = GetPrivateProfileSectionW(section, buffer.data(), static_cast<DWORD>(buffer.size()), g_wincmdIni.c_str());
    if (!n) return {};
    std::wstring out;
    for (const wchar_t* p = buffer.data(); *p; p += wcslen(p) + 1) {
        out += p;
        out += L'\n';
    }
    return out;
}

void WriteManagedColorRules() {
    const int steps = std::clamp(g_settings.stepsPerLevel, 1, 16);
    int rule = 0;
    for (int level = 1; level <= 7; ++level) {
        const COLORREF from = static_cast<COLORREF>(g_settings.colors[level]);
        const COLORREF to = static_cast<COLORREF>(g_settings.colors[std::min(level + 1, 7)]);
        const int count = (g_settings.smoothColors && level < 7) ? steps : 1;
        for (int s = 0; s < count; ++s) {
            double t = count > 1 ? static_cast<double>(s) / count : 0.0;
            COLORREF c = Interpolate(from, to, t);
            std::wstring name = L"FHM_" + std::to_wstring(rule);
            std::wstring filter = L"[=folderheatmap.Heat Level]=" + std::to_wstring(level);
            WritePrivateProfileStringW(L"Colors", name.c_str(), filter.c_str(), g_wincmdIni.c_str());
            WritePrivateProfileStringW(L"Colors", (name + L"Color").c_str(), std::to_wstring(static_cast<unsigned long>(c)).c_str(), g_wincmdIni.c_str());
            ++rule;
        }
    }
    WritePrivateProfileStringW(L"FolderHeatMap", L"ManagedColorRuleCount", std::to_wstring(rule).c_str(), g_wincmdIni.c_str());
}

bool ApplyTcColorRules() {
    if (g_wincmdIni.empty()) return false;
    int oldCount = GetPrivateProfileIntW(L"FolderHeatMap", L"ManagedColorRuleCount", 0, g_wincmdIni.c_str());
    for (int i = 0; i < oldCount; ++i) {
        std::wstring name = L"FHM_" + std::to_wstring(i);
        WritePrivateProfileStringW(L"Colors", name.c_str(), nullptr, g_wincmdIni.c_str());
        WritePrivateProfileStringW(L"Colors", (name + L"Color").c_str(), nullptr, g_wincmdIni.c_str());
    }
    WriteManagedColorRules();
    return true;
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
        MessageBoxW(hwnd, L"Total Commander se nepodařilo ukončit. Nastavení je uložené, ale barvy nebyly změněny.", L"FolderHeatMap", MB_ICONWARNING);
        return;
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
