#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <regex>

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
                        else if (std::string(".+^$|()[]{}").find(c) != std::string::npos) {
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
        } else {
            interp.set_fatal_error("file() sub-command not implemented: " + operation);
        }
    });
}

} // namespace dmake
