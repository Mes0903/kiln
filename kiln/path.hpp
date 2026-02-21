#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace kiln {

// Lightweight path class that wraps std::string and provides path operations
// via string manipulation. No filesystem parsing overhead — construction is
// just a string move/copy. Methods delegate to the same logic as cmake_path_utils
// but with a nicer API.
//
// Semantic rule: when a method shares a name with std::filesystem::path,
// it must produce identical results for all valid Unix paths.
class Path {
    std::string path_;

public:
    Path() = default;
    Path(std::string s) : path_(std::move(s)) {}
    Path(std::string_view sv) : path_(sv) {}
    Path(const char* s) : path_(s) {}

    // Implicit access — zero cost
    const std::string& str() const noexcept { return path_; }
    std::string_view view() const noexcept { return path_; }
    const char* c_str() const noexcept { return path_.c_str(); }
    bool empty() const noexcept { return path_.empty(); }

    // String-based path operations (no allocation for views)
    std::string_view filename() const noexcept;
    std::string_view parent_path() const noexcept;
    std::string_view stem() const noexcept;
    std::string_view extension() const noexcept;
    bool has_extension() const noexcept;
    bool is_absolute() const noexcept;
    bool is_relative() const noexcept { return !is_absolute(); }

    // Return the path with root stripped (e.g. "/foo/bar" → "foo/bar")
    std::string_view relative_path() const noexcept;

    // Operations that return new Paths (allocate)
    Path operator/(std::string_view rhs) const;
    Path lexically_normal() const;
    Path replace_extension(std::string_view new_ext) const;

    // Escape hatch for real filesystem operations
    std::filesystem::path fs_path() const { return std::filesystem::path(path_); }

    // Move the string out when done
    std::string&& release() && { return std::move(path_); }

    // Comparison
    bool operator==(const Path& other) const noexcept { return path_ == other.path_; }
    bool operator==(std::string_view sv) const noexcept { return path_ == sv; }
    bool operator!=(const Path& other) const noexcept { return path_ != other.path_; }
    bool operator!=(std::string_view sv) const noexcept { return path_ != sv; }

    // Static helpers (replace free functions from path_utils)
    static std::string join(std::string_view base, std::string_view rel);
    static std::string make_absolute_and_normal(std::string_view base, std::string_view path);

    // Resolve a path to absolute using cwd (no std::filesystem overhead).
    // Returns empty string on failure (e.g. getcwd fails).
    static std::string absolute(std::string_view path);
};

} // namespace kiln
