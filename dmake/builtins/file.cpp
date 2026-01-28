#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <regex>
#include <string_view>

namespace dmake {

namespace {

bool matches_glob(const std::string& text, const std::string& pattern) {
    // Convert glob pattern to regex
    // * -> .*
    // ? -> .
    // . -> \.
    std::string rx_str = "^";
    for (char c : pattern) {
        if (c == '*') rx_str += ".*";
        else if (c == '?') rx_str += ".";
        else if (std::string_view(".+^$|()[]{}").find(c) != std::string::npos) {
            rx_str += "\\";
            rx_str += c;
        }
        else {
            rx_str += c;
        }
    }
    rx_str += "$";
    try {
        std::regex rx(rx_str);
        return std::regex_match(text, rx);
    } catch (...) {
        return false;
    }
}

void perform_glob(Interpreter& interp, const std::string& var, const std::vector<std::string>& patterns, bool recurse, const std::string& relative) {
    CMakeList results;
    std::filesystem::path base_path = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");

    for (const auto& pattern : patterns) {
        bool pattern_recurse = recurse;
        std::string search_pattern = pattern;

        // If the pattern contains **, it's a recursive glob
        if (search_pattern.find("**") != std::string::npos) {
            pattern_recurse = true;
        }

        std::filesystem::path p(search_pattern);
        std::filesystem::path search_dir;
        std::string file_glob;

        // Find the first part of the path that contains a wildcard
        std::filesystem::path current_p;
        std::filesystem::path remaining_p;
        bool found_wildcard = false;

        std::filesystem::path pattern_path(search_pattern);
        if (!pattern_path.is_absolute()) {
            pattern_path = base_path / pattern_path;
        }

        // Decompose path to find the base directory for search
        std::vector<std::filesystem::path> components(pattern_path.begin(), pattern_path.end());
        size_t i = 0;
        std::filesystem::path base;
#ifdef _WIN32
        if (i < components.size() && components[i].string().find(':') != std::string::npos) {
             base = components[i++];
             base /= "/";
        }
#else
        if (i < components.size() && components[i] == "/") {
            base = "/";
            i++;
        }
#endif

        while (i < components.size()) {
            std::string s = components[i].string();
            if (s.find('*') != std::string::npos || s.find('?') != std::string::npos) {
                found_wildcard = true;
                break;
            }
            base /= components[i];
            i++;
        }

        search_dir = base;
        // The rest is the pattern to match against
        std::string remaining_pattern;
        bool first = true;
        while (i < components.size()) {
            if (!first) remaining_pattern += "/";
            remaining_pattern += components[i].string();
            i++;
            first = false;
        }

        // If no remaining pattern, we are just looking for the base itself (if it had a wildcard)
        if (remaining_pattern.empty()) {
            remaining_pattern = search_dir.filename().string();
            search_dir = search_dir.parent_path();
        }

        if (!std::filesystem::exists(search_dir)) continue;

        // Simplified: if it contains **, we match the filename against the part after the last **
        // or if no ** we match filename.
        // For a true CMake glob we'd need to match the whole relative path.
        std::string leaf_pattern = remaining_pattern;
        size_t last_slash = remaining_pattern.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            leaf_pattern = remaining_pattern.substr(last_slash + 1);
        }
        // If leaf is **, we match everything
        if (leaf_pattern == "**") leaf_pattern = "*";

        auto process_entry = [&](const std::filesystem::directory_entry& entry) {
            if (entry.is_directory()) return;
            if (matches_glob(entry.path().filename().string(), leaf_pattern)) {
                std::string path_str;
                if (!relative.empty()) {
                    path_str = std::filesystem::relative(entry.path(), relative).string();
                } else {
                    path_str = entry.path().string();
                }
                results.append(path_str);
            }
        };

        if (pattern_recurse) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(search_dir)) {
                process_entry(entry);
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(search_dir)) {
                process_entry(entry);
            }
        }
    }

    interp.set_variable(var, results.to_string());
}
} // namespace

void register_file_builtins(Interpreter& interp) {
    interp.add_builtin("file", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("file() requires at least one argument");
            return;
        }

        std::string operation = args[0];
        std::transform(operation.begin(), operation.end(), operation.begin(), [](unsigned char c){ return std::toupper(c); });
        std::span<const std::string> sub_args(args.begin() + 1, args.end());

        if (operation == "WRITE" || operation == "APPEND") {
            if (sub_args.empty()) {
                interp.set_fatal_error("file(" + operation + ") requires a filename");
                return;
            }
            std::filesystem::path path = sub_args[0];
            if (!path.is_absolute()) {
                path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
            }

            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, (operation == "APPEND") ? std::ios::app : std::ios::trunc);
            if (!file) {
                interp.set_fatal_error("file(" + operation + ") could not open file: " + path.string());
                return;
            }
            for (size_t i = 1; i < sub_args.size(); ++i) {
                file << sub_args[i];
            }
        } else if (operation == "READ") {
            CommandParser parser("file", "READ");
            std::string filename, var;
            parser.add_positional(filename, "filename");
            parser.add_positional(var, "variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            std::filesystem::path path = filename;
            if (!path.is_absolute()) {
                path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
            }

            std::ifstream file(path);
            if (!file) {
                interp.set_fatal_error("file(READ) could not open file: " + path.string());
                return;
            }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            interp.set_variable(var, content);
        } else if (operation == "GLOB" || operation == "GLOB_RECURSE") {
            if (sub_args.empty()) {
                interp.set_fatal_error("file(" + operation + ") requires a variable name");
                return;
            }
            std::string var = sub_args[0];
            std::string relative;
            std::vector<std::string> patterns;

            bool recurse = (operation == "GLOB_RECURSE");

            for (size_t i = 1; i < sub_args.size(); ++i) {
                if (sub_args[i] == "RELATIVE" && i + 1 < sub_args.size()) {
                    relative = sub_args[++i];
                } else if (sub_args[i] == "CONFIGURE_DEPENDS") {
                    // Ignore for now
                } else {
                    patterns.push_back(sub_args[i]);
                }
            }
            perform_glob(interp, var, patterns, recurse, relative);
        } else if (operation == "REAL_PATH") {
            CommandParser parser("file", "REAL_PATH");
            std::string input_path, out_var, base_dir;
            parser.add_positional(input_path, "input path");
            parser.add_positional(out_var, "output variable");
            parser.add_value("BASE_DIRECTORY", base_dir);
            PARSE_OR_RETURN(parser, interp, sub_args);

            std::filesystem::path p = input_path;
            if (!p.is_absolute()) {
                std::filesystem::path base = base_dir.empty() ? interp.get_variable("CMAKE_CURRENT_SOURCE_DIR") : base_dir;
                p = base / p;
            }

            try {
                // lexically_normal + absolute is often what REAL_PATH wants if it doesn't exist,
                // but CMake's REAL_PATH usually resolves symlinks too.
                if (std::filesystem::exists(p)) {
                    interp.set_variable(out_var, std::filesystem::canonical(p).string());
                } else {
                    interp.set_variable(out_var, p.lexically_normal().string());
                }
            } catch (...) {
                interp.set_variable(out_var, p.lexically_normal().string());
            }
        } else if(operation == "REMOVE") {
            if (sub_args.empty()) {
                interp.set_fatal_error("file(REMOVE) requires at least one file path");
                return;
            }

            for (const auto& file : sub_args) {
                std::filesystem::path path = file;
                if (!path.is_absolute()) {
                    path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
                }

                try {
                    if (std::filesystem::exists(path) && !std::filesystem::remove(path)) {
                        interp.set_fatal_error("file(REMOVE) could not remove file: " + path.string());
                    }
                } catch (const std::filesystem::filesystem_error& e) {
                    interp.set_fatal_error("file(REMOVE) encountered an error: " + std::string(e.what()));
                    return;
                }
            }
        } else if (operation == "STRINGS") {
            CommandParser parser("file", "STRINGS");
            std::string filename, var;
            std::string length_min_str, length_max_str;
            std::string limit_count_str, limit_input_str, limit_output_str;
            std::string regex_pattern, encoding;
            bool newline_consume = false;
            bool no_hex_conversion = false;

            parser.add_positional(filename, "filename");
            parser.add_positional(var, "variable");
            parser.add_value("LENGTH_MINIMUM", length_min_str);
            parser.add_value("LENGTH_MAXIMUM", length_max_str);
            parser.add_value("LIMIT_COUNT", limit_count_str);
            parser.add_value("LIMIT_INPUT", limit_input_str);
            parser.add_value("LIMIT_OUTPUT", limit_output_str);
            parser.add_value("REGEX", regex_pattern);
            parser.add_value("ENCODING", encoding);
            parser.add_flag("NEWLINE_CONSUME", newline_consume);
            parser.add_flag("NO_HEX_CONVERSION", no_hex_conversion);

            PARSE_OR_RETURN(parser, interp, sub_args);

            // Parse numeric limits
            size_t length_min = 0;
            size_t length_max = std::numeric_limits<size_t>::max();
            size_t limit_count = std::numeric_limits<size_t>::max();
            size_t limit_input = std::numeric_limits<size_t>::max();
            size_t limit_output = std::numeric_limits<size_t>::max();

            if (!length_min_str.empty()) {
                try {
                    length_min = std::stoull(length_min_str);
                } catch (...) {
                    interp.set_fatal_error("file(STRINGS) LENGTH_MINIMUM must be a number");
                    return;
                }
            }
            if (!length_max_str.empty()) {
                try {
                    length_max = std::stoull(length_max_str);
                } catch (...) {
                    interp.set_fatal_error("file(STRINGS) LENGTH_MAXIMUM must be a number");
                    return;
                }
            }
            if (!limit_count_str.empty()) {
                try {
                    limit_count = std::stoull(limit_count_str);
                } catch (...) {
                    interp.set_fatal_error("file(STRINGS) LIMIT_COUNT must be a number");
                    return;
                }
            }
            if (!limit_input_str.empty()) {
                try {
                    limit_input = std::stoull(limit_input_str);
                } catch (...) {
                    interp.set_fatal_error("file(STRINGS) LIMIT_INPUT must be a number");
                    return;
                }
            }
            if (!limit_output_str.empty()) {
                try {
                    limit_output = std::stoull(limit_output_str);
                } catch (...) {
                    interp.set_fatal_error("file(STRINGS) LIMIT_OUTPUT must be a number");
                    return;
                }
            }

            // Resolve file path
            std::filesystem::path path = filename;
            if (!path.is_absolute()) {
                path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
            }

            // Open file in binary mode
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                interp.set_fatal_error("file(STRINGS) could not open file: " + path.string());
                return;
            }

            // Compile regex if provided
            std::unique_ptr<std::regex> regex_filter;
            if (!regex_pattern.empty()) {
                try {
                    regex_filter = std::make_unique<std::regex>(regex_pattern);
                } catch (const std::regex_error& e) {
                    interp.set_fatal_error("file(STRINGS) invalid REGEX: " + std::string(e.what()));
                    return;
                }
            }

            // TODO: Handle encoding conversions (UTF-16, UTF-32) - for now only support default/UTF-8
            if (!encoding.empty() && encoding != "UTF-8") {
                interp.set_fatal_error("file(STRINGS) ENCODING not yet supported: " + encoding);
                return;
            }

            // Extract strings
            CMakeList results;
            std::string current_string;
            size_t bytes_read = 0;
            size_t output_bytes = 0;
            size_t string_count = 0;

            auto is_printable = [](unsigned char c) {
                // Printable characters: space (32) through ~ (126)
                // Also allow tab (9) if NEWLINE_CONSUME is set
                return (c >= 32 && c <= 126) || c == '\t';
            };

            auto finalize_string = [&]() {
                if (current_string.empty()) return;

                // Apply length filters
                if (current_string.length() < length_min || current_string.length() > length_max) {
                    current_string.clear();
                    return;
                }

                // Apply regex filter
                if (regex_filter && !std::regex_match(current_string, *regex_filter)) {
                    current_string.clear();
                    return;
                }

                // Check limits
                if (string_count >= limit_count) {
                    return;
                }

                size_t new_output = output_bytes + current_string.length();
                if (new_output > limit_output) {
                    return;
                }

                // Add to results
                results.append(current_string);
                output_bytes = new_output;
                string_count++;
                current_string.clear();
            };

            char c;
            while (file.get(c) && bytes_read < limit_input) {
                bytes_read++;
                unsigned char uc = static_cast<unsigned char>(c);

                // Check for string terminators
                if (uc == '\0') {
                    finalize_string();
                    continue;
                }

                if (uc == '\n') {
                    if (newline_consume) {
                        // Treat newline as part of string content
                        if (is_printable(uc) || uc == '\n' || uc == '\r') {
                            current_string += c;
                        }
                    } else {
                        // Newline terminates the string
                        finalize_string();
                    }
                    continue;
                }

                // Carriage return handling (usually paired with newline in Windows line endings)
                if (uc == '\r') {
                    if (newline_consume) {
                        current_string += c;
                    } else {
                        // Peek ahead to see if this is part of CRLF
                        if (file.peek() == '\n') {
                            finalize_string();
                            continue;
                        }
                        // Standalone CR - treat as part of string
                        if (is_printable(uc)) {
                            current_string += c;
                        } else {
                            // Non-printable CR terminates string
                            finalize_string();
                        }
                    }
                    continue;
                }

                // Regular character
                if (is_printable(uc)) {
                    current_string += c;
                } else {
                    // Non-printable character terminates the string
                    finalize_string();
                }

                // Check if we've hit the string count limit
                if (string_count >= limit_count) {
                    break;
                }
            }

            // Finalize any remaining string
            finalize_string();

            interp.set_variable(var, results.to_string());
        } else {
            interp.set_fatal_error("file() sub-command not implemented: " + operation);
        }
    });
}

} // namespace dmake
