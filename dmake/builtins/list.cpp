#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <algorithm>
#include <set>
#include <regex>

namespace dmake {

namespace {
    // Helper function to strip whitespace
    std::string strip(const std::string& str) {
        const std::string whitespace = " \t\n\r\f\v";
        size_t start = str.find_first_not_of(whitespace);
        if (start == std::string::npos) {
            return "";
        }
        size_t end = str.find_last_not_of(whitespace);
        return str.substr(start, end - start + 1);
    }

    // Helper function to extract basename from file path
    std::string get_basename(const std::string& path) {
        size_t last_slash = path.find_last_of("/\\");
        if (last_slash == std::string::npos) {
            return path;
        }
        return path.substr(last_slash + 1);
    }
}

void register_list_builtins(Interpreter& interp) {
    interp.add_builtin("list", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("list() requires at least one argument");
            return;
        }

        std::string operation = args[0];
        std::transform(operation.begin(), operation.end(), operation.begin(), [](unsigned char c){ return std::toupper(c); });

        std::vector<std::string> sub_args(args.begin() + 1, args.end());

        if (operation == "LENGTH") {
            CommandParser parser("list", "LENGTH");
            std::string list_var, out_var;
            parser.positional(list_var, "list variable");
            parser.positional(out_var, "output variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            interp.set_variable(out_var, std::to_string(list.size()));
        } else if (operation == "GET") {
            CommandParser parser("list", "GET");
            std::string list_var, out_var;
            std::vector<std::string> indices;
            parser.positional(list_var, "list variable");
            parser.positionals(indices, "indices");
            PARSE_OR_RETURN(parser, interp, sub_args);

            if (indices.empty()) {
                interp.set_fatal_error("list(GET) requires at least one index");
                return;
            }
            out_var = indices.back();
            indices.pop_back();

            CMakeList list(interp.get_variable(list_var));

            if (list.empty()) {
                interp.set_variable(out_var, "NOTFOUND");
                return;
            }

            CMakeList result;
            for (const auto& idx_str : indices) {
                try {
                    long idx = std::stol(idx_str);
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
        } else if (operation == "JOIN") {
            CommandParser parser("list", "JOIN");
            std::string list_var, glue, out_var;
            parser.positional(list_var, "list variable");
            parser.positional(glue, "glue string");
            parser.positional(out_var, "output variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            std::string result;
            for (size_t i = 0; i < list.size(); ++i) {
                if (i > 0) result += glue;
                result += list[i];
            }
            interp.set_variable(out_var, result);
        } else if (operation == "APPEND") {
            CommandParser parser("list", "APPEND");
            std::string list_var;
            std::vector<std::string> items;
            parser.positional(list_var, "list variable");
            parser.positionals(items, "items");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            for (const auto& item : items) list.append(item);
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "PREPEND") {
            CommandParser parser("list", "PREPEND");
            std::string list_var;
            std::vector<std::string> items;
            parser.positional(list_var, "list variable");
            parser.positionals(items, "items");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            // Insert items at beginning in order
            list.insert(0, items);
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "POP_BACK") {
            CommandParser parser("list", "POP_BACK");
            std::string list_var;
            std::vector<std::string> out_vars;
            parser.positional(list_var, "list variable");
            parser.positionals(out_vars, "output variables");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));

            if (list.empty()) {
                interp.set_fatal_error("list(POP_BACK) cannot pop from empty list");
                return;
            }

            // Pop one element for each output variable (default 1)
            size_t num_to_pop = out_vars.empty() ? 1 : out_vars.size();

            if (num_to_pop > list.size()) {
                interp.set_fatal_error("list(POP_BACK) not enough elements to pop");
                return;
            }

            // Store popped values in output variables (in reverse order from back)
            for (size_t i = 0; i < out_vars.size(); ++i) {
                size_t idx = list.size() - 1 - i;
                interp.set_variable(out_vars[i], list[idx]);
            }

            // Remove elements from the list
            for (size_t i = 0; i < num_to_pop; ++i) {
                list.erase(list.size() - 1);
            }

            interp.set_variable(list_var, list.to_string());
        } else if (operation == "POP_FRONT") {
            CommandParser parser("list", "POP_FRONT");
            std::string list_var;
            std::vector<std::string> out_vars;
            parser.positional(list_var, "list variable");
            parser.positionals(out_vars, "output variables");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));

            if (list.empty()) {
                interp.set_fatal_error("list(POP_FRONT) cannot pop from empty list");
                return;
            }

            // Pop one element for each output variable (default 1)
            size_t num_to_pop = out_vars.empty() ? 1 : out_vars.size();

            if (num_to_pop > list.size()) {
                interp.set_fatal_error("list(POP_FRONT) not enough elements to pop");
                return;
            }

            // Store popped values in output variables
            for (size_t i = 0; i < out_vars.size(); ++i) {
                interp.set_variable(out_vars[i], list[i]);
            }

            // Remove elements from the front
            for (size_t i = 0; i < num_to_pop; ++i) {
                list.erase(0);
            }

            interp.set_variable(list_var, list.to_string());
        } else if (operation == "INSERT") {
            CommandParser parser("list", "INSERT");
            std::string list_var, index_str;
            std::vector<std::string> items;
            parser.positional(list_var, "list variable");
            parser.positional(index_str, "index");
            parser.positionals(items, "items");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            try {
                long idx = std::stol(index_str);
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
            parser.positional(list_var, "list variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            list.reverse();
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "SORT") {
            CommandParser parser("list", "SORT");
            std::string list_var;
            std::string compare_mode, case_mode, order_mode;
            parser.positional(list_var, "list variable");
            parser.value("COMPARE", compare_mode);
            parser.value("CASE", case_mode);
            parser.value("ORDER", order_mode);
            PARSE_OR_RETURN(parser, interp, sub_args);

            bool natural = false;
            bool descending = false;
            bool file_basename = false;

            if (!compare_mode.empty()) {
                std::transform(compare_mode.begin(), compare_mode.end(), compare_mode.begin(),
                              [](unsigned char c){ return std::toupper(c); });
                if (compare_mode == "NATURAL") {
                    natural = true;
                } else if (compare_mode == "STRING") {
                    natural = false;
                } else if (compare_mode == "FILE_BASENAME") {
                    file_basename = true;
                } else {
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

            // Handle FILE_BASENAME comparison specially
            if (file_basename) {
                auto vec = list.to_vector();
                std::sort(vec.begin(), vec.end(), [descending](const std::string& a, const std::string& b) {
                    std::string basename_a = get_basename(a);
                    std::string basename_b = get_basename(b);
                    if (descending) {
                        return basename_a > basename_b;
                    }
                    return basename_a < basename_b;
                });
                list = CMakeList(vec);
            } else {
                // Note: CASE is handled inside CMakeList::sort if needed
                list.sort(natural, descending);
            }
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "REMOVE_DUPLICATES") {
            CommandParser parser("list", "REMOVE_DUPLICATES");
            std::string list_var;
            parser.positional(list_var, "list variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            list.remove_duplicates();
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "SUBLIST") {
            CommandParser parser("list", "SUBLIST");
            std::string list_var, out_var, start_str, length_str;
            parser.positional(list_var, "list variable");
            parser.positional(start_str, "start index");
            parser.positional(length_str, "length");
            parser.positional(out_var, "output variable");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));
            try {
                long start = std::stol(start_str);
                long length = std::stol(length_str);

                // Handle negative start index
                if (start < 0) {
                    start = static_cast<long>(list.size()) + start;
                }

                if (start < 0 || static_cast<size_t>(start) > list.size()) {
                    interp.set_fatal_error("list(SUBLIST) start index out of range");
                    return;
                }

                interp.set_variable(out_var, list.sublist(static_cast<size_t>(start), length).to_string());
            } catch (...) {
                interp.set_fatal_error("list(SUBLIST) invalid indices");
            }
        } else if (operation == "FIND") {
            CommandParser parser("list", "FIND");
            std::string list_var, value, out_var;
            parser.positional(list_var, "list variable");
            parser.positional(value, "value to find");
            parser.positional(out_var, "output variable");
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
            CommandParser parser("list", "REMOVE_ITEM");
            std::string list_var;
            std::vector<std::string> items;
            parser.positional(list_var, "list variable");
            parser.positionals(items, "items");
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
            parser.positional(list_var, "list variable");
            parser.positionals(indices, "indices");
            PARSE_OR_RETURN(parser, interp, sub_args);

            CMakeList list(interp.get_variable(list_var));

            std::vector<size_t> positive_indices;
            for (const auto& idx_str : indices) {
                try {
                    long idx = std::stol(idx_str);
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

            std::sort(positive_indices.begin(), positive_indices.end(), std::greater<size_t>());
            for (size_t idx : positive_indices) {
                list.erase(idx);
            }
            interp.set_variable(list_var, list.to_string());
        } else if (operation == "FILTER") {
            CommandParser parser("list", "FILTER");
            std::string list_var, mode, regex_mode, pattern;
            parser.positional(list_var, "list variable");
            parser.positional(mode, "mode (INCLUDE/EXCLUDE)");
            parser.positional(regex_mode, "REGEX keyword");
            parser.positional(pattern, "regex pattern");
            PARSE_OR_RETURN(parser, interp, sub_args);

            std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c){ return std::toupper(c); });
            std::transform(regex_mode.begin(), regex_mode.end(), regex_mode.begin(), [](unsigned char c){ return std::toupper(c); });

            if (mode != "INCLUDE" && mode != "EXCLUDE") {
                interp.set_fatal_error("list(FILTER) mode must be INCLUDE or EXCLUDE");
                return;
            }

            if (regex_mode != "REGEX") {
                interp.set_fatal_error("list(FILTER) requires REGEX keyword");
                return;
            }

            try {
                std::regex rx(pattern);
                CMakeList list(interp.get_variable(list_var));
                CMakeList result;

                for (size_t i = 0; i < list.size(); ++i) {
                    bool matches = std::regex_match(list[i], rx);
                    if ((mode == "INCLUDE" && matches) || (mode == "EXCLUDE" && !matches)) {
                        result.append(list[i]);
                    }
                }

                interp.set_variable(list_var, result.to_string());
            } catch (const std::regex_error& e) {
                interp.set_fatal_error("list(FILTER) invalid regex: " + std::string(e.what()));
                return;
            }
        } else if (operation == "TRANSFORM") {
            // list(TRANSFORM <list> <ACTION> [<SELECTOR>] [OUTPUT_VARIABLE <output variable>])
            if (sub_args.size() < 2) {
                interp.set_fatal_error("list(TRANSFORM) requires at least list variable and action");
                return;
            }

            std::string list_var = sub_args[0];
            std::string action = sub_args[1];
            std::transform(action.begin(), action.end(), action.begin(), [](unsigned char c){ return std::toupper(c); });

            // Parse remaining arguments
            std::string output_var;
            std::string selector_mode;
            std::vector<std::string> selector_params;
            std::vector<std::string> action_args;

            size_t i = 2;
            while (i < sub_args.size()) {
                std::string arg = sub_args[i];
                std::string arg_upper = arg;
                std::transform(arg_upper.begin(), arg_upper.end(), arg_upper.begin(), [](unsigned char c){ return std::toupper(c); });

                if (arg_upper == "OUTPUT_VARIABLE") {
                    if (i + 1 >= sub_args.size()) {
                        interp.set_fatal_error("list(TRANSFORM) OUTPUT_VARIABLE requires a variable name");
                        return;
                    }
                    output_var = sub_args[++i];
                } else if (arg_upper == "AT") {
                    selector_mode = arg_upper;
                    // Collect all indices until we hit another keyword or end
                    ++i;
                    while (i < sub_args.size()) {
                        std::string next = sub_args[i];
                        std::string next_upper = next;
                        std::transform(next_upper.begin(), next_upper.end(), next_upper.begin(), [](unsigned char c){ return std::toupper(c); });
                        if (next_upper == "OUTPUT_VARIABLE" || next_upper == "FOR" || next_upper == "REGEX") {
                            --i; // Back up so the outer loop processes this keyword
                            break;
                        }
                        selector_params.push_back(next);
                        ++i;
                    }
                    if (selector_params.empty()) {
                        interp.set_fatal_error("list(TRANSFORM) AT requires at least one index");
                        return;
                    }
                } else if (arg_upper == "FOR" || arg_upper == "REGEX") {
                    selector_mode = arg_upper;
                    if (i + 1 >= sub_args.size()) {
                        interp.set_fatal_error("list(TRANSFORM) " + arg_upper + " requires a parameter");
                        return;
                    }
                    selector_params.push_back(sub_args[++i]);
                } else {
                    // Action-specific arguments
                    action_args.push_back(arg);
                }
                ++i;
            }

            CMakeList list(interp.get_variable(list_var));
            CMakeList result = list;

            // Lambda to apply transformation to an element
            auto transform_element = [&](std::string& element) {
                if (action == "APPEND") {
                    if (action_args.empty()) {
                        interp.set_fatal_error("list(TRANSFORM APPEND) requires a string argument");
                        return false;
                    }
                    element += action_args[0];
                } else if (action == "PREPEND") {
                    if (action_args.empty()) {
                        interp.set_fatal_error("list(TRANSFORM PREPEND) requires a string argument");
                        return false;
                    }
                    element = action_args[0] + element;
                } else if (action == "TOLOWER") {
                    std::transform(element.begin(), element.end(), element.begin(), [](unsigned char c){ return std::tolower(c); });
                } else if (action == "TOUPPER") {
                    std::transform(element.begin(), element.end(), element.begin(), [](unsigned char c){ return std::toupper(c); });
                } else if (action == "STRIP") {
                    element = strip(element);
                } else if (action == "REPLACE") {
                    if (action_args.size() < 2) {
                        interp.set_fatal_error("list(TRANSFORM REPLACE) requires regex and replacement string");
                        return false;
                    }
                    try {
                        std::regex rx(action_args[0]);
                        element = std::regex_replace(element, rx, action_args[1]);
                    } catch (const std::regex_error& e) {
                        interp.set_fatal_error("list(TRANSFORM REPLACE) invalid regex: " + std::string(e.what()));
                        return false;
                    }
                } else {
                    interp.set_fatal_error("list(TRANSFORM) unknown action: " + action);
                    return false;
                }
                return true;
            };

            // Apply transformation based on selector
            if (selector_mode.empty()) {
                // Transform all elements
                for (size_t idx = 0; idx < result.size(); ++idx) {
                    std::string elem = result[idx];
                    if (!transform_element(elem)) return;
                    result = CMakeList(result.to_vector());
                    // Need to rebuild list after modification
                    auto vec = result.to_vector();
                    vec[idx] = elem;
                    result = CMakeList(vec);
                }
            } else if (selector_mode == "AT") {
                // Transform at specific indices
                auto vec = result.to_vector();
                for (const auto& idx_str : selector_params) {
                    try {
                        long idx = std::stol(idx_str);
                        if (idx < 0) {
                            idx = static_cast<long>(result.size()) + idx;
                        }
                        if (idx >= 0 && static_cast<size_t>(idx) < vec.size()) {
                            if (!transform_element(vec[idx])) return;
                        }
                    } catch (...) {
                        interp.set_fatal_error("list(TRANSFORM) invalid index in AT selector");
                        return;
                    }
                }
                result = CMakeList(vec);
            } else if (selector_mode == "REGEX") {
                // Transform elements matching regex
                if (selector_params.empty()) {
                    interp.set_fatal_error("list(TRANSFORM) REGEX requires a pattern");
                    return;
                }
                try {
                    std::regex rx(selector_params[0]);
                    auto vec = result.to_vector();
                    for (size_t idx = 0; idx < vec.size(); ++idx) {
                        if (std::regex_match(vec[idx], rx)) {
                            if (!transform_element(vec[idx])) return;
                        }
                    }
                    result = CMakeList(vec);
                } catch (const std::regex_error& e) {
                    interp.set_fatal_error("list(TRANSFORM) invalid REGEX selector: " + std::string(e.what()));
                    return;
                }
            } else if (selector_mode == "FOR") {
                // Transform elements in range (start, stop[, step])
                if (selector_params.empty()) {
                    interp.set_fatal_error("list(TRANSFORM) FOR requires at least start and stop parameters");
                    return;
                }

                // Parse FOR parameters: start, stop, [step]
                std::vector<std::string> for_parts;
                std::string combined = selector_params[0];

                // Split by comma
                size_t pos = 0;
                while ((pos = combined.find(',')) != std::string::npos) {
                    for_parts.push_back(strip(combined.substr(0, pos)));
                    combined.erase(0, pos + 1);
                }
                for_parts.push_back(strip(combined));

                if (for_parts.size() < 2 || for_parts.size() > 3) {
                    interp.set_fatal_error("list(TRANSFORM) FOR requires start,stop or start,stop,step");
                    return;
                }

                try {
                    long start = std::stol(for_parts[0]);
                    long stop = std::stol(for_parts[1]);
                    long step = 1;
                    if (for_parts.size() == 3) {
                        step = std::stol(for_parts[2]);
                        if (step == 0) {
                            interp.set_fatal_error("list(TRANSFORM) FOR step cannot be zero");
                            return;
                        }
                    }

                    // Handle negative indices
                    if (start < 0) {
                        start = static_cast<long>(result.size()) + start;
                    }
                    if (stop < 0) {
                        stop = static_cast<long>(result.size()) + stop;
                    }

                    auto vec = result.to_vector();

                    // Apply transformation to range
                    if (step > 0) {
                        for (long idx = start; idx <= stop && idx < static_cast<long>(vec.size()); idx += step) {
                            if (idx >= 0) {
                                if (!transform_element(vec[idx])) return;
                            }
                        }
                    } else {
                        for (long idx = start; idx >= stop && idx >= 0; idx += step) {
                            if (idx < static_cast<long>(vec.size())) {
                                if (!transform_element(vec[idx])) return;
                            }
                        }
                    }

                    result = CMakeList(vec);
                } catch (...) {
                    interp.set_fatal_error("list(TRANSFORM) invalid FOR parameters");
                    return;
                }
            }

            // Store result
            if (output_var.empty()) {
                interp.set_variable(list_var, result.to_string());
            } else {
                interp.set_variable(output_var, result.to_string());
            }
        } else {
            interp.set_fatal_error("Unknown list operation: " + operation);
        }
    });
}

} // namespace dmake
