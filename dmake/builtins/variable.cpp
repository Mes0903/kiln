#include "registry.hpp"
#include "../interperter.hpp"

namespace dmake {

void register_variable_builtins(Interpreter& interp) {
    interp.add_builtin("set", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 2) {
            interp.print_message("ERROR", "set() requires at least 2 arguments", true);
            return;
        }
        std::string var_name = interp.evaluate_argument(args[0]);

        // Warn if trying to modify CMAKE_BUILD_TYPE during script execution
        if (var_name == "CMAKE_BUILD_TYPE") {
            interp.print_message("WARN",
                "Modifying CMAKE_BUILD_TYPE in CMakeLists.txt is NOT RECOMMENDED. "
                "Use --config flag to set the build configuration.",
                false);
        }

        std::vector<Argument> value_args(args.begin() + 1, args.end());
        CMakeList value_list = interp.from_arguments(value_args);
        interp.set_variable(var_name, value_list.to_string());
    });

    interp.add_builtin("unset", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 1) {
            interp.print_message("ERROR", "unset() requires at least 1 argument", true);
            return;
        }
        std::string var_name = interp.evaluate_argument(args[0]);
        interp.unset_variable(var_name);
    });

    interp.add_builtin("option", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 2) {
            interp.print_message("ERROR", "option() requires at least 2 arguments", true);
            return;
        }
        std::string option_name = interp.evaluate_argument(args[0]);
        std::string help_message = interp.evaluate_argument(args[1]);
        (void)help_message; // Ignore the help message
        std::string value = "ON";
        if(args.size() > 2) {
            value = interp.evaluate_argument(args[2]);
        }
        if(!interp.is_variable_set(option_name)) {
            interp.set_variable(option_name, value);
        }
    });

    interp.add_builtin("get_filename_component", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 3) {
            interp.set_fatal_error("get_filename_component() requires at least 3 arguments");
            return;
        }

        std::string var_name = interp.evaluate_argument(args[0]);
        std::string filename = interp.evaluate_argument(args[1]);
        std::string mode = interp.evaluate_argument(args[2]);

        std::string base_dir;
        bool has_base_dir = false;

        for (size_t i = 3; i < args.size(); ++i) {
            std::string arg = interp.evaluate_argument(args[i]);
            if (arg == "CACHE") {
                // Ignore CACHE for now
            } else if (arg == "BASE_DIR" && i + 1 < args.size()) {
                base_dir = interp.evaluate_argument(args[++i]);
                has_base_dir = true;
            }
        }

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
                std::filesystem::path base = has_base_dir ?
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
