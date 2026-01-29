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

        // Parse LANGUAGES keyword
        std::vector<std::string> languages;
        bool in_languages = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "LANGUAGES") {
                in_languages = true;
            } else if (args[i] == "VERSION" || args[i] == "DESCRIPTION" ||
                       args[i] == "HOMEPAGE_URL") {
                in_languages = false;
            } else if (in_languages) {
                languages.push_back(args[i]);
            }
        }

        // If languages were specified, update the ENABLED_LANGUAGES global property
        if (!languages.empty()) {
            std::string enabled_langs;
            for (size_t i = 0; i < languages.size(); ++i) {
                if (i > 0) enabled_langs += ";";
                enabled_langs += languages[i];
            }
            interp.get_global_properties()["ENABLED_LANGUAGES"] = enabled_langs;
        }
        // If LANGUAGES was not specified, CMake defaults to C and CXX
        // which is already set in the interpreter constructor
    });
    interp.add_builtin("cmake_policy", [](Interpreter&, const std::vector<std::string>&) {});
    interp.add_builtin("mark_as_advanced", [](Interpreter&, const std::vector<std::string>&) {});
}

} // namespace dmake
