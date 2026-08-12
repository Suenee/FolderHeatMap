#include "Database.h"
#include "FolderIdentity.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr UINT kTcExecuteCommandMessage = WM_USER + 51;
constexpr WPARAM kTcRereadSourceCommand = 540;       // cm_RereadSource
constexpr WPARAM kTcSwitchToNextTabCommand = 3005;  // cm_SwitchToNextTab
constexpr WPARAM kTcSwitchToPrevTabCommand = 3006;  // cm_SwitchToPreviousTab

bool IsTotalCommanderWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    wchar_t className[128]{};
    if (!GetClassNameW(hwnd, className, static_cast<int>(std::size(className)))) return false;
    return _wcsicmp(className, L"TTOTAL_CMD") == 0;
}

BOOL CALLBACK FindTcWindowProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd) || !IsTotalCommanderWindow(hwnd)) return TRUE;
    *reinterpret_cast<HWND*>(lParam) = hwnd;
    return FALSE;
}

HWND FindTotalCommanderWindow() {
    HWND foreground = GetForegroundWindow();
    if (foreground) foreground = GetAncestor(foreground, GA_ROOT);
    if (IsTotalCommanderWindow(foreground)) return foreground;

    HWND found = nullptr;
    EnumWindows(FindTcWindowProc, reinterpret_cast<LPARAM>(&found));
    return found;
}

bool SendTcCommand(HWND tcWindow, WPARAM command) {
    if (!IsTotalCommanderWindow(tcWindow)) return false;
    DWORD_PTR ignored = 0;
    return SendMessageTimeoutW(tcWindow, kTcExecuteCommandMessage, command, 0,
                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &ignored) != 0;
}

void RefreshTotalCommander(HWND tcWindow) {
    if (!IsTotalCommanderWindow(tcWindow)) return;

    // cm_RereadSource updates ordinary WDX column values, but Total Commander
    // intentionally keeps cached file-type colors when no filesystem metadata
    // changed. Switching away from and back to a directory tab forces TC to
    // rebuild the panel and re-evaluate plugin based color rules as well.
    SendTcCommand(tcWindow, kTcRereadSourceCommand);

    // Known TC workaround for cached color filters. Previous -> next preserves
    // the current tab in normal multi-tab use while forcing a complete color
    // recalculation. If there is only one tab these commands are harmless no-ops.
    Sleep(40);
    SendTcCommand(tcWindow, kTcSwitchToPrevTabCommand);
    Sleep(80);
    SendTcCommand(tcWindow, kTcSwitchToNextTabCommand);
    Sleep(40);

    RedrawWindow(tcWindow, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

std::wstring ExpandEnvironment(const std::wstring& value) {
    if (value.empty()) return {};
    const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (!needed) return value;
    std::wstring out(static_cast<size_t>(needed), L'\0');
    if (!ExpandEnvironmentStringsW(value.c_str(), out.data(), needed)) return value;
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::wstring ReadRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
    wchar_t buffer[32768]{};
    DWORD type = 0;
    DWORD bytes = sizeof(buffer);
    const LONG rc = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
    return type == REG_EXPAND_SZ ? ExpandEnvironment(buffer) : std::wstring(buffer);
}

std::wstring FindTotalCommanderIni() {
    wchar_t env[32768]{};
    DWORD n = GetEnvironmentVariableW(L"COMMANDER_INI", env, static_cast<DWORD>(std::size(env)));
    if (n > 0 && n < std::size(env)) {
        const auto expanded = ExpandEnvironment(env);
        if (std::filesystem::exists(expanded)) return expanded;
    }

    const wchar_t* key = L"Software\\Ghisler\\Total Commander";
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        const auto value = ReadRegistryString(root, key, L"IniFileName");
        if (!value.empty() && std::filesystem::exists(value)) return value;
    }

    wchar_t appData[32768]{};
    n = GetEnvironmentVariableW(L"APPDATA", appData, static_cast<DWORD>(std::size(appData)));
    if (n > 0 && n < std::size(appData)) {
        const auto candidate = std::filesystem::path(appData) / L"GHISLER" / L"wincmd.ini";
        if (std::filesystem::exists(candidate)) return candidate.wstring();
    }
    return {};
}

std::wstring FindDatabasePath() {
    const auto ini = FindTotalCommanderIni();
    if (!ini.empty()) return (std::filesystem::path(ini).parent_path() / L"FolderHeatMap.db").wstring();

    wchar_t appData[32768]{};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, static_cast<DWORD>(std::size(appData)));
    if (n > 0 && n < std::size(appData)) return (std::filesystem::path(appData) / L"GHISLER" / L"FolderHeatMap.db").wstring();
    return {};
}

std::wstring TrimLine(std::wstring value) {
    while (!value.empty() && (value.back() == L'\r' || value.back() == L'\n' || value.back() == L' ' || value.back() == L'\t')) value.pop_back();
    size_t first = 0;
    while (first < value.size() && (value[first] == L' ' || value[first] == L'\t')) ++first;
    value.erase(0, first);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') value = value.substr(1, value.size() - 2);
    return value;
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find_first_of(L"\r\n", start);
        auto line = TrimLine(text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (!line.empty()) out.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        start = end + 1;
        if (text[end] == L'\r' && start < text.size() && text[start] == L'\n') ++start;
    }
    return out;
}

std::vector<std::wstring> ReadListFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(file);
        return {};
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) return {};
    bytes.resize(read);

    std::wstring text;
    const bool utf16Bom = bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE;
    bool looksUtf16 = utf16Bom;
    if (!looksUtf16 && bytes.size() >= 4) {
        size_t zeroOdd = 0;
        size_t samples = 0;
        for (size_t i = 1; i < bytes.size() && samples < 64; i += 2, ++samples) if (bytes[i] == 0) ++zeroOdd;
        looksUtf16 = samples > 0 && zeroOdd * 2 >= samples;
    }

    if (looksUtf16) {
        const size_t offset = utf16Bom ? 2 : 0;
        const size_t chars = (bytes.size() - offset) / sizeof(wchar_t);
        text.assign(reinterpret_cast<const wchar_t*>(bytes.data() + offset), chars);
    } else {
        if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            bytes.erase(bytes.begin(), bytes.begin() + 3);
        }
        const int count = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), nullptr, 0);
        if (count <= 0) return {};
        text.resize(static_cast<size_t>(count));
        MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), text.data(), count);
    }
    return SplitLines(text);
}

bool GetLastWriteTime(const std::wstring& path, FILETIME& value) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;
    value = data.ftLastWriteTime;
    return true;
}

std::wstring MakeAbsolute(const std::wstring& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    return ec ? path : absolute.wstring();
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HWND tcWindow = FindTotalCommanderWindow();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;

    std::vector<std::wstring> paths;
    if (argc >= 3 && _wcsicmp(argv[1], L"--list") == 0) {
        paths = ReadListFile(argv[2]);
    } else {
        for (int i = 1; i < argc; ++i) paths.push_back(argv[i]);
    }
    LocalFree(argv);

    for (auto& path : paths) path = MakeAbsolute(TrimLine(path));
    if (paths.empty()) {
        MessageBoxW(tcWindow, L"No files or folders were supplied.\n\nFor a Total Commander button use parameter: --list \"%WL\"",
                    L"FolderHeatMap - Reset heat", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    bool hasDirectory = false;
    for (const auto& path : paths) if (fhm::IsDirectory(path)) { hasDirectory = true; break; }

    bool recursive = false;
    if (hasDirectory) {
        const int answer = MessageBoxW(tcWindow,
            L"Reset heat for the selected items?\n\nYES  = reset selected items AND everything below selected folders\nNO   = reset only the selected items\nCANCEL = do nothing\n\nInherited heat is never forcibly hidden; parent folders will recalculate from the activity that remains.",
            L"FolderHeatMap - Reset heat", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer == IDCANCEL) return 0;
        recursive = answer == IDYES;
    } else {
        const int answer = MessageBoxW(tcWindow, L"Reset the direct heat of the selected file(s)?",
                                       L"FolderHeatMap - Reset heat", MB_OKCANCEL | MB_ICONQUESTION);
        if (answer != IDOK) return 0;
    }

    const auto dbPath = FindDatabasePath();
    if (dbPath.empty()) {
        MessageBoxW(tcWindow, L"FolderHeatMap could not locate the Total Commander configuration/database.",
                    L"FolderHeatMap - Reset heat", MB_OK | MB_ICONERROR);
        return 2;
    }

    int resetCount = 0;
    int failedCount = 0;
    {
        fhm::Database database;
        if (!database.Open(dbPath)) {
            MessageBoxW(tcWindow, L"FolderHeatMap database could not be opened.",
                        L"FolderHeatMap - Reset heat", MB_OK | MB_ICONERROR);
            return 2;
        }

        for (const auto& path : paths) {
            const DWORD attrs = GetFileAttributesW(path.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) { ++failedCount; continue; }
            const bool isDirectory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const auto identity = fhm::ResolveFolderIdentity(path);
            if (!identity) { ++failedCount; continue; }

            bool ok = false;
            if (recursive && isDirectory) {
                ok = database.ResetRecursiveActivity(*identity);
            } else if (isDirectory) {
                ok = database.ResetDirectActivity(*identity, true, nullptr);
            } else {
                FILETIME lastWrite{};
                ok = GetLastWriteTime(path, lastWrite) && database.ResetDirectActivity(*identity, false, &lastWrite);
            }
            if (ok) ++resetCount; else ++failedCount;
        }
    }

    RefreshTotalCommander(tcWindow);

    if (failedCount > 0) {
        std::wstring message = L"Heat reset completed with errors.\n\nReset: " + std::to_wstring(resetCount) +
                               L"\nFailed: " + std::to_wstring(failedCount);
        MessageBoxW(tcWindow, message.c_str(), L"FolderHeatMap - Reset heat", MB_OK | MB_ICONWARNING);
        return 3;
    }

    return 0;
}
