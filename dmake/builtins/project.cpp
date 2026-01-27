#include "registry.hpp"
#include "../interperter.hpp"

namespace dmake {

void register_project_builtins(Interpreter& interp) {
    interp.add_builtin("cmake_minimum_required", [](Interpreter&, const std::vector<std::string>&) {});
    interp.add_builtin("project", [](Interpreter& interp, const std::vector<std::string>& args) {
        if(args.size() == 0) {
            interp.set_fatal_error("project() requires at least one argument");
            return;
        }

        interp.set_variable("PROJECT_NAME", args[0]);
        // ignore the rest - we can deal with them later
    });
    interp.add_builtin("cmake_policy", [](Interpreter&, const std::vector<std::string>&) {});
    interp.add_builtin("mark_as_advanced", [](Interpreter&, const std::vector<std::string>&) {});
}

} // namespace dmake
