// Build wrapper and tuned UI for the settings application.
// Windows defines IDC_HELP as a predefined cursor resource macro. Include the
// Windows headers first, remove that macro, then include the shared backend.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>

#ifdef IDC_HELP
#undef IDC_HELP
#endif

// Keep the original implementation available as a backend, but provide the
// actual entry point below with the compact configurator UI.
#define wWinMain FolderHeatMapLegacyWinMain
#include "ConfigApp.cpp"
#undef wWinMain

namespace {
constexpr int IDC_DECAY_EDIT_TUNED = 1301;
constexpr int IDC_DECAY_SPIN_TUNED = 1302;

HWND g_decayEditTuned{};
HWND g_saveButtonTuned{};
bool g_uiReadyTuned = false;
fhm::Settings g_savedSettingsTuned{};
COLORREF g_savedLevel0Tuned = RGB(0, 0, 0);

int PeekEditInt(HWND edit, int minValue, int maxValue) {
    wchar_t buf[32]{};
    GetWindowTextW(edit, buf, static_cast<int>(std::size(buf)));
    return std::clamp(_wtoi(buf), minValue, maxValue);
}

fhm::Settings ReadUiSettingsTuned(HWND hwnd) {
    fhm::Settings s = g_settings;
    s.coolingAuto = SendDlgItemMessageW(hwnd, IDC_AUTO_FHM, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.coolingHalfLifeDays = PeekEditInt(g_halfEdit, 1, 365);
    s.includePathHeat = SendDlgItemMessageW(hwnd, IDC_PATH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.pathDecay = static_cast<double>(PeekEditInt(g_decayEditTuned, 0, 100)) / 100.0;
    s.repeatVisitCooldownSeconds = PeekEditInt(g_cooldownEdit, 0, 600);
    s.sessionResetHours = PeekEditInt(g_sessionEdit, 1, 24);
    s.smoothColors = SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.stepsPerLevel = PeekEditInt(g_stepsEdit, 1, 16);
    return s;
}

bool SameSettingsTuned(const fhm::Settings& a, const fhm::Settings& b) {
    if (a.coolingAuto != b.coolingAuto ||
        static_cast<int>(a.coolingHalfLifeDays) != static_cast<int>(b.coolingHalfLifeDays) ||
        a.includePathHeat != b.includePathHeat ||
        std::lround(a.pathDecay * 100.0) != std::lround(b.pathDecay * 100.0) ||
        a.repeatVisitCooldownSeconds != b.repeatVisitCooldownSeconds ||
        a.sessionResetHours != b.sessionResetHours ||
        a.smoothColors != b.smoothColors ||
        a.stepsPerLevel != b.stepsPerLevel)
        return false;
    for (int i = 1; i <= 7; ++i) if (a.colors[i] != b.colors[i]) return false;
    return true;
}

void UpdateSaveStateTuned(HWND hwnd) {
    if (!g_uiReadyTuned || !g_saveButtonTuned) return;
    const bool dirty = !SameSettingsTuned(ReadUiSettingsTuned(hwnd), g_savedSettingsTuned) || g_level0Color != g_savedLevel0Tuned;
    EnableWindow(g_saveButtonTuned, dirty ? TRUE : FALSE);
}

void UpdateEnabledStateTuned(HWND hwnd) {
    const bool manual = SendDlgItemMessageW(hwnd, IDC_MANUAL_FHM, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_halfEdit, manual);
    EnableWindow(GetDlgItem(hwnd, IDC_HALF_SPIN), manual);

    const bool path = SendDlgItemMessageW(hwnd, IDC_PATH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_decayEditTuned, path);
    EnableWindow(GetDlgItem(hwnd, IDC_DECAY_SPIN_TUNED), path);

    const bool smooth = SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_stepsEdit, smooth);
    EnableWindow(GetDlgItem(hwnd, IDC_STEPS_SPIN), smooth);
}

void SaveTuned(HWND hwnd) {
    if (!CanUseIni(g_wincmdIni)) {
        MessageBoxW(hwnd, L"Total Commander's wincmd.ini is no longer accessible. Reopen FolderHeatMap Settings and select a writable INI file.",
            L"FolderHeatMap", MB_ICONERROR);
        return;
    }

    g_settings = ReadUiSettingsTuned(hwnd);
    g_settings.coolingHalfLifeDays = EditInt(g_halfEdit, 1, 365);
    g_settings.pathDecay = static_cast<double>(EditInt(g_decayEditTuned, 0, 100)) / 100.0;
    g_settings.repeatVisitCooldownSeconds = EditInt(g_cooldownEdit, 0, 600);
    g_settings.sessionResetHours = EditInt(g_sessionEdit, 1, 24);
    g_settings.stepsPerLevel = EditInt(g_stepsEdit, 1, 16);

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

    g_savedSettingsTuned = g_settings;
    g_savedLevel0Tuned = g_level0Color;
    UpdateSaveStateTuned(hwnd);
}

LRESULT CALLBACK TunedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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
                HWND c = add(L"STATIC", txt, SS_LEFT, x, y, w, 18);
                if (c && g_boldFont) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_boldFont), TRUE);
                return c;
            };

            g_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, g_instance, nullptr);
            SendMessageW(g_tooltip, TTM_SETMAXTIPWIDTH, 0, 360);

            constexpr int LEFT = 16;
            constexpr int RIGHT = 255;
            constexpr int EDIT_X_LEFT = 88;
            constexpr int EDIT_X_RIGHT = 370;
            constexpr int EDIT_W = 58;
            constexpr int EDIT_H = 20;

            header(L"Cooling", LEFT, 12, 220);
            HWND autoBtn = add(L"BUTTON", L"Automatic", BS_AUTORADIOBUTTON | WS_GROUP, LEFT + 8, 34, 90, 20, IDC_AUTO_FHM);
            HWND manualBtn = add(L"BUTTON", L"Manual", BS_AUTORADIOBUTTON, LEFT + 104, 34, 72, 20, IDC_MANUAL_FHM);
            add(L"STATIC", L"Half-life:", SS_LEFT, LEFT + 8, 64, 64, 18);
            g_halfEdit = add(L"EDIT", L"30", WS_BORDER | ES_NUMBER | ES_CENTER, EDIT_X_LEFT, 61, EDIT_W, EDIT_H, IDC_HALF);
            AddSpin(hwnd, g_halfEdit, IDC_HALF_SPIN, 1, 365);
            add(L"STATIC", L"days", SS_LEFT, EDIT_X_LEFT + 64, 64, 34, 18);

            header(L"Activity tuning", RIGHT, 12, 220);
            add(L"STATIC", L"Repeat cooldown:", SS_LEFT, RIGHT + 8, 37, 102, 18);
            g_cooldownEdit = add(L"EDIT", L"90", WS_BORDER | ES_NUMBER | ES_CENTER, EDIT_X_RIGHT, 34, EDIT_W, EDIT_H, IDC_COOLDOWN);
            AddSpin(hwnd, g_cooldownEdit, IDC_COOLDOWN_SPIN, 0, 600);
            add(L"STATIC", L"sec", SS_LEFT, EDIT_X_RIGHT + 64, 37, 28, 18);
            add(L"STATIC", L"Session reset:", SS_LEFT, RIGHT + 8, 67, 102, 18);
            g_sessionEdit = add(L"EDIT", L"8", WS_BORDER | ES_NUMBER | ES_CENTER, EDIT_X_RIGHT, 64, EDIT_W, EDIT_H, IDC_SESSION);
            AddSpin(hwnd, g_sessionEdit, IDC_SESSION_SPIN, 1, 24);
            add(L"STATIC", L"hours", SS_LEFT, EDIT_X_RIGHT + 64, 67, 38, 18);

            header(L"Path heat", LEFT, 101, 220);
            HWND pathBtn = add(L"BUTTON", L"Include hot descendants", BS_AUTOCHECKBOX, LEFT + 8, 124, 172, 20, IDC_PATH);
            add(L"STATIC", L"Contribution:", SS_LEFT, LEFT + 8, 153, 72, 18);
            g_decayEditTuned = add(L"EDIT", L"50", WS_BORDER | ES_NUMBER | ES_CENTER, EDIT_X_LEFT, 150, EDIT_W, EDIT_H, IDC_DECAY_EDIT_TUNED);
            AddSpin(hwnd, g_decayEditTuned, IDC_DECAY_SPIN_TUNED, 0, 100);
            add(L"STATIC", L"%", SS_LEFT, EDIT_X_LEFT + 64, 153, 20, 18);

            header(L"Color behavior", RIGHT, 101, 220);
            HWND smoothBtn = add(L"BUTTON", L"Smooth transitions", BS_AUTOCHECKBOX, RIGHT + 8, 124, 145, 20, IDC_SMOOTH);
            add(L"STATIC", L"Steps per level:", SS_LEFT, RIGHT + 8, 153, 102, 18);
            g_stepsEdit = add(L"EDIT", L"4", WS_BORDER | ES_NUMBER | ES_CENTER, EDIT_X_RIGHT, 150, EDIT_W, EDIT_H, IDC_STEPS);
            AddSpin(hwnd, g_stepsEdit, IDC_STEPS_SPIN, 1, 16);

            header(L"Color heat map", LEFT, 194, 92);
            constexpr int SWATCH_X = 112;
            constexpr int SWATCH_Y = 189;
            constexpr int SWATCH_W = 45;
            constexpr int SWATCH_H = 27;
            constexpr int SWATCH_GAP = 2;
            for (int i = 0; i <= 7; ++i) {
                const int x = SWATCH_X + i * (SWATCH_W + SWATCH_GAP);
                g_colorSwatches[i] = add(L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY, x, SWATCH_Y, SWATCH_W, SWATCH_H, IDC_SWATCH_BASE + i);
                g_swatchTooltips[i] = i == 0
                    ? L"Heat level 0 color - Total Commander base text color."
                    : L"Heat level " + std::to_wstring(i) + L" color.";
                AddTooltip(g_colorSwatches[i], g_swatchTooltips[i].c_str());
            }

            HWND helpBtn = add(L"BUTTON", L"Help", BS_PUSHBUTTON, LEFT, 238, 72, 28, IDC_HELP_BUTTON);
            g_saveButtonTuned = add(L"BUTTON", L"Save", BS_DEFPUSHBUTTON, 350, 238, 72, 28, IDC_SAVE);
            add(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 430, 238, 72, 28, IDC_CANCEL);

            AddTooltip(autoBtn, L"Learns cooling speed from your Total Commander usage rhythm.");
            AddTooltip(manualBtn, L"Use a fixed cooling half-life instead of automatic learning.");
            AddTooltip(g_halfEdit, L"Manual cooling half-life. Range: 1-365 days.");
            AddTooltip(pathBtn, L"Let the hottest descendant contribute heat to its parent path.");
            AddTooltip(g_decayEditTuned, L"Descendant heat retained per directory level. Range: 0-100%.");
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
            SetWindowTextW(g_decayEditTuned, std::to_wstring(static_cast<int>(std::lround(g_settings.pathDecay * 100.0))).c_str());
            SetWindowTextW(g_stepsEdit, std::to_wstring(g_settings.stepsPerLevel).c_str());

            g_savedSettingsTuned = g_settings;
            g_savedLevel0Tuned = g_level0Color;
            UpdateEnabledStateTuned(hwnd);
            g_uiReadyTuned = true;
            UpdateSaveStateTuned(hwnd);
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
            const int notify = HIWORD(wp);

            if (id >= IDC_SWATCH_BASE && id <= IDC_SWATCH_BASE + 7 && notify == STN_CLICKED) {
                ChooseColor(hwnd, id - IDC_SWATCH_BASE);
                UpdateSaveStateTuned(hwnd);
                return 0;
            }

            if (id == IDC_AUTO_FHM || id == IDC_MANUAL_FHM || id == IDC_PATH || id == IDC_SMOOTH) {
                UpdateEnabledStateTuned(hwnd);
                UpdateSaveStateTuned(hwnd);
                return 0;
            }

            if ((id == IDC_HALF || id == IDC_COOLDOWN || id == IDC_SESSION || id == IDC_STEPS || id == IDC_DECAY_EDIT_TUNED) && notify == EN_CHANGE) {
                UpdateSaveStateTuned(hwnd);
                return 0;
            }

            if (id == IDC_HELP_BUTTON) { ShowHelp(hwnd); return 0; }
            if (id == IDC_SAVE) { SaveTuned(hwnd); return 0; }
            if (id == IDC_CANCEL) { DestroyWindow(hwnd); return 0; }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            g_uiReadyTuned = false;
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

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_UPDOWN_CLASS | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    g_wincmdIni = FindWincmdIni();
    if (g_wincmdIni.empty()) g_wincmdIni = PromptForWincmdIni();
    if (g_wincmdIni.empty()) { CloseHandle(g_singleInstanceMutex); return 1; }

    g_settingsIni = fhm::SettingsPathFromDefaultIni(g_wincmdIni);
    fhm::LoadSettings(g_settingsIni, g_settings);
    g_level0Color = TcBaseColor();
    g_windowBrush = GetSysColorBrush(COLOR_WINDOW);

    HICON appIcon = static_cast<HICON>(LoadImageW(instance, L"IDI_FHM_CONFIG", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    HICON smallIcon = static_cast<HICON>(LoadImageW(instance, L"IDI_FHM_CONFIG", IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TunedWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = WINDOW_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_windowBrush;
    wc.hIcon = appIcon;
    wc.hIconSm = smallIcon ? smallIcon : appIcon;
    const ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        const DWORD err = GetLastError();
        MessageBoxW(nullptr, (L"Could not register the settings window. Windows error: " + std::to_wstring(err)).c_str(), L"FolderHeatMap", MB_ICONERROR);
        CloseHandle(g_singleInstanceMutex);
        return 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS, L"FolderHeatMap - Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 525, 315, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        const DWORD err = GetLastError();
        MessageBoxW(nullptr, (L"Could not create the settings window. Windows error: " + std::to_wstring(err)).c_str(), L"FolderHeatMap", MB_ICONERROR);
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
