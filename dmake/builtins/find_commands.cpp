#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include "../CMakeList.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <cstdlib>
#include <sys/stat.h>
#include <algorithm>

namespace dmake {
namespace {  // Anonymous namespace for internal helpers

// Parsed command options
struct FindOptions {
    std::string var_name;
    std::vector<std::string> names;
    std::vector<std::string> hints;
    std::vector<std::string> paths;
    std::vector<std::string> path_suffixes;
    std::string doc;
    bool required = false;
    bool no_default_path = false;
    bool no_cache = false;
    bool names_per_dir = false;
};

// Parse a path argument that could be literal or "ENV VAR_NAME"
std::filesystem::path parse_path_arg(const std::string& arg) {
    if (arg.starts_with("ENV ")) {
        std::string env_var = arg.substr(4);
        const char* value = std::getenv(env_var.c_str());
        return value ? std::filesystem::path(value) : std::filesystem::path();
    }
    return std::filesystem::path(arg);
}

// Split PATH-like environment variable by colons
std::vector<std::filesystem::path> split_env_path(const char* env_value) {
    std::vector<std::filesystem::path> result;
    if (!env_value) return result;

    std::string value(env_value);
    size_t start = 0;
    size_t end = value.find(':');

    while (end != std::string::npos) {
        if (end > start) {
            result.emplace_back(value.substr(start, end - start));
        }
        start = end + 1;
        end = value.find(':', start);
    }

    if (start < value.length()) {
        result.emplace_back(value.substr(start));
    }

    return result;
}

// Build the complete search path list following CMake's specification
std::vector<std::filesystem::path> build_search_paths(
    Interpreter& interp,
    const FindOptions& opts,
    const std::vector<std::filesystem::path>& default_paths,
    const std::string& command_name
) {
    std::vector<std::filesystem::path> search_paths;

    if (opts.no_default_path) {
        // Only use HINTS and PATHS when NO_DEFAULT_PATH is set
        for (const auto& hint : opts.hints) {
            auto path = parse_path_arg(hint);
            if (!path.empty()) search_paths.push_back(path);
        }
        for (const auto& path_str : opts.paths) {
            auto path = parse_path_arg(path_str);
            if (!path.empty()) search_paths.push_back(path);
        }
        return search_paths;
    }

    // 1. Package-specific roots (would require PackageName parameter - skip for now)

    // 2. CMake-specific paths from CMAKE_PREFIX_PATH
    std::string prefix_path = interp.get_variable("CMAKE_PREFIX_PATH");
    if (!prefix_path.empty()) {
        auto paths = split_env_path(prefix_path.c_str());
        for (const auto& prefix : paths) {
            // For find_path and find_file, also add <prefix>/include
            // For find_library, also add <prefix>/lib
            // For find_program, also add <prefix>/bin
            if (command_name == "find_path" || command_name == "find_file") {
                search_paths.push_back(prefix / "include");
            } else if (command_name == "find_library") {
                search_paths.push_back(prefix / "lib");
                search_paths.push_back(prefix / "lib64");
            } else if (command_name == "find_program") {
                search_paths.push_back(prefix / "bin");
            }
            // Always add the prefix itself
            search_paths.push_back(prefix);
        }
    }

    // 3. HINTS paths (system introspection)
    for (const auto& hint : opts.hints) {
        auto path = parse_path_arg(hint);
        if (!path.empty()) search_paths.push_back(path);
    }

    // 4. System environment variables (already included in default_paths)

    // 5. CMake system paths and default paths
    for (const auto& path : default_paths) {
        search_paths.push_back(path);
    }

    // 6. PATHS (hard-coded guesses)
    for (const auto& path_str : opts.paths) {
        auto path = parse_path_arg(path_str);
        if (!path.empty()) search_paths.push_back(path);
    }

    return search_paths;
}

// Result type for search_for_file - contains both the file path and the base search directory
struct SearchResult {
    std::filesystem::path file_path;      // Full path to the found file
    std::filesystem::path search_dir;     // Base search directory where it was found
};

// Core search engine - supports both default and NAMES_PER_DIR algorithms
std::optional<SearchResult> search_for_file(
    Interpreter& interp,
    const FindOptions& opts,
    const std::vector<std::filesystem::path>& default_paths,
    std::vector<std::string> (*name_variants)(Interpreter&, const std::string&),
    bool (*validator)(const std::filesystem::path&),
    const std::string& command_name
) {
    auto search_paths = build_search_paths(interp, opts, default_paths, command_name);

    // Prepare suffix list (empty string first for no suffix)
    std::vector<std::string> suffixes = {""};
    suffixes.insert(suffixes.end(), opts.path_suffixes.begin(), opts.path_suffixes.end());

    if (opts.names_per_dir) {
        // NAMES_PER_DIR: Try all names in one directory before moving to next
        for (const auto& search_path : search_paths) {
            for (const auto& suffix : suffixes) {
                std::filesystem::path check_dir = search_path / suffix;

                // Check if directory exists using cache
                auto parent = check_dir.parent_path();
                auto dirname = check_dir.filename().string();
                if (!dirname.empty() && !interp.cached_file_exists(parent, dirname)) continue;
                // Verify it's actually a directory
                std::error_code ec;
                if (!std::filesystem::is_directory(check_dir, ec)) continue;

                for (const auto& name : opts.names) {
                    auto variants = name_variants(interp, name);
                    for (const auto& variant : variants) {
                        std::filesystem::path full_path = check_dir / variant;
                        // For names with path components (e.g., X11/X.h), use single-parameter check
                        bool exists = (variant.find('/') != std::string::npos)
                            ? interp.cached_file_exists(full_path)
                            : interp.cached_file_exists(check_dir, variant);
                        bool valid = exists && validator(full_path);

                        // DEBUG: Uncomment to trace search

                        if (valid) {
                            std::error_code ec;
                            return SearchResult{
                                std::filesystem::canonical(full_path, ec),
                                search_path  // Return the base search path, not check_dir
                            };
                        }
                    }
                }
            }
        }
    } else {
        // Default: Try all directories for each name before moving to next name
        for (const auto& name : opts.names) {
            auto variants = name_variants(interp, name);

            for (const auto& search_path : search_paths) {
                for (const auto& suffix : suffixes) {
                    std::filesystem::path check_dir = search_path / suffix;

                    // DEBUG

                    // Check if directory exists using cache
                    auto parent = check_dir.parent_path();
                    auto dirname = check_dir.filename().string();
                    if (!dirname.empty() && !interp.cached_file_exists(parent, dirname)) {
                        continue;
                    }
                    // Verify it's actually a directory
                    std::error_code ec;
                    if (!std::filesystem::is_directory(check_dir, ec)) {
                        continue;
                    }

                    for (const auto& variant : variants) {
                        std::filesystem::path full_path = check_dir / variant;
                        // For names with path components (e.g., X11/X.h), use single-parameter check
                        bool exists = (variant.find('/') != std::string::npos)
                            ? interp.cached_file_exists(full_path)
                            : interp.cached_file_exists(check_dir, variant);
                        if (exists && validator(full_path)) {
                            std::error_code ec;
                            return SearchResult{
                                std::filesystem::canonical(full_path, ec),
                                search_path  // Return the base search path, not check_dir
                            };
                        }
                    }
                }
            }
        }
    }

    return std::nullopt;
}

// Validate that a file is an executable program
bool validate_program(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        return false;
    }

    // Check if file has executable permission (owner, group, or others)
    struct stat st;
    if (stat(p.c_str(), &st) != 0) {
        return false;
    }

    return (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

// Validate that a file exists and is a regular file
bool validate_file(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

// Generate name variants for programs (no transformation on Linux)
std::vector<std::string> program_variants(Interpreter& interp, const std::string& name) {
    // TODO: On Windows, would add .exe, .com, .bat extensions
    return {name};
}

// Generate name variants for libraries (lib prefix and .so/.a suffixes)
std::vector<std::string> library_variants(Interpreter& interp, const std::string& name) {
    std::vector<std::string> variants;

    // Get custom prefixes/suffixes if set
    std::string prefixes_str = interp.get_variable("CMAKE_FIND_LIBRARY_PREFIXES");
    std::string suffixes_str = interp.get_variable("CMAKE_FIND_LIBRARY_SUFFIXES");

    std::vector<std::string> prefixes;
    std::vector<std::string> suffixes;

    if (!prefixes_str.empty()) {
        // Split by semicolon
        size_t start = 0;
        size_t end = prefixes_str.find(';');
        while (end != std::string::npos) {
            prefixes.push_back(prefixes_str.substr(start, end - start));
            start = end + 1;
            end = prefixes_str.find(';', start);
        }
        prefixes.push_back(prefixes_str.substr(start));
    } else {
        // Linux defaults
        prefixes = {"lib", ""};
    }

    if (!suffixes_str.empty()) {
        // Split by semicolon
        size_t start = 0;
        size_t end = suffixes_str.find(';');
        while (end != std::string::npos) {
            suffixes.push_back(suffixes_str.substr(start, end - start));
            start = end + 1;
            end = suffixes_str.find(';', start);
        }
        suffixes.push_back(suffixes_str.substr(start));
    } else {
        // Linux defaults
        suffixes = {".so", ".a", ""};
    }

    // Try name as-is first (might already have lib prefix or suffix)
    variants.push_back(name);

    // Then try all combinations
    for (const auto& prefix : prefixes) {
        for (const auto& suffix : suffixes) {
            std::string variant = prefix + name + suffix;
            if (variant != name) {  // Don't duplicate
                variants.push_back(variant);
            }
        }
    }

    return variants;
}

// Generate name variants for files (exact match only)
std::vector<std::string> file_variants(Interpreter& interp, const std::string& name) {
    return {name};
}

// Get default search paths for programs
std::vector<std::filesystem::path> get_program_default_paths(Interpreter& interp) {
    std::vector<std::filesystem::path> paths;

    // PATH environment variable
    const char* path_env = std::getenv("PATH");
    auto path_dirs = split_env_path(path_env);
    paths.insert(paths.end(), path_dirs.begin(), path_dirs.end());

    // Standard system paths
    paths.emplace_back("/usr/local/bin");
    paths.emplace_back("/usr/bin");
    paths.emplace_back("/bin");

    return paths;
}

// Get default search paths for libraries
std::vector<std::filesystem::path> get_library_default_paths(Interpreter& interp) {
    std::vector<std::filesystem::path> paths;

    // LD_LIBRARY_PATH environment variable
    const char* ld_path = std::getenv("LD_LIBRARY_PATH");
    auto ld_dirs = split_env_path(ld_path);
    paths.insert(paths.end(), ld_dirs.begin(), ld_dirs.end());

    // Standard system library paths
    paths.emplace_back("/usr/local/lib");
    paths.emplace_back("/usr/local/lib64");
    paths.emplace_back("/usr/lib");
    paths.emplace_back("/usr/lib64");
    paths.emplace_back("/lib");
    paths.emplace_back("/lib64");

    // Architecture-specific paths (common on Debian/Ubuntu)
    const char* arch_triplet = "x86_64-linux-gnu";  // TODO: Detect dynamically
    paths.emplace_back(std::string("/usr/lib/") + arch_triplet);
    paths.emplace_back(std::string("/lib/") + arch_triplet);

    return paths;
}

// Get default search paths for files (typically headers)
std::vector<std::filesystem::path> get_file_default_paths(Interpreter& interp) {
    std::vector<std::filesystem::path> paths;

    // Standard include paths
    paths.emplace_back("/usr/local/include");
    paths.emplace_back("/usr/include");

    return paths;
}

// Get default search paths for find_path (includes CMAKE_INCLUDE_PATH)
std::vector<std::filesystem::path> get_path_default_paths(Interpreter& interp) {
    std::vector<std::filesystem::path> paths;

    // CMAKE_INCLUDE_PATH variable
    std::string include_path = interp.get_variable("CMAKE_INCLUDE_PATH");
    if (!include_path.empty()) {
        auto include_dirs = split_env_path(include_path.c_str());
        paths.insert(paths.end(), include_dirs.begin(), include_dirs.end());
    }

    // INCLUDE environment variable
    const char* include_env = std::getenv("INCLUDE");
    auto include_env_dirs = split_env_path(include_env);
    paths.insert(paths.end(), include_env_dirs.begin(), include_env_dirs.end());

    // Standard include paths
    paths.emplace_back("/usr/local/include");
    paths.emplace_back("/usr/include");

    return paths;
}

// Generic registration helper for all find commands
void register_find_command(
    Interpreter& interp,
    const std::string& cmd_name,
    std::vector<std::filesystem::path> (*get_defaults)(Interpreter&),
    std::vector<std::string> (*get_variants)(Interpreter&, const std::string&),
    bool (*validator)(const std::filesystem::path&),
    bool return_directory = false  // find_path returns directory, not file
) {
    interp.add_builtin(cmd_name, [=](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser(cmd_name);

        FindOptions opts;
        std::vector<std::string> default_args;

        parser.add_positional(opts.var_name, "variable name", true);
        parser.add_default_list(default_args);
        parser.add_list("NAMES", opts.names);
        parser.add_list("HINTS", opts.hints);
        parser.add_list("PATHS", opts.paths);
        parser.add_list("PATH_SUFFIXES", opts.path_suffixes);
        parser.add_value("DOC", opts.doc);
        parser.add_flag("REQUIRED", opts.required);
        parser.add_flag("NO_DEFAULT_PATH", opts.no_default_path);
        parser.add_flag("NO_CACHE", opts.no_cache);
        parser.add_flag("NAMES_PER_DIR", opts.names_per_dir);

        PARSE_OR_RETURN(parser, interp, args);

        // Handle short-hand syntax: find_program(VAR name [path1 path2...])
        if (opts.names.empty() && !default_args.empty()) {
            // First item is always the name to search for
            opts.names.push_back(default_args[0]);

            // Rest are paths (if provided)
            for (size_t i = 1; i < default_args.size(); ++i) {
                opts.paths.push_back(default_args[i]);
            }
        }

        if (opts.names.empty()) {
            interp.set_fatal_error(cmd_name + " requires at least one name to search for");
            return;
        }

        // Before searching, check if variable already contains a non-NOTFOUND value
        std::string existing = interp.get_variable(opts.var_name);
        if (!existing.empty() && existing.find("-NOTFOUND") == std::string::npos) {
            // Already have a valid value, skip search
            return;
        }

        // Perform the search
        auto default_paths = get_defaults(interp);
        auto result = search_for_file(interp, opts, default_paths, get_variants, validator, cmd_name);

        if (result) {
            // Found it
            std::string result_str;
            if (return_directory) {
                // find_path returns the base search directory where the file was found
                result_str = result->search_dir.string();
            } else {
                // find_file, find_program, find_library return full path
                result_str = result->file_path.string();
            }
            interp.set_variable(opts.var_name, result_str);

            if (!opts.no_cache) {
                // Also store in cache
                interp.set_cache_variable(opts.var_name, result_str);
                // we don't do docs
            }
        } else {
            // Not found
            std::string notfound = opts.var_name + "-NOTFOUND";
            interp.set_variable(opts.var_name, notfound);

            if (!opts.no_cache) {
                interp.set_cache_variable(opts.var_name, notfound);
            }

            if (opts.required) {
                std::string err = "Could not find required " + cmd_name + ": " + opts.names[0];
                if (opts.names.size() > 1) {
                    err += " (or " + std::to_string(opts.names.size() - 1) + " alternative";
                    if (opts.names.size() > 2) err += "s";
                    err += ")";
                }
                interp.set_fatal_error(err);
            }
        }
    });
}

}  // anonymous namespace

// Public registration function
void register_find_commands_builtins(Interpreter& interp) {
    register_find_command(interp, "find_program",
        get_program_default_paths, program_variants, validate_program);

    register_find_command(interp, "find_library",
        get_library_default_paths, library_variants, validate_file);

    register_find_command(interp, "find_file",
        get_file_default_paths, file_variants, validate_file);

    // find_path returns the directory containing the file, not the file itself
    register_find_command(interp, "find_path",
        get_path_default_paths, file_variants, validate_file, true);
}

}  // namespace dmake
