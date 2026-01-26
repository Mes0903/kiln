#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <filesystem>
#include <algorithm>

namespace dmake {

void register_variable_builtins(Interpreter& interp) {
    interp.add_builtin("set", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("set() requires at least one argument");
            return;
        }

        std::string var_name = args[0];

        // INVALID: set(CACHE{VAR} value) - reject this (case-insensitive)
        std::string var_name_upper = var_name;
        std::transform(var_name_upper.begin(), var_name_upper.end(), var_name_upper.begin(), ::toupper);
        if (var_name_upper.find("CACHE{") == 0 && var_name_upper.back() == '}') {
            interp.set_fatal_error("set(CACHE{...} ...) is invalid. Use: set(VAR value CACHE TYPE \"doc\")");
            return;
        }

        // VALID: set(ENV{VAR} value) - case insensitive
        if (var_name_upper.find("ENV{") == 0 && var_name_upper.back() == '}') {
            // Extract variable name (preserve original case from inside {})
            std::string env_var_name = var_name.substr(4, var_name.size() - 5);

            if (args.size() == 1) {
                // set(ENV{VAR}) with no value = unset
                unsetenv(env_var_name.c_str());
            } else {
                // Combine remaining arguments into value
                std::vector<std::string> value_args(args.begin() + 1, args.end());
                CMakeList value_list(value_args);
                setenv(env_var_name.c_str(), value_list.to_string().c_str(), 1);
            }
            return;
        }

        // Check for CACHE keyword (case-insensitive)
        auto cache_it = std::find_if(args.begin() + 1, args.end(),
            [](const std::string& s) {
                std::string upper = s;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                return upper == "CACHE";
            });

        // Check for PARENT_SCOPE keyword (case-insensitive)
        auto parent_it = std::find_if(args.begin() + 1, args.end(),
            [](const std::string& s) {
                std::string upper = s;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                return upper == "PARENT_SCOPE";
            });

        // Cannot use both CACHE and PARENT_SCOPE
        if (cache_it != args.end() && parent_it != args.end()) {
            interp.set_fatal_error("set() cannot use both CACHE and PARENT_SCOPE");
            return;
        }

        // Handle: set(VAR value CACHE TYPE "doc")
        if (cache_it != args.end()) {
            // Value is everything between var_name and CACHE keyword
            std::vector<std::string> value_args(args.begin() + 1, cache_it);
            CMakeList value_list(value_args);
            std::string value = value_list.to_string();

            // Set in cache namespace
            auto* root = interp.get_root();
            root->cache_variables_[var_name] = value;

            // Also set as regular variable (CMake behavior)
            interp.set_variable(var_name, value);
            return;
        }

        // Handle: set(VAR value PARENT_SCOPE)
        if (parent_it != args.end()) {
            // Value is everything between var_name and PARENT_SCOPE keyword
            std::vector<std::string> value_args(args.begin() + 1, parent_it);
            CMakeList value_list(value_args);
            std::string value = value_list.to_string();

            // Set in parent scope
            if (interp.call_stack_.size() < 2) {
                interp.set_fatal_error("set() PARENT_SCOPE requires a parent scope (must be called from a function)");
                return;
            }

            interp.call_stack_[1].variables[var_name] = value;
            return;
        }

        // Warn if trying to modify CMAKE_BUILD_TYPE during script execution
        if (var_name == "CMAKE_BUILD_TYPE") {
            interp.print_message("WARN",
                "Modifying CMAKE_BUILD_TYPE in CMakeLists.txt is NOT RECOMMENDED. "
                "Use --config flag to set the build configuration.",
                false);
        }

        // Validate CMAKE_LINKER_TYPE
        if (var_name == "CMAKE_LINKER_TYPE" && args.size() > 1) {
            std::string linker_type = args[1];
            std::string linker_type_upper = linker_type;
            std::transform(linker_type_upper.begin(), linker_type_upper.end(), linker_type_upper.begin(), ::toupper);

            if (linker_type_upper != "BFD" && linker_type_upper != "GOLD" &&
                linker_type_upper != "MOLD" && linker_type_upper != "LLD") {
                interp.set_fatal_error("Invalid CMAKE_LINKER_TYPE: " + linker_type + ". Must be one of: BFD, GOLD, MOLD, LLD");
                return;
            }
        }

        // Regular: set(VAR value)
        std::vector<std::string> value_args(args.begin() + 1, args.end());
        CMakeList value_list(value_args);
        interp.set_variable(var_name, value_list.to_string());
    });

    interp.add_builtin("unset", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("unset() requires at least one argument");
            return;
        }

        std::string var_name = args[0];

        // INVALID: unset(CACHE{VAR}) - reject this (case-insensitive)
        std::string var_name_upper = var_name;
        std::transform(var_name_upper.begin(), var_name_upper.end(), var_name_upper.begin(), ::toupper);
        if (var_name_upper.find("CACHE{") == 0 && var_name_upper.back() == '}') {
            interp.set_fatal_error("unset(CACHE{...}) is invalid. Use: unset(VAR CACHE)");
            return;
        }

        // VALID: unset(ENV{VAR}) - case insensitive
        if (var_name_upper.find("ENV{") == 0 && var_name_upper.back() == '}') {
            // Extract variable name (preserve original case from inside {})
            std::string env_var_name = var_name.substr(4, var_name.size() - 5);
            unsetenv(env_var_name.c_str());
            return;
        }

        // Check for CACHE keyword (case-insensitive)
        auto cache_it = std::find_if(args.begin() + 1, args.end(),
            [](const std::string& s) {
                std::string upper = s;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                return upper == "CACHE";
            });

        // Check for PARENT_SCOPE keyword (case-insensitive)
        auto parent_it = std::find_if(args.begin() + 1, args.end(),
            [](const std::string& s) {
                std::string upper = s;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                return upper == "PARENT_SCOPE";
            });

        // Cannot use both CACHE and PARENT_SCOPE
        if (cache_it != args.end() && parent_it != args.end()) {
            interp.set_fatal_error("unset() cannot use both CACHE and PARENT_SCOPE");
            return;
        }

        // Handle: unset(VAR CACHE)
        if (cache_it != args.end()) {
            interp.get_root()->cache_variables_.erase(var_name);
            return;
        }

        // Handle: unset(VAR PARENT_SCOPE)
        if (parent_it != args.end()) {
            if (interp.call_stack_.size() < 2) {
                interp.set_fatal_error("unset() PARENT_SCOPE requires a parent scope (must be called from a function)");
                return;
            }
            interp.call_stack_[1].variables.erase(var_name);
            return;
        }

        // Regular: unset(VAR)
        interp.unset_variable(var_name);
    });

    interp.add_builtin("option", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("option");
        std::string option_name;
        std::string help_message;
        std::string initial_value;
        parser.add_positional(option_name, "option name");
        parser.add_positional(help_message, "help message");
        parser.add_positional(initial_value, "initial value", false);
        PARSE_OR_RETURN(parser, interp, args);

        std::string value = initial_value.empty() ? "OFF" : initial_value;
        if(!interp.is_variable_set(option_name)) {
            interp.set_variable(option_name, value);
        }
    });

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
}

} // namespace dmake
