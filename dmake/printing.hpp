#pragma once

#include "cmake-language.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <ostream>
#include <utility>

namespace dmake {

// ANSI escape codes for colors
namespace colors {
    inline constexpr std::string_view RESET = "\033[0m";
    inline constexpr std::string_view RED = "\033[31m";
    inline constexpr std::string_view BRIGHT_RED = "\033[91m";
    inline constexpr std::string_view BOLD_RED = "\033[1;31m";
    inline constexpr std::string_view YELLOW = "\033[33m";
    inline constexpr std::string_view BOLD_YELLOW = "\033[1;33m";
    inline constexpr std::string_view GREEN = "\033[32m";
    inline constexpr std::string_view CYAN = "\033[36m";
    inline constexpr std::string_view BOLD_CYAN = "\033[1;36m";
    inline constexpr std::string_view WHITE = "\033[37m";
    inline constexpr std::string_view DIM = "\033[2m";
    inline constexpr std::string_view DIM_CYAN = "\033[2;36m";
    inline constexpr std::string_view MAGENTA = "\033[35m";
    inline constexpr std::string_view BOLD_BLUE = "\033[1;34m";
    inline constexpr std::string_view BOLD_GREEN = "\033[1;32m";
    inline constexpr std::string_view BOLD_MAGENTA = "\033[1;35m";
} // namespace colors

// Check if an output stream is a terminal (cached, one isatty() call per fd)
bool is_color_enabled(std::ostream& os);

// Return color code if stream is a tty, empty string_view otherwise
inline std::string_view c(std::ostream& os, std::string_view code) {
    return is_color_enabled(os) ? code : std::string_view{};
}

// Print a CMake-style message (e.g. [STATUS], [WARNING], etc.)
void print_message(std::ostream& os, std::string_view mode, std::string_view msg,
                   std::string_view indent = "");

// Expand tabs to spaces (4-column tab stops) and return mapping from source column to visual column
std::pair<std::string, std::vector<size_t>> expand_tabs(std::string_view line, size_t tab_width = 4);

enum class DiagnosticSeverity { Error, Warning, Note };

// Print a Rust-style diagnostic with source context
void print_diagnostic(std::ostream& os,
                      DiagnosticSeverity severity,
                      const std::string& message,
                      const std::string& file_path,
                      size_t row, size_t col, size_t offset, size_t length,
                      const std::vector<CallLocation>& backtrace = {},
                      const std::optional<std::string>& source_content = std::nullopt,
                      const std::string& note = "");

} // namespace dmake
