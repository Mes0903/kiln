#include "registry.hpp"
#include "../interperter.hpp"
#include <sstream>
#include <algorithm>

namespace dmake {

void register_message_builtins(Interpreter& interp) {
    interp.add_builtin("message", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.empty()) return;

        size_t arg_idx = 0;
        std::string mode = "INFO";

        if (!args[0].quoted) {
            std::string first_arg = interp.evaluate_argument(args[0]);
            std::transform(first_arg.begin(), first_arg.end(), first_arg.begin(), ::toupper);

            if (first_arg == "STATUS") { mode = "STATUS"; arg_idx = 1; }
            else if (first_arg == "WARNING") { mode = "WARN"; arg_idx = 1; }
            else if (first_arg == "FATAL_ERROR") { mode = "FATAL"; arg_idx = 1; }
            else if (first_arg == "ERROR") { mode = "ERROR"; arg_idx = 1; }
        }

        std::ostringstream oss;
        for (size_t i = arg_idx; i < args.size(); ++i) {
            oss << interp.evaluate_argument(args[i]);
            if (i < args.size() - 1) oss << " ";
        }
        std::string content = oss.str();

        if (mode == "FATAL") interp.set_fatal_error(content);
        else interp.print_message(mode, content, mode == "WARN" || mode == "ERROR");
    });
}

} // namespace dmake
