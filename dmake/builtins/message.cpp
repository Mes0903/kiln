#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <sstream>
#include <algorithm>

namespace dmake {

void register_message_builtins(Interpreter& interp) {
    interp.add_builtin("message", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("message() requires at least one argument");
            return;
        }

        std::string first_arg = args[0];
        std::string mode_upper = first_arg;
        std::transform(mode_upper.begin(), mode_upper.end(), mode_upper.begin(), ::toupper);

        size_t arg_idx = 0;
        std::string mode = "INFO";
        bool mode_found = true;

        if (mode_upper == "STATUS") { mode = "STATUS"; arg_idx = 1; }
        else if (mode_upper == "WARNING") { mode = "WARN"; arg_idx = 1; }
        else if (mode_upper == "FATAL_ERROR") { mode = "FATAL"; arg_idx = 1; }
        else if (mode_upper == "SEND_ERROR") { mode = "ERROR"; arg_idx = 1; }
        else if (mode_upper == "AUTHOR_WARNING") { mode = "WARN"; arg_idx = 1; }
        else if (mode_upper == "DEPRECATION") { mode = "WARN"; arg_idx = 1; }
        else if (mode_upper == "NOTICE") { mode = "INFO"; arg_idx = 1; }
        else if (mode_upper == "DEBUG") { mode = "INFO"; arg_idx = 1; }
        else if (mode_upper == "TRACE") { mode = "INFO"; arg_idx = 1; }
        else {
            mode_found = false;
        }

        std::vector<std::string> message_args;
        if (mode_found) {
            message_args.assign(args.begin() + 1, args.end());
        } else {
            message_args = args;
        }

        std::ostringstream oss;
        for (const auto& arg : message_args) {
            oss << arg;
        }
        std::string content = oss.str();

        if (mode == "FATAL") {
            interp.set_fatal_error(content);
        } else {
            interp.print_message(mode, content, mode == "WARN" || mode == "ERROR");
        }
    });
}

} // namespace dmake
