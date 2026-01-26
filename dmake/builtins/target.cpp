#include "registry.hpp"
#include "../interperter.hpp"
#include "../target.hpp"
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace dmake {

void register_target_builtins(Interpreter& interp) {
    // Helper to configure common target properties (C++ standard, flags, inherited directories)
    auto configure_target = [](Interpreter& interp, const std::shared_ptr<Target>& target) {
        // Set C++ standard from CMAKE_CXX_STANDARD if available
        std::string cxx_std = interp.get_variable("CMAKE_CXX_STANDARD");
        if (!cxx_std.empty()) {
            target->set_cxx_standard(cxx_std);
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
                    target->add_compile_options(flag_list, PropertyVisibility::PRIVATE);
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
        target->add_include_directories(root->accumulated_include_directories_, PropertyVisibility::PRIVATE);
        target->add_link_directories(root->accumulated_link_directories_, PropertyVisibility::PRIVATE);
    };

    // Helper for adding sources to a target with validation
    auto add_sources_to_target = [](Interpreter& interp, const std::shared_ptr<Target>& target, const std::string& src_dir, const std::string& val) -> bool {
        CMakeList lst(val);
        std::vector<std::string> sources;
        for(const auto& file : lst) {
            std::filesystem::path p(file);
            if (!p.is_absolute()) {
                p = std::filesystem::path(src_dir) / p;
            }
            if (!std::filesystem::exists(p)) {
                interp.set_fatal_error("Source file not found: " + p.string() + " (for target " + target->get_name() + ")");
                return false;
            }
            sources.push_back(file);
        }
        target->add_sources(sources, PropertyVisibility::PRIVATE);
        return true;
    };

    interp.add_builtin("add_executable", [&](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) return;
        std::string name = args[0];
        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");

        auto target = std::make_shared<Target>(name, TargetType::EXECUTABLE, src_dir, bin_dir);
        configure_target(interp, target);

        for(size_t i = 1; i < args.size(); ++i) {
            if (!add_sources_to_target(interp, target, src_dir, args[i])) return;
        }
        interp.get_root()->targets_[name] = target;
    });

    interp.add_builtin("add_library", [&](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) return;
        std::string name = args[0];
        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");

        TargetType type = TargetType::STATIC_LIBRARY; // Default
        size_t start_idx = 1;
        
        if (args.size() > 1) {
            std::string first_val = args[1];
            if (first_val == "SHARED") {
                type = TargetType::SHARED_LIBRARY;
                start_idx = 2;
            } else if (first_val == "STATIC") {
                type = TargetType::STATIC_LIBRARY;
                start_idx = 2;
            } else if (first_val == "OBJECT") {
                type = TargetType::OBJECT_LIBRARY;
                start_idx = 2;
            } else if (first_val == "INTERFACE") {
                type = TargetType::INTERFACE_LIBRARY;
                start_idx = 2;
            }
        }

        auto target = std::make_shared<Target>(name, type, src_dir, bin_dir);
        configure_target(interp, target);

        for(size_t i = start_idx; i < args.size(); ++i) {
            if (!add_sources_to_target(interp, target, src_dir, args[i])) return;
        }
        interp.get_root()->targets_[name] = target;
    });

    auto get_target_or_error = [](Interpreter& interp, const std::vector<std::string>& args, const std::string& cmd_name) -> std::shared_ptr<Target> {
        if (args.empty()) {
            interp.set_fatal_error(cmd_name + "() requires a target name as first argument");
            return nullptr;
        }
        std::string name = args[0];
        auto& targets = interp.get_root()->targets_;
        auto it = targets.find(name);
        if (it == targets.end()) {
            interp.set_fatal_error(cmd_name + "() called on unknown target '" + name + "'");
            return nullptr;
        }
        return it->second;
    };

    interp.add_builtin("target_include_directories", [get_target_or_error](Interpreter& interp, const std::vector<std::string>& args) {
        auto target = get_target_or_error(interp, args, "target_include_directories");
        if (!target) return;

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> dirs;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = args[i];
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else dirs.push_back(val);
        }
        target->add_include_directories(dirs, vis);
    });

    interp.add_builtin("target_compile_definitions", [get_target_or_error](Interpreter& interp, const std::vector<std::string>& args) {
        auto target = get_target_or_error(interp, args, "target_compile_definitions");
        if (!target) return;

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> defs;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = args[i];
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else defs.push_back(val);
        }
        target->add_compile_definitions(defs, vis);
    });

    interp.add_builtin("target_compile_options", [get_target_or_error](Interpreter& interp, const std::vector<std::string>& args) {
        auto target = get_target_or_error(interp, args, "target_compile_options");
        if (!target) return;

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> opts;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = args[i];
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else opts.push_back(val);
        }
        target->add_compile_options(opts, vis);
    });

    interp.add_builtin("target_link_libraries", [get_target_or_error](Interpreter& interp, const std::vector<std::string>& args) {
        auto target = get_target_or_error(interp, args, "target_link_libraries");
        if (!target) return;

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> libs;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = args[i];
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else libs.push_back(val);
        }
        target->add_linked_libraries(libs, vis);
    });

    interp.add_builtin("set_target_properties", [get_target_or_error](Interpreter& interp, const std::vector<std::string>& args) {
        auto target = get_target_or_error(interp, args, "set_target_properties");
        if (!target) return;
        if (args.size() < 4 || args[1] != "PROPERTIES") return;
        
        for(size_t i = 2; i < args.size() - 1; i+=2) {
            std::string prop_name = args[i];
            std::string prop_value = args[i+1];

            if (prop_name == "OUTPUT_NAME") {
                target->set_output_name(prop_value);
            } else if (prop_name == "CXX_STANDARD") {
                target->set_cxx_standard(prop_value);
            }
        }
    });

    interp.add_builtin("target_precompile_headers", [get_target_or_error](Interpreter& interp, const std::vector<std::string>& args) {
        auto target = get_target_or_error(interp, args, "target_precompile_headers");
        if (!target) return;

        PropertyVisibility vis = PropertyVisibility::PRIVATE;
        std::vector<std::string> headers;
        for(size_t i = 1; i < args.size(); ++i) {
            std::string val = args[i];
            if (val == "PUBLIC") vis = PropertyVisibility::PUBLIC;
            else if (val == "PRIVATE") vis = PropertyVisibility::PRIVATE;
            else if (val == "INTERFACE") vis = PropertyVisibility::INTERFACE;
            else headers.push_back(val);
        }

        if (headers.empty()) {
            interp.set_fatal_error("target_precompile_headers() requires at least one header file");
            return;
        }

        target->add_precompiled_headers(headers, vis);
    });

    interp.add_builtin("include_directories", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("include_directories() requires at least one directory argument");
            return;
        }

        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        auto& root_dirs = interp.get_root()->accumulated_include_directories_;

        for (const auto& arg : args) {
            std::string dir = arg;
            std::filesystem::path resolved = std::filesystem::path(dir).is_absolute() ?
                std::filesystem::path(dir) :
                std::filesystem::path(src_dir) / dir;
            root_dirs.push_back(resolved.string());
        }
    });

    interp.add_builtin("link_directories", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("link_directories() requires at least one directory argument");
            return;
        }

        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        auto& root_dirs = interp.get_root()->accumulated_link_directories_;

        for (const auto& arg : args) {
            std::string dir = arg;
            std::filesystem::path resolved = std::filesystem::path(dir).is_absolute() ?
                std::filesystem::path(dir) :
                std::filesystem::path(src_dir) / dir;
            root_dirs.push_back(resolved.string());
        }
    });
}

} // namespace dmake