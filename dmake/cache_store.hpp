#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <expected>

namespace dmake {

// Subsystem identifiers for cache namespacing
enum class CacheSubsystem {
    TryCompile,
    FileListing,
    // Future: ModuleScanning, etc.
};

// Cache entry for try_compile results
struct TryCompileCacheEntry {
    bool success = false;                            // Compilation succeeded
    std::string output;                              // Compiler stdout/stderr
    std::map<std::string, int64_t> header_mtimes;   // Discovered header dependencies (path -> mtime)
};

// Cache entry for file listings (future use)
struct FileListingCacheEntry {
    int64_t dir_mtime = 0;                  // Directory modification time
    std::vector<std::string> files;         // List of files in directory
};

// Root structure for JSON serialization
struct CacheRoot {
    std::map<std::string, TryCompileCacheEntry> try_compile_cache;
    std::map<std::string, FileListingCacheEntry> file_listing_cache;
};

// Centralized cache store with subsystem namespacing
class CacheStore {
public:
    explicit CacheStore(const std::filesystem::path& cache_file);
    ~CacheStore() = default;

    // Load cache from disk (call at build start)
    std::expected<void, std::string> load();

    // Save cache to disk (call at build end)
    std::expected<void, std::string> save();

    // Lookup entry by signature
    template<CacheSubsystem S>
    std::optional<typename std::conditional<
        S == CacheSubsystem::TryCompile, TryCompileCacheEntry,
        typename std::conditional<S == CacheSubsystem::FileListing, FileListingCacheEntry, void>::type
    >::type> lookup(const std::string& signature);

    // Insert/update entry
    template<CacheSubsystem S>
    void insert(const std::string& signature, const typename std::conditional<
        S == CacheSubsystem::TryCompile, TryCompileCacheEntry,
        typename std::conditional<S == CacheSubsystem::FileListing, FileListingCacheEntry, void>::type
    >::type& entry);

    // Clear all entries for a subsystem
    template<CacheSubsystem S>
    void clear_subsystem();

private:
    std::filesystem::path cache_file_;
    CacheRoot cache_data_;
    std::mutex mutex_;  // Thread-safe access
};

// Template specializations for lookup
template<>
inline std::optional<TryCompileCacheEntry> CacheStore::lookup<CacheSubsystem::TryCompile>(const std::string& signature) {
    std::lock_guard lock(mutex_);
    auto it = cache_data_.try_compile_cache.find(signature);
    if (it != cache_data_.try_compile_cache.end()) {
        return it->second;
    }
    return std::nullopt;
}

template<>
inline std::optional<FileListingCacheEntry> CacheStore::lookup<CacheSubsystem::FileListing>(const std::string& signature) {
    std::lock_guard lock(mutex_);
    auto it = cache_data_.file_listing_cache.find(signature);
    if (it != cache_data_.file_listing_cache.end()) {
        return it->second;
    }
    return std::nullopt;
}

// Template specializations for insert
template<>
inline void CacheStore::insert<CacheSubsystem::TryCompile>(const std::string& signature, const TryCompileCacheEntry& entry) {
    std::lock_guard lock(mutex_);
    cache_data_.try_compile_cache[signature] = entry;
}

template<>
inline void CacheStore::insert<CacheSubsystem::FileListing>(const std::string& signature, const FileListingCacheEntry& entry) {
    std::lock_guard lock(mutex_);
    cache_data_.file_listing_cache[signature] = entry;
}

// Template specializations for clear_subsystem
template<>
inline void CacheStore::clear_subsystem<CacheSubsystem::TryCompile>() {
    std::lock_guard lock(mutex_);
    cache_data_.try_compile_cache.clear();
}

template<>
inline void CacheStore::clear_subsystem<CacheSubsystem::FileListing>() {
    std::lock_guard lock(mutex_);
    cache_data_.file_listing_cache.clear();
}

} // namespace dmake
