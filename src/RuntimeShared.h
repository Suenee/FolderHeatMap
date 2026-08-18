#pragma once

#include <windows.h>
#include <cstdint>
#include <cwctype>
#include <string>

namespace fhm::runtime {

constexpr wchar_t kMappingName[] = L"Local\\FolderHeatMapRuntimeV1";
constexpr wchar_t kEngineMutexName[] = L"Local\\FolderHeatMapEngineMutexV1";
constexpr wchar_t kEngineStoppedEventName[] = L"Local\\FolderHeatMapEngineStoppedV1";
constexpr std::uint32_t kMagic = 0x314D4846; // FHM1
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kBucketCount = 32768;
constexpr std::size_t kDirectoryChars = 1024;

constexpr std::uint32_t kFlagDirectory = 1u << 0;
constexpr std::uint32_t kFlagFileHeat = 1u << 1;
constexpr std::uint32_t kFlagLastVisit = 1u << 2;
constexpr std::uint32_t kFlagLastWrite = 1u << 3;

struct CacheEntry {
    std::uint64_t pathHash = 0;
    std::uint32_t pathLength = 0;
    std::uint32_t flags = 0;
    double heat = 0.0;
    std::int64_t visits = 0;
    std::int64_t writes = 0;
    FILETIME lastVisit{};
    FILETIME lastWrite{};
    std::int32_t heatLevel = 0;
    std::int32_t colorStep = 0;
};

struct CacheBuffer {
    volatile LONG readers = 0;
    volatile LONG count = 0;
    CacheEntry entries[kBucketCount]{};
};

struct SharedState {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    volatile LONG activeBuffer = 0;
    volatile LONG generation = 0;
    volatile LONG navigationSeq = 0;
    volatile LONG settingsSeq = 0;
    volatile LONG shutdownRequested = 0;
    volatile LONG clientCount = 0;
    wchar_t currentDirectory[kDirectoryChars]{};
    CacheBuffer buffers[2]{};
};

inline std::wstring NormalizePath(std::wstring path) {
    for (auto& ch : path) {
        if (ch == L'/') ch = L'\\';
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    while (path.size() > 3 && !path.empty() && path.back() == L'\\') path.pop_back();
    return path;
}

inline std::uint64_t HashNormalizedPath(const wchar_t* text, std::uint32_t& normalizedLength) {
    constexpr std::uint64_t offset = 1469598103934665603ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    normalizedLength = 0;
    if (!text) return 0;

    std::size_t rawLength = wcslen(text);
    while (rawLength > 3 && (text[rawLength - 1] == L'\\' || text[rawLength - 1] == L'/')) --rawLength;

    for (std::size_t i = 0; i < rawLength; ++i) {
        wchar_t ch = text[i] == L'/' ? L'\\' : text[i];
        ch = static_cast<wchar_t>(std::towlower(ch));
        hash ^= static_cast<std::uint16_t>(ch);
        hash *= prime;
        ++normalizedLength;
    }
    return hash ? hash : 1;
}

inline std::uint64_t HashNormalizedPath(const std::wstring& path) {
    std::uint32_t ignored = 0;
    return HashNormalizedPath(path.c_str(), ignored);
}

inline CacheEntry* FindEntry(CacheBuffer& buffer, std::uint64_t hash, std::uint32_t length) {
    const std::size_t start = static_cast<std::size_t>(hash % kBucketCount);
    for (std::size_t probe = 0; probe < kBucketCount; ++probe) {
        CacheEntry& entry = buffer.entries[(start + probe) % kBucketCount];
        if (entry.pathHash == 0) return nullptr;
        if (entry.pathHash == hash && entry.pathLength == length) return &entry;
    }
    return nullptr;
}

inline const CacheEntry* FindEntry(const CacheBuffer& buffer, std::uint64_t hash, std::uint32_t length) {
    return FindEntry(const_cast<CacheBuffer&>(buffer), hash, length);
}

inline bool InsertEntry(CacheBuffer& buffer, const CacheEntry& source) {
    const std::size_t start = static_cast<std::size_t>(source.pathHash % kBucketCount);
    for (std::size_t probe = 0; probe < kBucketCount; ++probe) {
        CacheEntry& entry = buffer.entries[(start + probe) % kBucketCount];
        if (entry.pathHash == 0 || (entry.pathHash == source.pathHash && entry.pathLength == source.pathLength)) {
            const bool wasEmpty = entry.pathHash == 0;
            entry = source;
            if (wasEmpty) InterlockedIncrement(&buffer.count);
            return true;
        }
    }
    return false;
}

} // namespace fhm::runtime
