#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <algorithm>

namespace dmake {

void register_list_builtins(Interpreter& interp) {
    interp.add_builtin("list", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("list() requires at least one argument");
            return;
        }

        std::string operation = args[0];
        std::transform(operation.begin(), operation.end(), operation.begin(), ::toupper);

        std::vector<std::string> sub_args(args.begin() + 1, args.end());

        if (operation == "LENGTH") {
            CommandParser parser("list", "LENGTH");
            std::string list_var, out_var;
            parser.add_positional(list_var, "list variable");
            parser.add_positional(out_var, "output variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            interp.set_variable(out_var, std::to_string(list.size()));
        } else if (operation == "GET") {
            CommandParser parser("list", "GET");
            std::string list_var, out_var;
            std::vector<std::string> indices;
            parser.add_positional(list_var, "list variable");
            parser.add_default_list(indices); // Indices are everything until the last one
            PARSE_OR_RETURN(parser, interp, sub_args);

            if (indices.empty()) {
                interp.set_fatal_error("list(GET) requires at least one index");
                return;
            }
            out_var = indices.back();
            indices.pop_back();

            CMakeList list(interp.get_variable(list_var));
            CMakeList result;
            for (const auto& idx_str : indices) {
                try {
                    size_t idx = std::stoul(idx_str);
                    if (idx < list.size()) result.append(list[idx]);
                    else {
                        interp.set_fatal_error("list(GET) index out of range: " + idx_str);
                        return;
                    }
                } catch (...) {
                    interp.set_fatal_error("list(GET) invalid index: " + idx_str);
                    return;
                }
            }
            interp.set_variable(out_var, result.to_string());
        } else if (operation == "APPEND") {
            CommandParser parser("list", "APPEND");
            std::string list_var;
            std::vector<std::string> items;
            parser.add_positional(list_var, "list variable");
            parser.add_default_list(items);
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            for (const auto& item : items) list.append(item);
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "REVERSE") {
            CommandParser parser("list", "REVERSE");
            std::string list_var;
            parser.add_positional(list_var, "list variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            list.reverse();
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "SORT") {
            CommandParser parser("list", "SORT");
            std::string list_var;
            parser.add_positional(list_var, "list variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            list.sort();
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "REMOVE_DUPLICATES") {
            CommandParser parser("list", "REMOVE_DUPLICATES");
            std::string list_var;
            parser.add_positional(list_var, "list variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            list.remove_duplicates();
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "SUBLIST") {
            CommandParser parser("list", "SUBLIST");
            std::string list_var, out_var, start_str, length_str;
            parser.add_positional(list_var, "list variable");
            parser.add_positional(start_str, "start index");
            parser.add_positional(length_str, "length");
            parser.add_positional(out_var, "output variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            try {
                size_t start = std::stoul(start_str);
                int length = std::stoi(length_str);
                interp.set_variable(out_var, list.sublist(start, length).to_string());
            } catch (...) {
                interp.set_fatal_error("list(SUBLIST) invalid indices");
            }
        } else {
            interp.set_fatal_error("Unknown list operation: " + operation);
        }
    });
}

} // namespace dmake
