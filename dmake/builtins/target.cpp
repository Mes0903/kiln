#include "registry.hpp"
#include "../interperter.hpp"
#include "../artifact.hpp"
#include <sstream>
#include <algorithm>

namespace dmake {

void register_target_builtins(Interpreter& interp) {
    // Helper to configure common artifact properties (C++ standard, flags, inherited directories)
    auto configure_artifact = [](Interpreter& interp, const std::shared_ptr<Artifact>& artifact) {
        // Set C++ standard from CMAKE_CXX_STANDARD if available
        std::string cxx_std = interp.get_variable("CMAKE_CXX_STANDARD");
        if (!cxx_std.empty()) {
            artifact->set_cxx_standard(cxx_std);
        }

        // Apply CMAKE_CXX_FLAGS and CMAKE_CXX_FLAGS_<CONFIG>
        auto apply_cxx_flags = [&](const std::string& flags_var) {
            std::string flags = interp.get_variable(flags_var);
            if (!flags.empty()) {
                // Split flags by spaces and add individually
                std::istringstream iss(flags);
                std::vector<std::string> flag_list;
                std::string flag;
                while (iss >> flag) {
                    flag_list.push_back(flag);
                }
                if (!flag_list.empty()) {
                    artifact->add_compile_options(flag_list, PropertyVisibility::PRIVATE);
                }
            }
        };

        apply_cxx_flags("CMAKE_CXX_FLAGS");
        std::string build_type = interp.get_variable("CMAKE_BUILD_TYPE");
        if (!build_type.empty()) {
            std::string upper_type = build_type;
            std::transform(upper_type.begin(), upper_type.end(), upper_type.begin(), ::toupper);
            apply_cxx_flags("CMAKE_CXX_FLAGS_" + upper_type);
        }

        // Inherit accumulated include and link directories
        auto* root = interp.get_root();
        artifact->add_include_directories(root->accumulated_include_directories_, PropertyVisibility::PRIVATE);
        artifact->add_link_directories(root->accumulated_link_directories_, PropertyVisibility::PRIVATE);
    };

    interp.add_builtin("add_executable", [&configure_artifact](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.empty()) return;
        std::string name = interp.evaluate_argument(args[0]);
        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");

        auto artifact = std::make_shared<ExecutableArtifact>(name, src_dir, bin_dir);
        configure_artifact(interp, artifact);

        std::vector<std::string> sources;
        for(size_t i = 1; i < args.size(); ++i) {
            CMakeList lst(interp.evaluate_argument(args[i]));
            for(const auto& file : lst) {
                sources.push_back(file);
            }
        }
        artifact->add_sources(sources, PropertyVisibility::PRIVATE);
        interp.get_root()->artifacts_[name] = artifact;
    });

    interp.add_builtin("add_library", [&configure_artifact](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 2) return;
        std::string name = interp.evaluate_argument(args[0]);
        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");

        std::vector<std::string> sources;
        bool is_shared = false;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = interp.evaluate_argument(args[i]);
            if(val == "SHARED") is_shared = true;
            else if (val != "STATIC") {
                CMakeList lst(val);
                for(const auto& file : lst) {
                    sources.push_back(file);
                }
            }
        }
        auto artifact = std::make_shared<LibraryArtifact>(name, is_shared ? ArtifactType::SHARED_LIBRARY : ArtifactType::STATIC_LIBRARY, src_dir, bin_dir);
        configure_artifact(interp, artifact);

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

    interp.add_builtin("target_compile_definitions", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 2) return;
        std::string name = interp.evaluate_argument(args[0]);
        auto& artifacts = interp.get_root()->artifacts_;
        auto it = artifacts.find(name);
        if (it == artifacts.end()) return;

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> defs;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = interp.evaluate_argument(args[i]);
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else defs.push_back(val);
        }
        it->second->add_compile_definitions(defs, vis);
    });

    interp.add_builtin("target_compile_options", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 2) return;
        std::string name = interp.evaluate_argument(args[0]);
        auto& artifacts = interp.get_root()->artifacts_;
        auto it = artifacts.find(name);
        if (it == artifacts.end()) return;

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> opts;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = interp.evaluate_argument(args[i]);
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else opts.push_back(val);
        }
        it->second->add_compile_options(opts, vis);
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
            std::string prop_name = interp.evaluate_argument(args[i]);
            std::string prop_value = interp.evaluate_argument(args[i+1]);

            if (prop_name == "OUTPUT_NAME") {
                it->second->set_output_name(prop_value);
            } else if (prop_name == "CXX_STANDARD") {
                it->second->set_cxx_standard(prop_value);
            }
        }
    });

    interp.add_builtin("target_precompile_headers", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.size() < 2) {
            interp.set_fatal_error("target_precompile_headers() requires at least 2 arguments: target_name and header(s)");
            return;
        }

        std::string name = interp.evaluate_argument(args[0]);
        auto& artifacts = interp.get_root()->artifacts_;
        auto it = artifacts.find(name);
        if (it == artifacts.end()) {
            interp.set_fatal_error("target_precompile_headers() called on unknown target '" + name + "'");
            return;
        }

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> headers;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = interp.evaluate_argument(args[i]);
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else headers.push_back(val);
        }

        if (headers.empty()) {
            interp.set_fatal_error("target_precompile_headers() requires at least one header file");
            return;
        }

        it->second->add_precompiled_headers(headers, vis);
    });

    interp.add_builtin("include_directories", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.empty()) {
            interp.set_fatal_error("include_directories() requires at least one directory argument");
            return;
        }

        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        auto& root_dirs = interp.get_root()->accumulated_include_directories_;

        for (const auto& arg : args) {
            std::string dir = interp.evaluate_argument(arg);
            // Resolve relative paths to absolute based on current source directory
            std::filesystem::path resolved = std::filesystem::path(dir).is_absolute() ?
                std::filesystem::path(dir) :
                std::filesystem::path(src_dir) / dir;
            root_dirs.push_back(resolved.string());
        }
    });

    interp.add_builtin("link_directories", [](Interpreter& interp, const std::vector<Argument>& args) {
        if (args.empty()) {
            interp.set_fatal_error("link_directories() requires at least one directory argument");
            return;
        }

        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        auto& root_dirs = interp.get_root()->accumulated_link_directories_;

        for (const auto& arg : args) {
            std::string dir = interp.evaluate_argument(arg);
            // Resolve relative paths to absolute based on current source directory
            std::filesystem::path resolved = std::filesystem::path(dir).is_absolute() ?
                std::filesystem::path(dir) :
                std::filesystem::path(src_dir) / dir;
            root_dirs.push_back(resolved.string());
        }
    });
}

} // namespace dmake
