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

// Check if a directory name matches a package name (case-insensitive prefix match)
// e.g., "Botan-3.10.0" matches package "Botan" or "botan"
bool directory_matches_package(const std::string& dir_name, const std::string& package_name) {
    std::string lower_dir = to_lower(dir_name);
    std::string lower_pkg = to_lower(package_name);

    // Exact match
    if (lower_dir == lower_pkg) return true;

    // Prefix match followed by a separator (-, _, or digit for versioned dirs)
    if (lower_dir.length() > lower_pkg.length() &&
        lower_dir.substr(0, lower_pkg.length()) == lower_pkg) {
        char next_char = lower_dir[lower_pkg.length()];
        if (next_char == '-' || next_char == '_' || std::isdigit(next_char)) {
            return true;
        }
    }

    return false;
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
        bool module_only = false;
        bool no_module = false;
        bool quiet = false;
        std::vector<std::string> components;
        std::vector<std::string> optional_components;
        std::vector<std::string> hints;
        std::vector<std::string> paths;
        std::vector<std::string> path_suffixes;
        bool no_default_path = false;
        bool exact = false;

        parser.positional(package_name, "package name", true);
        parser.positional(version, "version", false);
        parser.flag("REQUIRED", required);
        parser.flag("CONFIG", config);
        parser.flag("MODULE", module_only);
        parser.flag("NO_MODULE", no_module);
        parser.flag("QUIET", quiet);
        parser.flag("NO_DEFAULT_PATH", no_default_path);
        parser.flag("EXACT", exact);
        parser.list("COMPONENTS", components);
        parser.list("OPTIONAL_COMPONENTS", optional_components);
        parser.list("HINTS", hints);
        parser.list("PATHS", paths);
        parser.list("PATH_SUFFIXES", path_suffixes);

        PARSE_OR_RETURN(parser, interp, args);

        std::string found_var = package_name + "_FOUND";

        // Save and restore _FIND_* input variables so they don't leak between
        // repeated find_package calls for the same package. We can't use
        // push_scope/pop_scope because output variables (like _VERSION, _FOUND,
        // targets) set by config files must survive.
        struct FindVarGuard {
            Interpreter& interp;
            std::string pkg;
            std::vector<std::pair<std::string, std::string>> saved;

            FindVarGuard(Interpreter& i, const std::string& p) : interp(i), pkg(p) {
                for (const char* suffix : {"_FIND_REQUIRED", "_FIND_QUIETLY",
                                           "_FIND_COMPONENTS"}) {
                    std::string name = pkg + suffix;
                    saved.emplace_back(name, interp.get_variable(name));
                }
                saved.emplace_back("CMAKE_FIND_PACKAGE_NAME",
                                   interp.get_variable("CMAKE_FIND_PACKAGE_NAME"));
                // Save per-component FIND_REQUIRED vars
                std::string comps = interp.get_variable(pkg + "_FIND_COMPONENTS");
                if (!comps.empty()) {
                    size_t start = 0, end;
                    while ((end = comps.find(';', start)) != std::string::npos) {
                        std::string n = pkg + "_FIND_REQUIRED_" + comps.substr(start, end - start);
                        saved.emplace_back(n, interp.get_variable(n));
                        start = end + 1;
                    }
                    std::string n = pkg + "_FIND_REQUIRED_" + comps.substr(start);
                    saved.emplace_back(n, interp.get_variable(n));
                }
            }

            ~FindVarGuard() {
                for (const auto& [name, value] : saved) {
                    if (value.empty()) {
                        interp.unset_variable(name);
                    } else {
                        interp.set_variable(name, value);
                    }
                }
            }
        } find_var_guard(interp, package_name);

        // Set CMAKE_FIND_PACKAGE_NAME so Find modules know which package is being searched
        interp.set_variable("CMAKE_FIND_PACKAGE_NAME", package_name);

        // Set component-related variables before calling Find module or Config file
        std::vector<std::string> all_components = components;
        all_components.insert(all_components.end(), optional_components.begin(), optional_components.end());

        if (!all_components.empty()) {
            std::string components_str;
            for (size_t i = 0; i < all_components.size(); ++i) {
                if (i > 0) components_str += ";";
                components_str += all_components[i];
            }
            interp.set_variable(package_name + "_FIND_COMPONENTS", components_str);

            for (const auto& comp : components) {
                interp.set_variable(package_name + "_FIND_REQUIRED_" + comp, "TRUE");
            }
        }

        // Set <Package>_FIND_REQUIRED
        if (required) {
            interp.set_variable(package_name + "_FIND_REQUIRED", "TRUE");
        }
        // Set <Package>_FIND_QUIETLY
        if (quiet) {
            interp.set_variable(package_name + "_FIND_QUIETLY", "TRUE");
        }

        // Set version-related variables (needed by some Find modules like FindPython)
        // Only set these if a version is specified to avoid empty version range errors
        VersionComponents v_req = parse_version(version);
        if (!version.empty()) {
            interp.set_variable(package_name + "_FIND_VERSION", v_req.full);
            interp.set_variable(package_name + "_FIND_VERSION_MAJOR", v_req.major);
            interp.set_variable(package_name + "_FIND_VERSION_MINOR", v_req.minor);
            interp.set_variable(package_name + "_FIND_VERSION_PATCH", v_req.patch);
            interp.set_variable(package_name + "_FIND_VERSION_TWEAK", v_req.tweak);
            interp.set_variable(package_name + "_FIND_VERSION_COUNT", v_req.count);
            interp.set_variable(package_name + "_FIND_VERSION_EXACT", exact ? "TRUE" : "FALSE");
        }

        // Helper to validate that all required components were found
        auto validate_components = [&]() -> bool {
            if (components.empty()) {
                return true; // No required components to check
            }

            // Only check components if package was found
            std::string pkg_found = interp.get_variable(found_var);
            if (interp.is_falsy(pkg_found)) {
                return false; // Package not found, so components can't be found either
            }

            std::vector<std::string> missing_components;
            for (const auto& comp : components) {
                std::string comp_found_var = package_name + "_" + comp + "_FOUND";
                std::string comp_found = interp.get_variable(comp_found_var);

                // Only consider a component missing if it's explicitly set to FALSE
                // If undefined, assume the Find module doesn't set component variables
                // and trust that it validated components internally
                if (!comp_found.empty() && interp.is_falsy(comp_found)) {
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

        // Search order:
        // 1. CMAKE_MODULE_PATH Find modules (user-provided, always first)
        // 2. Config mode (PackageConfig.cmake)
        // 3. System Find modules (last resort fallback)
        //
        // This differs from CMake's default (module-first) but matches modern
        // best practice. System Find modules depend on CMake internals that
        // dmake doesn't implement, while config files are self-contained.

        bool try_module = !config && !no_module;
        bool try_config = !module_only;

        // Helper to run a Find module and return true if it handled the package
        auto try_find_module = [&](const std::filesystem::path& found_module) -> bool {
            if (!quiet) {
                interp.print_message("STATUS", "Found module: " + found_module.string());
            }
            auto res = interp.include_file(found_module.string());
            if (!res) {
                interp.set_fatal_error(res.error());
                return true; // Handled (with error)
            }

            std::string found = interp.get_variable(found_var);
            if (interp.is_falsy(found)) {
                if (required) {
                    interp.set_fatal_error("Could not find package " + package_name + " (missing: " + package_name + "_FOUND)");
                } else if (!quiet) {
                    interp.print_message("STATUS", "Could not find package " + package_name + " (Module mode)");
                }
                return true; // Handled (not found, but module ran)
            }

            validate_components();
            return true; // Handled (found)
        };

        // 1. Check CMAKE_MODULE_PATH for user-provided Find modules
        if (try_module) {
            std::string module_path_var = interp.get_variable("CMAKE_MODULE_PATH");
            if (!module_path_var.empty()) {
                std::vector<std::filesystem::path> user_module_paths;
                size_t start = 0;
                size_t end = module_path_var.find(';');
                while (end != std::string::npos) {
                    user_module_paths.push_back(module_path_var.substr(start, end - start));
                    start = end + 1;
                    end = module_path_var.find(';', start);
                }
                user_module_paths.push_back(module_path_var.substr(start));

                std::string module_filename = "Find" + package_name + ".cmake";
                for (const auto& path : user_module_paths) {
                    if (interp.cached_file_exists(path, module_filename)) {
                        if (try_find_module(path / module_filename)) return;
                    }
                }
            }
        }

        // 2. Config Mode (skip if MODULE was explicitly requested)
        std::string dir_var = package_name + "_DIR";
        std::filesystem::path found_path;

        if (try_config) {
        std::vector<std::filesystem::path> search_paths;

        // Helper: expand a base path with PATH_SUFFIXES (adds base + base/suffix for each suffix)
        auto add_with_suffixes = [&](const std::filesystem::path& base) {
            search_paths.push_back(base);
            for (const auto& suffix : path_suffixes) {
                search_paths.push_back(base / suffix);
            }
        };

        // 2.1 Check PackageName_DIR hint (always checked, even with NO_DEFAULT_PATH)
        std::string hint_dir = interp.get_variable(dir_var);
        if (!hint_dir.empty()) {
            add_with_suffixes(hint_dir);
        }

        if (!no_default_path) {
            // 2.2 Check CMAKE_PREFIX_PATH
            std::string prefix_path = interp.get_variable("CMAKE_PREFIX_PATH");
            if (!prefix_path.empty()) {
                size_t start = 0;
                size_t end = prefix_path.find(';');
                while (end != std::string::npos) {
                    add_with_suffixes(prefix_path.substr(start, end - start));
                    start = end + 1;
                    end = prefix_path.find(';', start);
                }
                add_with_suffixes(prefix_path.substr(start));
            }
        }

        // 2.3 HINTS paths (after CMAKE_PREFIX_PATH, before system roots)
        for (const auto& h : hints) {
            add_with_suffixes(h);
        }

        std::vector<std::string> system_roots = {
            "/usr/lib/cmake",
            "/usr/share/cmake",
            "/usr/local/lib/cmake",
            "/usr/local/share/cmake",
            "/usr/lib/x86_64-linux-gnu/cmake"
        };

        if (!no_default_path) {
            // 2.4 Standard system paths
            for (const auto& root : system_roots) {
                add_with_suffixes(std::filesystem::path(root) / package_name);
                add_with_suffixes(root); // Also check the root itself
            }
        }

        // 2.5 PATHS (hard-coded guesses, after system roots)
        for (const auto& p : paths) {
            add_with_suffixes(p);
        }

        std::string lower_name = to_lower(package_name);

        // Candidates for Config file and Version file (used in multiple places)
        struct Candidate {
            std::string config;
            std::string version;
        };

        // Helper lambda to check a directory for config files
        auto check_directory_for_config = [&](const std::filesystem::path& path) -> bool {
            std::vector<Candidate> candidates = {
                {package_name + "Config.cmake", package_name + "ConfigVersion.cmake"},
                {lower_name + "-config.cmake", lower_name + "-config-version.cmake"}
            };

            for (const auto& cand : candidates) {
                if (!interp.cached_file_exists(path, cand.config)) continue;
                std::filesystem::path config_path = path / cand.config;

                // Run version file if it exists (always, for <Package>_VERSION)
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
                    interp.set_variable("PACKAGE_FIND_VERSION_EXACT", exact ? "TRUE" : "FALSE");

                    auto res = interp.include_file(version_path.string());
                    if (!res) {
                        interp.print_message("WARN", "Error processing version file " + version_path.string() + ": " + res.error().message);
                        continue;
                    }

                    // Map PACKAGE_VERSION* → <Package>_VERSION* (CMake does this automatically)
                    std::string pkg_version = interp.get_variable("PACKAGE_VERSION");
                    if (!pkg_version.empty()) {
                        VersionComponents v_found = parse_version(pkg_version);
                        interp.set_variable(package_name + "_VERSION", v_found.full);
                        interp.set_variable(package_name + "_VERSION_MAJOR", v_found.major);
                        interp.set_variable(package_name + "_VERSION_MINOR", v_found.minor);
                        interp.set_variable(package_name + "_VERSION_PATCH", v_found.patch);
                        interp.set_variable(package_name + "_VERSION_TWEAK", v_found.tweak);
                        interp.set_variable(package_name + "_VERSION_COUNT", v_found.count);
                    }

                    // Check version compatibility if a version was requested
                    if (!version.empty()) {
                        std::string compatible = interp.get_variable("PACKAGE_VERSION_COMPATIBLE");
                        std::string exact_match = interp.get_variable("PACKAGE_VERSION_EXACT");

                        interp.set_variable("PACKAGE_VERSION_COMPATIBLE", "");
                        interp.set_variable("PACKAGE_VERSION_EXACT", "");

                        if (interp.is_falsy(compatible)) {
                            continue;
                        }
                        if (exact && interp.is_falsy(exact_match)) {
                            continue;
                        }
                    }
                } else if (!version.empty()) {
                    // Version requested but no version file found - skip
                    if (!quiet) {
                         interp.print_message("STATUS", "Checking " + config_path.string() + ": No version file found, assuming incompatible.");
                    }
                    continue;
                }

                found_path = config_path;
                return true;
            }
            return false;
        };

        // First pass: check explicit search paths
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

            if (check_directory_for_config(path)) break;
        }

        // Second pass: scan directories for subdirs matching package name pattern
        // This handles cases like boost_system-1.89.0/ for package boost_system
        // Scan HINTS first, then system roots
        if (found_path.empty()) {
            auto scan_root_for_package = [&](const std::string& root) -> bool {
                auto* entries = interp.get_directory_listing(root);
                if (!entries) return false;

                for (const auto& entry_name : *entries) {
                    if (!directory_matches_package(entry_name, package_name)) continue;

                    std::filesystem::path subdir = std::filesystem::path(root) / entry_name;
                    std::error_code ec;
                    if (!std::filesystem::is_directory(subdir, ec)) continue;

                    if (check_directory_for_config(subdir)) return true;
                }
                return false;
            };

            // Scan HINTS directories
            for (const auto& h : hints) {
                if (scan_root_for_package(h)) break;
            }

            // Scan system roots
            if (found_path.empty()) {
                for (const auto& root : system_roots) {
                    if (scan_root_for_package(root)) break;
                }
            }
        }
        } // if (try_config)

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
            // 3. System Find modules (last resort fallback)
            if (try_module) {
                std::vector<std::string> system_modules = {
                    "/usr/share/cmake/Modules",
                    "/usr/local/share/cmake/Modules",
                    "/usr/lib/cmake/Modules",
                    "/usr/lib/x86_64-linux-gnu/cmake/Modules"
                };

                std::string module_filename = "Find" + package_name + ".cmake";
                for (const auto& path : system_modules) {
                    if (interp.cached_file_exists(path, module_filename)) {
                        if (try_find_module(std::filesystem::path(path) / module_filename)) return;
                    }
                }
            }

            // Nothing found
            interp.set_variable(found_var, "OFF");
            if (required) {
                std::string error_msg = "Could not find package " + package_name;
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
