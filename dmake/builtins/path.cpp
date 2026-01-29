#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <filesystem>
#include <algorithm>

namespace dmake {

namespace {

// Helper to normalize path component names (case-insensitive matching)
std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

// Helper to convert path to CMake format (forward slashes)
std::string to_cmake_path(const std::filesystem::path& p) {
    std::string s = p.string();
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

// Helper to convert path to native format
std::string to_native_path(const std::filesystem::path& p) {
    std::filesystem::path native = p;
    return native.make_preferred().string();
}

// GET subcommands
void handle_get(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 4) {
        interp.set_fatal_error("cmake_path(GET) requires at least 4 arguments");
        return;
    }

    std::filesystem::path path(args[1]);
    std::string component_upper = to_upper(args[2]);
    std::string out_var = args[3];
    std::string result;

    if (component_upper == "ROOT_NAME") {
        result = to_cmake_path(path.root_name());
    } else if (component_upper == "ROOT_DIRECTORY") {
        result = to_cmake_path(path.root_directory());
    } else if (component_upper == "ROOT_PATH") {
        result = to_cmake_path(path.root_path());
    } else if (component_upper == "FILENAME") {
        result = to_cmake_path(path.filename());
    } else if (component_upper == "EXTENSION") {
        bool last_only = (args.size() > 4 && to_upper(args[4]) == "LAST_ONLY");
        std::string filename = path.filename().string();

        if (!filename.empty() && filename != "." && filename != "..") {
            size_t dot_pos = last_only ? filename.find_last_of('.') : filename.find_first_of('.', 1);
            if (dot_pos != std::string::npos && dot_pos > 0) {
                result = filename.substr(dot_pos);
            }
        }
    } else if (component_upper == "STEM") {
        bool last_only = (args.size() > 4 && to_upper(args[4]) == "LAST_ONLY");
        std::string filename = path.filename().string();

        if (!filename.empty() && filename != "." && filename != "..") {
            size_t dot_pos = last_only ? filename.find_last_of('.') : filename.find_first_of('.', 1);
            if (dot_pos != std::string::npos && dot_pos > 0) {
                result = filename.substr(0, dot_pos);
            } else {
                result = filename;
            }
        }
    } else if (component_upper == "RELATIVE_PART") {
        result = to_cmake_path(path.relative_path());
    } else if (component_upper == "PARENT_PATH") {
        result = to_cmake_path(path.parent_path());
    } else {
        interp.set_fatal_error("cmake_path(GET): unknown component '" + args[2] + "'");
        return;
    }

    interp.set_variable(out_var, result);
}

// HAS_* query subcommands
void handle_has(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 4) {
        interp.set_fatal_error("cmake_path(HAS_*) requires at least 4 arguments");
        return;
    }

    std::filesystem::path path(args[1]);
    std::string component_upper = to_upper(args[2]);
    std::string out_var = args[3];
    bool result = false;

    if (component_upper == "HAS_ROOT_NAME") {
        result = path.has_root_name();
    } else if (component_upper == "HAS_ROOT_DIRECTORY") {
        result = path.has_root_directory();
    } else if (component_upper == "HAS_ROOT_PATH") {
        result = path.has_root_path();
    } else if (component_upper == "HAS_FILENAME") {
        result = path.has_filename();
    } else if (component_upper == "HAS_EXTENSION") {
        result = path.has_extension();
    } else if (component_upper == "HAS_STEM") {
        result = path.has_stem();
    } else if (component_upper == "HAS_RELATIVE_PATH") {
        result = path.has_relative_path();
    } else if (component_upper == "HAS_PARENT_PATH") {
        result = path.has_parent_path();
    } else {
        interp.set_fatal_error("cmake_path: unknown query '" + args[2] + "'");
        return;
    }

    interp.set_variable(out_var, result ? "TRUE" : "FALSE");
}

// IS_ABSOLUTE, IS_RELATIVE, IS_PREFIX
void handle_is(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 4) {
        interp.set_fatal_error("cmake_path(IS_*) requires at least 4 arguments");
        return;
    }

    std::filesystem::path path(args[1]);
    std::string query_upper = to_upper(args[2]);
    std::string out_var = args[3];
    bool result = false;

    if (query_upper == "IS_ABSOLUTE") {
        result = path.is_absolute();
    } else if (query_upper == "IS_RELATIVE") {
        result = path.is_relative();
    } else if (query_upper == "IS_PREFIX") {
        if (args.size() < 5) {
            interp.set_fatal_error("cmake_path(IS_PREFIX) requires 5 arguments");
            return;
        }
        std::filesystem::path other(args[4]);

        // Check if 'path' is a prefix of 'other'
        auto path_it = path.begin();
        auto other_it = other.begin();

        result = true;
        while (path_it != path.end() && other_it != other.end()) {
            if (*path_it != *other_it) {
                result = false;
                break;
            }
            ++path_it;
            ++other_it;
        }

        // If we ran out of path components, it's a prefix
        if (path_it != path.end()) {
            result = false;
        }
    } else {
        interp.set_fatal_error("cmake_path: unknown query '" + args[2] + "'");
        return;
    }

    interp.set_variable(out_var, result ? "TRUE" : "FALSE");
}

// COMPARE
void handle_compare(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 5) {
        interp.set_fatal_error("cmake_path(COMPARE) requires at least 5 arguments");
        return;
    }

    std::filesystem::path path1(args[1]);
    std::string op_upper = to_upper(args[2]);
    std::filesystem::path path2(args[3]);
    std::string out_var = args[4];
    bool result = false;

    if (op_upper == "EQUAL") {
        result = (path1 == path2);
    } else if (op_upper == "NOT_EQUAL") {
        result = (path1 != path2);
    } else {
        interp.set_fatal_error("cmake_path(COMPARE): unknown operator '" + args[2] + "'");
        return;
    }

    interp.set_variable(out_var, result ? "TRUE" : "FALSE");
}

// SET
void handle_set(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        interp.set_fatal_error("cmake_path(SET) requires at least 3 arguments");
        return;
    }

    std::string path_var = args[1];
    std::string input = args[2];
    bool normalize = false;

    // Check for NORMALIZE flag
    for (size_t i = 3; i < args.size(); ++i) {
        if (to_upper(args[i]) == "NORMALIZE") {
            normalize = true;
        }
    }

    std::filesystem::path path(input);
    if (normalize) {
        path = path.lexically_normal();
    }

    interp.set_variable(path_var, to_cmake_path(path));
}

// APPEND
void handle_append(Interpreter& interp, std::vector<std::string> args) {
    if (args.size() < 3) {
        interp.set_fatal_error("cmake_path(APPEND) requires at least 3 arguments");
        return;
    }

    std::string path_var = args[1];
    std::filesystem::path path(interp.get_variable(path_var));

    // Check for OUTPUT_VARIABLE
    std::string output_var = path_var;
    for (size_t i = 2; i < args.size(); ++i) {
        if (to_upper(args[i]) == "OUTPUT_VARIABLE" && i + 1 < args.size()) {
            output_var = args[i + 1];
            // Remove OUTPUT_VARIABLE and its value from processing
            std::vector<std::string> new_args(args.begin(), args.begin() + i);
            new_args.insert(new_args.end(), args.begin() + i + 2, args.end());
            args = new_args;
            break;
        }
    }

    // Append all remaining arguments as path components
    for (size_t i = 2; i < args.size(); ++i) {
        path /= args[i];
    }

    interp.set_variable(output_var, to_cmake_path(path));
}

// APPEND_STRING
void handle_append_string(Interpreter& interp, std::vector<std::string> args) {
    if (args.size() < 3) {
        interp.set_fatal_error("cmake_path(APPEND_STRING) requires at least 3 arguments");
        return;
    }

    std::string path_var = args[1];
    std::string path_str = interp.get_variable(path_var);

    // Check for OUTPUT_VARIABLE
    std::string output_var = path_var;
    for (size_t i = 2; i < args.size(); ++i) {
        if (to_upper(args[i]) == "OUTPUT_VARIABLE" && i + 1 < args.size()) {
            output_var = args[i + 1];
            // Remove OUTPUT_VARIABLE and its value from processing
            std::vector<std::string> new_args(args.begin(), args.begin() + i);
            new_args.insert(new_args.end(), args.begin() + i + 2, args.end());
            args = new_args;
            break;
        }
    }

    // Append all remaining arguments as strings
    for (size_t i = 2; i < args.size(); ++i) {
        path_str += args[i];
    }

    interp.set_variable(output_var, path_str);
}

// REMOVE_FILENAME
void handle_remove_filename(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        interp.set_fatal_error("cmake_path(REMOVE_FILENAME) requires at least 2 arguments");
        return;
    }

    std::string path_var = args[1];
    std::filesystem::path path(interp.get_variable(path_var));

    // Check for OUTPUT_VARIABLE
    std::string output_var = path_var;
    if (args.size() > 2 && to_upper(args[2]) == "OUTPUT_VARIABLE" && args.size() > 3) {
        output_var = args[3];
    }

    path.remove_filename();
    interp.set_variable(output_var, to_cmake_path(path));
}

// REPLACE_FILENAME
void handle_replace_filename(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        interp.set_fatal_error("cmake_path(REPLACE_FILENAME) requires at least 3 arguments");
        return;
    }

    std::string path_var = args[1];
    std::string new_filename = args[2];
    std::filesystem::path path(interp.get_variable(path_var));

    // Check for OUTPUT_VARIABLE
    std::string output_var = path_var;
    if (args.size() > 3 && to_upper(args[3]) == "OUTPUT_VARIABLE" && args.size() > 4) {
        output_var = args[4];
    }

    path.replace_filename(new_filename);
    interp.set_variable(output_var, to_cmake_path(path));
}

// REMOVE_EXTENSION
void handle_remove_extension(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        interp.set_fatal_error("cmake_path(REMOVE_EXTENSION) requires at least 2 arguments");
        return;
    }

    std::string path_var = args[1];
    std::filesystem::path path(interp.get_variable(path_var));
    bool last_only = false;

    // Check for LAST_ONLY and OUTPUT_VARIABLE
    std::string output_var = path_var;
    for (size_t i = 2; i < args.size(); ++i) {
        if (to_upper(args[i]) == "LAST_ONLY") {
            last_only = true;
        } else if (to_upper(args[i]) == "OUTPUT_VARIABLE" && i + 1 < args.size()) {
            output_var = args[i + 1];
        }
    }

    std::string filename = path.filename().string();
    std::string stem;

    if (!filename.empty() && filename != "." && filename != "..") {
        if (last_only) {
            size_t dot_pos = filename.find_last_of('.');
            stem = (dot_pos != std::string::npos && dot_pos > 0) ? filename.substr(0, dot_pos) : filename;
        } else {
            size_t dot_pos = filename.find_first_of('.', 1);
            stem = (dot_pos != std::string::npos) ? filename.substr(0, dot_pos) : filename;
        }
    } else {
        stem = filename;
    }

    path.replace_filename(stem);
    interp.set_variable(output_var, to_cmake_path(path));
}

// REPLACE_EXTENSION
void handle_replace_extension(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        interp.set_fatal_error("cmake_path(REPLACE_EXTENSION) requires at least 3 arguments");
        return;
    }

    std::string path_var = args[1];
    std::string new_ext = args[2];
    std::filesystem::path path(interp.get_variable(path_var));
    bool last_only = false;

    // Check for LAST_ONLY and OUTPUT_VARIABLE
    std::string output_var = path_var;
    for (size_t i = 3; i < args.size(); ++i) {
        if (to_upper(args[i]) == "LAST_ONLY") {
            last_only = true;
        } else if (to_upper(args[i]) == "OUTPUT_VARIABLE" && i + 1 < args.size()) {
            output_var = args[i + 1];
        }
    }

    std::string filename = path.filename().string();
    std::string stem;

    if (!filename.empty() && filename != "." && filename != "..") {
        if (last_only) {
            size_t dot_pos = filename.find_last_of('.');
            stem = (dot_pos != std::string::npos && dot_pos > 0) ? filename.substr(0, dot_pos) : filename;
        } else {
            size_t dot_pos = filename.find_first_of('.', 1);
            stem = (dot_pos != std::string::npos) ? filename.substr(0, dot_pos) : filename;
        }
    } else {
        stem = filename;
    }

    // Ensure extension starts with '.' if not empty
    if (!new_ext.empty() && new_ext[0] != '.') {
        new_ext = "." + new_ext;
    }

    path.replace_filename(stem + new_ext);
    interp.set_variable(output_var, to_cmake_path(path));
}

// NORMAL_PATH
void handle_normal_path(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        interp.set_fatal_error("cmake_path(NORMAL_PATH) requires at least 2 arguments");
        return;
    }

    std::string path_var = args[1];
    std::string out_var = path_var;

    // Check for OUTPUT_VARIABLE
    for (size_t i = 2; i < args.size(); ++i) {
        if (to_upper(args[i]) == "OUTPUT_VARIABLE" && i + 1 < args.size()) {
            out_var = args[i + 1];
            break;
        }
    }

    std::filesystem::path path(interp.get_variable(path_var));
    path = path.lexically_normal();
    interp.set_variable(out_var, to_cmake_path(path));
}

// RELATIVE_PATH
void handle_relative_path(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        interp.set_fatal_error("cmake_path(RELATIVE_PATH) requires at least 2 arguments");
        return;
    }

    std::string path_var = args[1];
    std::string out_var = path_var;
    std::filesystem::path path(interp.get_variable(path_var));

    // Check for BASE_DIRECTORY and OUTPUT_VARIABLE
    std::filesystem::path base_dir(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR"));
    for (size_t i = 2; i < args.size(); ++i) {
        if (to_upper(args[i]) == "BASE_DIRECTORY" && i + 1 < args.size()) {
            base_dir = std::filesystem::path(args[i + 1]);
        } else if (to_upper(args[i]) == "OUTPUT_VARIABLE" && i + 1 < args.size()) {
            out_var = args[i + 1];
        }
    }

    std::filesystem::path result = path.lexically_relative(base_dir);
    interp.set_variable(out_var, to_cmake_path(result));
}

// ABSOLUTE_PATH
void handle_absolute_path(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        interp.set_fatal_error("cmake_path(ABSOLUTE_PATH) requires at least 2 arguments");
        return;
    }

    std::string path_var = args[1];
    std::string out_var = path_var;
    std::filesystem::path path(interp.get_variable(path_var));
    bool normalize = false;

    // Check for BASE_DIRECTORY, NORMALIZE, and OUTPUT_VARIABLE
    std::filesystem::path base_dir(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR"));
    for (size_t i = 2; i < args.size(); ++i) {
        if (to_upper(args[i]) == "BASE_DIRECTORY" && i + 1 < args.size()) {
            base_dir = std::filesystem::path(args[i + 1]);
        } else if (to_upper(args[i]) == "NORMALIZE") {
            normalize = true;
        } else if (to_upper(args[i]) == "OUTPUT_VARIABLE" && i + 1 < args.size()) {
            out_var = args[i + 1];
        }
    }

    if (!path.is_absolute()) {
        path = base_dir / path;
    }

    if (normalize) {
        path = path.lexically_normal();
    }

    interp.set_variable(out_var, to_cmake_path(path));
}

// NATIVE_PATH
void handle_native_path(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        interp.set_fatal_error("cmake_path(NATIVE_PATH) requires at least 2 arguments");
        return;
    }

    std::string path_var = args[1];
    std::string out_var = path_var;
    std::filesystem::path path(interp.get_variable(path_var));

    // Check for NORMALIZE and OUTPUT_VARIABLE
    bool normalize = false;
    for (size_t i = 2; i < args.size(); ++i) {
        if (to_upper(args[i]) == "NORMALIZE") {
            normalize = true;
        } else if (to_upper(args[i]) == "OUTPUT_VARIABLE" && i + 1 < args.size()) {
            out_var = args[i + 1];
        }
    }

    if (normalize) {
        path = path.lexically_normal();
    }

    interp.set_variable(out_var, to_native_path(path));
}

// CONVERT
void handle_convert(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 4) {
        interp.set_fatal_error("cmake_path(CONVERT) requires at least 4 arguments");
        return;
    }

    std::string input = args[1];
    std::string mode_upper = to_upper(args[2]);
    std::string out_var = args[3];

    if (mode_upper == "TO_CMAKE_PATH_LIST") {
        // Convert native path list to CMake format
        std::string result;

#ifdef _WIN32
        // Windows: semicolon-separated
        size_t start = 0;
        size_t end = input.find(';');
        while (end != std::string::npos) {
            std::filesystem::path p(input.substr(start, end - start));
            if (!result.empty()) result += ";";
            result += to_cmake_path(p);
            start = end + 1;
            end = input.find(';', start);
        }
        std::filesystem::path p(input.substr(start));
        if (!result.empty()) result += ";";
        result += to_cmake_path(p);
#else
        // Unix: colon-separated
        size_t start = 0;
        size_t end = input.find(':');
        while (end != std::string::npos) {
            std::filesystem::path p(input.substr(start, end - start));
            if (!result.empty()) result += ";";
            result += to_cmake_path(p);
            start = end + 1;
            end = input.find(':', start);
        }
        std::filesystem::path p(input.substr(start));
        if (!result.empty()) result += ";";
        result += to_cmake_path(p);
#endif

        interp.set_variable(out_var, result);
    } else if (mode_upper == "TO_NATIVE_PATH_LIST") {
        // Convert CMake list to native path list
        std::string result;

        size_t start = 0;
        size_t end = input.find(';');
        while (end != std::string::npos) {
            std::filesystem::path p(input.substr(start, end - start));
            if (!result.empty()) {
#ifdef _WIN32
                result += ";";
#else
                result += ":";
#endif
            }
            result += to_native_path(p);
            start = end + 1;
            end = input.find(';', start);
        }
        std::filesystem::path p(input.substr(start));
        if (!result.empty()) {
#ifdef _WIN32
            result += ";";
#else
            result += ":";
#endif
        }
        result += to_native_path(p);

        interp.set_variable(out_var, result);
    } else {
        interp.set_fatal_error("cmake_path(CONVERT): unknown mode '" + args[2] + "'");
    }
}

// HASH
void handle_hash(Interpreter& interp, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        interp.set_fatal_error("cmake_path(HASH) requires at least 3 arguments");
        return;
    }

    std::string path_var = args[1];
    std::string out_var = args[2];
    std::filesystem::path path(interp.get_variable(path_var));

    // Normalize the path before hashing for consistency
    path = path.lexically_normal();

    // Use std::hash
    size_t hash_value = std::filesystem::hash_value(path);

    interp.set_variable(out_var, std::to_string(hash_value));
}

} // anonymous namespace

void register_path_builtins(Interpreter& interp) {
    interp.add_builtin("get_filename_component", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("get_filename_component");
        std::string var_name;
        std::string filename;
        std::string mode;
        std::string base_dir;
        bool cache = false;

        parser.add_positional(var_name, "variable name");
        parser.add_positional(filename, "filename");
        parser.add_positional(mode, "mode");
        parser.add_value("BASE_DIR", base_dir);
        parser.add_flag("CACHE", cache);

        PARSE_OR_RETURN(parser, interp, args);

        std::filesystem::path path(filename);
        std::string result;

        if (mode == "DIRECTORY" || mode == "PATH") {
            result = path.parent_path().string();
        } else if (mode == "NAME") {
            result = path.filename().string();
        } else if (mode == "EXT") {
            std::string name = path.filename().string();
            size_t first_dot = name.find_first_of('.', 1);
            if (first_dot != std::string::npos) {
                result = name.substr(first_dot);
            }
        } else if (mode == "NAME_WE") {
            std::string name = path.filename().string();
            size_t first_dot = name.find_first_of('.', 1);
            if (first_dot != std::string::npos) {
                result = name.substr(0, first_dot);
            } else {
                result = name;
            }
        } else if (mode == "LAST_EXT") {
            std::string name = path.filename().string();
            size_t last_dot = name.find_last_of('.');
            if (last_dot != std::string::npos && last_dot > 0) {
                result = name.substr(last_dot);
            }
        } else if (mode == "NAME_WLE") {
            std::string name = path.filename().string();
            size_t last_dot = name.find_last_of('.');
            if (last_dot != std::string::npos && last_dot > 0) {
                result = name.substr(0, last_dot);
            } else {
                result = name;
            }
        } else if (mode == "ABSOLUTE" || mode == "REALPATH") {
            std::filesystem::path abs_path = path;
            if (!path.is_absolute()) {
                std::filesystem::path base = !base_dir.empty() ?
                    std::filesystem::path(base_dir) :
                    std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR"));
                abs_path = base / path;
            }

            if (mode == "REALPATH") {
                try {
                    // weakly_canonical handles non-existent paths by resolving what it can
                    result = std::filesystem::weakly_canonical(abs_path).string();
                } catch (...) {
                    result = abs_path.lexically_normal().string();
                }
            } else {
                result = abs_path.lexically_normal().string();
            }
        } else {
            interp.set_fatal_error("Invalid mode '" + mode + "' for get_filename_component()");
            return;
        }

        interp.set_variable(var_name, result);
    });

    interp.add_builtin("cmake_path", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("cmake_path() requires at least one argument");
            return;
        }

        std::string subcommand_upper = to_upper(args[0]);

        // GET subcommands
        if (subcommand_upper == "GET") {
            handle_get(interp, args);
        }
        // HAS_* queries
        else if (subcommand_upper == "HAS_ROOT_NAME" || subcommand_upper == "HAS_ROOT_DIRECTORY" ||
                 subcommand_upper == "HAS_ROOT_PATH" || subcommand_upper == "HAS_FILENAME" ||
                 subcommand_upper == "HAS_EXTENSION" || subcommand_upper == "HAS_STEM" ||
                 subcommand_upper == "HAS_RELATIVE_PATH" || subcommand_upper == "HAS_PARENT_PATH") {
            handle_has(interp, args);
        }
        // IS_* queries
        else if (subcommand_upper == "IS_ABSOLUTE" || subcommand_upper == "IS_RELATIVE" ||
                 subcommand_upper == "IS_PREFIX") {
            handle_is(interp, args);
        }
        // COMPARE
        else if (subcommand_upper == "COMPARE") {
            handle_compare(interp, args);
        }
        // SET
        else if (subcommand_upper == "SET") {
            handle_set(interp, args);
        }
        // APPEND
        else if (subcommand_upper == "APPEND") {
            handle_append(interp, args);
        }
        // APPEND_STRING
        else if (subcommand_upper == "APPEND_STRING") {
            handle_append_string(interp, args);
        }
        // REMOVE_FILENAME
        else if (subcommand_upper == "REMOVE_FILENAME") {
            handle_remove_filename(interp, args);
        }
        // REPLACE_FILENAME
        else if (subcommand_upper == "REPLACE_FILENAME") {
            handle_replace_filename(interp, args);
        }
        // REMOVE_EXTENSION
        else if (subcommand_upper == "REMOVE_EXTENSION") {
            handle_remove_extension(interp, args);
        }
        // REPLACE_EXTENSION
        else if (subcommand_upper == "REPLACE_EXTENSION") {
            handle_replace_extension(interp, args);
        }
        // NORMAL_PATH
        else if (subcommand_upper == "NORMAL_PATH") {
            handle_normal_path(interp, args);
        }
        // RELATIVE_PATH
        else if (subcommand_upper == "RELATIVE_PATH") {
            handle_relative_path(interp, args);
        }
        // ABSOLUTE_PATH
        else if (subcommand_upper == "ABSOLUTE_PATH") {
            handle_absolute_path(interp, args);
        }
        // NATIVE_PATH
        else if (subcommand_upper == "NATIVE_PATH") {
            handle_native_path(interp, args);
        }
        // CONVERT
        else if (subcommand_upper == "CONVERT") {
            handle_convert(interp, args);
        }
        // HASH
        else if (subcommand_upper == "HASH") {
            handle_hash(interp, args);
        }
        else {
            interp.set_fatal_error("cmake_path: unknown subcommand '" + args[0] + "'");
        }
    });
}

} // namespace dmake
