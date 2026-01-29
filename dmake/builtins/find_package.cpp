#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>
#include <cctype>

namespace dmake {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

struct VersionComponents {
    std::string full;
    std::string major;
    std::string minor;
    std::string patch;
    std::string tweak;
    std::string count;
};

VersionComponents parse_version(const std::string& version) {
    VersionComponents v;
    v.full = version;
    
    if (version.empty()) {
        v.count = "0";
        return v;
    }

    std::vector<std::string> parts;
    size_t start = 0;
    size_t end = version.find('.');
    while (end != std::string::npos) {
        parts.push_back(version.substr(start, end - start));
        start = end + 1;
        end = version.find('.', start);
    }
    parts.push_back(version.substr(start));

    v.count = std::to_string(parts.size());
    if (parts.size() > 0) v.major = parts[0];
    if (parts.size() > 1) v.minor = parts[1];
    if (parts.size() > 2) v.patch = parts[2];
    if (parts.size() > 3) v.tweak = parts[3];
    
    return v;
}

} // namespace

void register_find_package_builtins(Interpreter& interp) {
    interp.add_builtin("find_package", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("find_package");
        std::string package_name;
        std::string version;
        bool required = false;
        bool config = false;
        bool no_module = false;
        bool quiet = false;
        std::vector<std::string> components;
        std::vector<std::string> optional_components;

        parser.add_positional(package_name, "package name", true);
        parser.add_positional(version, "version", false);
        parser.add_flag("REQUIRED", required);
        parser.add_flag("CONFIG", config);
        parser.add_flag("NO_MODULE", no_module);
        parser.add_flag("QUIET", quiet);
        parser.add_list("COMPONENTS", components);
        parser.add_list("OPTIONAL_COMPONENTS", optional_components);

        PARSE_OR_RETURN(parser, interp, args);

        std::string found_var = package_name + "_FOUND";

        // Set component-related variables before calling Find module or Config file
        std::vector<std::string> all_components = components;
        all_components.insert(all_components.end(), optional_components.begin(), optional_components.end());

        if (!all_components.empty()) {
            // Set <Package>_FIND_COMPONENTS to semicolon-separated list of all components
            std::string components_str;
            for (size_t i = 0; i < all_components.size(); ++i) {
                if (i > 0) components_str += ";";
                components_str += all_components[i];
            }
            interp.set_variable(package_name + "_FIND_COMPONENTS", components_str);

            // Set <Package>_FIND_REQUIRED_<Component> for each component
            for (const auto& comp : components) {
                interp.set_variable(package_name + "_FIND_REQUIRED_" + comp, "TRUE");
            }
            for (const auto& comp : optional_components) {
                interp.set_variable(package_name + "_FIND_REQUIRED_" + comp, "FALSE");
            }
        }

        // Set <Package>_FIND_REQUIRED
        interp.set_variable(package_name + "_FIND_REQUIRED", required ? "TRUE" : "FALSE");

        // Set <Package>_FIND_QUIETLY
        interp.set_variable(package_name + "_FIND_QUIETLY", quiet ? "TRUE" : "FALSE");

        // Helper to validate that all required components were found
        auto validate_components = [&]() -> bool {
            if (components.empty()) {
                return true; // No required components to check
            }

            std::vector<std::string> missing_components;
            for (const auto& comp : components) {
                std::string comp_found_var = package_name + "_" + comp + "_FOUND";
                std::string comp_found = interp.get_variable(comp_found_var);
                if (interp.is_falsy(comp_found)) {
                    missing_components.push_back(comp);
                }
            }

            if (!missing_components.empty()) {
                // Build error message
                std::string missing_str;
                for (size_t i = 0; i < missing_components.size(); ++i) {
                    if (i > 0) missing_str += " ";
                    missing_str += missing_components[i];
                }

                if (required) {
                    interp.set_fatal_error("Could not find package " + package_name + " (missing required components: " + missing_str + ")");
                } else if (!quiet) {
                    interp.print_message("STATUS", "Package " + package_name + " missing components: " + missing_str);
                }

                // Set package as not found if required components are missing
                interp.set_variable(found_var, "OFF");
                return false;
            }

            return true;
        };

        // 1. Module Mode
        // Unless NO_MODULE is specified, try to find a module first.
        // CMake logic: If CONFIG is specified, skip Module mode.
        // If neither CONFIG nor NO_MODULE is specified, try Module mode.
        bool try_module = !config && !no_module;
        
        if (try_module) {
            std::vector<std::filesystem::path> module_paths;
            
            // 1.1 Check CMAKE_MODULE_PATH
            std::string module_path_var = interp.get_variable("CMAKE_MODULE_PATH");
            if (!module_path_var.empty()) {
                 size_t start = 0;
                size_t end = module_path_var.find(';');
                while (end != std::string::npos) {
                    module_paths.push_back(module_path_var.substr(start, end - start));
                    start = end + 1;
                    end = module_path_var.find(';', start);
                }
                module_paths.push_back(module_path_var.substr(start));
            }
            
            // 1.2 System module paths
            std::vector<std::string> system_modules = {
                "/usr/share/cmake/Modules",
                "/usr/local/share/cmake/Modules",
                "/usr/lib/cmake/Modules",
                "/usr/lib/x86_64-linux-gnu/cmake/Modules"
            };
            for(const auto& p : system_modules) module_paths.push_back(p);
            
            std::string module_filename = "Find" + package_name + ".cmake";
            std::filesystem::path found_module;
            
            for(const auto& path : module_paths) {
                if(interp.cached_file_exists(path, module_filename)) {
                    found_module = path / module_filename;
                    break;
                }
            }
            
            if (!found_module.empty()) {
                if (!quiet) {
                     interp.print_message("STATUS", "Found module: " + found_module.string());
                }
                auto res = interp.include_file(found_module.string());
                if (!res) {
                    interp.set_fatal_error(res.error());
                    return;
                }

                // The module is responsible for setting PackageName_FOUND
                std::string found = interp.get_variable(found_var);
                if (interp.is_falsy(found)) {
                     if (required) {
                        interp.set_fatal_error("Could not find package " + package_name + " (missing: " + package_name + "_FOUND)");
                    } else if (!quiet) {
                        interp.print_message("STATUS", "Could not find package " + package_name + " (Module mode)");
                    }
                    return;
                }

                // Validate required components
                validate_components();
                return; // Module mode executed, we are done.
            }
            // If module not found, proceed to config mode (standard CMake behavior)
        }

        // 2. Config Mode
        std::string dir_var = package_name + "_DIR";
        std::vector<std::filesystem::path> search_paths;

        // 2.1 Check PackageName_DIR hint
        std::string hint_dir = interp.get_variable(dir_var);
        if (!hint_dir.empty()) {
            search_paths.push_back(hint_dir);
        }

        // 2.2 Check CMAKE_PREFIX_PATH
        std::string prefix_path = interp.get_variable("CMAKE_PREFIX_PATH");
        if (!prefix_path.empty()) {
            size_t start = 0;
            size_t end = prefix_path.find(';');
            while (end != std::string::npos) {
                search_paths.push_back(prefix_path.substr(start, end - start));
                start = end + 1;
                end = prefix_path.find(';', start);
            }
            search_paths.push_back(prefix_path.substr(start));
        }

        // 2.3 Standard system paths
        std::vector<std::string> system_roots = {
            "/usr/lib/cmake",
            "/usr/share/cmake",
            "/usr/local/lib/cmake",
            "/usr/local/share/cmake",
            "/usr/lib/x86_64-linux-gnu/cmake"
        };

        for (const auto& root : system_roots) {
            search_paths.push_back(std::filesystem::path(root) / package_name);
            search_paths.push_back(root); // Also check the root itself
        }

        std::filesystem::path found_path;
        std::string lower_name = to_lower(package_name);
        
        VersionComponents v_req = parse_version(version);

        for (const auto& path : search_paths) {
            // Check if directory exists using cache
            auto path_parent = path.parent_path();
            auto path_name = path.filename().string();
            if (!path_parent.empty() && !path_name.empty()) {
                if (!interp.cached_file_exists(path_parent, path_name)) continue;
            }
            // Verify it's actually a directory
            std::error_code ec;
            if (!std::filesystem::is_directory(path, ec)) continue;

            // Candidates for Config file and Version file
            struct Candidate {
                std::string config;
                std::string version;
            };

            std::vector<Candidate> candidates = {
                {package_name + "Config.cmake", package_name + "ConfigVersion.cmake"},
                {lower_name + "-config.cmake", lower_name + "-config-version.cmake"}
            };

            for (const auto& cand : candidates) {
                if (!interp.cached_file_exists(path, cand.config)) continue;
                std::filesystem::path config_path = path / cand.config;

                // Found a config file. If version checking is requested, look for version file.
                if (!version.empty()) {
                    if (interp.cached_file_exists(path, cand.version)) {
                        std::filesystem::path version_path = path / cand.version;

                        // Set variables for the version file
                        interp.set_variable("PACKAGE_FIND_NAME", package_name);
                        interp.set_variable("PACKAGE_FIND_VERSION", v_req.full);
                        interp.set_variable("PACKAGE_FIND_VERSION_MAJOR", v_req.major);
                        interp.set_variable("PACKAGE_FIND_VERSION_MINOR", v_req.minor);
                        interp.set_variable("PACKAGE_FIND_VERSION_PATCH", v_req.patch);
                        interp.set_variable("PACKAGE_FIND_VERSION_TWEAK", v_req.tweak);
                        interp.set_variable("PACKAGE_FIND_VERSION_COUNT", v_req.count);

                        // Execute version file
                        auto res = interp.include_file(version_path.string());
                        if (!res) {
                            interp.print_message("WARN", "Error processing version file " + version_path.string() + ": " + res.error().message);
                            continue; // Treat as incompatible/error
                        }

                        // Check result
                        std::string compatible = interp.get_variable("PACKAGE_VERSION_COMPATIBLE");

                        // Clear the compatibility result so it doesn't bleed into next check
                        interp.set_variable("PACKAGE_VERSION_COMPATIBLE", "");

                        if (interp.is_falsy(compatible)) {
                            // Version not compatible
                            continue;
                        }
                    } else {
                        // Version requested but no version file found.
                        // Standard CMake behavior is complex here, but usually it ignores the package
                        // if strict versioning is expected.
                        // For now, let's skip it to be safe.
                        if (!quiet) {
                             interp.print_message("STATUS", "Checking " + config_path.string() + ": No version file found, assuming incompatible.");
                        }
                        continue;
                    }
                }

                // If we get here, it's either no version requested OR version check passed
                found_path = config_path;
                break;
            }
            if (!found_path.empty()) break;
        }

        if (!found_path.empty()) {
            interp.set_variable(found_var, "ON");
            interp.set_variable(dir_var, found_path.parent_path().string());

            auto res = interp.include_file(found_path.string());
            if (!res) {
                interp.set_fatal_error(res.error());
                return;
            }

            // Validate required components
            validate_components();
        } else {
            interp.set_variable(found_var, "OFF");
            if (required) {
                std::string error_msg = "Could not find package " + package_name + " in CONFIG mode";
                if (!version.empty()) {
                    error_msg += " with version " + version;
                }
                if (!components.empty()) {
                    error_msg += " (required components:";
                    for (size_t i = 0; i < components.size(); ++i) {
                        error_msg += " " + components[i];
                    }
                    error_msg += ")";
                }
                error_msg += ".";
                interp.set_fatal_error(error_msg);
            } else if (!quiet) {
                interp.print_message("STATUS", "Could not find package " + package_name + " (optional)");
            }
        }
    });
}

} // namespace dmake