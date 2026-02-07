#include "utils.hpp"
#include "inner/blake2b.h"
#include "inner/sha256.h"
#include "inner/md5.h"
#ifdef __linux__
#include <linux/limits.h>
#endif
#include <type_traits>
#include <iostream>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <chrono>

dmake::Hash256 dmake::blake2b(const void *data, size_t len, const void* key, size_t keylen)
{
    Hash256 hash;
    static_assert(sizeof(hash) == 32, "Hash256 must be 32 bytes");
    static_assert(std::is_standard_layout<Hash256>::value, "Hash256 must be a standard layout type");
    dmake_blake2b(&hash, sizeof(hash), data, len, key, keylen);
    return hash;
}

dmake::Hash256 dmake::sha256(const void* data, size_t len)
{
    Hash256 hash;
    SHA256_CTX ctx;
    dmake_sha256_init(&ctx);
    dmake_sha256_update(&ctx, (const uint8_t*)data, len);
    dmake_sha256_final(&ctx, (uint8_t*)&hash);
    return hash;
}

dmake::Hash128 dmake::md5(const void* data, size_t len)
{
    Hash128 hash;
    MD5_CTX ctx;
    dmake_md5_init(&ctx);
    dmake_md5_update(&ctx, (const uint8_t*)data, len);
    dmake_md5_final(&ctx, (uint8_t*)&hash);
    return hash;
}

std::string dmake::Hash256::to_string() const
{
    std::string result;
    result.reserve(2 * sizeof(bytes));
    for (unsigned char byte : bytes)
    {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 0xf];
    }
    return result;
}

std::string dmake::Hash128::to_string() const
{
    std::string result;
    result.reserve(2 * sizeof(bytes));
    for (unsigned char byte : bytes)
    {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 0xf];
    }
    return result;
}

dmake::CommandResult dmake::run_command(const std::string& command, const std::string& working_dir)
{
    std::string prev_dir;
    if (!working_dir.empty()) {
        char* cwd = getcwd(nullptr, 0);
        if (cwd) {
            prev_dir = cwd;
            free(cwd);
        }
        if (chdir(working_dir.c_str()) != 0) {
            return {-1, "Failed to change directory to " + working_dir};
        }
    }

    std::string full_command = command + " 2>&1";
    FILE* pipe = popen(full_command.c_str(), "r");
    if (!pipe) {
        if (!prev_dir.empty()) chdir(prev_dir.c_str());
        return {-1, "Failed to execute command"};
    }

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }

    int status = pclose(pipe);
    if (!prev_dir.empty()) chdir(prev_dir.c_str());

    // WEXITSTATUS is only valid if WIFEXITED is true.
    // On many systems pclose returns the raw status.
    int exit_code = (status == -1) ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : status);

    return {exit_code, output};
}

std::string dmake::escape_shell_arg(const std::string& arg) {
    if (arg.empty()) return "''";

    bool needed = false;
    for (char c : arg) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '\'' || c == '"' || c == '\\' ||
            c == '`' || c == '$' || c == '&' || c == '|' || c == ';' || c == '<' || c == '>' ||
            c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || c == '*' ||
            c == '?' || c == '~' || c == '#' || c == '!' || c == '^') {
            needed = true;
            break;
        }
    }

    if (!needed) return arg;

    std::string result = "'";
    for (char c : arg) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

// Check if arg starts with a shell redirection prefix.
// Returns the length of the prefix (0 if none).
// Handles: >>, 2>>, 2>, 1>, >, <
static size_t shell_redirect_prefix_len(const std::string& arg) {
    // Order matters: check longer prefixes first
    if (arg.starts_with("2>>")) return 3;
    if (arg.starts_with("1>>")) return 3;
    if (arg.starts_with(">>"))  return 2;
    if (arg.starts_with("2>"))  return 2;
    if (arg.starts_with("1>"))  return 2;
    if (arg.starts_with(">"))   return 1;
    if (arg.starts_with("<"))   return 1;
    return 0;
}

static bool is_shell_operator(const std::string& arg) {
    return arg == "|" || arg == "&&" || arg == "||" || arg == "2>&1";
}

std::string dmake::join_command(const std::vector<std::string>& args) {
    std::string result;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) result += " ";
        if (is_shell_operator(args[i])) {
            result += args[i];
        } else if (size_t pfx = shell_redirect_prefix_len(args[i])) {
            // Split redirection operator from path: ">file" → > + escaped(file)
            result += args[i].substr(0, pfx);
            std::string path = args[i].substr(pfx);
            if (!path.empty()) {
                result += escape_shell_arg(path);
            }
        } else {
            result += escape_shell_arg(args[i]);
        }
    }
    return result;
}

std::string dmake::join_command_raw(const std::vector<std::string>& args) {
    std::string result;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) result += " ";
        result += args[i];
    }
    return result;
}

dmake::CommandResult dmake::run_command(const std::vector<std::string>& command, const std::string& working_dir) {
    return run_command(join_command(command), working_dir);
}

std::string dmake::get_executable_path() {
#ifdef __linux__
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
        return std::string(result, count);
    }
#endif
    // Fallback or other platforms
    return "dmake";
}

dmake::PipelineResult dmake::execute_pipeline(const std::vector<std::vector<std::string>>& commands, const ProcessOptions& options) {
    if (commands.empty()) return {};

    size_t num_commands = commands.size();
    std::vector<int> pids(num_commands);
    std::vector<std::vector<int>> pipes(num_commands - 1, std::vector<int>(2));

    for (size_t i = 0; i < num_commands - 1; ++i) {
        if (pipe(pipes[i].data()) == -1) {
            return {{1}, "", "Failed to create pipe"};
        }
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (options.output_variable) pipe(stdout_pipe);
    if (options.error_variable) pipe(stderr_pipe);

    for (size_t i = 0; i < num_commands; ++i) {
        pids[i] = fork();
        if (pids[i] == 0) { // Child
            if (!options.working_dir.empty()) {
                if (chdir(options.working_dir.c_str()) != 0) {
                    perror("chdir");
                    exit(1);
                }
            }

            // Stdin
            if (i == 0) {
                if (!options.input_file.empty()) {
                    int fd = open(options.input_file.c_str(), O_RDONLY);
                    if (fd != -1) { dup2(fd, STDIN_FILENO); close(fd); }
                }
            } else {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            // Stdout
            if (i == num_commands - 1) {
                if (options.output_variable) {
                    dup2(stdout_pipe[1], STDOUT_FILENO);
                } else if (!options.output_file.empty()) {
                    int fd = open(options.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd != -1) { dup2(fd, STDOUT_FILENO); close(fd); }
                } else if (options.output_quiet) {
                    int fd = open("/dev/null", O_WRONLY);
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
            } else {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // Stderr
            if (options.error_variable) {
                dup2(stderr_pipe[1], STDERR_FILENO);
            } else if (!options.error_file.empty()) {
                int fd = open(options.error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd != -1) { dup2(fd, STDERR_FILENO); close(fd); }
            } else if (options.error_quiet) {
                int fd = open("/dev/null", O_WRONLY);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            // Close all pipes in child
            for (auto& p : pipes) { close(p[0]); close(p[1]); }
            if (options.output_variable) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
            if (options.error_variable) { close(stderr_pipe[0]); close(stderr_pipe[1]); }

            // Exec
            std::vector<char*> argv;
            for (const auto& arg : commands[i]) argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            perror("execvp");
            exit(1);
        }
    }

    // Parent
    for (auto& p : pipes) { close(p[0]); close(p[1]); }
    if (options.output_variable) close(stdout_pipe[1]);
    if (options.error_variable) close(stderr_pipe[1]);

    PipelineResult result;

    // Read stdout/stderr if needed
    auto read_all = [](int fd) {
        std::string out;
        char buffer[4096];
        ssize_t bytes;
        while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) {
            out.append(buffer, bytes);
        }
        return out;
    };

    if (options.output_variable) {
        result.captured_stdout = read_all(stdout_pipe[0]);
        close(stdout_pipe[0]);
    }
    if (options.error_variable) {
        result.captured_stderr = read_all(stderr_pipe[0]);
        close(stderr_pipe[0]);
    }

    // Wait for all processes with optional timeout
    auto start_time = std::chrono::steady_clock::now();
    std::vector<bool> finished(num_commands, false);
    size_t finished_count = 0;
    result.exit_codes.resize(num_commands, -1);

    while (finished_count < num_commands) {
        bool any_progress = false;
        for (size_t i = 0; i < num_commands; ++i) {
            if (!finished[i]) {
                int status;
                pid_t res = waitpid(pids[i], &status, WNOHANG);
                if (res > 0) {
                    if (WIFEXITED(status)) result.exit_codes[i] = WEXITSTATUS(status);
                    else result.exit_codes[i] = -1;
                    finished[i] = true;
                    finished_count++;
                    any_progress = true;
                } else if (res == -1) {
                    finished[i] = true;
                    finished_count++;
                }
            }
        }

        if (finished_count == num_commands) break;

        if (options.timeout > 0.0) {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = now - start_time;
            if (elapsed.count() >= options.timeout) {
                // Timeout! Kill remaining
                for (size_t i = 0; i < num_commands; ++i) {
                    if (!finished[i]) {
                        kill(pids[i], SIGKILL);
                        int status;
                        waitpid(pids[i], &status, 0);
                        result.exit_codes[i] = -1; // Or some other indicator
                    }
                }
                break;
            }
        }

        if (!any_progress) {
            usleep(10000); // 10ms sleep to avoid busy wait
        }
    }

    return result;
}
