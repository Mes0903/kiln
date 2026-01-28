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
        if (!root->accumulated_include_directories_.empty())
            target->append_property("INCLUDE_DIRECTORIES", root->accumulated_include_directories_, PropertyVisibility::PRIVATE);
        
        if (!root->accumulated_link_directories_.empty())
            target->append_property("LINK_DIRECTORIES", root->accumulated_link_directories_, PropertyVisibility::PRIVATE);
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
        target->append_property("SOURCES", sources, PropertyVisibility::PRIVATE);
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

    interp.add_builtin("add_custom_target", [&](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("add_custom_target");
        std::string name;
        bool all = false;
        std::vector<std::vector<std::string>> commands;
        std::vector<std::string> depends;
        std::string working_dir;
        std::string comment;
        std::vector<std::string> sources;

        parser.add_positional(name, "target name");
        parser.add_flag("ALL", all);
        parser.add_multi_list("COMMAND", commands);
        parser.add_list("DEPENDS", depends);
        parser.add_value("WORKING_DIRECTORY", working_dir);
        parser.add_value("COMMENT", comment);
        parser.add_list("SOURCES", sources);
        // Note: VERBATIM is ignored as we currently use shell execution via popen
        PARSE_OR_RETURN(parser, interp, args);

        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");

        auto target = std::make_shared<CustomTarget>(name, src_dir, bin_dir);
        target->set_build_by_default(all);
        if (!comment.empty()) {
            target->set_property("COMMENT", comment);
        }

        for (const auto& cmd_args : commands) {
            CustomCommand cmd;
            cmd.command = cmd_args;
            cmd.comment = comment;
            cmd.working_dir = working_dir;
            target->add_custom_command(std::move(cmd));
        }

        for (const auto& dep : depends) {
            target->add_custom_dependency(dep);
        }

        if (!sources.empty()) {
            target->append_property("SOURCES", sources, PropertyVisibility::PRIVATE);
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

    // Generic handler generator for target_* commands
    auto make_target_command = [get_target_from_name](std::string cmd_name, std::string prop_name) {
        return [get_target_from_name, cmd_name, prop_name](Interpreter& interp, const std::vector<std::string>& args) {
            CommandParser parser(cmd_name);
            std::string name;
            std::vector<std::string> pub, priv, inter;
            parser.add_positional(name, "target name");
            parser.add_list("PUBLIC", pub);
            parser.add_list("PRIVATE", priv);
            parser.add_list("INTERFACE", inter);
            PARSE_OR_RETURN(parser, interp, args);

            auto target = get_target_from_name(interp, name, cmd_name);
            if (!target) return;

            if (!pub.empty()) target->append_property(prop_name, pub, PropertyVisibility::PUBLIC);
            if (!priv.empty()) target->append_property(prop_name, priv, PropertyVisibility::PRIVATE);
            if (!inter.empty()) target->append_property(prop_name, inter, PropertyVisibility::INTERFACE);
        };
    };

    interp.add_builtin("target_include_directories", make_target_command("target_include_directories", "INCLUDE_DIRECTORIES"));
    interp.add_builtin("target_compile_definitions", make_target_command("target_compile_definitions", "COMPILE_DEFINITIONS"));
    interp.add_builtin("target_compile_options", make_target_command("target_compile_options", "COMPILE_OPTIONS"));
    interp.add_builtin("target_precompile_headers", make_target_command("target_precompile_headers", "PRECOMPILE_HEADERS"));

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

        if (!pub.empty()) target->append_property("LINK_LIBRARIES", pub, PropertyVisibility::PUBLIC);
        if (!priv.empty()) target->append_property("LINK_LIBRARIES", priv, PropertyVisibility::PRIVATE);
        if (!inter.empty()) target->append_property("LINK_LIBRARIES", inter, PropertyVisibility::INTERFACE);
        // Default (legacy CMake) is roughly PRIVATE or PUBLIC depending on target type, 
        // but modern CMake treats it as PRIVATE/PUBLIC. In dmake we map to PRIVATE for safety.
        if (!def.empty()) target->append_property("LINK_LIBRARIES", def, PropertyVisibility::PRIVATE);
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

        // Helper to parse semicolon-separated list and append as INTERFACE property
        auto parse_and_append_interface = [&](const std::string& base_prop_name, const std::string& value) {
            std::vector<std::string> items;
            std::string item;
            std::istringstream ss(value);
            while (std::getline(ss, item, ';')) {
                if (!item.empty()) {
                    items.push_back(item);
                }
            }
            if (!items.empty()) {
                target->append_property(base_prop_name, items, PropertyVisibility::INTERFACE);
            }
        };

        for(size_t i = 0; i < props.size(); i += 2) {
            std::string prop_name = props[i];
            std::string prop_value = props[i+1];

            if (prop_name == "OUTPUT_NAME") {
                target->set_output_name(prop_value);
            } else if (prop_name == "CXX_STANDARD") {
                target->set_cxx_standard(prop_value);
            } else if (prop_name == "IMPORTED_LOCATION" ||
                       prop_name.rfind("IMPORTED_LOCATION_", 0) == 0) {
                target->set_imported_location(prop_value);
            } else if (prop_name == "INTERFACE_LINK_LIBRARIES") {
                parse_and_append_interface("LINK_LIBRARIES", prop_value);
            } else if (prop_name == "INTERFACE_INCLUDE_DIRECTORIES") {
                parse_and_append_interface("INCLUDE_DIRECTORIES", prop_value);
            } else if (prop_name == "INTERFACE_COMPILE_DEFINITIONS") {
                parse_and_append_interface("COMPILE_DEFINITIONS", prop_value);
            } else if (prop_name == "INTERFACE_COMPILE_OPTIONS") {
                parse_and_append_interface("COMPILE_OPTIONS", prop_value);
            } else if (prop_name == "INTERFACE_LINK_DIRECTORIES") {
                parse_and_append_interface("LINK_DIRECTORIES", prop_value);
            } else {
                // Fallback: Generic property set
                target->set_property(prop_name, prop_value);
            }
        }
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
                append = true; 
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

        // Helper to parse and append INTERFACE properties
        auto handle_interface_property = [](const std::shared_ptr<Target>& target,
                                             const std::string& prop_name,
                                             const std::vector<std::string>& values,
                                             bool append_mode) -> bool {
            // Map INTERFACE_* to base property name
            std::string base_prop;
            if (prop_name == "INTERFACE_INCLUDE_DIRECTORIES") base_prop = "INCLUDE_DIRECTORIES";
            else if (prop_name == "INTERFACE_COMPILE_DEFINITIONS") base_prop = "COMPILE_DEFINITIONS";
            else if (prop_name == "INTERFACE_COMPILE_OPTIONS") base_prop = "COMPILE_OPTIONS";
            else if (prop_name == "INTERFACE_LINK_LIBRARIES") base_prop = "LINK_LIBRARIES";
            else if (prop_name == "INTERFACE_LINK_DIRECTORIES") base_prop = "LINK_DIRECTORIES";
            else return false; // Not an INTERFACE property we handle

            // Parse values (may already be a list or semicolon-separated)
            std::vector<std::string> items;
            for (const auto& v : values) {
                std::istringstream ss(v);
                std::string item;
                while (std::getline(ss, item, ';')) {
                    if (!item.empty()) items.push_back(item);
                }
            }

            if (!items.empty()) {
                target->append_property(base_prop, items, PropertyVisibility::INTERFACE);
            }
            return true;
        };

        for (size_t i = 1; i < property_idx; ++i) {
            if (args[i] == "APPEND" || args[i] == "APPEND_STRING") continue;

            auto target = get_target_from_name(interp, args[i], "set_property");
            if (!target) continue;

            // Try to handle as INTERFACE property first
            if (handle_interface_property(target, property_name, property_values, append)) {
                continue;
            }

            // Fallback: generic property handling
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

    interp.add_builtin("cmake_dump_target_info", [get_target_from_name](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("cmake_dump_target_info");
        std::string name;
        parser.add_positional(name, "target name", true);
        PARSE_OR_RETURN(parser, interp, args);

        auto target = get_target_from_name(interp, name, "cmake_dump_target_info");
        if (!target) return;

        // Helper to print a vector
        auto print_vec = [](const std::string& indent, const std::vector<std::string>& vec) -> std::string {
            if (vec.empty()) return indent + "(empty)";
            std::string result;
            for (const auto& item : vec) {
                result += indent + "- " + item + "\n";
            }
            return result;
        };

        // Helper to get type name
        auto type_name = [](TargetType type) -> std::string {
            switch(type) {
                case TargetType::EXECUTABLE: return "EXECUTABLE";
                case TargetType::SHARED_LIBRARY: return "SHARED_LIBRARY";
                case TargetType::STATIC_LIBRARY: return "STATIC_LIBRARY";
                case TargetType::OBJECT_LIBRARY: return "OBJECT_LIBRARY";
                case TargetType::INTERFACE_LIBRARY: return "INTERFACE_LIBRARY";
                case TargetType::CUSTOM: return "CUSTOM";
                default: return "UNKNOWN";
            }
        };

        // Print basic info
        std::string output;
        output += "=== Target: " + target->get_name() + " ===\n";
        output += "Type: " + type_name(target->get_type()) + "\n";
        output += "Source Dir: " + target->get_source_dir() + "\n";
        output += "Binary Dir: " + target->get_binary_dir() + "\n";
        output += "Output Path: " + target->get_output_path() + "\n";
        output += "Imported: " + std::string(target->is_imported() ? "yes" : "no") + "\n";
        if (target->is_imported()) {
            output += "Imported Location: " + target->get_imported_location() + "\n";
        }
        output += "\n";

        // Print unresolved properties
        std::vector<std::string> prop_names = {
            "SOURCES", "INCLUDE_DIRECTORIES", "COMPILE_DEFINITIONS",
            "COMPILE_OPTIONS", "LINK_LIBRARIES", "LINK_DIRECTORIES",
            "PRECOMPILE_HEADERS"
        };

        output += "--- Unresolved Properties ---\n";
        for (const auto& prop : prop_names) {
            const auto& priv = target->get_property_list(prop, PropertyVisibility::PRIVATE);
            const auto& pub = target->get_property_list(prop, PropertyVisibility::PUBLIC);
            const auto& iface = target->get_property_list(prop, PropertyVisibility::INTERFACE);

            if (!priv.empty() || !pub.empty() || !iface.empty()) {
                output += "\n" + prop + ":\n";
                if (!priv.empty()) {
                    output += "  PRIVATE:\n";
                    output += print_vec("    ", priv);
                }
                if (!pub.empty()) {
                    output += "  PUBLIC:\n";
                    output += print_vec("    ", pub);
                }
                if (!iface.empty()) {
                    output += "  INTERFACE:\n";
                    output += print_vec("    ", iface);
                }
            }
        }

        // Print resolved properties (if resolved)
        output += "\n--- Resolved Properties (after dependency resolution) ---\n";
        for (const auto& prop : prop_names) {
            const auto& resolved = target->get_resolved_property(prop);
            if (!resolved.empty()) {
                output += "\n" + prop + " (for building this target):\n";
                output += print_vec("  ", resolved);
            }
        }

        output += "\n--- Resolved Interface Properties (propagated to dependents) ---\n";
        for (const auto& prop : prop_names) {
            const auto& resolved_iface = target->get_resolved_interface_property(prop);
            if (!resolved_iface.empty()) {
                output += "\n" + prop + " (for dependents):\n";
                output += print_vec("  ", resolved_iface);
            }
        }

        output += "\n";
        interp.print_message("", output);
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
            interp.set_variable(var_name, "");
        }
    });
}

} // namespace dmake