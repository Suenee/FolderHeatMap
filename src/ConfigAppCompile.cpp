// Thin Win32 build wrapper for FolderHeatMapConfig.
//
// ConfigApp.cpp owns shared configurator logic. This wrapper provides the
// tuned compact UI, including File Heat and the configurable folder-icon map.

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
#pragma warning(disable : 4389)
#endif

#define wWinMain FolderHeatMapBackendWinMain
#include "ConfigApp.cpp"
#undef wWinMain

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace {

constexpr int IDC_FILE_HEAT = 1401;
constexpr int IDC_FILE_CONTRIBUTION = 1402;
constexpr int IDC_FILE_CONTRIBUTION_SPIN = 1403;
constexpr int IDC_ICON_SWATCH_BASE = 1500;
HWND g_fileContributionEdit{};
std::array<HWND, 8> g_iconSwatches{};
std::array<HICON, 8> g_iconHandles{};
std::array<std::wstring, 8> g_iconTooltips{};

int ReadFileContribution(bool normalize = false) {
    return EditInt(g_fileContributionEdit, 0, 100, normalize);
}

bool IconSettingsDirty() {
    for (int i = 1; i <= 7; ++i)
        if (g_settings.folderIconSources[i] != g_savedSettings.folderIconSources[i]) return true;
    return false;
}

bool FileSettingsDirty(HWND hwnd) {
    const bool enabled = SendDlgItemMessageW(hwnd, IDC_FILE_HEAT, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int contribution = ReadFileContribution(false);
    return enabled != g_savedSettings.fileHeatEnabled ||
        contribution != static_cast<int>(std::lround(g_savedSettings.fileContribution * 100.0));
}

void UpdateSaveStateWithFile(HWND hwnd) {
    UpdateSaveState(hwnd);
    if (FileSettingsDirty(hwnd) || IconSettingsDirty()) EnableWindow(g_saveButton, TRUE);
}

void UpdateEnabledStateWithFile(HWND hwnd) {
    UpdateEnabledState(hwnd);
    const bool enabled = SendDlgItemMessageW(hwnd, IDC_FILE_HEAT, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_fileContributionEdit, enabled);
    EnableWindow(GetDlgItem(hwnd, IDC_FILE_CONTRIBUTION_SPIN), enabled);
    UpdateSaveStateWithFile(hwnd);
}

std::wstring GeneratedIconPath(int level) {
    if (level < 1 || level > 7) return {};
    return (std::filesystem::path(g_wincmdIni).parent_path() / L"FolderHeatMapIcons" /
            (L"heat-" + std::to_wstring(level) + L".ico")).wstring();
}

void DestroyIconPreview(int level) {
    if (level >= 0 && level <= 7 && g_iconHandles[level]) {
        DestroyIcon(g_iconHandles[level]);
        g_iconHandles[level] = nullptr;
    }
}

void ReloadIconPreview(int level) {
    if (level < 0 || level > 7) return;
    DestroyIconPreview(level);

    if (level == 0) {
        SHFILEINFOW info{};
        const std::wstring sample = std::filesystem::path(g_wincmdIni).parent_path().wstring();
        if (SHGetFileInfoW(sample.c_str(), FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
                           SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
            g_iconHandles[level] = info.hIcon;
    } else {
        std::wstring candidate;
        const auto& custom = g_settings.folderIconSources[level];
        if (!custom.empty() && _wcsicmp(std::filesystem::path(custom).extension().c_str(), L".ico") == 0)
            candidate = custom;
        if (candidate.empty() || !std::filesystem::exists(candidate)) candidate = GeneratedIconPath(level);
        if (!candidate.empty() && std::filesystem::exists(candidate)) {
            g_iconHandles[level] = static_cast<HICON>(LoadImageW(nullptr, candidate.c_str(), IMAGE_ICON,
                32, 32, LR_LOADFROMFILE));
        }
    }
    if (g_iconSwatches[level]) InvalidateRect(g_iconSwatches[level], nullptr, TRUE);
}

void ReloadAllIconPreviews() {
    for (int i = 0; i <= 7; ++i) ReloadIconPreview(i);
}

void ChooseFolderIcon(HWND hwnd, int level) {
    if (level == 0) {
        MessageBoxW(hwnd, L"Heat level 0 uses Total Commander's normal folder icon and is read-only.",
                    L"FolderHeatMap", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (level < 1 || level > 7) return;

    wchar_t file[32768]{};
    if (!g_settings.folderIconSources[level].empty())
        wcsncpy_s(file, g_settings.folderIconSources[level].c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        L"Supported images (*.ico;*.png;*.gif;*.bmp;*.jpg;*.jpeg)\0*.ico;*.png;*.gif;*.bmp;*.jpg;*.jpeg\0"
        L"Icon files (*.ico)\0*.ico\0PNG images (*.png)\0*.png\0GIF images (*.gif)\0*.gif\0"
        L"Bitmap/JPEG images (*.bmp;*.jpg;*.jpeg)\0*.bmp;*.jpg;*.jpeg\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrTitle = L"Choose folder icon artwork";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;

    g_settings.folderIconSources[level] = file;
    ReloadIconPreview(level);
    UpdateSaveStateWithFile(hwnd);
}

void ResetFolderIconToGenerated(HWND hwnd, int level) {
    if (level < 1 || level > 7 || g_settings.folderIconSources[level].empty()) return;
    g_settings.folderIconSources[level].clear();
    ReloadIconPreview(level);
    UpdateSaveStateWithFile(hwnd);
}

bool ColorsChangedFromSaved() {
    for (size_t i = 1; i < g_settings.colors.size(); ++i)
        if (g_settings.colors[i] != g_savedSettings.colors[i]) return true;
    return false;
}

bool RunIconSetup(HWND hwnd) {
    wchar_t exePath[32768]{};
    if (!GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)))) return false;
    const auto script = std::filesystem::path(exePath).parent_path() / L"setup_icons.ps1";
    if (!std::filesystem::exists(script)) {
        MessageBoxW(hwnd,
            (L"Folder icon setup script was not found:\n" + script.wstring()).c_str(),
            L"FolderHeatMap", MB_OK | MB_ICONWARNING);
        return false;
    }

    const bool wasRunning = IsTcRunning();
    if (wasRunning && !StopTc()) return false;

    std::wstring params = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd;
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    bool ok = ShellExecuteExW(&sei) != FALSE;
    if (ok && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        ok = exitCode == 0;
    }
    if (wasRunning) StartTc();
    ReloadAllIconPreviews();
    if (!ok)
        MessageBoxW(hwnd, L"Folder icons could not be regenerated. Existing icon settings were kept.",
                    L"FolderHeatMap", MB_OK | MB_ICONWARNING);
    return ok;
}

void SaveWithFile(HWND hwnd) {
    const bool iconsNeedUpdate = IconSettingsDirty() || ColorsChangedFromSaved();
    g_settings.fileHeatEnabled = SendDlgItemMessageW(hwnd, IDC_FILE_HEAT, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.fileContribution = ReadFileContribution(true) / 100.0;
    Save(hwnd);

    // Save() updates g_savedSettings only after a successful TC settings update.
    if (iconsNeedUpdate && !IconSettingsDirty()) RunIconSetup(hwnd);
    UpdateSaveStateWithFile(hwnd);
}

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
                HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                    x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                    g_instance, nullptr);
                if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return control;
            };

            auto header = [&](const wchar_t* text, int x, int y, int w, DWORD align = SS_LEFT) {
                HWND control = add(L"STATIC", text, align, x, y, w, 20);
                if (control && g_boldFont)
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_boldFont), TRUE);
                return control;
            };

            g_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hwnd, nullptr, g_instance, nullptr);
            SendMessageW(g_tooltip, TTM_SETMAXTIPWIDTH, 0, 420);

            header(L"Cooling", 28, 18, 210);
            HWND autoBtn = add(L"BUTTON", L"Automatic", BS_AUTORADIOBUTTON | WS_GROUP,
                36, 44, 92, 22, IDC_AUTO_FHM);
            HWND manualBtn = add(L"BUTTON", L"Manual", BS_AUTORADIOBUTTON,
                132, 44, 78, 22, IDC_MANUAL_FHM);
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
            HWND pathBtn = add(L"BUTTON", L"Include hot descendants", BS_AUTOCHECKBOX,
                36, 152, 182, 22, IDC_PATH);
            add(L"STATIC", L"Contribution:", SS_LEFT, 36, 186, 82, 20);
            g_decayEdit = add(L"EDIT", L"50", WS_BORDER | ES_NUMBER | ES_CENTER,
                120, 182, 58, 22, IDC_DECAY);
            AddSpin(hwnd, g_decayEdit, IDC_DECAY_SPIN, 0, 100);
            add(L"STATIC", L"%", SS_LEFT, 186, 186, 20, 20);

            HWND fileHeatBtn = add(L"BUTTON", L"File heat", BS_AUTOCHECKBOX,
                36, 216, 120, 22, IDC_FILE_HEAT);
            add(L"STATIC", L"File contribution:", SS_LEFT, 36, 250, 96, 20);
            g_fileContributionEdit = add(L"EDIT", L"50", WS_BORDER | ES_NUMBER | ES_CENTER,
                136, 246, 58, 22, IDC_FILE_CONTRIBUTION);
            AddSpin(hwnd, g_fileContributionEdit, IDC_FILE_CONTRIBUTION_SPIN, 0, 100);
            add(L"STATIC", L"%", SS_LEFT, 202, 250, 20, 20);

            header(L"Color behavior", 292, 126, 220);
            HWND smoothBtn = add(L"BUTTON", L"Smooth transitions", BS_AUTOCHECKBOX,
                300, 152, 154, 22, IDC_SMOOTH);
            add(L"STATIC", L"Steps per level:", SS_LEFT, 300, 186, 108, 20);
            g_stepsEdit = add(L"EDIT", L"4", WS_BORDER | ES_NUMBER | ES_CENTER,
                412, 182, 58, 22, IDC_STEPS);
            AddSpin(hwnd, g_stepsEdit, IDC_STEPS_SPIN, 1, 16);

            header(L"Color heat map", 28, 294, 484, SS_CENTER);
            constexpr int stripX = 28;
            constexpr int stripWidth = 484;
            constexpr int gap = 2;
            const int swatchWidth = (stripWidth - gap * 7) / 8;
            int x = stripX;
            for (int i = 0; i <= 7; ++i) {
                const int w = (i == 7) ? (stripX + stripWidth - x) : swatchWidth;
                g_colorSwatches[i] = add(L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY,
                    x, 322, w, 36, IDC_SWATCH_BASE + i);
                g_swatchTooltips[i] = i == 0
                    ? L"Heat level 0 color - Total Commander base text color (editable)."
                    : L"Heat level " + std::to_wstring(i) + L" color.";
                AddTooltip(g_colorSwatches[i], g_swatchTooltips[i].c_str());
                x += w + gap;
            }

            header(L"Folder icon map", 28, 372, 484, SS_CENTER);
            x = stripX;
            for (int i = 0; i <= 7; ++i) {
                const int w = (i == 7) ? (stripX + stripWidth - x) : swatchWidth;
                g_iconSwatches[i] = add(L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY,
                    x, 398, w, 42, IDC_ICON_SWATCH_BASE + i);
                g_iconTooltips[i] = i == 0
                    ? L"Level 0: Total Commander's normal folder icon (read-only)."
                    : L"Level " + std::to_wstring(i) +
                      L" folder icon. Click to choose ICO/PNG/GIF/BMP/JPG. Right-click to restore the generated heat icon.";
                AddTooltip(g_iconSwatches[i], g_iconTooltips[i].c_str());
                x += w + gap;
            }
            ReloadAllIconPreviews();

            HWND helpBtn = add(L"BUTTON", L"Help", BS_PUSHBUTTON,
                28, 466, 78, 30, IDC_HELP_BUTTON);
            g_saveButton = add(L"BUTTON", L"Save", BS_DEFPUSHBUTTON,
                356, 466, 78, 30, IDC_SAVE);
            add(L"BUTTON", L"Cancel", BS_PUSHBUTTON,
                442, 466, 78, 30, IDC_CANCEL);

            AddTooltip(autoBtn, L"Learns cooling speed from your Total Commander usage rhythm.");
            AddTooltip(manualBtn, L"Use a fixed cooling half-life instead of automatic learning.");
            AddTooltip(g_halfEdit, L"Manual cooling half-life. Range: 1-365 days.");
            AddTooltip(pathBtn, L"Let hot descendant folders contribute heat to their parent path.");
            AddTooltip(g_decayEdit, L"Heat retained per directory level when propagating upward. Range: 0-100%.");
            AddTooltip(fileHeatBtn, L"Color files from write activity. A file becomes hot when its Last Write timestamp changes; merely displaying it does not count as a visit.");
            AddTooltip(g_fileContributionEdit, L"How strongly the hottest file warms its containing folder. Range: 0-100%.");
            AddTooltip(g_cooldownEdit, L"Repeated directory entries inside this interval do not increase Heat. Range: 0-600 seconds.");
            AddTooltip(g_sessionEdit, L"Idle gap after which recent directory work starts a new session. Range: 1-24 hours.");
            AddTooltip(smoothBtn, L"Generate intermediate Total Commander colors between heat anchors.");
            AddTooltip(g_stepsEdit, L"Intermediate color steps per heat level. Range: 1-16.");
            AddTooltip(helpBtn, L"Show help for FolderHeatMap settings.");

            SendDlgItemMessageW(hwnd, g_settings.coolingAuto ? IDC_AUTO_FHM : IDC_MANUAL_FHM,
                BM_SETCHECK, BST_CHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_PATH, BM_SETCHECK,
                g_settings.includePathHeat ? BST_CHECKED : BST_UNCHECKED, 0);
            SendDlgItemMessageW(hwnd, IDC_FILE_HEAT, BM_SETCHECK,
                g_settings.fileHeatEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
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
            SetWindowTextW(g_fileContributionEdit,
                std::to_wstring(static_cast<int>(std::lround(g_settings.fileContribution * 100.0))).c_str());
            SetWindowTextW(g_stepsEdit, std::to_wstring(g_settings.stepsPerLevel).c_str());

            g_initializing = false;
            UpdateEnabledStateWithFile(hwnd);
            UpdateSaveStateWithFile(hwnd);
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
            if (dis && dis->CtlID >= IDC_ICON_SWATCH_BASE && dis->CtlID <= IDC_ICON_SWATCH_BASE + 7) {
                const int level = static_cast<int>(dis->CtlID) - IDC_ICON_SWATCH_BASE;
                RECT r = dis->rcItem;
                FillRect(dis->hDC, &r, g_windowBrush);
                FrameRect(dis->hDC, &r, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                if (g_iconHandles[level]) {
                    const int size = 32;
                    const int dx = r.left + ((r.right - r.left) - size) / 2;
                    const int dy = r.top + ((r.bottom - r.top) - size) / 2;
                    DrawIconEx(dis->hDC, dx, dy, g_iconHandles[level], size, size, 0, nullptr, DI_NORMAL);
                }
                return TRUE;
            }
            break;
        }

        case WM_CONTEXTMENU: {
            HWND source = reinterpret_cast<HWND>(wp);
            const int id = source ? GetDlgCtrlID(source) : 0;
            if (id >= IDC_ICON_SWATCH_BASE + 1 && id <= IDC_ICON_SWATCH_BASE + 7) {
                ResetFolderIconToGenerated(hwnd, id - IDC_ICON_SWATCH_BASE);
                return 0;
            }
            break;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id >= IDC_SWATCH_BASE && id <= IDC_SWATCH_BASE + 7 && code == STN_CLICKED) {
                ChooseColor(hwnd, id - IDC_SWATCH_BASE);
                UpdateSaveStateWithFile(hwnd);
                return 0;
            }
            if (id >= IDC_ICON_SWATCH_BASE && id <= IDC_ICON_SWATCH_BASE + 7 && code == STN_CLICKED) {
                ChooseFolderIcon(hwnd, id - IDC_ICON_SWATCH_BASE);
                return 0;
            }
            if (id == IDC_AUTO_FHM || id == IDC_MANUAL_FHM ||
                id == IDC_PATH || id == IDC_SMOOTH || id == IDC_FILE_HEAT) {
                UpdateEnabledStateWithFile(hwnd);
                return 0;
            }
            if ((id == IDC_HALF || id == IDC_DECAY || id == IDC_STEPS ||
                 id == IDC_COOLDOWN || id == IDC_SESSION || id == IDC_FILE_CONTRIBUTION) && code == EN_CHANGE) {
                UpdateSaveStateWithFile(hwnd);
                return 0;
            }
            if (id == IDC_HELP_BUTTON) { ShowHelp(hwnd); return 0; }
            if (id == IDC_SAVE) { SaveWithFile(hwnd); return 0; }
            if (id == IDC_CANCEL) { DestroyWindow(hwnd); return 0; }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            for (int i = 0; i <= 7; ++i) DestroyIconPreview(i);
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
            (L"Could not register the settings window. Windows error: " + std::to_wstring(err)).c_str(),
            L"FolderHeatMap", MB_ICONERROR);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return 2;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, WINDOW_CLASS, L"FolderHeatMap - Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 556, 552,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        const DWORD err = GetLastError();
        MessageBoxW(nullptr,
            (L"Could not create the settings window. Windows error: " + std::to_wstring(err)).c_str(),
            L"FolderHeatMap", MB_ICONERROR);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
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
