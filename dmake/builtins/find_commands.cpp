#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include "../profiler.hpp"
#include "../CMakeArray.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <cstdlib>
#include <sys/stat.h>
#include <algorithm>
#include <sstream>
#include <iostream>

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
    std::string validator;  // User-provided validator function name
    bool required = false;
    bool no_default_path = false;
    bool no_package_root_path = false;
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

// Collect <PackageName>_ROOT prefixes from CMake vars and env vars.
// Returns empty vector if disabled by flags or CMAKE_FIND_USE_PACKAGE_ROOT_PATH.
std::vector<std::filesystem::path> collect_package_root_prefixes(
    Interpreter& interp,
    bool no_default_path,
    bool no_package_root_path
) {
    std::vector<std::filesystem::path> roots;

    if (no_default_path || no_package_root_path) return roots;

    // Check global disable variable
    std::string use_var = interp.get_variable("CMAKE_FIND_USE_PACKAGE_ROOT_PATH");
    if (!use_var.empty() && interp.is_falsy(use_var)) return roots;

    // Get current package name (set by find_package before calling Find modules)
    std::string pkg = interp.get_variable("CMAKE_FIND_PACKAGE_NAME");
    if (pkg.empty()) return roots;

    std::string upper_pkg = pkg;
    for (auto& c : upper_pkg)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    auto maybe_add = [&](const std::string& val) {
        if (!val.empty()) roots.emplace_back(val);
    };

    // CMake variable <PackageName>_ROOT, then <PACKAGENAME>_ROOT
    maybe_add(interp.get_variable(pkg + "_ROOT"));
    if (upper_pkg != pkg)
        maybe_add(interp.get_variable(upper_pkg + "_ROOT"));

    // Environment variable <PackageName>_ROOT, then <PACKAGENAME>_ROOT
    if (const char* env = std::getenv((pkg + "_ROOT").c_str()))
        maybe_add(env);
    if (upper_pkg != pkg) {
        if (const char* env = std::getenv((upper_pkg + "_ROOT").c_str()))
            maybe_add(env);
    }

    return roots;
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

    // 1. Package-specific roots from <PackageName>_ROOT
    auto root_prefixes = collect_package_root_prefixes(
        interp, opts.no_default_path, opts.no_package_root_path);
    for (const auto& root : root_prefixes) {
        if (command_name == "find_path" || command_name == "find_file") {
            search_paths.push_back(root / "include");
        } else if (command_name == "find_library") {
            search_paths.push_back(root / "lib");
            search_paths.push_back(root / "lib64");
        } else if (command_name == "find_program") {
            search_paths.push_back(root / "bin");
        }
        search_paths.push_back(root);
    }

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
    bool found = false;                      // Whether file was found
    std::filesystem::path file_path;         // Full path to the found file (empty if not found)
    std::filesystem::path search_dir;        // Base search directory where it was found (empty if not found)
    std::vector<std::string> searched_dirs;  // All directories checked (in order, up to hit OR end)
};

// Compute cache signature for find command
std::string compute_find_signature(
    const std::string& cmd_name,
    const FindOptions& opts,
    const std::vector<std::filesystem::path>& search_paths
) {
    std::ostringstream oss;
    oss << "cmd:" << cmd_name << "|";

    // Names (sorted for consistency)
    std::vector<std::string> sorted_names = opts.names;
    std::sort(sorted_names.begin(), sorted_names.end());
    for (const auto& name : sorted_names) {
        oss << "name:" << name << "|";
    }

    // Search paths (in order - matters!)
    for (const auto& path : search_paths) {
        oss << "path:" << path.string() << "|";
    }

    // Suffixes (in order)
    for (const auto& suffix : opts.path_suffixes) {
        oss << "suffix:" << suffix << "|";
    }

    // Flags
    oss << "names_per_dir:" << opts.names_per_dir << "|";

    return oss.str();
}

// Validate cached find result by checking directory mtimes
bool validate_find_cache_entry(
    Interpreter& interp,
    const FindResultCacheEntry& entry
) {
    for (const auto& [dir, cached_mtime] : entry.searched_dirs) {
        auto current_mtime = interp.get_dir_mtime_cached(dir);

        // Check state change
        if (!cached_mtime.has_value() && current_mtime.has_value()) {
            // Directory didn't exist before, exists now -> invalidate
            return false;
        }
        if (cached_mtime.has_value() && !current_mtime.has_value()) {
            // Directory existed, now gone -> continue checking (might still be valid)
            continue;
        }
        if (cached_mtime.has_value() && current_mtime.has_value()) {
            if (*cached_mtime != *current_mtime) {
                // Directory mtime changed -> invalidate
                return false;
            }
        }
    }
    return true;
}

// Core search engine - supports both default and NAMES_PER_DIR algorithms
SearchResult search_for_file(
    Interpreter& interp,
    const FindOptions& opts,
    const std::vector<std::filesystem::path>& default_paths,
    std::vector<std::string> (*name_variants)(Interpreter&, const std::string&),
    bool (*builtin_validator)(const std::filesystem::path&),
    const std::string& command_name
) {
    auto search_paths = build_search_paths(interp, opts, default_paths, command_name);

    // Track searched directories for caching
    std::vector<std::string> searched_dirs;

    // Prepare suffix list (empty string first for no suffix)
    std::vector<std::string> suffixes = {""};
    suffixes.insert(suffixes.end(), opts.path_suffixes.begin(), opts.path_suffixes.end());

    if (opts.names_per_dir) {
        // NAMES_PER_DIR: Try all names in one directory before moving to next
        for (const auto& search_path : search_paths) {
            for (const auto& suffix : suffixes) {
                std::filesystem::path check_dir = suffix.empty() ? search_path : search_path / suffix;

                // Check if directory exists using cache
                auto parent = check_dir.parent_path();
                auto dirname = check_dir.filename().string();
                if (!dirname.empty() && !interp.cached_file_exists(parent, dirname)) {
                    searched_dirs.push_back(check_dir.string());
                    continue;
                }
                // Verify it's actually a directory
                std::error_code ec;
                if (!std::filesystem::is_directory(check_dir, ec)) {
                    searched_dirs.push_back(check_dir.string());
                    continue;
                }

                searched_dirs.push_back(check_dir.string());

                for (const auto& name : opts.names) {
                    auto variants = name_variants(interp, name);
                    for (const auto& variant : variants) {
                        std::filesystem::path full_path = check_dir / variant;
                        // For names with path components (e.g., X11/X.h), use single-parameter check
                        bool exists = (variant.find('/') != std::string::npos)
                            ? interp.cached_file_exists(full_path)
                            : interp.cached_file_exists(check_dir, variant);
                        bool valid = exists && builtin_validator(full_path);

                        // Check user-provided VALIDATOR function if specified
                        if (valid && !opts.validator.empty()) {
                            // Set the variable to the candidate path before calling validator
                            interp.set_variable(opts.var_name, full_path.string());

                            // Call user's validator function with the candidate path as argument
                            if (!interp.call_user_function(opts.validator, {full_path.string()})) {
                                // Function doesn't exist or failed - reject candidate
                                valid = false;
                            } else {
                                // Check if the variable was cleared by the validator (CMake convention for invalid)
                                std::string result = interp.get_variable(opts.var_name);
                                if (result.empty()) {
                                    valid = false;
                                }
                            }
                        }

                        // DEBUG: Uncomment to trace search

                        if (valid) {
                            std::error_code ec;
                            return SearchResult{
                                true,  // found
                                std::filesystem::canonical(full_path, ec),
                                check_dir,  // Return the directory where the file was found (includes suffix)
                                searched_dirs
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
                    std::filesystem::path check_dir = suffix.empty() ? search_path : search_path / suffix;

                    // DEBUG

                    // Check if directory exists using cache
                    auto parent = check_dir.parent_path();
                    auto dirname = check_dir.filename().string();
                    if (!dirname.empty() && !interp.cached_file_exists(parent, dirname)) {
                        searched_dirs.push_back(check_dir.string());
                        continue;
                    }
                    // Verify it's actually a directory
                    std::error_code ec;
                    if (!std::filesystem::is_directory(check_dir, ec)) {
                        searched_dirs.push_back(check_dir.string());
                        continue;
                    }

                    searched_dirs.push_back(check_dir.string());

                    for (const auto& variant : variants) {
                        std::filesystem::path full_path = check_dir / variant;
                        // For names with path components (e.g., X11/X.h), use single-parameter check
                        bool exists = (variant.find('/') != std::string::npos)
                            ? interp.cached_file_exists(full_path)
                            : interp.cached_file_exists(check_dir, variant);
                        bool valid = exists && builtin_validator(full_path);

                        // Check user-provided VALIDATOR function if specified
                        if (valid && !opts.validator.empty()) {
                            // Set the variable to the candidate path before calling validator
                            interp.set_variable(opts.var_name, full_path.string());

                            // Call user's validator function with the candidate path as argument
                            if (!interp.call_user_function(opts.validator, {full_path.string()})) {
                                // Function doesn't exist or failed - reject candidate
                                valid = false;
                            } else {
                                // Check if the variable was cleared by the validator (CMake convention for invalid)
                                std::string result = interp.get_variable(opts.var_name);
                                if (result.empty()) {
                                    valid = false;
                                }
                            }
                        }

                        if (valid) {
                            std::error_code ec;
                            return SearchResult{
                                true,  // found
                                std::filesystem::canonical(full_path, ec),
                                check_dir,  // Return the directory where the file was found (includes suffix)
                                searched_dirs
                            };
                        }
                    }
                }
            }
        }
    }

    // Not found - return with all searched directories
    return SearchResult{
        false,  // not found
        std::filesystem::path(),  // empty path
        std::filesystem::path(),  // empty search_dir
        searched_dirs
    };
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

        parser.positional(opts.var_name, "variable name", true);
        parser.positionals(default_args, "args");
        parser.list("NAMES", opts.names);
        parser.list("HINTS", opts.hints);
        parser.list("PATHS", opts.paths);
        parser.list("PATH_SUFFIXES", opts.path_suffixes);
        parser.value("DOC", opts.doc);
        parser.value("VALIDATOR", opts.validator);
        parser.flag("REQUIRED", opts.required);
        parser.flag("NO_DEFAULT_PATH", opts.no_default_path);
        parser.flag("NO_PACKAGE_ROOT_PATH", opts.no_package_root_path);
        parser.flag("NO_CACHE", opts.no_cache);
        parser.flag("NAMES_PER_DIR", opts.names_per_dir);

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
            // CMake silently fails when no names are provided (e.g. from empty variable expansion)
            interp.set_variable(opts.var_name, opts.var_name + "-NOTFOUND");
            return;
        }

        // Before searching, check if variable already contains a non-NOTFOUND value
        std::string existing = interp.get_variable(opts.var_name);
        if (!existing.empty() && existing.find("-NOTFOUND") == std::string::npos) {
            // Already have a valid value, skip search
            return;
        }

        // Profiling setup
        int64_t profile_start = 0;
        bool profiling = g_profiling_enabled.load(std::memory_order_relaxed);
        if (profiling) profile_start = Profiler::instance().now_us();

        // Check persistent cache
        auto default_paths = get_defaults(interp);
        auto search_paths = build_search_paths(interp, opts, default_paths, cmd_name);
        std::string signature = compute_find_signature(cmd_name, opts, search_paths);

        auto& cache = interp.get_cache_store();
        auto cached = cache.lookup<CacheSubsystem::FindResult>(signature);

        if (cached) {
            // Validate cache entry
            if (validate_find_cache_entry(interp, *cached)) {
                // Cache hit - no filesystem scan needed
                if (profiling) {
                    auto dur = Profiler::instance().now_us() - profile_start;
                    Profiler::instance().add_complete(cmd_name + " " + opts.names[0] + " (cached)", "configure", profile_start, dur);
                }
                if (!cached->found_path.empty()) {
                    interp.set_variable(opts.var_name, cached->found_path);
                    if (!opts.no_cache) {
                        interp.set_cache_variable(opts.var_name, cached->found_path);
                    }
                } else {
                    // Cached negative result
                    std::string notfound = opts.var_name + "-NOTFOUND";
                    interp.set_variable(opts.var_name, notfound);
                    if (!opts.no_cache) {
                        interp.set_cache_variable(opts.var_name, notfound);
                    }
                }
                return;
            }
        }

        // Cache miss - perform the search
        auto result = search_for_file(interp, opts, default_paths, get_variants, validator, cmd_name);

        // Store in cache with searched directory mtimes
        FindResultCacheEntry cache_entry;
        if (result.found) {
            // Found - store result path
            std::string result_str;
            if (return_directory) {
                result_str = result.search_dir.string();
            } else {
                result_str = result.file_path.string();
            }
            cache_entry.found_path = result_str;
        } else {
            // Not found - store empty path
            cache_entry.found_path = "";
        }

        // Record searched directories with their mtimes
        for (const auto& dir : result.searched_dirs) {
            auto mtime = interp.get_dir_mtime_cached(dir);
            cache_entry.searched_dirs.push_back({dir, mtime});
        }

        cache.insert<CacheSubsystem::FindResult>(signature, cache_entry);

        if (result.found) {
            // Found it
            std::string result_str;
            if (return_directory) {
                // find_path returns the base search directory where the file was found
                // For "fontconfig/fontconfig.h" found in /usr/include, return /usr/include
                result_str = result.search_dir.string();
            } else {
                // find_file, find_program, find_library return full path
                result_str = result.file_path.string();
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

        if (profiling) {
            auto dur = Profiler::instance().now_us() - profile_start;
            Profiler::instance().add_complete(cmd_name + " " + opts.names[0], "configure", profile_start, dur);
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
