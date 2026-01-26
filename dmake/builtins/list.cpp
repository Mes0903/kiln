#include "registry.hpp"
#include "../interperter.hpp"
#include <algorithm>

namespace dmake {

void register_list_builtins(Interpreter& interp) {
    interp.add_builtin("list", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.size() < 2) {
            interp.print_message("ERROR", "list() requires at least 2 arguments", true);
            return;
        }

        std::string operation = args[0];
        std::transform(operation.begin(), operation.end(), operation.begin(), ::toupper);

        if (operation == "LENGTH") {
            if (args.size() != 3) { interp.print_message("ERROR", "list(LENGTH) requires 3 args", true); return; }
            CMakeList list(interp.get_variable(args[1]));
            interp.set_variable(args[2], std::to_string(list.size()));
        } else if (operation == "GET") {
            if (args.size() < 4) { interp.print_message("ERROR", "list(GET) requires 4+ args", true); return; }
            CMakeList list(interp.get_variable(args[1]));
            CMakeList result;
            for (size_t i = 2; i < args.size() - 1; ++i) {
                size_t idx = std::stoul(args[i]);
                if (idx < list.size()) result.append(list[idx]);
            }
            interp.set_variable(args[args.size() - 1], result.to_string());
        } else if (operation == "APPEND") {
            std::string var = args[1];
            CMakeList list(interp.get_variable(var));
            for (size_t i = 2; i < args.size(); ++i) list.append(args[i]);
            interp.set_variable(var, list.to_string());
        } else if (operation == "REVERSE") {
            std::string var = args[1];
            CMakeList list(interp.get_variable(var));
            list.reverse();
            interp.set_variable(var, list.to_string());
        } else if (operation == "SORT") {
            std::string var = args[1];
            CMakeList list(interp.get_variable(var));
            list.sort();
            interp.set_variable(var, list.to_string());
        } else if (operation == "REMOVE_DUPLICATES") {
            std::string var = args[1];
            CMakeList list(interp.get_variable(var));
            list.remove_duplicates();
            interp.set_variable(var, list.to_string());
        } else if (operation == "SUBLIST") {
            if (args.size() != 5) { interp.print_message("ERROR", "list(SUBLIST) requires 5 args", true); return; }
            CMakeList list(interp.get_variable(args[1]));
            size_t start = std::stoul(args[2]);
            size_t len = std::stoul(args[3]);
            interp.set_variable(args[4], list.sublist(start, len).to_string());
        }
    });
}

} // namespace dmake
