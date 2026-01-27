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

        parser.add_positional(package_name, "package name", true);
        parser.add_positional(version, "version", false);
        parser.add_flag("REQUIRED", required);
        parser.add_flag("CONFIG", config);
        parser.add_flag("NO_MODULE", no_module);
        parser.add_flag("QUIET", quiet);

        PARSE_OR_RETURN(parser, interp, args);

        std::string found_var = package_name + "_FOUND";
        std::string dir_var = package_name + "_DIR";

        // Check Module mode rejection
        if (!config && !no_module) {
            std::filesystem::path module_path = "/usr/share/cmake/Modules/Find" + package_name + ".cmake";
            if (std::filesystem::exists(module_path)) {
                interp.set_fatal_error("Module mode (Find" + package_name + ".cmake) is not supported yet. Please use CONFIG mode or ensure the package provides a config file.");
                return;
            }
        }

        std::vector<std::filesystem::path> search_paths;

        // 1. Check PackageName_DIR hint
        std::string hint_dir = interp.get_variable(dir_var);
        if (!hint_dir.empty()) {
            search_paths.push_back(hint_dir);
        }

        // 2. Check CMAKE_PREFIX_PATH
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

        // 3. Standard system paths
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
            if (!std::filesystem::exists(path)) continue;

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
                std::filesystem::path config_path = path / cand.config;
                if (!std::filesystem::exists(config_path)) continue;
                
                // Found a config file. If version checking is requested, look for version file.
                if (!version.empty()) {
                    std::filesystem::path version_path = path / cand.version;
                    if (std::filesystem::exists(version_path)) {
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
            }
        } else {
            interp.set_variable(found_var, "OFF");
            if (required) {
                interp.set_fatal_error("Could not find package " + package_name + " in CONFIG mode" + (version.empty() ? "" : " with version " + version) + ".");
            } else if (!quiet) {
                interp.print_message("STATUS", "Could not find package " + package_name + " (optional)");
            }
        }
    });
}

} // namespace dmake
