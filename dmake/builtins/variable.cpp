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

        // Combine remaining arguments into a list
        std::vector<std::string> value_args(args.begin() + 1, args.end());
        CMakeList value_list(value_args);
        interp.set_variable(var_name, value_list.to_string());
    });

    interp.add_builtin("unset", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("unset");
        std::string var_name;
        parser.add_positional(var_name, "variable name");
        PARSE_OR_RETURN(parser, interp, args);

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
