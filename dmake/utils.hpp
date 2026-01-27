#pragma once

#include <cstddef>
#include <unistd.h>
#include <string>
#include <vector>

namespace dmake {
struct Hash256
{
    unsigned char bytes[32];

    std::string to_string() const;
};

/**
 * @brief Compute the BLAKE2b hash of the given data
 * @note When in doubt, use SHA3 or BLAKE2b. Both are safe and SHA3 is faster if
 * you are using OpenSSL and it has SHA3 in hardware mode. Otherwise BLAKE2b is
 * faster in software.
 */
Hash256 blake2b(const void *data, size_t len, const void* key, size_t keylen);
inline Hash256 blake2b(std::string_view str, std::string_view key="")
{
    return blake2b(str.data(), str.size(), key.data(), key.size());
}

/**
 * @brief Compute the SHA256 hash of the given data
 */
Hash256 sha256(const void* data, size_t len);
inline Hash256 sha256(std::string_view str)
{
    return sha256(str.data(), str.size());
}

struct CommandResult {
    int exit_code;
    std::string output;
};

/**
 * @brief Escapes a string for use as a shell argument.
 */
std::string escape_shell_arg(const std::string& arg);

/**
 * @brief Joins a command vector into a single string, escaping each argument.
 */
std::string join_command(const std::vector<std::string>& args);

/**
 * @brief Execute a shell command and capture its output.
 * @param command The command to execute.
 * @param working_dir The directory to run the command in (empty for current dir).
 * @return CommandResult containing exit code and combined stdout/stderr.
 */
CommandResult run_command(const std::string& command, const std::string& working_dir = "");

/**
 * @brief Execute a command (vector of args) and capture its output.
 */
CommandResult run_command(const std::vector<std::string>& command, const std::string& working_dir = "");

/**
 * @brief Get the absolute path to the current executable.
 */
std::string get_executable_path();

struct ProcessOptions {
    std::string working_dir;
    std::string input_file;
    std::string output_file;
    std::string error_file;
    bool output_quiet = false;
    bool error_quiet = false;
    double timeout = 0.0; // Seconds, 0 means no timeout
    // If not empty, stdout/stderr will be captured here
    std::string* output_variable = nullptr;
    std::string* error_variable = nullptr;
};

struct PipelineResult {
    std::vector<int> exit_codes;
    std::string captured_stdout;
    std::string captured_stderr;
};

/**
 * @brief Execute a pipeline of commands.
 */
PipelineResult execute_pipeline(const std::vector<std::vector<std::string>>& commands, const ProcessOptions& options = {});

}
