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

}

} // namespace dmake
