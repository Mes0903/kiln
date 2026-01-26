#include "registry.hpp"
#include "../interperter.hpp"
#include "../target.hpp"
#include "../command_parser.hpp"
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace dmake {

void register_target_builtins(Interpreter& interp) {
    // Helper to configure common target properties (C/C++ standards, flags, inherited directories)
    auto configure_target = [](Interpreter& interp, const std::shared_ptr<Target>& target) {
        auto configure_lang = [&](Language lang, const std::string& lang_prefix) {
            // Set standard from CMAKE_<LANG>_STANDARD
            std::string std_var = "CMAKE_" + lang_prefix + "_STANDARD";
            std::string lang_std = interp.get_variable(std_var);
            if (!lang_std.empty()) {
                target->set_language_standard(lang, lang_std);
            }

            // Apply CMAKE_<LANG>_FLAGS and CMAKE_<LANG>_FLAGS_<CONFIG>
            auto get_flags = [&](const std::string& var_name) -> std::vector<std::string> {
                std::string flags = interp.get_variable(var_name);
                std::vector<std::string> flag_list;
                if (!flags.empty()) {
                    std::istringstream iss(flags);
                    std::string flag;
                    while (iss >> flag) flag_list.push_back(flag);
                }
                return flag_list;
            };

            target->add_language_flags(lang, get_flags("CMAKE_" + lang_prefix + "_FLAGS"));

            std::string build_type = interp.get_variable("CMAKE_BUILD_TYPE");
            if (!build_type.empty()) {
                std::string upper_type = build_type;
                std::transform(upper_type.begin(), upper_type.end(), upper_type.begin(), [](unsigned char c){ return std::toupper(c); });
                target->add_language_flags(lang, get_flags("CMAKE_" + lang_prefix + "_FLAGS_" + upper_type));
            }
        };

        configure_lang(Language::CXX, "CXX");
        configure_lang(Language::C, "C");

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
        bool imported = false;
        std::vector<std::string> sources;
        parser.add_positional(name, "target name");
        parser.add_flag("IMPORTED", imported);
        parser.add_default_list(sources);
        PARSE_OR_RETURN(parser, interp, args);

        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");

        auto target = std::make_shared<Target>(name, TargetType::EXECUTABLE, src_dir, bin_dir);
        if (imported) {
            target->set_imported(true);
        } else {
            configure_target(interp, target);
            if (!add_sources_to_target(interp, target, src_dir, sources)) return;
        }
        interp.get_root()->targets_[name] = target;
    });

    interp.add_builtin("add_library", [&](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("add_library");
        std::string name;
        bool shared = false, static_lib = false, object_lib = false, interface_lib = false, imported = false;
        std::vector<std::string> sources;

        parser.add_positional(name, "target name");
        parser.add_flag("SHARED", shared);
        parser.add_flag("STATIC", static_lib);
        parser.add_flag("OBJECT", object_lib);
        parser.add_flag("INTERFACE", interface_lib);
        parser.add_flag("IMPORTED", imported);
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
        if (imported) {
            target->set_imported(true);
        } else {
            configure_target(interp, target);
            if (!add_sources_to_target(interp, target, src_dir, sources)) return;
        }
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
            } else if (prop_name == "IMPORTED_LOCATION" ||
                       prop_name.rfind("IMPORTED_LOCATION_", 0) == 0) {
                // Handle both IMPORTED_LOCATION and IMPORTED_LOCATION_<CONFIG>
                target->set_imported_location(prop_value);
            } else if (prop_name == "INTERFACE_LINK_LIBRARIES") {
                // Parse semicolon-separated list of libraries
                std::vector<std::string> libs;
                std::string lib;
                std::istringstream ss(prop_value);
                while (std::getline(ss, lib, ';')) {
                    if (!lib.empty()) {
                        libs.push_back(lib);
                    }
                }
                if (!libs.empty()) {
                    target->add_linked_libraries(libs, PropertyVisibility::INTERFACE);
                }
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

    interp.add_builtin("set_property", [get_target_from_name](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.size() < 4) {
            interp.set_fatal_error("set_property() requires at least 4 arguments: <GLOBAL|DIRECTORY|TARGET|...> <items> PROPERTY <name> <value...>");
            return;
        }

        std::string type = args[0];
        if (type != "TARGET") {
            // Silently ignore non-target properties for now
            return;
        }

        size_t property_idx = 0;
        bool append = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "APPEND") {
                append = true;
                continue;
            }
            if (args[i] == "APPEND_STRING") {
                append = true; // For now, treat same as append (semicolon joined)
                continue;
            }
            if (args[i] == "PROPERTY") {
                property_idx = i;
                break;
            }
        }

        if (property_idx == 0 || property_idx + 1 >= args.size()) {
            interp.set_fatal_error("set_property() missing PROPERTY keyword or property name");
            return;
        }

        std::string property_name = args[property_idx + 1];
        std::vector<std::string> property_values;
        for (size_t i = property_idx + 2; i < args.size(); ++i) {
            property_values.push_back(args[i]);
        }

        for (size_t i = 1; i < property_idx; ++i) {
            if (args[i] == "APPEND" || args[i] == "APPEND_STRING") continue;

            auto target = get_target_from_name(interp, args[i], "set_property");
            if (!target) continue;

            // Join values with semicolon for CMake property convention
            std::string value;
            for (const auto& v : property_values) {
                if (!value.empty()) value += ";";
                value += v;
            }

            if (append) {
                std::string old_val = target->get_property(property_name);
                if (!old_val.empty() && !value.empty()) {
                    target->set_property(property_name, old_val + ";" + value);
                } else if (!value.empty()) {
                    target->set_property(property_name, value);
                }
            } else {
                target->set_property(property_name, value);
            }
        }
    });

    interp.add_builtin("get_property", [get_target_from_name](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.size() < 4) {
            interp.set_fatal_error("get_property() requires at least 4 arguments: <variable> <GLOBAL|DIRECTORY|TARGET|...> <item> PROPERTY <name>");
            return;
        }

        std::string var_name = args[0];
        std::string type = args[1];
        std::string item = args[2];
        std::string property_kw = args[3];
        
        if (property_kw != "PROPERTY" || args.size() < 5) {
            interp.set_fatal_error("get_property() missing PROPERTY keyword or property name");
            return;
        }

        std::string property_name = args[4];

        if (type == "TARGET") {
            auto target = get_target_from_name(interp, item, "get_property");
            if (target) {
                interp.set_variable(var_name, target->get_property(property_name));
            } else {
                interp.set_variable(var_name, "");
            }
        } else {
            // Other property types not implemented, return empty
            interp.set_variable(var_name, "");
        }
    });
}

} // namespace dmake
