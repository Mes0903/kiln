#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <filesystem>
#include <algorithm>

namespace dmake {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
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

        if (!version.empty()) {
            interp.print_message("WARN", "Version checking is not implemented for find_package, ignoring: " + version);
        }

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

        for (const auto& path : search_paths) {
            if (!std::filesystem::exists(path)) continue;

            std::vector<std::string> candidates = {
                package_name + "Config.cmake",
                lower_name + "-config.cmake"
            };

            for (const auto& candidate : candidates) {
                std::filesystem::path full_path = path / candidate;
                if (std::filesystem::exists(full_path)) {
                    found_path = full_path;
                    break;
                }
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
                interp.set_fatal_error("Could not find package " + package_name + " in CONFIG mode.");
            } else if (!quiet) {
                interp.print_message("STATUS", "Could not find package " + package_name + " (optional)");
            }
        }
    });
}

} // namespace dmake
