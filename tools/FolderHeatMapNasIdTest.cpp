#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* kVersion = L"1.00";
constexpr ULONG kFileFsVolumeInformation = 1;
constexpr ULONG kFileFsObjectIdInformation = 8;

struct IoStatusBlockCompat {
    union {
        LONG status;
        void* pointer;
    };
    ULONG_PTR information;
};

struct FsObjectIdInformationCompat {
    unsigned char objectId[16];
    unsigned char extendedInfo[48];
};

struct FsVolumeInformationCompat {
    LARGE_INTEGER volumeCreationTime;
    ULONG volumeSerialNumber;
    ULONG volumeLabelLength;
    BOOLEAN supportsObjects;
    WCHAR volumeLabel[1];
};

using NtQueryVolumeInformationFileFn = LONG(NTAPI*)(HANDLE, IoStatusBlockCompat*, void*, ULONG, ULONG);

struct IdentityResult {
    std::wstring inputPath;
    std::wstring finalPath;
    bool opened = false;
    DWORD openError = ERROR_SUCCESS;

    bool fileIdOk = false;
    DWORD fileIdError = ERROR_SUCCESS;
    std::uint64_t fileIdVolumeSerial = 0;
    std::array<unsigned char, 16> fileId{};

    bool remoteInfoOk = false;
    DWORD remoteInfoError = ERROR_SUCCESS;
    ULONG remoteProtocol = 0;
    USHORT remoteMajor = 0;
    USHORT remoteMinor = 0;
    USHORT remoteRevision = 0;
    ULONG remoteFlags = 0;

    bool objectIdQueried = false;
    bool objectIdOk = false;
    LONG objectIdStatus = 0;
    std::array<unsigned char, 16> volumeObjectId{};

    bool fsVolumeQueried = false;
    bool fsVolumeOk = false;
    LONG fsVolumeStatus = 0;
    ULONG fsVolumeSerial = 0;
    std::wstring fsVolumeLabel;
};

std::wstring HexBytes(const unsigned char* bytes, size_t count) {
    std::wostringstream out;
    out << std::hex << std::setfill(L'0');
    for (size_t i = 0; i < count; ++i) out << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    return out.str();
}

std::wstring Hex32(ULONG value) {
    std::wostringstream out;
    out << L"0x" << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0') << value;
    return out.str();
}

std::wstring Hex64(std::uint64_t value) {
    std::wostringstream out;
    out << L"0x" << std::uppercase << std::hex << std::setw(16) << std::setfill(L'0') << value;
    return out.str();
}

std::wstring NtStatusText(LONG status) {
    std::wostringstream out;
    out << L"0x" << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
        << static_cast<ULONG>(status);
    return out.str();
}

std::wstring Win32Message(DWORD error) {
    wchar_t* raw = nullptr;
    const DWORD chars = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, error, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    if (!chars || !raw) return L"Win32 error " + std::to_wstring(error);
    std::wstring result(raw, chars);
    LocalFree(raw);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' '))
        result.pop_back();
    return result;
}

std::wstring GetFinalPath(HANDLE handle) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                   FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= buffer.size()) return {};
    std::wstring path(buffer.data(), length);
    if (path.rfind(L"\\\\?\\UNC\\", 0) == 0) return L"\\\\" + path.substr(8);
    if (path.rfind(L"\\\\?\\", 0) == 0) return path.substr(4);
    return path;
}

IdentityResult QueryIdentity(const std::wstring& path) {
    IdentityResult result;
    result.inputPath = path;

    const DWORD attributes = GetFileAttributesW(path.c_str());
    const bool directory = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const DWORD flags = directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;

    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        result.openError = GetLastError();
        return result;
    }
    result.opened = true;
    result.finalPath = GetFinalPath(handle);

    FILE_ID_INFO fileInfo{};
    if (GetFileInformationByHandleEx(handle, FileIdInfo, &fileInfo, sizeof(fileInfo))) {
        result.fileIdOk = true;
        result.fileIdVolumeSerial = static_cast<std::uint64_t>(fileInfo.VolumeSerialNumber);
        for (size_t i = 0; i < result.fileId.size(); ++i) result.fileId[i] = fileInfo.FileId.Identifier[i];
    } else {
        result.fileIdError = GetLastError();
    }

    FILE_REMOTE_PROTOCOL_INFO remote{};
    if (GetFileInformationByHandleEx(handle, FileRemoteProtocolInfo, &remote, sizeof(remote))) {
        result.remoteInfoOk = true;
        result.remoteProtocol = remote.Protocol;
        result.remoteMajor = remote.ProtocolMajorVersion;
        result.remoteMinor = remote.ProtocolMinorVersion;
        result.remoteRevision = remote.ProtocolRevision;
        result.remoteFlags = remote.Flags;
    } else {
        result.remoteInfoError = GetLastError();
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto query = ntdll ? reinterpret_cast<NtQueryVolumeInformationFileFn>(
                             GetProcAddress(ntdll, "NtQueryVolumeInformationFile"))
                       : nullptr;
    if (query) {
        IoStatusBlockCompat iosb{};
        FsObjectIdInformationCompat objectInfo{};
        result.objectIdQueried = true;
        result.objectIdStatus = query(handle, &iosb, &objectInfo, sizeof(objectInfo), kFileFsObjectIdInformation);
        if (result.objectIdStatus >= 0) {
            result.objectIdOk = true;
            for (size_t i = 0; i < result.volumeObjectId.size(); ++i)
                result.volumeObjectId[i] = objectInfo.objectId[i];
        }

        alignas(8) std::array<unsigned char, 2048> volumeBuffer{};
        iosb = {};
        result.fsVolumeQueried = true;
        result.fsVolumeStatus = query(handle, &iosb, volumeBuffer.data(), static_cast<ULONG>(volumeBuffer.size()),
                                      kFileFsVolumeInformation);
        if (result.fsVolumeStatus >= 0) {
            result.fsVolumeOk = true;
            const auto* volume = reinterpret_cast<const FsVolumeInformationCompat*>(volumeBuffer.data());
            result.fsVolumeSerial = volume->volumeSerialNumber;
            const size_t labelChars = volume->volumeLabelLength / sizeof(wchar_t);
            const size_t maxChars = (volumeBuffer.size() - offsetof(FsVolumeInformationCompat, volumeLabel)) /
                                    sizeof(wchar_t);
            if (labelChars <= maxChars) result.fsVolumeLabel.assign(volume->volumeLabel, labelChars);
        }
    }

    CloseHandle(handle);
    return result;
}

void PrintOne(const wchar_t* heading, const IdentityResult& r) {
    std::wcout << L"\n=== " << heading << L" =====================================\n";
    std::wcout << L"Input path:          " << r.inputPath << L"\n";
    if (!r.opened) {
        std::wcout << L"Open:                FAILED - " << Win32Message(r.openError) << L" (" << r.openError << L")\n";
        return;
    }
    std::wcout << L"Open:                OK\n";
    std::wcout << L"Final path:          " << (r.finalPath.empty() ? L"<unavailable>" : r.finalPath) << L"\n";

    if (r.remoteInfoOk) {
        std::wcout << L"Remote protocol:     " << Hex32(r.remoteProtocol) << L" version "
                   << r.remoteMajor << L"." << r.remoteMinor << L" revision " << r.remoteRevision << L"\n";
        std::wcout << L"Remote flags:        " << Hex32(r.remoteFlags) << L"\n";
    } else {
        std::wcout << L"Remote protocol:     unavailable - " << Win32Message(r.remoteInfoError)
                   << L" (" << r.remoteInfoError << L")\n";
    }

    if (r.objectIdOk) {
        std::wcout << L"Volume Object ID:    " << HexBytes(r.volumeObjectId.data(), r.volumeObjectId.size()) << L"\n";
    } else if (r.objectIdQueried) {
        std::wcout << L"Volume Object ID:    unsupported/failed - NTSTATUS " << NtStatusText(r.objectIdStatus) << L"\n";
    } else {
        std::wcout << L"Volume Object ID:    query API unavailable\n";
    }

    if (r.fileIdOk) {
        std::wcout << L"FILE_ID volume:      " << Hex64(r.fileIdVolumeSerial) << L"\n";
        std::wcout << L"FILE_ID 128:         " << HexBytes(r.fileId.data(), r.fileId.size()) << L"\n";
    } else {
        std::wcout << L"FILE_ID_INFO:        failed - " << Win32Message(r.fileIdError)
                   << L" (" << r.fileIdError << L")\n";
    }

    if (r.fsVolumeOk) {
        std::wcout << L"FS volume serial:    " << Hex32(r.fsVolumeSerial) << L"\n";
        std::wcout << L"FS volume label:     " << (r.fsVolumeLabel.empty() ? L"<empty>" : r.fsVolumeLabel) << L"\n";
    } else if (r.fsVolumeQueried) {
        std::wcout << L"FS volume info:      failed - NTSTATUS " << NtStatusText(r.fsVolumeStatus) << L"\n";
    }
}

void PrintComparison(const IdentityResult& a, const IdentityResult& b) {
    std::wcout << L"\n=== COMPARISON =================================\n";
    if (!a.opened || !b.opened) {
        std::wcout << L"RESULT:              CANNOT COMPARE - one or both paths could not be opened\n";
        return;
    }

    const bool objectComparable = a.objectIdOk && b.objectIdOk;
    const bool objectMatch = objectComparable && a.volumeObjectId == b.volumeObjectId;
    const bool fileVolumeComparable = a.fileIdOk && b.fileIdOk;
    const bool fileVolumeMatch = fileVolumeComparable && a.fileIdVolumeSerial == b.fileIdVolumeSerial;
    const bool fileIdMatch = fileVolumeComparable && a.fileId == b.fileId;
    const bool fsSerialComparable = a.fsVolumeOk && b.fsVolumeOk;
    const bool fsSerialMatch = fsSerialComparable && a.fsVolumeSerial == b.fsVolumeSerial;

    auto state = [](bool comparable, bool match) -> const wchar_t* {
        return !comparable ? L"N/A" : (match ? L"MATCH" : L"DIFFERENT");
    };

    std::wcout << L"Volume Object ID:    " << state(objectComparable, objectMatch) << L"\n";
    std::wcout << L"FILE_ID volume:      " << state(fileVolumeComparable, fileVolumeMatch) << L"\n";
    std::wcout << L"FILE_ID 128:         " << state(fileVolumeComparable, fileIdMatch) << L"\n";
    std::wcout << L"FS volume serial:    " << state(fsSerialComparable, fsSerialMatch) << L"\n";

    if (objectComparable && objectMatch && fileVolumeComparable && fileVolumeMatch && fileIdMatch) {
        std::wcout << L"RESULT:              SAME VOLUME + SAME FILESYSTEM OBJECT\n";
    } else if (objectComparable && objectMatch) {
        std::wcout << L"RESULT:              SAME VOLUME OBJECT ID; object-level evidence differs or is unavailable\n";
    } else if (!objectComparable && fileVolumeComparable && fileVolumeMatch && fileIdMatch) {
        std::wcout << L"RESULT:              SAME FILE_ID_INFO identity; Volume Object ID unavailable\n";
    } else {
        std::wcout << L"RESULT:              IDENTITY NOT PROVEN EQUAL\n";
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wcout << L"FolderHeatMap NAS Identity Test " << kVersion << L"\n";
    std::wcout << L"Read-only diagnostic. No filesystem metadata is modified.\n";

    if (argc < 2 || argc > 3) {
        std::wcout << L"\nUsage:\n  FolderHeatMapNasIdTest.exe <path-A> [path-B]\n";
        return 2;
    }

    const IdentityResult a = QueryIdentity(argv[1]);
    PrintOne(L"PATH A", a);

    if (argc == 3) {
        const IdentityResult b = QueryIdentity(argv[2]);
        PrintOne(L"PATH B", b);
        PrintComparison(a, b);
        return (a.opened && b.opened) ? 0 : 1;
    }

    return a.opened ? 0 : 1;
}
