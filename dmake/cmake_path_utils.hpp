#pragma once

#include <string>
#include <string_view>

namespace dmake::path_utils {

// Check if a CMake-style (forward-slash) path is absolute.
// Recognizes Unix (/...), Windows drive (X:/...), and UNC (//server/...) forms.
bool is_absolute(std::string_view path) noexcept;

// Join base and rel with '/'. If rel is absolute, returns rel.
// Does NOT normalize the result.
std::string join(std::string_view base, std::string_view rel);

// Normalize a CMake-style path: remove '.', collapse '..', deduplicate '/'.
// Operates purely on string — no filesystem access.
std::string lexically_normal(std::string_view path);

// Return everything before the last '/' (or empty if no separator).
std::string_view parent_path(std::string_view path) noexcept;

// Return the component after the last '/'.
std::string_view filename(std::string_view path) noexcept;

// Convenience: if path is relative, join(base, path) then normalize; if absolute, normalize.
std::string make_absolute_and_normal(std::string_view base, std::string_view path);

}
