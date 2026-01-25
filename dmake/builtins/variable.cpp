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
        std::vector<Argument> value_args(args.begin() + 1, args.end());
        CMakeList value_list = CMakeList::from_arguments(value_args, &interp);
        interp.set_variable(var_name, value_list.to_string());
    });
}

} // namespace dmake
