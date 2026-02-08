#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <algorithm>
#include <unistd.h>

namespace dmake {

void register_variable_builtins(Interpreter& interp) {
    interp.add_builtin("set", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("set() requires at least one argument");
            return;
        }

        std::string var_name = args[0];

        // INVALID: set(CACHE{VAR} value) - reject this (case-insensitive)
        std::string var_name_upper = dmake::to_upper(var_name);
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
                CMakeArray value_list(value_args);
                setenv(env_var_name.c_str(), value_list.to_string().c_str(), 1);
            }
            return;
        }

        // Check for CACHE keyword (case-insensitive)
        auto cache_it = std::find_if(args.begin() + 1, args.end(),
            [](const std::string& s) {
                return dmake::to_upper(s) == "CACHE";
            });

        // Check for PARENT_SCOPE keyword (case-insensitive)
        auto parent_it = std::find_if(args.begin() + 1, args.end(),
            [](const std::string& s) {
                return dmake::to_upper(s) == "PARENT_SCOPE";
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
            CMakeArray value_list(value_args);
            std::string value = value_list.to_string();

            // Set in cache namespace only - CACHE variables are globally accessible
            // via get_variable()'s cache lookup, not via local scope
            auto* root = interp.get_root();
            root->cache_variables_[var_name] = value;
            return;
        }

        // Handle: set(VAR value PARENT_SCOPE)
        if (parent_it != args.end()) {
            // Value is everything between var_name and PARENT_SCOPE keyword
            std::vector<std::string> value_args(args.begin() + 1, parent_it);
            CMakeArray value_list(value_args);
            std::string value = value_list.to_string();

            // Set in parent scope (handles both function and subdirectory contexts)
            auto result = interp.set_variable_parent_scope(var_name, value);
            if (!result) {
                interp.set_fatal_error("set() " + result.error());
            }
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
            std::string linker_type_upper = dmake::to_upper(linker_type);

            if (linker_type_upper != "BFD" && linker_type_upper != "GOLD" &&
                linker_type_upper != "MOLD" && linker_type_upper != "LLD") {
                interp.set_fatal_error("Invalid CMAKE_LINKER_TYPE: " + linker_type + ". Must be one of: BFD, GOLD, MOLD, LLD");
                return;
            }
        }

        // Regular: set(VAR value)
        // Special case: set(VAR) with no value is equivalent to unset(VAR)
        if (args.size() == 1) {
            interp.unset_variable(var_name);
            return;
        }

        std::vector<std::string> value_args(args.begin() + 1, args.end());
        CMakeArray value_list(value_args);
        std::string value = value_list.to_string();

        interp.set_variable(var_name, value);
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
                return dmake::to_upper(s) == "CACHE";
            });

        // Check for PARENT_SCOPE keyword (case-insensitive)
        auto parent_it = std::find_if(args.begin() + 1, args.end(),
            [](const std::string& s) {
                return dmake::to_upper(s) == "PARENT_SCOPE";
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
            auto result = interp.unset_variable_parent_scope(var_name);
            if (!result) {
                interp.set_fatal_error("unset() " + result.error());
            }
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
        parser.positional(option_name, "option name");
        parser.positional(help_message, "help message");
        parser.positional(initial_value, "initial value", false);
        PARSE_OR_RETURN(parser, interp, args);

        std::string value = initial_value.empty() ? "OFF" : initial_value;
        if(!interp.is_variable_set(option_name)) {
            interp.set_variable(option_name, value);
        }
    });

    interp.add_builtin("cmake_parse_arguments", [](Interpreter& interp, const std::vector<std::string>& args) {
        // Parse two forms:
        // 1. PARSE_ARGV <N> <prefix> <options> <one_value> <multi_value>
        // 2. <prefix> <options> <one_value> <multi_value> <args>...

        if (args.empty()) {
            interp.set_fatal_error("cmake_parse_arguments() requires at least 1 argument");
            return;
        }

        std::string prefix;
        CMakeArray options, one_value_keywords, multi_value_keywords;
        std::vector<std::string> to_parse;

        // Determine which form we're using
        bool is_parse_argv = (args[0] == "PARSE_ARGV");

        if (is_parse_argv) {
            // PARSE_ARGV form: PARSE_ARGV <N> <prefix> <options> <one_value> <multi_value>
            if (args.size() < 6) {
                interp.set_fatal_error("cmake_parse_arguments(PARSE_ARGV ...) requires 6 arguments");
                return;
            }

            // Parse N (starting index)
            int start_idx = 0;
            try {
                start_idx = std::stoi(args[1]);
            } catch (...) {
                interp.set_fatal_error("cmake_parse_arguments(PARSE_ARGV): index must be a number");
                return;
            }

            if (start_idx < 0) {
                interp.set_fatal_error("cmake_parse_arguments(PARSE_ARGV): index cannot be negative");
                return;
            }

            prefix = args[2];
            options = CMakeArray(args[3]);
            one_value_keywords = CMakeArray(args[4]);
            multi_value_keywords = CMakeArray(args[5]);

            // Read arguments from ARGV variables
            std::string argc_str = interp.get_variable("ARGC");
            int argc = 0;
            if (!argc_str.empty()) {
                try {
                    argc = std::stoi(argc_str);
                } catch (...) {
                    // If ARGC is not a number, treat as 0
                    argc = 0;
                }
            }

            // Collect arguments from ARGV{start_idx} to ARGV{argc-1}
            for (int i = start_idx; i < argc; ++i) {
                std::string argv_var = "ARGV" + std::to_string(i);
                std::string value = interp.get_variable(argv_var);
                // ARGV variables preserve semicolons, so we add them as-is
                to_parse.push_back(value);
            }
        } else {
            // Standard form: <prefix> <options> <one_value> <multi_value> <args>...
            if (args.size() < 4) {
                interp.set_fatal_error("cmake_parse_arguments() requires at least 4 arguments");
                return;
            }

            prefix = args[0];
            options = CMakeArray(args[1]);
            one_value_keywords = CMakeArray(args[2]);
            multi_value_keywords = CMakeArray(args[3]);

            // Remaining arguments are what we parse
            to_parse.assign(args.begin() + 4, args.end());
        }

        // Initialize all option variables to FALSE
        for (const auto& opt : options) {
            if (!opt.empty()) {
                interp.set_variable(prefix + "_" + opt, "FALSE");
            }
        }

        // Unset all one-value keyword variables (undefined if not provided)
        for (const auto& kw : one_value_keywords) {
            if (!kw.empty()) {
                interp.unset_variable(prefix + "_" + kw);
            }
        }

        // Unset all multi-value keyword variables (undefined if not provided)
        for (const auto& kw : multi_value_keywords) {
            if (!kw.empty()) {
                interp.unset_variable(prefix + "_" + kw);
            }
        }

        // Track unparsed arguments and keywords missing values
        std::vector<std::string> unparsed;
        std::vector<std::string> keywords_missing_values;

        // Parse the arguments
        for (size_t i = 0; i < to_parse.size(); ) {
            std::string arg = to_parse[i];

            // Check if it's an option (boolean flag)
            if (options.contains(arg)) {
                interp.set_variable(prefix + "_" + arg, "TRUE");
                i++;
                continue;
            }

            // Check if it's a one-value keyword
            if (one_value_keywords.contains(arg)) {
                if (i + 1 < to_parse.size()) {
                    // Check if next argument is also a keyword - if so, this keyword has no value
                    const std::string& next_arg = to_parse[i + 1];
                    if (options.contains(next_arg) ||
                        one_value_keywords.contains(next_arg) ||
                        multi_value_keywords.contains(next_arg)) {
                        // Next arg is a keyword, so this one-value keyword has no value
                        interp.set_variable(prefix + "_" + arg, "");
                        keywords_missing_values.push_back(arg);
                        i++;
                    } else {
                        // Next argument is the value
                        interp.set_variable(prefix + "_" + arg, to_parse[i + 1]);
                        i += 2;
                    }
                } else {
                    // Keyword at end with no value
                    interp.set_variable(prefix + "_" + arg, "");
                    keywords_missing_values.push_back(arg);
                    i++;
                }
                continue;
            }

            // Check if it's a multi-value keyword
            if (multi_value_keywords.contains(arg)) {
                std::string keyword = arg;
                i++; // Move past the keyword

                std::vector<std::string> values;
                // Collect values until we hit another keyword or end
                while (i < to_parse.size() &&
                       !options.contains(to_parse[i]) &&
                       !one_value_keywords.contains(to_parse[i]) &&
                       !multi_value_keywords.contains(to_parse[i])) {
                    values.push_back(to_parse[i]);
                    i++;
                }

                // Set variable (even if empty list)
                CMakeArray value_list(values);
                interp.set_variable(prefix + "_" + keyword, value_list.to_string());
                continue;
            }

            // Not a keyword - add to unparsed
            unparsed.push_back(arg);
            i++;
        }

        // Set special output variables
        CMakeArray unparsed_list(unparsed);
        interp.set_variable(prefix + "_UNPARSED_ARGUMENTS", unparsed_list.to_string());

        CMakeArray missing_list(keywords_missing_values);
        interp.set_variable(prefix + "_KEYWORDS_MISSING_VALUES", missing_list.to_string());
    });

    interp.add_builtin("variable_watch", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("variable_watch() requires at least one argument (variable name)");
            return;
        }
        if (args.size() > 2) {
            interp.set_fatal_error("variable_watch() takes at most 2 arguments (variable name, optional callback)");
            return;
        }

        std::optional<std::string> callback;
        if (args.size() == 2) {
            callback = args[1];
        }
        interp.add_variable_watch(args[0], std::move(callback));
    });

    interp.add_builtin("site_name", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.size() != 1) {
            interp.set_fatal_error("site_name() requires exactly one argument");
            return;
        }

        std::string var_name = args[0];
        std::string hostname;

        // Get hostname using gethostname()
        char buffer[HOST_NAME_MAX + 1];
        if (gethostname(buffer, sizeof(buffer)) == 0) {
            hostname = buffer;
        } else {
            // Fallback: try HOSTNAME environment variable
            if (const char* env_hostname = std::getenv("HOSTNAME")) {
                hostname = env_hostname;
            }
            // If both fail, hostname remains empty
        }

        interp.set_variable(var_name, hostname);
    });
}

} // namespace dmake
