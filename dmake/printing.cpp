#include "printing.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unistd.h>

namespace dmake {

bool is_color_enabled(std::ostream& os) {
    static const bool stdout_tty = isatty(STDOUT_FILENO);
    static const bool stderr_tty = isatty(STDERR_FILENO);
    if (&os == &std::cout) return stdout_tty;
    if (&os == &std::cerr) return stderr_tty;
    return false;
}

void print_message(std::ostream& os, std::string_view mode, std::string_view msg,
                   std::string_view indent) {
    bool color = is_color_enabled(os);
    std::string_view prefix, msg_color;

    if (mode == "FATAL_ERROR") {
        prefix = "[FATAL_ERROR]";
        msg_color = colors::BOLD_RED;
    } else if (mode == "SEND_ERROR") {
        prefix = "[SEND_ERROR]";
        msg_color = colors::RED;
    } else if (mode == "WARNING") {
        prefix = "[WARNING]";
        msg_color = colors::YELLOW;
    } else if (mode == "AUTHOR_WARNING") {
        prefix = "[AUTHOR_WARNING]";
        msg_color = colors::YELLOW;
    } else if (mode == "DEPRECATION") {
        prefix = "[DEPRECATION]";
        msg_color = colors::YELLOW;
    } else if (mode == "DEPRECATION_ERROR") {
        prefix = "[DEPRECATION]";
        msg_color = colors::RED;
    } else if (mode == "NOTICE") {
        prefix = "[NOTICE]";
        msg_color = colors::WHITE;
    } else if (mode == "STATUS") {
        prefix = "[STATUS]";
        msg_color = colors::CYAN;
    } else if (mode == "VERBOSE") {
        prefix = "[VERBOSE]";
        msg_color = colors::DIM;
    } else if (mode == "DEBUG") {
        prefix = "[DEBUG]";
        msg_color = colors::DIM_CYAN;
    } else if (mode == "TRACE") {
        prefix = "[TRACE]";
        msg_color = colors::DIM;
    } else if (mode == "CHECK_START") {
        prefix = "--";
        msg_color = colors::CYAN;
    } else if (mode == "CHECK_PASS") {
        prefix = "--";
        msg_color = colors::GREEN;
    } else if (mode == "CHECK_FAIL") {
        prefix = "--";
        msg_color = colors::RED;
    } else {
        prefix = "[INFO]";
        msg_color = "";
    }

    if (color && !msg_color.empty()) {
        os << msg_color << prefix << " " << indent << msg << colors::RESET << std::endl;
    } else {
        os << prefix << " " << indent << msg << std::endl;
    }
}

std::pair<std::string, std::vector<size_t>> expand_tabs(std::string_view line, size_t tab_width) {
    std::string result;
    std::vector<size_t> col_map;  // col_map[i] = visual position of source column i

    for (char c : line) {
        col_map.push_back(result.length());
        if (c == '\t') {
            size_t spaces = tab_width - (result.length() % tab_width);
            result.append(spaces, ' ');
        } else {
            result += c;
        }
    }
    col_map.push_back(result.length());  // For positions at/past end of line
    return {result, col_map};
}

void print_diagnostic(std::ostream& os,
                      DiagnosticSeverity severity,
                      const std::string& message,
                      const std::string& file_path,
                      size_t row, size_t col, size_t offset, size_t length,
                      const std::vector<CallLocation>& backtrace,
                      const std::optional<std::string>& source_content,
                      const std::string& note) {
    bool color = is_color_enabled(os);

    // Severity label
    std::string_view sev_color, sev_label, caret_color;
    switch (severity) {
        case DiagnosticSeverity::Error:
            sev_color = colors::BOLD_RED;
            sev_label = "error:";
            caret_color = colors::BOLD_RED;
            break;
        case DiagnosticSeverity::Warning:
            sev_color = colors::BOLD_YELLOW;
            sev_label = "warning:";
            caret_color = colors::BOLD_YELLOW;
            break;
        case DiagnosticSeverity::Note:
            sev_color = colors::BOLD_CYAN;
            sev_label = "note:";
            caret_color = colors::BOLD_CYAN;
            break;
    }

    // Header line: "error: message"
    if (color) {
        os << sev_color << sev_label << colors::RESET << " " << message << std::endl;
    } else {
        os << sev_label << " " << message << std::endl;
    }

    // Location: "  --> file:row:col"
    if (color) {
        os << "  " << colors::BOLD_BLUE << "-->" << colors::RESET << " ";
    } else {
        os << "  --> ";
    }
    os << file_path << ":" << row << ":" << col << std::endl;

    // Source context
    std::string file_content;
    bool has_content = false;

    if (source_content) {
        file_content = *source_content;
        has_content = true;
    } else {
        std::ifstream error_file(file_path);
        if (error_file) {
            file_content.assign((std::istreambuf_iterator<char>(error_file)),
                                std::istreambuf_iterator<char>());
            has_content = true;
        }
    }

    if (has_content) {
        size_t line_start = 0;
        size_t line_end = 0;

        // Use offset if available to find the line boundaries directly
        if (offset > 0 && offset < file_content.size()) {
            line_start = offset;
            while (line_start > 0 && file_content[line_start - 1] != '\n') {
                line_start--;
            }
            line_end = offset;
            while (line_end < file_content.size() && file_content[line_end] != '\n') {
                line_end++;
            }
        } else {
            // Fallback: count lines from the beginning
            size_t current_line = 1;
            for (size_t i = 0; i < file_content.size() && current_line < row; ++i) {
                if (file_content[i] == '\n') {
                    ++current_line;
                    line_start = i + 1;
                }
            }
            line_end = file_content.find('\n', line_start);
            if (line_end == std::string::npos) line_end = file_content.size();
        }

        std::string line = file_content.substr(line_start, line_end - line_start);
        auto [display_line, col_map] = expand_tabs(line);
        std::string padding(std::to_string(row).length(), ' ');

        if (color) {
            os << "   " << padding << " " << colors::BOLD_BLUE << "|" << colors::RESET << std::endl;
            os << "   " << colors::BOLD_BLUE << row << " |" << colors::RESET << " " << display_line << std::endl;
        } else {
            os << "   " << padding << " |" << std::endl;
            os << "   " << row << " | " << display_line << std::endl;
        }

        size_t caret_col = std::min(col, col_map.size()) - 1;
        size_t visual_start = col_map[caret_col];
        size_t visual_end = (caret_col + length < col_map.size()) ? col_map[caret_col + length] : display_line.length();
        size_t caret_len = std::max(visual_end - visual_start, size_t{1});

        if (color) {
            os << "   " << padding << " " << colors::BOLD_BLUE << "|" << colors::RESET << " "
               << std::string(visual_start, ' ')
               << caret_color << std::string(caret_len, '^') << colors::RESET << std::endl;
        } else {
            os << "   " << padding << " | " << std::string(visual_start, ' ')
               << std::string(caret_len, '^') << std::endl;
        }
    }

    // Note line
    if (!note.empty()) {
        if (color) {
            os << "   " << colors::BOLD_BLUE << "=" << colors::RESET << " note: " << note << std::endl;
        } else {
            os << "   = note: " << note << std::endl;
        }
    }

    // Backtrace
    if (!backtrace.empty()) {
        os << "Call Stack (most recent call first):" << std::endl;
        for (auto it = backtrace.rbegin(); it != backtrace.rend(); ++it) {
            os << "  " << it->file << ":" << it->row << " (" << it->command << ")" << std::endl;
        }
    }
}

} // namespace dmake
