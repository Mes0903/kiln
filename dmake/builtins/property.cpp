#include "registry.hpp"
#include "../interperter.hpp"
#include "../target.hpp"
#include "../command_parser.hpp"
#include <filesystem>
#include <algorithm>

namespace dmake {

// Helper to parse property scope from string
// Note: CMake inconsistently uses "CACHED_VARIABLE" for define_property() but "CACHE" for set/get_property()
static std::optional<PropertyScope> parse_property_scope(const std::string& scope_str) {
    if (scope_str == "GLOBAL") return PropertyScope::GLOBAL;
    if (scope_str == "DIRECTORY") return PropertyScope::DIRECTORY;
    if (scope_str == "TARGET") return PropertyScope::TARGET;
    if (scope_str == "SOURCE") return PropertyScope::SOURCE;
    if (scope_str == "TEST") return PropertyScope::TEST;
    if (scope_str == "VARIABLE") return PropertyScope::VARIABLE;
    if (scope_str == "CACHED_VARIABLE") return PropertyScope::CACHED_VARIABLE;
    if (scope_str == "CACHE") return PropertyScope::CACHED_VARIABLE; // Alias for set/get_property
    if (scope_str == "INSTALL") return PropertyScope::INSTALL;
    return std::nullopt;
}

// Helper to get target by name (used in multiple places)
static std::shared_ptr<Target> get_target_from_name(Interpreter& interp, const std::string& name, const std::string& cmd_name) {
    auto& targets = interp.get_targets();
    auto it = targets.find(name);
    if (it == targets.end()) {
        interp.set_fatal_error(cmd_name + "() called on unknown target '" + name + "'");
        return nullptr;
    }
    return it->second;
}

// Helper to get test by name
static TestDefinition* get_test_from_name(Interpreter& interp, const std::string& name) {
    auto& tests = interp.get_tests();
    for (auto& test : tests) {
        if (test.name == name) {
            return &test;
        }
    }
    return nullptr;
}

// Helper to resolve source file path to absolute
static std::string resolve_source_path(Interpreter& interp, const std::string& source,
                                       const std::string* opt_directory,
                                       const std::string* opt_target_directory) {
    std::filesystem::path source_path(source);

    if (source_path.is_absolute()) {
        return source_path.string();
    }

    // Determine the base directory
    std::string base_dir;
    if (opt_target_directory) {
        auto target = get_target_from_name(interp, *opt_target_directory, "resolve_source_path");
        if (!target) return "";
        base_dir = target->get_source_dir();
    } else if (opt_directory) {
        std::filesystem::path dir_path(*opt_directory);
        if (dir_path.is_absolute()) {
            base_dir = *opt_directory;
        } else {
            base_dir = (std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / dir_path).string();
        }
    } else {
        base_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
    }

    return (std::filesystem::path(base_dir) / source_path).lexically_normal().string();
}

void register_property_builtins(Interpreter& interp) {

    // define_property() - Define a new property with metadata
    interp.add_builtin("define_property", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.size() < 3) {
            interp.set_fatal_error("define_property() requires at least 3 arguments: <scope> PROPERTY <name>");
            return;
        }

        std::string scope_str = args[0];
        auto scope_opt = parse_property_scope(scope_str);
        if (!scope_opt) {
            interp.set_fatal_error("define_property() invalid scope: " + scope_str);
            return;
        }
        PropertyScope scope = *scope_opt;

        if (args[1] != "PROPERTY") {
            interp.set_fatal_error("define_property() expected PROPERTY keyword, got: " + args[1]);
            return;
        }

        std::string prop_name = args[2];

        PropertyDefinition def;
        def.scope = scope;
        def.name = prop_name;

        // Parse optional arguments
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "INHERITED") {
                def.inherited = true;
            } else if (args[i] == "BRIEF_DOCS" && i + 1 < args.size()) {
                // Collect all args until next keyword
                ++i;
                while (i < args.size() && args[i] != "FULL_DOCS" && args[i] != "INITIALIZE_FROM_VARIABLE") {
                    if (!def.brief_docs.empty()) def.brief_docs += " ";
                    def.brief_docs += args[i];
                    ++i;
                }
                --i; // Back up one since loop will increment
            } else if (args[i] == "FULL_DOCS" && i + 1 < args.size()) {
                ++i;
                while (i < args.size() && args[i] != "BRIEF_DOCS" && args[i] != "INITIALIZE_FROM_VARIABLE") {
                    if (!def.full_docs.empty()) def.full_docs += " ";
                    def.full_docs += args[i];
                    ++i;
                }
                --i;
            } else if (args[i] == "INITIALIZE_FROM_VARIABLE" && i + 1 < args.size()) {
                ++i;
                def.initialize_from_variable = args[i];
            }
        }

        // Check if property is already defined for this scope
        auto& property_definitions = interp.get_property_definitions();
        if (property_definitions[scope].count(prop_name)) {
            // Silently ignore redefinition attempts (CMake behavior)
            return;
        }

        // Store the definition
        property_definitions[scope][prop_name] = def;
    });

    // set_property() - Set property values for various scopes
    interp.add_builtin("set_property", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.size() < 4) {
            interp.set_fatal_error("set_property() requires at least 4 arguments: <scope> <items...> PROPERTY <name> [values...]");
            return;
        }

        std::string scope_str = args[0];
        auto scope_opt = parse_property_scope(scope_str);
        if (!scope_opt) {
            interp.set_fatal_error("set_property() invalid scope: " + scope_str);
            return;
        }
        PropertyScope scope = *scope_opt;

        // Parse items, APPEND/APPEND_STRING flags, and find PROPERTY keyword
        std::vector<std::string> items;
        bool append = false;
        bool append_string = false;
        size_t property_idx = 0;

        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "APPEND") {
                append = true;
            } else if (args[i] == "APPEND_STRING") {
                append_string = true;
            } else if (args[i] == "PROPERTY") {
                property_idx = i;
                break;
            } else {
                items.push_back(args[i]);
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

        // Build the value string
        std::string value;
        if (append_string) {
            // APPEND_STRING: concatenate as single string
            for (const auto& v : property_values) {
                value += v;
            }
        } else {
            // Regular or APPEND: semicolon-separated list
            for (size_t i = 0; i < property_values.size(); ++i) {
                if (i > 0) value += ";";
                value += property_values[i];
            }
        }

        // Handle each scope
        auto& global_properties = interp.get_global_properties();
        auto& source_properties = interp.get_source_properties();
        auto& cache_variables = interp.get_cache_variables();

        switch (scope) {
            case PropertyScope::GLOBAL: {
                if (!items.empty()) {
                    interp.set_fatal_error("set_property(GLOBAL ...) does not accept item names");
                    return;
                }
                if (append || append_string) {
                    std::string old_val = global_properties[property_name];
                    if (!old_val.empty() && !value.empty()) {
                        if (append_string) {
                            global_properties[property_name] = old_val + value;
                        } else {
                            global_properties[property_name] = old_val + ";" + value;
                        }
                    } else if (!value.empty()) {
                        global_properties[property_name] = value;
                    }
                } else {
                    global_properties[property_name] = value;
                }
                break;
            }

            case PropertyScope::DIRECTORY: {
                // Items are directory paths (optional)
                std::vector<Interpreter*> target_interpreters;
                if (items.empty()) {
                    // Current directory
                    target_interpreters.push_back(&interp);
                } else {
                    for (const auto& dir_path : items) {
                        // Check if it's the current directory
                        std::string current_source_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
                        if (dir_path == "." || dir_path == current_source_dir) {
                            target_interpreters.push_back(&interp);
                        } else {
                            // Look up the interpreter for the specified directory
                            Interpreter* dir_interp = interp.get_interpreter_for_directory(dir_path);
                            if (dir_interp) {
                                target_interpreters.push_back(dir_interp);
                            } else {
                                interp.set_fatal_error("set_property(DIRECTORY ...) unknown directory: " + dir_path);
                                return;
                            }
                        }
                    }
                }

                for (auto* target_interp : target_interpreters) {
                    auto& dir_props = target_interp->get_directory_properties();
                    if (append || append_string) {
                        std::string old_val = dir_props[property_name];
                        if (!old_val.empty() && !value.empty()) {
                            if (append_string) {
                                dir_props[property_name] = old_val + value;
                            } else {
                                dir_props[property_name] = old_val + ";" + value;
                            }
                        } else if (!value.empty()) {
                            dir_props[property_name] = value;
                        }
                    } else {
                        dir_props[property_name] = value;
                    }
                }
                break;
            }

            case PropertyScope::TARGET: {
                // Items are target names
                if (items.empty()) {
                    interp.set_fatal_error("set_property(TARGET ...) requires at least one target name");
                    return;
                }

                // Helper to handle INTERFACE properties
                auto handle_interface_property = [](const std::shared_ptr<Target>& target,
                                                     const std::string& prop_name,
                                                     const std::vector<std::string>& values,
                                                     bool append_mode) -> bool {
                    std::string base_prop;
                    if (prop_name == "INTERFACE_INCLUDE_DIRECTORIES") base_prop = "INCLUDE_DIRECTORIES";
                    else if (prop_name == "INTERFACE_COMPILE_DEFINITIONS") base_prop = "COMPILE_DEFINITIONS";
                    else if (prop_name == "INTERFACE_COMPILE_OPTIONS") base_prop = "COMPILE_OPTIONS";
                    else if (prop_name == "INTERFACE_LINK_LIBRARIES") base_prop = "LINK_LIBRARIES";
                    else if (prop_name == "INTERFACE_LINK_DIRECTORIES") base_prop = "LINK_DIRECTORIES";
                    else return false;

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

                for (const auto& target_name : items) {
                    auto target = get_target_from_name(interp, target_name, "set_property");
                    if (!target) continue;

                    // Try to handle as INTERFACE property first
                    if (handle_interface_property(target, property_name, property_values, append)) {
                        continue;
                    }

                    // Special handling for language standard properties
                    if (property_name == "C_STANDARD") {
                        target->set_language_standard(Language::C, value);
                        continue;
                    } else if (property_name == "CXX_STANDARD") {
                        target->set_language_standard(Language::CXX, value);
                        continue;
                    } else if (property_name == "C_EXTENSIONS") {
                        bool enabled = !interp.is_falsy(value);
                        target->set_language_extensions(Language::C, enabled);
                        continue;
                    } else if (property_name == "CXX_EXTENSIONS") {
                        bool enabled = !interp.is_falsy(value);
                        target->set_language_extensions(Language::CXX, enabled);
                        continue;
                    }

                    // Generic property handling
                    if (append || append_string) {
                        std::string old_val = target->get_property(property_name);
                        if (!old_val.empty() && !value.empty()) {
                            if (append_string) {
                                target->set_property(property_name, old_val + value);
                            } else {
                                target->set_property(property_name, old_val + ";" + value);
                            }
                        } else if (!value.empty()) {
                            target->set_property(property_name, value);
                        }
                    } else {
                        target->set_property(property_name, value);
                    }
                }
                break;
            }

            case PropertyScope::SOURCE: {
                // Items are source file paths
                if (items.empty()) {
                    interp.set_fatal_error("set_property(SOURCE ...) requires at least one source file");
                    return;
                }

                // Parse optional DIRECTORY or TARGET_DIRECTORY
                std::string opt_directory;
                std::string opt_target_directory;

                // Re-scan args for sub-options (after items, before PROPERTY)
                for (size_t i = 1; i < property_idx; ++i) {
                    if (args[i] == "DIRECTORY" && i + 1 < property_idx) {
                        opt_directory = args[++i];
                    } else if (args[i] == "TARGET_DIRECTORY" && i + 1 < property_idx) {
                        opt_target_directory = args[++i];
                    }
                }

                for (const auto& source : items) {
                    std::string abs_source = resolve_source_path(
                        interp, source,
                        opt_directory.empty() ? nullptr : &opt_directory,
                        opt_target_directory.empty() ? nullptr : &opt_target_directory
                    );
                    if (abs_source.empty()) continue;

                    if (append || append_string) {
                        std::string old_val = source_properties[abs_source][property_name];
                        if (!old_val.empty() && !value.empty()) {
                            if (append_string) {
                                source_properties[abs_source][property_name] = old_val + value;
                            } else {
                                source_properties[abs_source][property_name] = old_val + ";" + value;
                            }
                        } else if (!value.empty()) {
                            source_properties[abs_source][property_name] = value;
                        }
                    } else {
                        source_properties[abs_source][property_name] = value;
                    }
                }
                break;
            }

            case PropertyScope::TEST: {
                // Items are test names
                if (items.empty()) {
                    interp.set_fatal_error("set_property(TEST ...) requires at least one test name");
                    return;
                }

                for (const auto& test_name : items) {
                    auto* test = get_test_from_name(interp, test_name);
                    if (!test) {
                        interp.set_fatal_error("set_property(TEST ...) unknown test: " + test_name);
                        return;
                    }

                    if (append || append_string) {
                        std::string old_val = test->properties[property_name];
                        if (!old_val.empty() && !value.empty()) {
                            if (append_string) {
                                test->properties[property_name] = old_val + value;
                            } else {
                                test->properties[property_name] = old_val + ";" + value;
                            }
                        } else if (!value.empty()) {
                            test->properties[property_name] = value;
                        }
                    } else {
                        test->properties[property_name] = value;
                    }
                }
                break;
            }

            case PropertyScope::CACHED_VARIABLE: {
                // Items are cache entry names
                if (items.empty()) {
                    interp.set_fatal_error("set_property(CACHE ...) requires at least one cache entry name");
                    return;
                }

                for (const auto& entry_name : items) {
                    // For cache properties, we store them in a special way
                    // Since we don't have a separate cache property storage, we'll use a naming convention
                    std::string cache_prop_key = entry_name + ".__property__." + property_name;

                    if (append || append_string) {
                        std::string old_val = cache_variables[cache_prop_key];
                        if (!old_val.empty() && !value.empty()) {
                            if (append_string) {
                                cache_variables[cache_prop_key] = old_val + value;
                            } else {
                                cache_variables[cache_prop_key] = old_val + ";" + value;
                            }
                        } else if (!value.empty()) {
                            cache_variables[cache_prop_key] = value;
                        }
                    } else {
                        cache_variables[cache_prop_key] = value;
                    }
                }
                break;
            }

            case PropertyScope::VARIABLE: {
                // VARIABLE scope is only for documentation
                interp.set_fatal_error("set_property(VARIABLE ...) is only for documentation, cannot set values");
                return;
            }

            case PropertyScope::INSTALL: {
                // Items are installed file paths
                if (items.empty()) {
                    interp.set_fatal_error("set_property(INSTALL ...) requires at least one installed file path");
                    return;
                }

                auto& install_properties = interp.get_install_properties();
                for (const auto& install_path : items) {
                    // Normalize the install path
                    std::filesystem::path normalized = std::filesystem::path(install_path).lexically_normal();
                    std::string path_key = normalized.string();

                    if (append || append_string) {
                        std::string old_val = install_properties[path_key][property_name];
                        if (!old_val.empty() && !value.empty()) {
                            if (append_string) {
                                install_properties[path_key][property_name] = old_val + value;
                            } else {
                                install_properties[path_key][property_name] = old_val + ";" + value;
                            }
                        } else if (!value.empty()) {
                            install_properties[path_key][property_name] = value;
                        }
                    } else {
                        install_properties[path_key][property_name] = value;
                    }
                }
                break;
            }
        }
    });

    // get_property() - Get property values with inheritance support
    interp.add_builtin("get_property", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.size() < 4) {
            interp.set_fatal_error("get_property() requires at least 4 arguments: <variable> <scope> <item> PROPERTY <name>");
            return;
        }

        std::string var_name = args[0];
        std::string scope_str = args[1];

        auto scope_opt = parse_property_scope(scope_str);
        if (!scope_opt) {
            interp.set_fatal_error("get_property() invalid scope: " + scope_str);
            return;
        }
        PropertyScope scope = *scope_opt;

        // Parse the rest of the arguments
        std::string item_name;
        std::string property_name;
        bool get_set = false;
        bool get_defined = false;
        bool get_brief_docs = false;
        bool get_full_docs = false;

        size_t arg_idx = 2;

        // For DIRECTORY scope, item name is optional (defaults to current directory)
        // For SOURCE, TARGET, TEST, CACHED_VARIABLE - item name is required
        // For GLOBAL and VARIABLE - no item name
        if (scope == PropertyScope::DIRECTORY) {
            // Item name is optional for DIRECTORY
            // If next arg is not PROPERTY, it's an item name
            if (arg_idx < args.size() && args[arg_idx] != "PROPERTY") {
                item_name = args[arg_idx++];
            }
        } else if (scope != PropertyScope::GLOBAL && scope != PropertyScope::VARIABLE) {
            // For other scopes (TARGET, SOURCE, TEST, CACHED_VARIABLE), item name is required
            if (arg_idx >= args.size()) {
                interp.set_fatal_error("get_property() missing item name for scope " + scope_str);
                return;
            }
            item_name = args[arg_idx++];
        }

        // Parse DIRECTORY sub-options for SOURCE and TEST
        std::string opt_directory;
        std::string opt_target_directory;

        while (arg_idx < args.size() && args[arg_idx] != "PROPERTY") {
            if (scope == PropertyScope::SOURCE) {
                if (args[arg_idx] == "DIRECTORY" && arg_idx + 1 < args.size()) {
                    opt_directory = args[++arg_idx];
                } else if (args[arg_idx] == "TARGET_DIRECTORY" && arg_idx + 1 < args.size()) {
                    opt_target_directory = args[++arg_idx];
                }
            } else if (scope == PropertyScope::TEST) {
                if (args[arg_idx] == "DIRECTORY" && arg_idx + 1 < args.size()) {
                    opt_directory = args[++arg_idx];
                }
            } else if (scope == PropertyScope::DIRECTORY && args[arg_idx] != "PROPERTY") {
                // Skip any unrecognized args before PROPERTY for DIRECTORY scope
                ++arg_idx;
                continue;
            }
            ++arg_idx;
        }

        // Find PROPERTY keyword
        if (arg_idx >= args.size() || args[arg_idx] != "PROPERTY") {
            interp.set_fatal_error("get_property() missing PROPERTY keyword");
            return;
        }
        ++arg_idx;

        if (arg_idx >= args.size()) {
            interp.set_fatal_error("get_property() missing property name");
            return;
        }
        property_name = args[arg_idx++];

        // Parse optional query mode
        if (arg_idx < args.size()) {
            std::string query_mode = args[arg_idx];
            if (query_mode == "SET") get_set = true;
            else if (query_mode == "DEFINED") get_defined = true;
            else if (query_mode == "BRIEF_DOCS") get_brief_docs = true;
            else if (query_mode == "FULL_DOCS") get_full_docs = true;
            else {
                interp.set_fatal_error("get_property() invalid query mode: " + query_mode);
                return;
            }
        }

        auto& property_definitions = interp.get_property_definitions();
        auto& global_properties = interp.get_global_properties();
        auto& source_properties = interp.get_source_properties();
        auto& cache_variables = interp.get_cache_variables();
        auto& directory_properties = interp.get_directory_properties();

        // Handle query modes (DEFINED, BRIEF_DOCS, FULL_DOCS)
        if (get_defined) {
            bool defined = property_definitions[scope].count(property_name) > 0;
            interp.set_variable(var_name, defined ? "1" : "0");
            return;
        }

        if (get_brief_docs || get_full_docs) {
            auto def_it = property_definitions[scope].find(property_name);
            if (def_it == property_definitions[scope].end()) {
                interp.set_variable(var_name, property_name + "-NOTFOUND");
            } else {
                interp.set_variable(var_name, get_brief_docs ? def_it->second.brief_docs : def_it->second.full_docs);
            }
            return;
        }

        // Helper to check if property is inherited
        auto is_inherited = [&]() -> bool {
            auto def_it = property_definitions[scope].find(property_name);
            return def_it != property_definitions[scope].end() && def_it->second.inherited;
        };

        // Get the property value based on scope
        std::string value;
        bool value_found = false;

        switch (scope) {
            case PropertyScope::GLOBAL: {
                auto it = global_properties.find(property_name);
                if (it != global_properties.end()) {
                    value = it->second;
                    value_found = true;
                }
                break;
            }

            case PropertyScope::DIRECTORY: {
                // Determine which interpreter's directory properties to use
                Interpreter* target_interp = &interp;
                if (!item_name.empty()) {
                    // Explicit directory path provided
                    std::string current_source_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
                    if (item_name != "." && item_name != current_source_dir) {
                        target_interp = interp.get_interpreter_for_directory(item_name);
                        if (!target_interp) {
                            // Unknown directory - return empty
                            interp.set_variable(var_name, "");
                            return;
                        }
                    }
                }

                // Check target directory properties
                auto& target_dir_props = target_interp->get_directory_properties();
                auto it = target_dir_props.find(property_name);
                if (it != target_dir_props.end()) {
                    value = it->second;
                    value_found = true;
                }

                // If not found and property is inherited, check parent directories
                if (!value_found && is_inherited()) {
                    Interpreter* current = target_interp->parent_;
                    while (current != nullptr) {
                        auto& parent_dir_props = current->get_directory_properties();
                        auto parent_it = parent_dir_props.find(property_name);
                        if (parent_it != parent_dir_props.end()) {
                            value = parent_it->second;
                            value_found = true;
                            break;
                        }
                        current = current->parent_;
                    }

                    // If still not found, check GLOBAL scope
                    if (!value_found) {
                        auto global_it = global_properties.find(property_name);
                        if (global_it != global_properties.end()) {
                            value = global_it->second;
                            value_found = true;
                        }
                    }
                }
                break;
            }

            case PropertyScope::TARGET: {
                auto target = get_target_from_name(interp, item_name, "get_property");
                if (!target) {
                    interp.set_variable(var_name, "");
                    return;
                }

                // Handle special built-in target properties
                if (property_name == "TYPE") {
                    switch(target->get_type()) {
                        case TargetType::EXECUTABLE: value = "EXECUTABLE"; break;
                        case TargetType::SHARED_LIBRARY: value = "SHARED_LIBRARY"; break;
                        case TargetType::STATIC_LIBRARY: value = "STATIC_LIBRARY"; break;
                        case TargetType::OBJECT_LIBRARY: value = "OBJECT_LIBRARY"; break;
                        case TargetType::INTERFACE_LIBRARY: value = "INTERFACE_LIBRARY"; break;
                        case TargetType::CUSTOM: value = "UTILITY"; break;
                    }
                    value_found = true;
                } else if (property_name == "NAME") {
                    value = target->get_name();
                    value_found = true;
                } else if (property_name == "SOURCE_DIR") {
                    value = target->get_source_dir();
                    value_found = true;
                } else if (property_name == "BINARY_DIR") {
                    value = target->get_binary_dir();
                    value_found = true;
                } else if (property_name == "IMPORTED") {
                    value = target->is_imported() ? "TRUE" : "FALSE";
                    value_found = true;
                } else if (property_name == "IMPORTED_LOCATION") {
                    value = target->get_imported_location();
                    value_found = !value.empty();
                } else {
                    // Try generic property
                    value = target->get_property(property_name);
                    value_found = !value.empty();
                }

                // If not found and inherited, check DIRECTORY scope
                if (!value_found && is_inherited()) {
                    auto dir_it = directory_properties.find(property_name);
                    if (dir_it != directory_properties.end()) {
                        value = dir_it->second;
                        value_found = true;
                    }
                }
                break;
            }

            case PropertyScope::SOURCE: {
                std::string abs_source = resolve_source_path(
                    interp, item_name,
                    opt_directory.empty() ? nullptr : &opt_directory,
                    opt_target_directory.empty() ? nullptr : &opt_target_directory
                );
                if (abs_source.empty()) {
                    interp.set_variable(var_name, "");
                    return;
                }

                auto source_it = source_properties.find(abs_source);
                if (source_it != source_properties.end()) {
                    auto prop_it = source_it->second.find(property_name);
                    if (prop_it != source_it->second.end()) {
                        value = prop_it->second;
                        value_found = true;
                    }
                }

                // If not found and inherited, check DIRECTORY scope
                if (!value_found && is_inherited()) {
                    auto dir_it = directory_properties.find(property_name);
                    if (dir_it != directory_properties.end()) {
                        value = dir_it->second;
                        value_found = true;
                    }
                }
                break;
            }

            case PropertyScope::TEST: {
                auto* test = get_test_from_name(interp, item_name);
                if (!test) {
                    interp.set_variable(var_name, "");
                    return;
                }

                auto it = test->properties.find(property_name);
                if (it != test->properties.end()) {
                    value = it->second;
                    value_found = true;
                }

                // If not found and inherited, check DIRECTORY scope
                if (!value_found && is_inherited()) {
                    auto dir_it = directory_properties.find(property_name);
                    if (dir_it != directory_properties.end()) {
                        value = dir_it->second;
                        value_found = true;
                    }
                }
                break;
            }

            case PropertyScope::CACHED_VARIABLE: {
                // Handle special built-in cache properties
                if (property_name == "TYPE") {
                    // Check if the cache variable exists
                    auto cache_it = cache_variables.find(item_name);
                    if (cache_it != cache_variables.end()) {
                        // For now, all cache variables are treated as STRING type
                        // CMake has BOOL, FILEPATH, PATH, STRING, INTERNAL types
                        value = "STRING";
                        value_found = true;
                    }
                } else if (property_name == "VALUE") {
                    // Built-in VALUE property returns the cache variable's value
                    auto cache_it = cache_variables.find(item_name);
                    if (cache_it != cache_variables.end()) {
                        value = cache_it->second;
                        value_found = true;
                    }
                } else if (property_name == "HELPSTRING") {
                    // Built-in HELPSTRING property (we don't track this currently)
                    value = "";
                    value_found = true;
                } else if (property_name == "ADVANCED") {
                    // Built-in ADVANCED property (we don't track this currently)
                    value = "0";
                    value_found = true;
                } else {
                    // Custom cache property
                    std::string cache_prop_key = item_name + ".__property__." + property_name;
                    auto it = cache_variables.find(cache_prop_key);
                    if (it != cache_variables.end()) {
                        value = it->second;
                        value_found = true;
                    }
                }
                break;
            }

            case PropertyScope::VARIABLE: {
                // VARIABLE scope only has documentation
                interp.set_variable(var_name, "");
                return;
            }

            case PropertyScope::INSTALL: {
                // Item name is the installed file path
                if (item_name.empty()) {
                    interp.set_fatal_error("get_property(INSTALL ...) requires an installed file path");
                    return;
                }

                auto& install_properties = interp.get_install_properties();
                std::filesystem::path normalized = std::filesystem::path(item_name).lexically_normal();
                std::string path_key = normalized.string();

                auto path_it = install_properties.find(path_key);
                if (path_it != install_properties.end()) {
                    auto prop_it = path_it->second.find(property_name);
                    if (prop_it != path_it->second.end()) {
                        value = prop_it->second;
                        value_found = true;
                    }
                }
                break;
            }
        }

        // Handle SET query mode
        if (get_set) {
            interp.set_variable(var_name, value_found ? "1" : "0");
            return;
        }

        // Return the value (or empty if not found)
        interp.set_variable(var_name, value);
    });
}

} // namespace dmake
