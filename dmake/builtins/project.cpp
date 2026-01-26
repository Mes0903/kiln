#include "registry.hpp"
#include "../interperter.hpp"

namespace dmake {

void register_project_builtins(Interpreter& interp) {
    interp.add_builtin("cmake_minimum_required", [](Interpreter&, const std::vector<std::string>&) {});
    interp.add_builtin("project", [](Interpreter&, const std::vector<std::string>&) {});
    interp.add_builtin("cmake_policy", [](Interpreter&, const std::vector<std::string>&) {});
}

} // namespace dmake
