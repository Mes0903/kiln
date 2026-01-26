#include "registry.hpp"
#include "../interperter.hpp"
#include <sstream>
#include <algorithm>

namespace dmake {

void register_message_builtins(Interpreter& interp) {
    interp.add_builtin("message", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("message called with incorrect number of arguments");
            return;
        }

        size_t arg_idx = 0;
        std::string mode = "INFO";
        std::string first_arg = args[0];
        std::transform(first_arg.begin(), first_arg.end(), first_arg.begin(), ::toupper);

        if (first_arg == "STATUS") { mode = "STATUS"; arg_idx = 1; }
        else if (first_arg == "WARNING") { mode = "WARN"; arg_idx = 1; }
        else if (first_arg == "FATAL_ERROR") { mode = "FATAL"; arg_idx = 1; }
        else if (first_arg == "ERROR") { mode = "ERROR"; arg_idx = 1; }
        else if (first_arg == "DEPRECATION") { mode = "WARN"; arg_idx = 1; }
        else if (first_arg == "NOTICE") { mode = "INFO"; arg_idx = 1; }
        else if (first_arg == "DEBUG") { mode = "INFO"; arg_idx = 1; } // Treat debug as info for now
        else if (first_arg == "TRACE") { mode = "INFO"; arg_idx = 1; }

        // If the first arg was a mode, but no message follows, args.size() == 1, arg_idx == 1. Loop won't run.
        // If args.size() == arg_idx (e.g. message(STATUS)), we print empty string.

        std::ostringstream oss;
        for (size_t i = arg_idx; i < args.size(); ++i) {
            oss << args[i];
        }
        std::string content = oss.str();

        if (mode == "FATAL") interp.set_fatal_error(content);
        else interp.print_message(mode, content, mode == "WARN" || mode == "ERROR");
    });
}

} // namespace dmake
