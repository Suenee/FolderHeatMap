// Thin Win32 build wrapper for FolderHeatMapConfig.
//
// ConfigApp.cpp owns all configurator logic. This file only provides the
// approved compact window layout and the real entry point. Keeping the logic
// in one place prevents an older UI implementation from drifting out of sync.

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>

#ifdef IDC_HELP
#undef IDC_HELP
#endif

#ifdef _MSC_VER
#pragma warning(push)
// The backend contains one harmless Win32 UINT/int comparison. Keep the
// release build clean until that API boundary is normalized in the backend.
#pragma warning(disable : 4389)
#endif

#define wWinMain FolderHeatMapBackendWinMain
#include "ConfigApp.cpp"
#undef wWinMain

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace {

LRESULT CALLBACK ConfigWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            LOGFONTW lf{};
            GetObjectW(font, sizeof(lf), &lf);
            lf.lfWeight = FW_SEMIBOLD;
            g_boldFont = CreateFontIndirectW(&lf);

            auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                           int x, int y, int w, int h, int id = 0) {
                HWND control = CreateWindowExW(
                    0, cls, text, WS_CHILD | WS_VISIBLE | style,
                    x, y, w, h, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                    g_instance, nullptr);
                if (control)
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return control;
            };

            auto header = [&](const wchar_t* text, int x, int y, int w, DWORD align = SS_LEFT) {
                HWND control = add(L"STATIC", text, align, x, y, w, 20);
                if (control && g_boldFont)
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_boldFont), TRUE);
                return control;
            };

            g_tooltip = CreateWindowExW(
                WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hwnd, nullptr, g_instance, nullptr);
            SendMessageW(g_tooltip, TTM_SETMAXTIPWIDTH, 0, 420);

            // Top settings area: two balanced columns.
            header(L"Cooling", 28, 18, 210);
            HWND autoBtn = add(L"BUTTON", L"Automatic",
                BS_AUTORADIOBUTTON | WS_GROUP, 36, 44, 92, 22, IDC_AUTO_FHM);
            HWND manualBtn = add(L"BUTTON", L"Manual",
                BS_AUTORADIOBUTTON, 132, 44, 78, 22, IDC_MANUAL_FHM);
            add(L"STATIC", L"Half-life:", SS_LEFT, 36, 78, 62, 20);
            g_halfEdit = add(L"EDIT", L"30", WS_BORDER | ES_NUMBER | ES_CENTER,
                100, 74, 58, 22, IDC_HALF);
            AddSpin(hwnd, g_halfEdit, IDC_HALF_SPIN, 1, 365);
            add(L"STATIC", L"days", SS_LEFT, 166, 78, 36, 20);

            header(L"Activity tuning", 292, 18, 220);
            add(L"STATIC", L"Repeat cooldown:", SS_LEFT, 300, 48, 108, 20);
            g_cooldownEdit = add(L"EDIT", L"90", WS_BORDER | ES_NUMBER | ES_CENTER,
                412, 44, 58, 22, IDC_COOLDOWN);
            AddSpin(hwnd, g_cooldownEdit, IDC_COOLDOWN_SPIN, 0, 600);
            add(L"STATIC", L"sec", SS_LEFT, 478, 48, 30, 20);
            add(L"STATIC", L"Session reset:", SS_LEFT, 300, 82, 108, 20);
            g_sessionEdit = add(L"EDIT", L"8", WS_BORDER | ES_NUMBER | ES_CENTER,
                412, 78, 58, 22, IDC_SESSION);
            AddSpin(hwnd, g_sessionEdit, IDC_SESSION_SPIN, 1, 24);
            add(L"STATIC", L"hours", SS_LEFT, 478, 82, 42, 20);

            header(L"Path heat", 28, 126, 210);
            HWND pathBtn = add(L"BUTTON", L"Include hot descendants",
                BS_AUTOCHECKBOX, 36, 152, 182, 22, IDC_PATH);
            add(L"STATIC", L"Contribution:", SS_LEFT, 36, 186, 82, 20);
            g_decayEdit = add(L"EDIT", L"50", WS_BORDER | ES_NUMBER | ES_CENTER,
                120, 182, 58, 22, IDC_DECAY);
            AddSpin(hwnd, g_decayEdit, IDC_DECAY_SPIN, 0, 100);
            add(L"STATIC", L"%", SS_LEFT, 186, 186, 20, 20);

            header(L"Color behavior", 292, 126, 220);
            HWND smoothBtn = add(L"BUTTON", L"Smooth transitions",
                BS_AUTOCHECKBOX, 300, 152, 154, 22, IDC_SMOOTH);
            add(L"STATIC", L"Steps per level:", SS_LEFT, 300, 186, 108, 20);
            g_stepsEdit = add(L"EDIT", L"4", WS_BORDER | ES_NUMBER | ES_CENTER,
                412, 182, 58, 22, IDC_STEPS);
            AddSpin(hwnd, g_stepsEdit, IDC_STEPS_SPIN, 1, 16);

            // Approved heat-map presentation: centered title, then one wide strip.
            header(L"Color heat map", 28, 230, 484, SS_CENTER);
            constexpr int stripX = 28;
            constexpr int stripY = 258;
            constexpr int stripWidth = 484;
            constexpr int gap = 2;
            constexpr int swatchHeight = 36;
            const int swatchWidth = (stripWidth - gap * 7) / 8;
            int x = stripX;
            for (int i = 0; i <= 7; ++i) {
                const int w = (i == 7) ? (stripX + stripWidth - x) : swatchWidth;
                g_colorSwatches[i] = add(
                    L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY,
                    x, stripY, w, swatchHeight, IDC_SWATCH_BASE + i);
                g_swatchTooltips[i] = i == 0
                    ? L"Heat level 0 color - Total Commander base text color (editable)."
                    : L"Heat level " + std::to_wstring(i) + L" color.";
                AddTooltip(g_colorSwatches[i], g_swatchTooltips[i].c_str());
                x += w + gap;
            }

            HWND helpBtn = add(L"BUTTON", L"Help", BS_PUSHBUTTON,
                28, 326, 78, 30, IDC_HELP_BUTTON);
            g_saveButton = add(L"BUTTON", L"Save", BS_DEFPUSHBUTTON,
                356, 326, 78, 30, IDC_SAVE);
            add(L"BUTTON", L"Cancel", BS_PUSHBUTTON,
                442, 326, 78, 30, IDC_CANCEL);

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

            SendDlgItemMessageW(hwnd,
                g_settings.coolingAuto ? IDC_AUTO_FHM : IDC_MANUAL_FHM,
                BM_SETCHECK, BST_CHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_PATH, BM_SETCHECK,
                g_settings.includePathHeat ? BST_CHECKED : BST_UNCHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_SMOOTH, BM_SETCHECK,
                g_settings.smoothColors ? BST_CHECKED : BST_UNCHECKED, 0);

            SetWindowTextW(g_halfEdit,
                std::to_wstring(static_cast<int>(g_settings.coolingHalfLifeDays)).c_str());
            SetWindowTextW(g_cooldownEdit,
                std::to_wstring(g_settings.repeatVisitCooldownSeconds).c_str());
            SetWindowTextW(g_sessionEdit,
                std::to_wstring(g_settings.sessionResetHours).c_str());
            SetWindowTextW(g_decayEdit,
                std::to_wstring(static_cast<int>(std::lround(g_settings.pathDecay * 100.0))).c_str());
            SetWindowTextW(g_stepsEdit,
                std::to_wstring(g_settings.stepsPerLevel).c_str());

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
                const COLORREF color = level == 0
                    ? g_level0Color
                    : static_cast<COLORREF>(g_settings.colors[level]);
                HBRUSH brush = CreateSolidBrush(color);
                FillRect(dis->hDC, &r, brush);
                DeleteObject(brush);
                FrameRect(dis->hDC, &r,
                    static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
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
            if (id == IDC_AUTO_FHM || id == IDC_MANUAL_FHM ||
                id == IDC_PATH || id == IDC_SMOOTH) {
                UpdateEnabledState(hwnd);
                return 0;
            }
            if ((id == IDC_HALF || id == IDC_DECAY || id == IDC_STEPS ||
                 id == IDC_COOLDOWN || id == IDC_SESSION) && code == EN_CHANGE) {
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
            if (g_boldFont) {
                DeleteObject(g_boldFont);
                g_boldFont = nullptr;
            }
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
    if (g_wincmdIni.empty()) {
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return 1;
    }

    g_settingsIni = fhm::SettingsPathFromDefaultIni(g_wincmdIni);
    fhm::LoadSettings(g_settingsIni, g_settings);
    g_savedSettings = g_settings;
    g_level0Color = TcBaseColor();
    g_savedLevel0Color = g_level0Color;
    g_windowBrush = GetSysColorBrush(COLOR_WINDOW);

    HICON appIcon = static_cast<HICON>(LoadImageW(
        instance, L"IDI_FHM_CONFIG", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    HICON smallIcon = static_cast<HICON>(LoadImageW(
        instance, L"IDI_FHM_CONFIG", IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ConfigWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = WINDOW_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_windowBrush;
    wc.hIcon = appIcon;
    wc.hIconSm = smallIcon ? smallIcon : appIcon;

    const ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        const DWORD err = GetLastError();
        MessageBoxW(nullptr,
            (L"Could not register the settings window. Windows error: " +
             std::to_wstring(err)).c_str(),
            L"FolderHeatMap", MB_ICONERROR);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return 2;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, WINDOW_CLASS, L"FolderHeatMap - Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 556, 414,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        const DWORD err = GetLastError();
        MessageBoxW(nullptr,
            (L"Could not create the settings window. Windows error: " +
             std::to_wstring(err)).c_str(),
            L"FolderHeatMap", MB_ICONERROR);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return 3;
    }

    if (appIcon)
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
    if (smallIcon)
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));

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
