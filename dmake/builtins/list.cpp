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
        std::transform(operation.begin(), operation.end(), operation.begin(), [](unsigned char c){ return std::toupper(c); });

        std::span<const std::string> sub_args(args.begin() + 1, args.end());

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

            // If list doesn't exist or is empty, set result to NOTFOUND
            if (list.empty()) {
                interp.set_variable(out_var, "NOTFOUND");
                return;
            }

            CMakeList result;
            for (const auto& idx_str : indices) {
                try {
                    long idx = std::stol(idx_str);
                    // Handle negative indices
                    if (idx < 0) {
                        idx = static_cast<long>(list.size()) + idx;
                    }
                    if (idx >= 0 && static_cast<size_t>(idx) < list.size()) {
                        result.append(list[static_cast<size_t>(idx)]);
                    } else {
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
        } else if (operation == "INSERT") {
            CommandParser parser("list", "INSERT");
            std::string list_var, index_str;
            std::vector<std::string> items;
            parser.add_positional(list_var, "list variable");
            parser.add_positional(index_str, "index");
            parser.add_default_list(items);
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            try {
                long idx = std::stol(index_str);
                // Handle negative indices
                if (idx < 0) {
                    idx = static_cast<long>(list.size()) + idx;
                }
                if (idx < 0 || static_cast<size_t>(idx) > list.size()) {
                    interp.set_fatal_error("list(INSERT) index out of range: " + index_str);
                    return;
                }
                list.insert(static_cast<size_t>(idx), items);
            } catch (...) {
                interp.set_fatal_error("list(INSERT) invalid index: " + index_str);
                return;
            }
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
            std::string compare_mode, order_mode;
            parser.add_positional(list_var, "list variable");
            parser.add_value("COMPARE", compare_mode);
            parser.add_value("ORDER", order_mode);
            PARSE_OR_RETURN(parser, interp, sub_args);

            bool natural = false;
            bool descending = false;

            if (!compare_mode.empty()) {
                std::transform(compare_mode.begin(), compare_mode.end(), compare_mode.begin(),
                              [](unsigned char c){ return std::toupper(c); });
                if (compare_mode == "NATURAL") {
                    natural = true;
                } else if (compare_mode != "STRING") {
                    interp.set_fatal_error("list(SORT) unknown COMPARE mode: " + compare_mode);
                    return;
                }
            }

            if (!order_mode.empty()) {
                std::transform(order_mode.begin(), order_mode.end(), order_mode.begin(),
                              [](unsigned char c){ return std::toupper(c); });
                if (order_mode == "DESCENDING") {
                    descending = true;
                } else if (order_mode != "ASCENDING") {
                    interp.set_fatal_error("list(SORT) unknown ORDER mode: " + order_mode);
                    return;
                }
            }

            CMakeList list(interp.get_variable(list_var));
            list.sort(natural, descending);
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
        } else if (operation == "FIND") {
            CommandParser parser("list", "FIND");
            std::string list_var, value, out_var;
            parser.add_positional(list_var, "list variable");
            parser.add_positional(value, "value to find");
            parser.add_positional(out_var, "output variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            long found_index = -1;
            for (size_t i = 0; i < list.size(); ++i) {
                if (list[i] == value) {
                    found_index = static_cast<long>(i);
                    break;
                }
            }
            interp.set_variable(out_var, std::to_string(found_index));
        } else if (operation == "REMOVE_ITEM") {
            CommandParser parser("list", "FIND");
            std::string list_var;
            std::vector<std::string> items;
            parser.add_positional(list_var, "list variable");
            parser.add_default_list(items);
            PARSE_OR_RETURN(parser, interp, sub_args);


            CMakeList list(interp.get_variable(list_var));
            std::set<std::string> items_set(items.begin(), items.end());
            std::vector<size_t> remove_idxs;
            for(size_t i=0;i<list.size();i++) {
                const auto& item = list[i];
                if(items_set.contains(item)) {
                    remove_idxs.push_back(i);
                }
            }
            for(auto it = remove_idxs.rbegin(); it != remove_idxs.rend(); it++) {
                list.erase(*it);
            }
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "REMOVE_AT") {
            CommandParser parser("list", "REMOVE_AT");
            std::string list_var;
            std::vector<std::string> indices;
            parser.add_positional(list_var, "list variable");
            parser.add_default_list(indices);
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));

            // Convert all indices to positive and collect them
            std::vector<size_t> positive_indices;
            for (const auto& idx_str : indices) {
                try {
                    long idx = std::stol(idx_str);
                    // Handle negative indices
                    if (idx < 0) {
                        idx = static_cast<long>(list.size()) + idx;
                    }
                    if (idx < 0 || static_cast<size_t>(idx) >= list.size()) {
                        interp.set_fatal_error("list(REMOVE_AT) index out of range: " + idx_str);
                        return;
                    }
                    positive_indices.push_back(static_cast<size_t>(idx));
                } catch (...) {
                    interp.set_fatal_error("list(REMOVE_AT) invalid index: " + idx_str);
                    return;
                }
            }

            // Sort indices in descending order to remove from back to front
            std::sort(positive_indices.begin(), positive_indices.end(), std::greater<size_t>());
            for (size_t idx : positive_indices) {
                list.erase(idx);
            }
            interp.set_variable(list_var, list.to_string());
        } else {
            interp.set_fatal_error("Unknown list operation: " + operation);
        }
    });
}

} // namespace dmake
