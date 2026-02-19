#include "cmake_path_utils.hpp"

namespace dmake::path_utils {

bool is_absolute(std::string_view path) noexcept {
    if (path.empty()) return false;
    // Unix absolute
    if (path[0] == '/') return true;
    // Windows drive letter: X:/ or X:backslash (but we use cmake-style forward slashes)
    if (path.size() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':' && path[2] == '/') return true;
    return false;
}

std::string join(std::string_view base, std::string_view rel) {
    if (rel.empty()) return std::string(base);
    if (base.empty()) return std::string(rel);
    if (is_absolute(rel)) return std::string(rel);

    std::string result;
    result.reserve(base.size() + 1 + rel.size());
    result.append(base);
    if (result.back() != '/') result += '/';
    result.append(rel);
    return result;
}

// Parse the root prefix length: "/" for Unix, "X:/" for drive, "//" for UNC.
static size_t root_prefix_len(std::string_view path) noexcept {
    if (path.empty()) return 0;
    // Windows drive: X:/
    if (path.size() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':' && path[2] == '/') {
        return 3;
    }
    // UNC: //server (at least //x)
    if (path.size() >= 3 && path[0] == '/' && path[1] == '/') {
        // Find end of server component
        auto pos = path.find('/', 2);
        if (pos == std::string_view::npos) return path.size(); // entire path is //server
        return pos + 1; // include the '/' after server
    }
    // Unix root
    if (path[0] == '/') return 1;
    return 0;
}

std::string lexically_normal(std::string_view path) {
    if (path.empty()) return ".";

    size_t root_len = root_prefix_len(path);
    std::string_view root = path.substr(0, root_len);
    std::string_view rest = path.substr(root_len);

    // Split into components and process
    // Use a stack-like approach with a small vector
    struct Component { std::string_view sv; };
    Component stack[256]; // stack-allocated, plenty for any real path
    size_t stack_size = 0;

    size_t pos = 0;
    while (pos < rest.size()) {
        // Skip consecutive slashes
        while (pos < rest.size() && rest[pos] == '/') ++pos;
        if (pos >= rest.size()) break;

        // Find end of component
        size_t end = pos;
        while (end < rest.size() && rest[end] != '/') ++end;

        auto comp = rest.substr(pos, end - pos);
        pos = end;

        if (comp == ".") {
            // Skip
        } else if (comp == "..") {
            if (stack_size > 0 && stack[stack_size - 1].sv != "..") {
                --stack_size; // Pop parent
            } else if (root_len == 0) {
                // Relative path: keep the ".."
                if (stack_size < 256) stack[stack_size++] = {comp};
            }
            // If we have a root, ".." past root is just ignored
        } else {
            if (stack_size < 256) stack[stack_size++] = {comp};
        }
    }

    // Reconstruct
    std::string result;
    result.reserve(path.size());
    result.append(root);

    for (size_t i = 0; i < stack_size; ++i) {
        if (i > 0 || root_len > 0) {
            // Don't add leading '/' for relative paths, but do add separator between components
            if (i > 0 || (root_len > 0 && !root.empty() && root.back() != '/'))
                result += '/';
        }
        result.append(stack[i].sv);
    }

    // Handle degenerate cases
    if (result.empty()) return ".";
    // Root-only: ensure single "/" not empty
    if (result == root && root_len > 0) return std::string(root);

    return result;
}

std::string_view parent_path(std::string_view path) noexcept {
    if (path.empty()) return {};

    // Remove trailing slash (unless it's the root)
    size_t root_len = root_prefix_len(path);
    size_t end = path.size();
    while (end > root_len && path[end - 1] == '/') --end;

    // Find last '/'
    size_t pos = path.rfind('/', end - 1);
    if (pos == std::string_view::npos || pos < root_len) {
        if (root_len > 0) return path.substr(0, root_len);
        return {};
    }
    // Don't return empty for root
    if (pos == 0) return path.substr(0, 1);
    return path.substr(0, pos);
}

std::string_view filename(std::string_view path) noexcept {
    if (path.empty()) return {};

    // Remove trailing slashes
    size_t end = path.size();
    while (end > 0 && path[end - 1] == '/') --end;
    if (end == 0) return {};

    size_t pos = path.rfind('/', end - 1);
    if (pos == std::string_view::npos) return path.substr(0, end);
    return path.substr(pos + 1, end - pos - 1);
}

std::string make_absolute_and_normal(std::string_view base, std::string_view path) {
    if (is_absolute(path)) return lexically_normal(path);
    return lexically_normal(join(base, path));
}

}
