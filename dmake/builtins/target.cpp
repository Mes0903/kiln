#include "registry.hpp"
#include "../interperter.hpp"
#include "../target.hpp"
#include "../command_parser.hpp"
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
    auto add_sources_to_target = [](Interpreter& interp, const std::shared_ptr<Target>& target, const std::string& src_dir, const std::vector<std::string>& sources) -> bool {
        for(const auto& file : sources) {
            std::filesystem::path p(file);
            if (!p.is_absolute()) {
                p = std::filesystem::path(src_dir) / p;
            }
            if (!std::filesystem::exists(p)) {
                interp.set_fatal_error("Source file not found: " + p.string() + " (for target " + target->get_name() + ")");
                return false;
            }
        }
        target->add_sources(sources, PropertyVisibility::PRIVATE);
        return true;
    };

    interp.add_builtin("add_executable", [&](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("add_executable");
        std::string name;
        std::vector<std::string> sources;
        parser.add_positional(name, "target name");
        parser.add_default_list(sources);
        PARSE_OR_RETURN(parser, interp, args);

        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");

        auto target = std::make_shared<Target>(name, TargetType::EXECUTABLE, src_dir, bin_dir);
        configure_target(interp, target);

        if (!add_sources_to_target(interp, target, src_dir, sources)) return;
        interp.get_root()->targets_[name] = target;
    });

    interp.add_builtin("add_library", [&](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("add_library");
        std::string name;
        bool shared = false, static_lib = false, object_lib = false, interface_lib = false;
        std::vector<std::string> sources;

        parser.add_positional(name, "target name");
        parser.add_flag("SHARED", shared);
        parser.add_flag("STATIC", static_lib);
        parser.add_flag("OBJECT", object_lib);
        parser.add_flag("INTERFACE", interface_lib);
        parser.add_default_list(sources);
        PARSE_OR_RETURN(parser, interp, args);

        int type_count = (shared ? 1 : 0) + (static_lib ? 1 : 0) + (object_lib ? 1 : 0) + (interface_lib ? 1 : 0);
        if (type_count > 1) {
            interp.set_fatal_error("add_library() called with multiple conflicting types");
            return;
        }

        TargetType type = TargetType::STATIC_LIBRARY;
        if (shared) type = TargetType::SHARED_LIBRARY;
        else if (object_lib) type = TargetType::OBJECT_LIBRARY;
        else if (interface_lib) type = TargetType::INTERFACE_LIBRARY;

        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");

        auto target = std::make_shared<Target>(name, type, src_dir, bin_dir);
        configure_target(interp, target);

        if (!add_sources_to_target(interp, target, src_dir, sources)) return;
        interp.get_root()->targets_[name] = target;
    });

    auto get_target_from_name = [](Interpreter& interp, const std::string& name, const std::string& cmd_name) -> std::shared_ptr<Target> {
        auto& targets = interp.get_root()->targets_;
        auto it = targets.find(name);
        if (it == targets.end()) {
            interp.set_fatal_error(cmd_name + "() called on unknown target '" + name + "'");
            return nullptr;
        }
        return it->second;
    };

    interp.add_builtin("target_include_directories", [get_target_from_name](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("target_include_directories");
        std::string name;
        std::vector<std::string> pub, priv, inter;
        parser.add_positional(name, "target name");
        parser.add_list("PUBLIC", pub);
        parser.add_list("PRIVATE", priv);
        parser.add_list("INTERFACE", inter);
        PARSE_OR_RETURN(parser, interp, args);

        auto target = get_target_from_name(interp, name, "target_include_directories");
        if (!target) return;

        if (!pub.empty()) target->add_include_directories(pub, PropertyVisibility::PUBLIC);
        if (!priv.empty()) target->add_include_directories(priv, PropertyVisibility::PRIVATE);
        if (!inter.empty()) target->add_include_directories(inter, PropertyVisibility::INTERFACE);
    });

    interp.add_builtin("target_compile_definitions", [get_target_from_name](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("target_compile_definitions");
        std::string name;
        std::vector<std::string> pub, priv, inter;
        parser.add_positional(name, "target name");
        parser.add_list("PUBLIC", pub);
        parser.add_list("PRIVATE", priv);
        parser.add_list("INTERFACE", inter);
        PARSE_OR_RETURN(parser, interp, args);

        auto target = get_target_from_name(interp, name, "target_compile_definitions");
        if (!target) return;

        if (!pub.empty()) target->add_compile_definitions(pub, PropertyVisibility::PUBLIC);
        if (!priv.empty()) target->add_compile_definitions(priv, PropertyVisibility::PRIVATE);
        if (!inter.empty()) target->add_compile_definitions(inter, PropertyVisibility::INTERFACE);
    });

    interp.add_builtin("target_compile_options", [get_target_from_name](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("target_compile_options");
        std::string name;
        std::vector<std::string> pub, priv, inter;
        parser.add_positional(name, "target name");
        parser.add_list("PUBLIC", pub);
        parser.add_list("PRIVATE", priv);
        parser.add_list("INTERFACE", inter);
        PARSE_OR_RETURN(parser, interp, args);

        auto target = get_target_from_name(interp, name, "target_compile_options");
        if (!target) return;

        if (!pub.empty()) target->add_compile_options(pub, PropertyVisibility::PUBLIC);
        if (!priv.empty()) target->add_compile_options(priv, PropertyVisibility::PRIVATE);
        if (!inter.empty()) target->add_compile_options(inter, PropertyVisibility::INTERFACE);
    });

    interp.add_builtin("target_link_libraries", [get_target_from_name](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("target_link_libraries");
        std::string name;
        std::vector<std::string> pub, priv, inter, def;
        parser.add_positional(name, "target name");
        parser.add_list("PUBLIC", pub);
        parser.add_list("PRIVATE", priv);
        parser.add_list("INTERFACE", inter);
        parser.add_default_list(def);
        PARSE_OR_RETURN(parser, interp, args);

        auto target = get_target_from_name(interp, name, "target_link_libraries");
        if (!target) return;

        if (!pub.empty()) target->add_linked_libraries(pub, PropertyVisibility::PUBLIC);
        if (!priv.empty()) target->add_linked_libraries(priv, PropertyVisibility::PRIVATE);
        if (!inter.empty()) target->add_linked_libraries(inter, PropertyVisibility::INTERFACE);
        if (!def.empty()) target->add_linked_libraries(def, PropertyVisibility::PRIVATE);
    });

    interp.add_builtin("set_target_properties", [get_target_from_name](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("set_target_properties");
        std::string name;
        std::vector<std::string> props;
        parser.add_positional(name, "target name");
        parser.add_list("PROPERTIES", props);
        PARSE_OR_RETURN(parser, interp, args);

        auto target = get_target_from_name(interp, name, "set_target_properties");
        if (!target) return;

        if (props.size() % 2 != 0) {
            interp.set_fatal_error("set_target_properties() PROPERTIES must be key-value pairs");
            return;
        }

        for(size_t i = 0; i < props.size(); i += 2) {
            std::string prop_name = props[i];
            std::string prop_value = props[i+1];

            if (prop_name == "OUTPUT_NAME") {
                target->set_output_name(prop_value);
            } else if (prop_name == "CXX_STANDARD") {
                target->set_cxx_standard(prop_value);
            }
        }
    });

    interp.add_builtin("target_precompile_headers", [get_target_from_name](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("target_precompile_headers");
        std::string name;
        std::vector<std::string> pub, priv, inter;
        parser.add_positional(name, "target name");
        parser.add_list("PUBLIC", pub);
        parser.add_list("PRIVATE", priv);
        parser.add_list("INTERFACE", inter);
        PARSE_OR_RETURN(parser, interp, args);

        auto target = get_target_from_name(interp, name, "target_precompile_headers");
        if (!target) return;

        if (!pub.empty()) target->add_precompiled_headers(pub, PropertyVisibility::PUBLIC);
        if (!priv.empty()) target->add_precompiled_headers(priv, PropertyVisibility::PRIVATE);
        if (!inter.empty()) target->add_precompiled_headers(inter, PropertyVisibility::INTERFACE);
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