#include "cache_store.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include "glaze/glaze.hpp"

namespace dmake {

CacheStore::CacheStore(const std::filesystem::path& cache_file)
    : cache_file_(cache_file) {
}

std::expected<void, std::string> CacheStore::load() {
    std::lock_guard lock(mutex_);

    // If cache file doesn't exist, start with empty cache
    if (!std::filesystem::exists(cache_file_)) {
        cache_data_ = CacheRoot{};
        return {};
    }

    // Read file contents
    std::ifstream file(cache_file_);
    if (!file) {
        return std::unexpected("Failed to open cache file: " + cache_file_.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_str = buffer.str();

    // Parse JSON using Glaze
    auto parse_result = glz::read_json(cache_data_, json_str);
    if (parse_result) {
        // Parse error - start with empty cache (graceful degradation)
        cache_data_ = CacheRoot{};
        return std::unexpected("Cache file corrupted, starting fresh: " + glz::format_error(parse_result, json_str));
    }

    return {};
}

std::expected<void, std::string> CacheStore::save() {
    std::lock_guard lock(mutex_);

    // Create parent directory if needed
    std::filesystem::path parent = cache_file_.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return std::unexpected("Failed to create cache directory: " + ec.message());
        }
    }

    // Serialize to JSON
    std::string json_str;
    auto write_result = glz::write_json(cache_data_, json_str);
    if (write_result) {
        return std::unexpected("Failed to serialize cache: " + glz::format_error(write_result));
    }

    // Write to temporary file first (atomic operation)
    std::filesystem::path temp_file = cache_file_.string() + ".tmp";
    std::ofstream file(temp_file);
    if (!file) {
        return std::unexpected("Failed to open temp cache file: " + temp_file.string());
    }

    file << json_str;
    file.close();

    if (!file) {
        return std::unexpected("Failed to write temp cache file: " + temp_file.string());
    }

    // Atomic rename (POSIX guarantee)
    std::error_code ec;
    std::filesystem::rename(temp_file, cache_file_, ec);
    if (ec) {
        return std::unexpected("Failed to rename cache file: " + ec.message());
    }

    return {};
}

} // namespace dmake
