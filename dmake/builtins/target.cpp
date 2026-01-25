#include "registry.hpp"
#include "../interperter.hpp"
#include "../artifact.hpp"

namespace dmake {

void register_target_builtins(Interpreter& interp) {
    interp.add_builtin("add_executable", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.empty()) return;
        std::string name = interp.evaluate_argument(args[0]);
        auto artifact = std::make_shared<ExecutableArtifact>(name);
        std::vector<std::string> sources;
        for(size_t i = 1; i < args.size(); ++i) sources.push_back(interp.evaluate_argument(args[i]));
        artifact->add_sources(sources, PropertyVisibility::PRIVATE);
        interp.get_root()->artifacts_[name] = artifact;
    });

    interp.add_builtin("add_library", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 2) return;
        std::string name = interp.evaluate_argument(args[0]);
        std::vector<std::string> sources;
        bool is_shared = false;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = interp.evaluate_argument(args[i]);
            if(val == "SHARED") is_shared = true;
            else if (val != "STATIC") sources.push_back(val);
        }
        auto artifact = std::make_shared<LibraryArtifact>(name, is_shared ? ArtifactType::SHARED_LIBRARY : ArtifactType::STATIC_LIBRARY);
        artifact->add_sources(sources, PropertyVisibility::PRIVATE);
        interp.get_root()->artifacts_[name] = artifact;
    });

    interp.add_builtin("target_include_directories", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 2) return;
        std::string name = interp.evaluate_argument(args[0]);
        auto& artifacts = interp.get_root()->artifacts_;
        auto it = artifacts.find(name);
        if (it == artifacts.end()) return;

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> dirs;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = interp.evaluate_argument(args[i]);
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else dirs.push_back(val);
        }
        it->second->add_include_directories(dirs, vis);
    });

    interp.add_builtin("target_link_libraries", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 2) return;
        std::string name = interp.evaluate_argument(args[0]);
        auto& artifacts = interp.get_root()->artifacts_;
        auto it = artifacts.find(name);
        if (it == artifacts.end()) return;

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> libs;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = interp.evaluate_argument(args[i]);
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else libs.push_back(val);
        }
        it->second->add_linked_libraries(libs, vis);
    });

    interp.add_builtin("set_target_properties", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 4) return;
        std::string name = interp.evaluate_argument(args[0]);
        auto& artifacts = interp.get_root()->artifacts_;
        auto it = artifacts.find(name);
        if (it == artifacts.end()) return;
        if(interp.evaluate_argument(args[1]) != "PROPERTIES") return;
        for(size_t i = 2; i < args.size() - 1; i+=2) {
            if (interp.evaluate_argument(args[i]) == "OUTPUT_NAME")
                it->second->set_output_name(interp.evaluate_argument(args[i+1]));
        }
    });
}

} // namespace dmake
