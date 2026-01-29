#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include "../utils.hpp"
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <cstdlib>

namespace dmake {

// Helper: Get file mtime or nullopt if doesn't exist
static std::optional<int64_t> get_file_mtime(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_mtime;
    }
    return std::nullopt;
}

// Helper: Get directory mtime or nullopt if doesn't exist
static std::optional<int64_t> get_dir_mtime(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return st.st_mtime;
    }
    return std::nullopt;
}

// Helper: Check if command is pkg-config/pkgconf
static bool is_pkgconfig_command(const std::string& cmd) {
    // Check for pkg-config or pkgconf in command name
    std::filesystem::path cmd_path(cmd);
    std::string filename = cmd_path.filename().string();
    return filename == "pkg-config" || filename == "pkgconf";
}

// Helper: Get standard pkgconfig directories
static std::vector<std::string> get_pkgconfig_search_dirs() {
    std::vector<std::string> dirs;

    // Standard system directories
    dirs.push_back("/usr/lib/pkgconfig");
    dirs.push_back("/usr/lib64/pkgconfig");
    dirs.push_back("/usr/share/pkgconfig");
    dirs.push_back("/usr/local/lib/pkgconfig");
    dirs.push_back("/usr/local/lib64/pkgconfig");
    dirs.push_back("/usr/local/share/pkgconfig");

    // Check PKG_CONFIG_PATH environment variable
    if (const char* pkg_path = std::getenv("PKG_CONFIG_PATH")) {
        std::string path_str(pkg_path);
        std::istringstream iss(path_str);
        std::string dir;
        while (std::getline(iss, dir, ':')) {
            if (!dir.empty()) {
                dirs.push_back(dir);
            }
        }
    }

    // Check PKG_CONFIG_LIBDIR (overrides default paths if set)
    if (const char* pkg_libdir = std::getenv("PKG_CONFIG_LIBDIR")) {
        std::string libdir_str(pkg_libdir);
        std::istringstream iss(libdir_str);
        std::string dir;
        while (std::getline(iss, dir, ':')) {
            if (!dir.empty()) {
                dirs.push_back(dir);
            }
        }
    }

    return dirs;
}

// Helper: Compute cache signature for external command (pkg-config, etc.)
static std::string compute_command_signature(
    const std::vector<std::vector<std::string>>& commands,
    const ProcessOptions& options
) {
    std::ostringstream oss;

    // Include command and all arguments
    for (size_t i = 0; i < commands.size(); ++i) {
        if (i > 0) oss << "||";
        for (size_t j = 0; j < commands[i].size(); ++j) {
            if (j > 0) oss << " ";
            oss << commands[i][j];
        }
    }
    oss << "|";

    // Include working directory
    if (!options.working_dir.empty()) {
        oss << "wd:" << options.working_dir << "|";
    }

    // Include relevant environment variables
    if (const char* pkg_path = std::getenv("PKG_CONFIG_PATH")) {
        oss << "PKG_CONFIG_PATH:" << pkg_path << "|";
    }
    if (const char* sysroot = std::getenv("PKG_CONFIG_SYSROOT_DIR")) {
        oss << "PKG_CONFIG_SYSROOT_DIR:" << sysroot << "|";
    }
    if (const char* libdir = std::getenv("PKG_CONFIG_LIBDIR")) {
        oss << "PKG_CONFIG_LIBDIR:" << libdir << "|";
    }

    return oss.str();
}

// Helper: Get mtimes for directories to track (based on command type)
// For pkg-config: tracks standard pkg-config directories
static std::map<std::string, std::optional<int64_t>> get_tracked_dir_mtimes(const std::string& cmd) {
    std::map<std::string, std::optional<int64_t>> mtimes;

    if (is_pkgconfig_command(cmd)) {
        auto dirs = get_pkgconfig_search_dirs();
        for (const auto& dir : dirs) {
            mtimes[dir] = get_dir_mtime(dir);
        }
    }
    // Future: Add tracking for other command types (compilers, etc.)

    return mtimes;
}

// Helper: Validate external command cache entry by checking tracked directories
static bool validate_command_cache(const ExternalCommandCacheEntry& entry) {
    // Check if any tracked directory mtime changed
    for (const auto& [dir, cached_mtime] : entry.tracked_dir_mtimes) {
        auto current_mtime = get_dir_mtime(dir);

        // Directory appeared
        if (!cached_mtime.has_value() && current_mtime.has_value()) {
            return false;
        }

        // Directory disappeared
        if (cached_mtime.has_value() && !current_mtime.has_value()) {
            return false;
        }

        // Directory changed
        if (cached_mtime.has_value() && current_mtime.has_value()) {
            if (*cached_mtime != *current_mtime) {
                return false;
            }
        }
    }

    return true;
}

void register_process_builtins(Interpreter& interp) {
    interp.add_builtin("execute_process", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("execute_process");
        std::vector<std::vector<std::string>> commands;
        std::string working_dir;
        std::string timeout;
        std::string result_variable;
        std::string results_variable;
        std::string output_variable;
        std::string error_variable;
        std::string input_file;
        std::string output_file;
        std::string error_file;
        std::string command_echo;
        std::string encoding;
        bool output_quiet = false;
        bool error_quiet = false;
        bool output_strip_trailing_whitespace = false;
        bool error_strip_trailing_whitespace = false;
        bool echo_output_variable = false;
        bool echo_error_variable = false;
        bool command_error_is_fatal = false;

        parser.add_multi_list("COMMAND", commands);
        parser.add_value("WORKING_DIRECTORY", working_dir);
        parser.add_value("TIMEOUT", timeout);
        parser.add_value("RESULT_VARIABLE", result_variable);
        parser.add_value("RESULTS_VARIABLE", results_variable);
        parser.add_value("OUTPUT_VARIABLE", output_variable);
        parser.add_value("ERROR_VARIABLE", error_variable);
        parser.add_value("INPUT_FILE", input_file);
        parser.add_value("OUTPUT_FILE", output_file);
        parser.add_value("ERROR_FILE", error_file);
        parser.add_value("COMMAND_ECHO", command_echo);
        parser.add_value("ENCODING", encoding);
        parser.add_flag("OUTPUT_QUIET", output_quiet);
        parser.add_flag("ERROR_QUIET", error_quiet);
        parser.add_flag("OUTPUT_STRIP_TRAILING_WHITESPACE", output_strip_trailing_whitespace);
        parser.add_flag("ERROR_STRIP_TRAILING_WHITESPACE", error_strip_trailing_whitespace);
        parser.add_flag("ECHO_OUTPUT_VARIABLE", echo_output_variable);
        parser.add_flag("ECHO_ERROR_VARIABLE", echo_error_variable);
        parser.add_flag("COMMAND_ERROR_IS_FATAL", command_error_is_fatal);

        PARSE_OR_RETURN(parser, interp, args);

        if (commands.empty()) {
            interp.set_fatal_error("execute_process requires at least one COMMAND");
            return;
        }

        ProcessOptions options;
        options.working_dir = working_dir;
        options.input_file = input_file;
        options.output_file = output_file;
        options.error_file = error_file;
        options.output_quiet = output_quiet;
        options.error_quiet = error_quiet;

        if (!timeout.empty()) {
            try {
                options.timeout = std::stod(timeout);
            } catch (...) {
                interp.set_fatal_error("execute_process invalid TIMEOUT: " + timeout);
                return;
            }
        }

        if (!output_variable.empty()) options.output_variable = &output_variable;
        if (!error_variable.empty()) options.error_variable = &error_variable;

        // Check if this is a pkg-config command that we can cache
        bool is_pkgconfig = !commands.empty() && !commands[0].empty() && is_pkgconfig_command(commands[0][0]);
        bool can_cache = is_pkgconfig &&
                        output_file.empty() &&   // No file redirection
                        error_file.empty() &&
                        input_file.empty() &&
                        (!output_variable.empty() || !result_variable.empty());  // Must capture output or result

        PipelineResult res;

        if (can_cache) {
            // Try cache lookup
            auto& cache = interp.get_cache_store();
            std::string signature = compute_command_signature(commands, options);

            auto cached = cache.lookup<CacheSubsystem::ExternalCommand>(signature);
            if (cached && validate_command_cache(*cached)) {
                // Cache hit - use cached results
                res.captured_stdout = cached->stdout_output;
                res.captured_stderr = cached->stderr_output;
                res.exit_codes.push_back(cached->exit_code);
            } else {
                // Cache miss - execute and store
                res = execute_pipeline(commands, options);

                // Store in cache
                ExternalCommandCacheEntry entry;
                entry.stdout_output = res.captured_stdout;
                entry.stderr_output = res.captured_stderr;
                entry.exit_code = res.exit_codes.empty() ? -1 : res.exit_codes.back();
                entry.tracked_dir_mtimes = get_tracked_dir_mtimes(commands[0][0]);

                cache.insert<CacheSubsystem::ExternalCommand>(signature, entry);
            }
        } else {
            // Not cacheable - execute normally
            res = execute_pipeline(commands, options);
        }

        auto strip_trailing = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
                s.pop_back();
            }
        };

        if (output_strip_trailing_whitespace) {
            strip_trailing(res.captured_stdout);
        }
        if (error_strip_trailing_whitespace) {
            strip_trailing(res.captured_stderr);
        }

        if (!output_variable.empty()) {
            interp.set_variable(output_variable, res.captured_stdout);
        }
        if (!error_variable.empty()) {
            interp.set_variable(error_variable, res.captured_stderr);
        }

        if (!result_variable.empty()) {
            interp.set_variable(result_variable, res.exit_codes.empty() ? "-1" : std::to_string(res.exit_codes.back()));
        }

        if (!results_variable.empty()) {
            std::string results;
            for (size_t i = 0; i < res.exit_codes.size(); ++i) {
                if (i > 0) results += ";";
                results += std::to_string(res.exit_codes[i]);
            }
            interp.set_variable(results_variable, results);
        }

        if (command_error_is_fatal) {
            for (int code : res.exit_codes) {
                if (code != 0) {
                    interp.set_fatal_error("execute_process failed with exit code " + std::to_string(code));
                    return;
                }
            }
        }
    });
}

} // namespace dmake
