#include "registry.hpp"
#include "../interperter.hpp"
#include "../profiler.hpp"
#include "../command_parser.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <regex>
#include <string_view>
#include <ctime>
#include <chrono>

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
    CMakeArray results;
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
            parser.positional(filename, "filename");
            parser.positional(var, "variable");
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
            dmake::ProfileScope configure_profile("glob", "configure");
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
            parser.positional(input_path, "input path");
            parser.positional(out_var, "output variable");
            parser.value("BASE_DIRECTORY", base_dir);
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

            parser.positional(filename, "filename");
            parser.positional(var, "variable");
            parser.value("LENGTH_MINIMUM", length_min_str);
            parser.value("LENGTH_MAXIMUM", length_max_str);
            parser.value("LIMIT_COUNT", limit_count_str);
            parser.value("LIMIT_INPUT", limit_input_str);
            parser.value("LIMIT_OUTPUT", limit_output_str);
            parser.value("REGEX", regex_pattern);
            parser.value("ENCODING", encoding);
            parser.flag("NEWLINE_CONSUME", newline_consume);
            parser.flag("NO_HEX_CONVERSION", no_hex_conversion);

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
            CMakeArray results;
            std::string current_string;
            size_t bytes_read = 0;
            size_t output_bytes = 0;
            size_t string_count = 0;
            std::vector<std::string> last_match_groups;  // For CMAKE_MATCH_* variables

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

                // Apply regex filter - use regex_search for substring matching (not full match)
                if (regex_filter) {
                    std::smatch match;
                    if (!std::regex_search(current_string, match, *regex_filter)) {
                        current_string.clear();
                        return;
                    }
                    // Store match groups for CMAKE_MATCH_* variables
                    last_match_groups.clear();
                    for (size_t i = 0; i < match.size(); ++i) {
                        last_match_groups.push_back(match[i].str());
                    }
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

            // Set CMAKE_MATCH_* variables from the last regex match (if any)
            if (!last_match_groups.empty()) {
                interp.set_variable("CMAKE_MATCH_COUNT", std::to_string(last_match_groups.size() - 1));
                for (size_t i = 0; i < last_match_groups.size() && i < 10; ++i) {
                    interp.set_variable("CMAKE_MATCH_" + std::to_string(i), last_match_groups[i]);
                }
            }
        } else if (operation == "MAKE_DIRECTORY") {
            // file(MAKE_DIRECTORY <directories>...)
            // Creates the specified directories, including parent directories if needed
            if (sub_args.empty()) {
                // No directories specified - this is valid in CMake (no-op)
                return;
            }

            for (const auto& dir : sub_args) {
                std::filesystem::path path = dir;
                if (!path.is_absolute()) {
                    path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
                }

                std::error_code ec;
                std::filesystem::create_directories(path, ec);
                if (ec) {
                    interp.set_fatal_error("file(MAKE_DIRECTORY) failed to create directory '" +
                                          path.string() + "': " + ec.message());
                    return;
                }
            }
        } else if (operation == "TOUCH") {
            // file(TOUCH <files>...)
            // Creates files if they don't exist, updates timestamp if they do
            for (const auto& file_arg : sub_args) {
                std::filesystem::path path = file_arg;
                if (!path.is_absolute()) {
                    path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
                }

                // Create parent directories if needed
                if (path.has_parent_path()) {
                    std::error_code ec;
                    std::filesystem::create_directories(path.parent_path(), ec);
                }

                if (!std::filesystem::exists(path)) {
                    // Create empty file
                    std::ofstream ofs(path);
                    if (!ofs) {
                        interp.set_fatal_error("file(TOUCH) could not create file: " + path.string());
                        return;
                    }
                } else {
                    // Update timestamp
                    std::error_code ec;
                    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
                    if (ec) {
                        interp.set_fatal_error("file(TOUCH) could not update timestamp: " + path.string());
                        return;
                    }
                }
            }
        } else if (operation == "TOUCH_NOCREATE") {
            // file(TOUCH_NOCREATE <files>...)
            // Updates timestamp only if file exists, silently ignores non-existent files
            for (const auto& file_arg : sub_args) {
                std::filesystem::path path = file_arg;
                if (!path.is_absolute()) {
                    path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
                }

                if (std::filesystem::exists(path)) {
                    std::error_code ec;
                    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
                    // Silently ignore errors per CMake behavior
                }
            }
        } else if (operation == "REMOVE_RECURSE") {
            // file(REMOVE_RECURSE <files>...)
            // Recursively removes files and directories
            for (const auto& file_arg : sub_args) {
                std::filesystem::path path = file_arg;
                if (!path.is_absolute()) {
                    path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
                }

                std::error_code ec;
                std::filesystem::remove_all(path, ec);
                // CMake silently ignores errors for non-existent paths
            }
        } else if (operation == "RENAME") {
            // file(RENAME <oldname> <newname> [RESULT <result>] [NO_REPLACE])
            CommandParser parser("file", "RENAME");
            std::string oldname, newname, result_var;
            bool no_replace = false;
            parser.positional(oldname, "old name");
            parser.positional(newname, "new name");
            parser.value("RESULT", result_var);
            parser.flag("NO_REPLACE", no_replace);
            PARSE_OR_RETURN(parser, interp, sub_args);

            std::filesystem::path old_path = oldname;
            std::filesystem::path new_path = newname;
            if (!old_path.is_absolute()) {
                old_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / old_path;
            }
            if (!new_path.is_absolute()) {
                new_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / new_path;
            }

            std::error_code ec;
            if (no_replace && std::filesystem::exists(new_path)) {
                if (!result_var.empty()) {
                    interp.set_variable(result_var, "File already exists");
                } else {
                    interp.set_fatal_error("file(RENAME) destination already exists: " + new_path.string());
                }
                return;
            }

            std::filesystem::rename(old_path, new_path, ec);
            if (ec) {
                if (!result_var.empty()) {
                    interp.set_variable(result_var, ec.message());
                } else {
                    interp.set_fatal_error("file(RENAME) failed: " + ec.message());
                }
                return;
            }
            if (!result_var.empty()) {
                interp.set_variable(result_var, "0");
            }
        } else if (operation == "COPY_FILE") {
            // file(COPY_FILE <oldname> <newname> [RESULT <result>] [ONLY_IF_DIFFERENT])
            CommandParser parser("file", "COPY_FILE");
            std::string oldname, newname, result_var;
            bool only_if_different = false;
            parser.positional(oldname, "source file");
            parser.positional(newname, "destination file");
            parser.value("RESULT", result_var);
            parser.flag("ONLY_IF_DIFFERENT", only_if_different);
            PARSE_OR_RETURN(parser, interp, sub_args);

            std::filesystem::path src_path = oldname;
            std::filesystem::path dst_path = newname;
            if (!src_path.is_absolute()) {
                src_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / src_path;
            }
            if (!dst_path.is_absolute()) {
                dst_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / dst_path;
            }

            // Create destination directory if needed
            if (dst_path.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(dst_path.parent_path(), ec);
            }

            std::error_code ec;
            auto copy_opts = std::filesystem::copy_options::overwrite_existing;

            if (only_if_different && std::filesystem::exists(dst_path)) {
                // Compare file sizes first (quick check)
                if (std::filesystem::file_size(src_path, ec) == std::filesystem::file_size(dst_path, ec)) {
                    // Compare contents
                    std::ifstream src_file(src_path, std::ios::binary);
                    std::ifstream dst_file(dst_path, std::ios::binary);
                    if (src_file && dst_file) {
                        std::string src_content((std::istreambuf_iterator<char>(src_file)), std::istreambuf_iterator<char>());
                        std::string dst_content((std::istreambuf_iterator<char>(dst_file)), std::istreambuf_iterator<char>());
                        if (src_content == dst_content) {
                            if (!result_var.empty()) {
                                interp.set_variable(result_var, "0");
                            }
                            return;  // Files are identical, skip copy
                        }
                    }
                }
            }

            std::filesystem::copy_file(src_path, dst_path, copy_opts, ec);
            if (ec) {
                if (!result_var.empty()) {
                    interp.set_variable(result_var, ec.message());
                } else {
                    interp.set_fatal_error("file(COPY_FILE) failed: " + ec.message());
                }
                return;
            }
            if (!result_var.empty()) {
                interp.set_variable(result_var, "0");
            }
        } else if (operation == "SIZE") {
            // file(SIZE <filename> <variable>)
            CommandParser parser("file", "SIZE");
            std::string filename, var;
            parser.positional(filename, "filename");
            parser.positional(var, "variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            std::filesystem::path path = filename;
            if (!path.is_absolute()) {
                path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
            }

            std::error_code ec;
            auto size = std::filesystem::file_size(path, ec);
            if (ec) {
                interp.set_fatal_error("file(SIZE) could not determine size of: " + path.string());
                return;
            }
            interp.set_variable(var, std::to_string(size));
        } else if (operation == "READ_SYMLINK") {
            // file(READ_SYMLINK <linkname> <variable>)
            CommandParser parser("file", "READ_SYMLINK");
            std::string linkname, var;
            parser.positional(linkname, "link name");
            parser.positional(var, "variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            std::filesystem::path path = linkname;
            if (!path.is_absolute()) {
                path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
            }

            std::error_code ec;
            auto target = std::filesystem::read_symlink(path, ec);
            if (ec) {
                interp.set_fatal_error("file(READ_SYMLINK) failed: " + path.string() + " is not a symlink or cannot be read");
                return;
            }
            interp.set_variable(var, target.string());
        } else if (operation == "CREATE_LINK") {
            // file(CREATE_LINK <original> <linkname> [RESULT <result>] [COPY_ON_ERROR] [SYMBOLIC])
            CommandParser parser("file", "CREATE_LINK");
            std::string original, linkname, result_var;
            bool copy_on_error = false, symbolic = false;
            parser.positional(original, "original");
            parser.positional(linkname, "link name");
            parser.value("RESULT", result_var);
            parser.flag("COPY_ON_ERROR", copy_on_error);
            parser.flag("SYMBOLIC", symbolic);
            PARSE_OR_RETURN(parser, interp, sub_args);

            std::filesystem::path orig_path = original;
            std::filesystem::path link_path = linkname;
            if (!orig_path.is_absolute()) {
                orig_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / orig_path;
            }
            if (!link_path.is_absolute()) {
                link_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / link_path;
            }

            // Create parent directories
            if (link_path.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(link_path.parent_path(), ec);
            }

            std::error_code ec;
            if (symbolic) {
                std::filesystem::create_symlink(orig_path, link_path, ec);
            } else {
                std::filesystem::create_hard_link(orig_path, link_path, ec);
            }

            if (ec) {
                if (copy_on_error) {
                    std::filesystem::copy_file(orig_path, link_path, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec) {
                        if (!result_var.empty()) {
                            interp.set_variable(result_var, ec.message());
                        } else {
                            interp.set_fatal_error("file(CREATE_LINK) failed and copy fallback also failed: " + ec.message());
                        }
                        return;
                    }
                } else {
                    if (!result_var.empty()) {
                        interp.set_variable(result_var, ec.message());
                    } else {
                        interp.set_fatal_error("file(CREATE_LINK) failed: " + ec.message());
                    }
                    return;
                }
            }
            if (!result_var.empty()) {
                interp.set_variable(result_var, "0");
            }
        } else if (operation == "RELATIVE_PATH") {
            // file(RELATIVE_PATH <variable> <directory> <file>)
            CommandParser parser("file", "RELATIVE_PATH");
            std::string var, directory, file_path;
            parser.positional(var, "variable");
            parser.positional(directory, "directory");
            parser.positional(file_path, "file");
            PARSE_OR_RETURN(parser, interp, sub_args);

            std::filesystem::path dir_p = directory;
            std::filesystem::path file_p = file_path;
            if (!dir_p.is_absolute()) {
                dir_p = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / dir_p;
            }
            if (!file_p.is_absolute()) {
                file_p = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / file_p;
            }

            auto rel = std::filesystem::relative(file_p, dir_p);
            interp.set_variable(var, rel.string());
        } else if (operation == "TO_CMAKE_PATH") {
            // file(TO_CMAKE_PATH "<path>" <variable>)
            // Converts native path separators to forward slashes
            if (sub_args.size() < 2) {
                interp.set_fatal_error("file(TO_CMAKE_PATH) requires path and variable arguments");
                return;
            }
            std::string path = sub_args[0];
            std::string var = sub_args[1];

            // Replace backslashes with forward slashes
            std::replace(path.begin(), path.end(), '\\', '/');
            interp.set_variable(var, path);
        } else if (operation == "TO_NATIVE_PATH") {
            // file(TO_NATIVE_PATH "<path>" <variable>)
            // Converts to native path separators
            if (sub_args.size() < 2) {
                interp.set_fatal_error("file(TO_NATIVE_PATH) requires path and variable arguments");
                return;
            }
            std::string path = sub_args[0];
            std::string var = sub_args[1];

#ifdef _WIN32
            // Convert forward slashes to backslashes on Windows
            std::replace(path.begin(), path.end(), '/', '\\');
#endif
            // On Unix, path separators are already forward slashes
            interp.set_variable(var, path);
        } else if (operation == "TIMESTAMP") {
            // file(TIMESTAMP <filename> <variable> [<format>] [UTC])
            if (sub_args.size() < 2) {
                interp.set_fatal_error("file(TIMESTAMP) requires filename and variable arguments");
                return;
            }
            std::string filename = sub_args[0];
            std::string var = sub_args[1];
            std::string format = "%Y-%m-%dT%H:%M:%S";
            bool utc = false;

            for (size_t i = 2; i < sub_args.size(); ++i) {
                if (sub_args[i] == "UTC") {
                    utc = true;
                } else {
                    format = sub_args[i];
                }
            }

            std::filesystem::path path = filename;
            if (!path.is_absolute()) {
                path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
            }

            std::error_code ec;
            auto ftime = std::filesystem::last_write_time(path, ec);
            if (ec) {
                interp.set_variable(var, "");
                return;
            }

            // Convert file_time to system_clock time
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            auto time_t_val = std::chrono::system_clock::to_time_t(sctp);

            std::tm* tm_ptr = utc ? std::gmtime(&time_t_val) : std::localtime(&time_t_val);
            if (!tm_ptr) {
                interp.set_variable(var, "");
                return;
            }

            char buffer[256];
            if (std::strftime(buffer, sizeof(buffer), format.c_str(), tm_ptr) == 0) {
                interp.set_variable(var, "");
                return;
            }
            interp.set_variable(var, buffer);
        } else if (operation == "COPY" || operation == "INSTALL") {
            // file(COPY <files>... DESTINATION <dir> [options...])
            // file(INSTALL <files>... DESTINATION <dir> [options...])
            std::vector<std::string> files;
            std::string destination;
            bool files_matching = false;
            std::vector<std::string> patterns;
            std::vector<std::string> regexes;

            // Parse arguments
            size_t i = 0;
            while (i < sub_args.size()) {
                if (sub_args[i] == "DESTINATION" && i + 1 < sub_args.size()) {
                    destination = sub_args[++i];
                } else if (sub_args[i] == "FILES_MATCHING") {
                    files_matching = true;
                } else if (sub_args[i] == "PATTERN" && i + 1 < sub_args.size()) {
                    patterns.push_back(sub_args[++i]);
                } else if (sub_args[i] == "REGEX" && i + 1 < sub_args.size()) {
                    regexes.push_back(sub_args[++i]);
                } else if (sub_args[i] == "NO_SOURCE_PERMISSIONS" ||
                           sub_args[i] == "USE_SOURCE_PERMISSIONS" ||
                           sub_args[i] == "FOLLOW_SYMLINK_CHAIN" ||
                           sub_args[i] == "FILE_PERMISSIONS" ||
                           sub_args[i] == "DIRECTORY_PERMISSIONS" ||
                           sub_args[i] == "EXCLUDE") {
                    // Skip these options and their values for now
                    if ((sub_args[i] == "FILE_PERMISSIONS" || sub_args[i] == "DIRECTORY_PERMISSIONS") &&
                        i + 1 < sub_args.size()) {
                        // Skip permission values
                        ++i;
                        while (i + 1 < sub_args.size() &&
                               (sub_args[i + 1].find("OWNER_") == 0 ||
                                sub_args[i + 1].find("GROUP_") == 0 ||
                                sub_args[i + 1].find("WORLD_") == 0)) {
                            ++i;
                        }
                    }
                } else {
                    files.push_back(sub_args[i]);
                }
                ++i;
            }

            if (destination.empty()) {
                interp.set_fatal_error("file(" + operation + ") requires DESTINATION");
                return;
            }

            std::filesystem::path dest_path = destination;
            if (!dest_path.is_absolute()) {
                dest_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / dest_path;
            }

            // Create destination directory
            std::error_code ec;
            std::filesystem::create_directories(dest_path, ec);

            auto should_copy = [&](const std::filesystem::path& p) -> bool {
                if (patterns.empty() && regexes.empty()) return true;
                if (files_matching && patterns.empty() && regexes.empty()) return false;

                std::string filename = p.filename().string();
                for (const auto& pattern : patterns) {
                    if (matches_glob(filename, pattern)) return true;
                }
                for (const auto& rx_str : regexes) {
                    try {
                        std::regex rx(rx_str);
                        if (std::regex_search(filename, rx)) return true;
                    } catch (...) {}
                }
                return patterns.empty() && regexes.empty();
            };

            for (const auto& file_arg : files) {
                std::filesystem::path src = file_arg;
                if (!src.is_absolute()) {
                    src = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / src;
                }

                if (std::filesystem::is_directory(src)) {
                    // Copy directory recursively
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
                        if (entry.is_regular_file() && should_copy(entry.path())) {
                            auto rel = std::filesystem::relative(entry.path(), src);
                            auto target = dest_path / src.filename() / rel;
                            std::filesystem::create_directories(target.parent_path(), ec);
                            std::filesystem::copy_file(entry.path(), target,
                                std::filesystem::copy_options::overwrite_existing, ec);
                        }
                    }
                } else if (std::filesystem::exists(src)) {
                    if (should_copy(src)) {
                        auto target = dest_path / src.filename();
                        std::filesystem::copy_file(src, target,
                            std::filesystem::copy_options::overwrite_existing, ec);
                    }
                }
            }
        } else if (operation == "CHMOD" || operation == "CHMOD_RECURSE") {
            // file(CHMOD <files>... [PERMISSIONS <perms>...] [FILE_PERMISSIONS <perms>...] [DIRECTORY_PERMISSIONS <perms>...])
            std::vector<std::string> files;
            std::filesystem::perms file_perms = std::filesystem::perms::none;
            std::filesystem::perms dir_perms = std::filesystem::perms::none;
            bool has_file_perms = false;
            bool has_dir_perms = false;

            auto parse_permission = [](const std::string& perm) -> std::filesystem::perms {
                if (perm == "OWNER_READ") return std::filesystem::perms::owner_read;
                if (perm == "OWNER_WRITE") return std::filesystem::perms::owner_write;
                if (perm == "OWNER_EXECUTE") return std::filesystem::perms::owner_exec;
                if (perm == "GROUP_READ") return std::filesystem::perms::group_read;
                if (perm == "GROUP_WRITE") return std::filesystem::perms::group_write;
                if (perm == "GROUP_EXECUTE") return std::filesystem::perms::group_exec;
                if (perm == "WORLD_READ") return std::filesystem::perms::others_read;
                if (perm == "WORLD_WRITE") return std::filesystem::perms::others_write;
                if (perm == "WORLD_EXECUTE") return std::filesystem::perms::others_exec;
                if (perm == "SETUID") return std::filesystem::perms::set_uid;
                if (perm == "SETGID") return std::filesystem::perms::set_gid;
                return std::filesystem::perms::none;
            };

            auto is_permission = [](const std::string& s) -> bool {
                return s.find("OWNER_") == 0 || s.find("GROUP_") == 0 ||
                       s.find("WORLD_") == 0 || s == "SETUID" || s == "SETGID";
            };

            // Parse arguments
            size_t i = 0;
            while (i < sub_args.size()) {
                if (sub_args[i] == "PERMISSIONS") {
                    ++i;
                    while (i < sub_args.size() && is_permission(sub_args[i])) {
                        auto p = parse_permission(sub_args[i]);
                        file_perms |= p;
                        dir_perms |= p;
                        has_file_perms = has_dir_perms = true;
                        ++i;
                    }
                } else if (sub_args[i] == "FILE_PERMISSIONS") {
                    ++i;
                    while (i < sub_args.size() && is_permission(sub_args[i])) {
                        file_perms |= parse_permission(sub_args[i]);
                        has_file_perms = true;
                        ++i;
                    }
                } else if (sub_args[i] == "DIRECTORY_PERMISSIONS") {
                    ++i;
                    while (i < sub_args.size() && is_permission(sub_args[i])) {
                        dir_perms |= parse_permission(sub_args[i]);
                        has_dir_perms = true;
                        ++i;
                    }
                } else {
                    files.push_back(sub_args[i]);
                    ++i;
                }
            }

            auto apply_chmod = [&](const std::filesystem::path& p) {
                std::error_code ec;
                if (std::filesystem::is_directory(p)) {
                    if (has_dir_perms) {
                        std::filesystem::permissions(p, dir_perms, std::filesystem::perm_options::replace, ec);
                    }
                } else {
                    if (has_file_perms) {
                        std::filesystem::permissions(p, file_perms, std::filesystem::perm_options::replace, ec);
                    }
                }
            };

            for (const auto& file_arg : files) {
                std::filesystem::path path = file_arg;
                if (!path.is_absolute()) {
                    path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / path;
                }

                if (operation == "CHMOD_RECURSE" && std::filesystem::is_directory(path)) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                        apply_chmod(entry.path());
                    }
                    apply_chmod(path);  // Also apply to the directory itself
                } else {
                    apply_chmod(path);
                }
            }
        } else if (operation == "CONFIGURE") {
            // file(CONFIGURE OUTPUT <output-file> CONTENT <content> [ESCAPE_QUOTES] [@ONLY] [NEWLINE_STYLE ...])
            CommandParser parser("file", "CONFIGURE");
            std::string output_file, content, newline_style;
            bool escape_quotes = false, at_only = false;
            parser.value("OUTPUT", output_file);
            parser.value("CONTENT", content);
            parser.value("NEWLINE_STYLE", newline_style);
            parser.flag("ESCAPE_QUOTES", escape_quotes);
            parser.flag("@ONLY", at_only);
            PARSE_OR_RETURN(parser, interp, sub_args);

            if (output_file.empty()) {
                interp.set_fatal_error("file(CONFIGURE) requires OUTPUT");
                return;
            }
            if (content.empty()) {
                // Content can be empty string, but the argument must be present
                // Check if CONTENT was actually provided
                bool has_content = false;
                for (size_t i = 0; i < sub_args.size(); ++i) {
                    if (sub_args[i] == "CONTENT") {
                        has_content = true;
                        break;
                    }
                }
                if (!has_content) {
                    interp.set_fatal_error("file(CONFIGURE) requires CONTENT");
                    return;
                }
            }

            std::filesystem::path out_path = output_file;
            if (!out_path.is_absolute()) {
                out_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_BINARY_DIR")) / out_path;
            }

            // Perform variable substitution
            std::string result = content;

            // Replace @VAR@ patterns
            std::regex at_var_regex("@([A-Za-z_][A-Za-z0-9_]*)@");
            std::string temp;
            std::sregex_iterator it(result.begin(), result.end(), at_var_regex);
            std::sregex_iterator end;
            size_t last_pos = 0;
            temp.reserve(result.size());

            for (; it != end; ++it) {
                temp.append(result, last_pos, it->position() - last_pos);
                std::string var_name = (*it)[1].str();
                std::string var_value = interp.get_variable(var_name);
                if (escape_quotes) {
                    // Escape backslashes and quotes
                    std::string escaped;
                    for (char c : var_value) {
                        if (c == '\\' || c == '"') escaped += '\\';
                        escaped += c;
                    }
                    var_value = escaped;
                }
                temp.append(var_value);
                last_pos = it->position() + it->length();
            }
            temp.append(result, last_pos, result.size() - last_pos);
            result = temp;

            // Replace ${VAR} patterns (unless @ONLY)
            if (!at_only) {
                std::regex dollar_var_regex("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}");
                temp.clear();
                temp.reserve(result.size());
                std::sregex_iterator it2(result.begin(), result.end(), dollar_var_regex);
                last_pos = 0;

                for (; it2 != end; ++it2) {
                    temp.append(result, last_pos, it2->position() - last_pos);
                    std::string var_name = (*it2)[1].str();
                    std::string var_value = interp.get_variable(var_name);
                    if (escape_quotes) {
                        std::string escaped;
                        for (char c : var_value) {
                            if (c == '\\' || c == '"') escaped += '\\';
                            escaped += c;
                        }
                        var_value = escaped;
                    }
                    temp.append(var_value);
                    last_pos = it2->position() + it2->length();
                }
                temp.append(result, last_pos, result.size() - last_pos);
                result = temp;
            }

            // Handle newline style
            if (!newline_style.empty()) {
                std::string nl;
                if (newline_style == "UNIX" || newline_style == "LF") {
                    nl = "\n";
                } else if (newline_style == "DOS" || newline_style == "WIN32" || newline_style == "CRLF") {
                    nl = "\r\n";
                }
                if (!nl.empty() && nl != "\n") {
                    // Replace \n with the specified newline
                    std::string converted;
                    for (size_t j = 0; j < result.size(); ++j) {
                        if (result[j] == '\r' && j + 1 < result.size() && result[j + 1] == '\n') {
                            converted += nl;
                            ++j;
                        } else if (result[j] == '\n') {
                            converted += nl;
                        } else {
                            converted += result[j];
                        }
                    }
                    result = converted;
                }
            }

            // Create parent directories and write file
            if (out_path.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(out_path.parent_path(), ec);
            }

            std::ofstream out(out_path);
            if (!out) {
                interp.set_fatal_error("file(CONFIGURE) could not write to: " + out_path.string());
                return;
            }
            out << result;
        } else if (operation == "GENERATE") {
            // file(GENERATE OUTPUT <output-file> [INPUT <input-file>|CONTENT <content>] ...)
            // Simplified implementation - doesn't support all generator expressions
            CommandParser parser("file", "GENERATE");
            std::string output_file, input_file, content, condition, target_name, newline_style;
            parser.value("OUTPUT", output_file);
            parser.value("INPUT", input_file);
            parser.value("CONTENT", content);
            parser.value("CONDITION", condition);
            parser.value("TARGET", target_name);
            parser.value("NEWLINE_STYLE", newline_style);
            PARSE_OR_RETURN(parser, interp, sub_args);

            if (output_file.empty()) {
                interp.set_fatal_error("file(GENERATE) requires OUTPUT");
                return;
            }

            std::string file_content;
            if (!input_file.empty()) {
                std::filesystem::path in_path = input_file;
                if (!in_path.is_absolute()) {
                    in_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / in_path;
                }
                std::ifstream in(in_path);
                if (!in) {
                    interp.set_fatal_error("file(GENERATE) could not read INPUT: " + in_path.string());
                    return;
                }
                file_content = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            } else {
                file_content = content;
            }

            // Note: Full generator expression support would require build-time evaluation
            // For now, we do basic variable substitution
            std::filesystem::path out_path = output_file;
            if (!out_path.is_absolute()) {
                out_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_BINARY_DIR")) / out_path;
            }

            if (out_path.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(out_path.parent_path(), ec);
            }

            std::ofstream out(out_path);
            if (!out) {
                interp.set_fatal_error("file(GENERATE) could not write to: " + out_path.string());
                return;
            }
            out << file_content;
        } else if (operation == "LOCK") {
            // file(LOCK <path> [DIRECTORY] [RELEASE] [GUARD <scope>] [RESULT_VARIABLE <var>] [TIMEOUT <sec>])
            // Simplified implementation - advisory locking is platform-specific
            CommandParser parser("file", "LOCK");
            std::string path, guard, result_var, timeout_str;
            bool directory = false, release = false;
            parser.positional(path, "path");
            parser.flag("DIRECTORY", directory);
            parser.flag("RELEASE", release);
            parser.value("GUARD", guard);
            parser.value("RESULT_VARIABLE", result_var);
            parser.value("TIMEOUT", timeout_str);
            PARSE_OR_RETURN(parser, interp, sub_args);

            // For now, just acknowledge the lock request without actual locking
            // Full implementation would require platform-specific file locking
            if (!result_var.empty()) {
                interp.set_variable(result_var, "0");
            }
        } else {
            interp.set_fatal_error("file() sub-command not implemented: " + operation);
        }
    });
}

} // namespace dmake
