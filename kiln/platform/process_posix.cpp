#include "process.hpp"

#include "kiln/build_system.hpp"
#include "kiln/container_utils.hpp"
#include "kiln/utils.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace kiln::platform {

namespace {

struct ResolvedCommand {
    std::vector<std::string> argv;
    std::string stdin_file;
    std::string stdout_file;
    std::string stderr_file;
    bool stdout_append = false;
};

size_t shell_redirect_prefix_len(const std::string& arg) {
    if (arg.starts_with("2>>")) return 3;
    if (arg.starts_with("1>>")) return 3;
    if (arg.starts_with(">>")) return 2;
    if (arg.starts_with("2>")) return 2;
    if (arg.starts_with("1>")) return 2;
    if (arg.starts_with(">")) return 1;
    if (arg.starts_with("<")) return 1;
    return 0;
}

std::pair<size_t, size_t> find_embedded_redirect(const std::string& arg) {
    for (size_t i = 1; i < arg.size(); ++i) {
        if (arg[i] == '>') {
            if (i + 1 < arg.size() && arg[i + 1] == '>') return {i, 2};
            return {i, 1};
        }
        if (arg[i] == '<') return {i, 1};
    }
    return {std::string::npos, 0};
}

void reset_child_signals() {
    ::signal(SIGINT, SIG_DFL);
    ::signal(SIGTERM, SIG_DFL);
    ::signal(SIGHUP, SIG_DFL);
}

void apply_env_changes(const std::vector<EnvChange>& changes) {
    for (const auto& change : changes) {
        if (change.value) {
            ::setenv(change.name.c_str(), change.value->c_str(), /*overwrite=*/1);
        } else {
            ::unsetenv(change.name.c_str());
        }
    }
}

CommandResult exec_resolved(const ResolvedCommand& rc, const std::string& working_dir) {
    int pipe_fd[2];
    if (::pipe(pipe_fd) != 0) return {-1, "Failed to create pipe"};

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return {-1, "Failed to fork"};
    }

    if (pid == 0) {
        ::setpgid(0, 0);
        kiln::set_parent_death_signal(SIGTERM);
        reset_child_signals();

        ::close(pipe_fd[0]);

        if (!working_dir.empty() && ::chdir(working_dir.c_str()) != 0) _exit(127);

        if (!rc.stdin_file.empty()) {
            int fd = ::open(rc.stdin_file.c_str(), O_RDONLY);
            if (fd < 0) _exit(127);
            ::dup2(fd, STDIN_FILENO);
            ::close(fd);
        }
        if (!rc.stdout_file.empty()) {
            int flags = O_WRONLY | O_CREAT | (rc.stdout_append ? O_APPEND : O_TRUNC);
            int fd = ::open(rc.stdout_file.c_str(), flags, 0644);
            if (fd < 0) _exit(127);
            ::dup2(fd, STDOUT_FILENO);
            ::close(fd);
        } else {
            ::dup2(pipe_fd[1], STDOUT_FILENO);
        }
        if (!rc.stderr_file.empty()) {
            int fd = ::open(rc.stderr_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) _exit(127);
            ::dup2(fd, STDERR_FILENO);
            ::close(fd);
        } else {
            ::dup2(pipe_fd[1], STDERR_FILENO);
        }
        ::close(pipe_fd[1]);

        std::vector<const char*> argv;
        argv.reserve(rc.argv.size() + 1);
        for (const auto& arg : rc.argv) argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        ::execvp(argv[0], const_cast<char* const*>(argv.data()));
        std::fprintf(stderr, "exec: %s: %s\n", argv[0], std::strerror(errno));
        _exit(127);
    }

    ::setpgid(pid, pid);
    kiln::register_child_pid(pid);

    ::close(pipe_fd[1]);

    std::string output;
    char buffer[4096];
    ssize_t n = 0;
    while ((n = ::read(pipe_fd[0], buffer, sizeof(buffer))) > 0) { output.append(buffer, n); }
    ::close(pipe_fd[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    kiln::unregister_child_pid(pid);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    return {exit_code, output};
}

} // namespace

CommandResult run_command(const std::vector<std::string>& command, const std::string& working_dir) {
    if (command.empty()) return {-1, "Empty command"};

    ResolvedCommand rc;

    for (size_t i = 0; i < command.size(); ++i) {
        const auto& arg = command[i];

        if (arg == "|" || arg == "&&" || arg == "||" || arg == "2>&1") {
            ResolvedCommand shell_rc;
            shell_rc.argv = {"/bin/sh", "-c", join(command, " ")};
            return exec_resolved(shell_rc, working_dir);
        }

        size_t pfx = shell_redirect_prefix_len(arg);
        if (pfx > 0) {
            std::string op = arg.substr(0, pfx);
            std::string path = arg.substr(pfx);
            if (path.empty() && i + 1 < command.size()) path = command[++i];
            path = kiln::strip_shell_quoting(path);

            if (op == "<")
                rc.stdin_file = path;
            else if (op == ">>" || op == "1>>") {
                rc.stdout_file = path;
                rc.stdout_append = true;
            } else if (op == ">" || op == "1>")
                rc.stdout_file = path;
            else if (op == "2>" || op == "2>>")
                rc.stderr_file = path;
        } else if (auto [pos, len] = find_embedded_redirect(arg); pos != std::string::npos) {
            std::string pre = arg.substr(0, pos);
            std::string op = arg.substr(pos, len);
            std::string path = arg.substr(pos + len);
            if (path.empty() && i + 1 < command.size()) path = command[++i];
            path = kiln::strip_shell_quoting(path);

            if (!pre.empty()) rc.argv.push_back(pre);

            if (op == "<")
                rc.stdin_file = path;
            else if (op == ">>") {
                rc.stdout_file = path;
                rc.stdout_append = true;
            } else if (op == ">")
                rc.stdout_file = path;
        } else {
            rc.argv.push_back(arg);
        }
    }

    if (rc.argv.empty()) return {-1, "Empty command after parsing"};
    return exec_resolved(rc, working_dir);
}

PipelineResult execute_pipeline(const std::vector<std::vector<std::string>>& commands, const ProcessOptions& options) {
    if (commands.empty()) return {};

    size_t num_commands = commands.size();
    std::vector<int> pids(num_commands);
    std::vector<std::vector<int>> pipes(num_commands - 1, std::vector<int>(2));

    for (size_t i = 0; i < num_commands - 1; ++i) {
        if (::pipe(pipes[i].data()) == -1) return {{1}, "", "", "Failed to create pipe"};
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (options.output_variable && ::pipe(stdout_pipe) == -1) return {{1}, "", "", "Failed to create stdout pipe"};
    if (options.error_variable && ::pipe(stderr_pipe) == -1) return {{1}, "", "", "Failed to create stderr pipe"};

    int setup_pipe[2];
    if (::pipe(setup_pipe) == -1) return {{1}, "", "", "Failed to create setup pipe"};
    int setup_flags = ::fcntl(setup_pipe[1], F_GETFD);
    if (setup_flags == -1 || ::fcntl(setup_pipe[1], F_SETFD, setup_flags | FD_CLOEXEC) == -1) {
        ::close(setup_pipe[0]);
        ::close(setup_pipe[1]);
        return {{1}, "", "", "Failed to configure setup pipe"};
    }

    for (size_t i = 0; i < num_commands; ++i) {
        pids[i] = ::fork();
        if (pids[i] == 0) {
            ::setpgid(0, 0);
            reset_child_signals();

            ::close(setup_pipe[0]);
            auto report_setup_error = [&](const std::string& message) {
                std::string msg = message + ": " + std::strerror(errno);
                ssize_t ignored = ::write(setup_pipe[1], msg.data(), msg.size());
                (void) ignored;
                _exit(127);
            };

            if (!options.working_dir.empty() && ::chdir(options.working_dir.c_str()) != 0) {
                report_setup_error("Failed to change directory to " + options.working_dir);
            }

            if (i == 0) {
                if (!options.input_file.empty()) {
                    int fd = ::open(options.input_file.c_str(), O_RDONLY);
                    if (fd == -1) report_setup_error("Failed to open INPUT_FILE " + options.input_file);
                    if (::dup2(fd, STDIN_FILENO) == -1) report_setup_error("Failed to redirect INPUT_FILE " + options.input_file);
                    ::close(fd);
                }
            } else {
                ::dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            if (i == num_commands - 1) {
                if (options.output_variable) {
                    ::dup2(stdout_pipe[1], STDOUT_FILENO);
                } else if (!options.output_file.empty()) {
                    int fd = ::open(options.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd == -1) report_setup_error("Failed to open OUTPUT_FILE " + options.output_file);
                    if (::dup2(fd, STDOUT_FILENO) == -1) report_setup_error("Failed to redirect OUTPUT_FILE " + options.output_file);
                    ::close(fd);
                } else if (options.output_quiet) {
                    int fd = ::open("/dev/null", O_WRONLY);
                    if (fd == -1 || ::dup2(fd, STDOUT_FILENO) == -1) report_setup_error("Failed to redirect stdout to /dev/null");
                    ::close(fd);
                }
            } else {
                ::dup2(pipes[i][1], STDOUT_FILENO);
            }

            if (options.error_variable) {
                ::dup2(stderr_pipe[1], STDERR_FILENO);
            } else if (!options.error_file.empty()) {
                int fd = ::open(options.error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) report_setup_error("Failed to open ERROR_FILE " + options.error_file);
                if (::dup2(fd, STDERR_FILENO) == -1) report_setup_error("Failed to redirect ERROR_FILE " + options.error_file);
                ::close(fd);
            } else if (options.error_quiet) {
                int fd = ::open("/dev/null", O_WRONLY);
                if (fd == -1 || ::dup2(fd, STDERR_FILENO) == -1) report_setup_error("Failed to redirect stderr to /dev/null");
                ::close(fd);
            }

            for (auto& p : pipes) {
                ::close(p[0]);
                ::close(p[1]);
            }
            if (options.output_variable) {
                ::close(stdout_pipe[0]);
                ::close(stdout_pipe[1]);
            }
            if (options.error_variable) {
                ::close(stderr_pipe[0]);
                ::close(stderr_pipe[1]);
            }

            std::vector<char*> argv;
            for (const auto& arg : commands[i]) argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);

            ::execvp(argv[0], argv.data());
            report_setup_error(std::string("exec ") + argv[0]);
        }

        ::setpgid(pids[i], pids[i]);
        kiln::register_child_pid(pids[i]);
    }

    for (auto& p : pipes) {
        ::close(p[0]);
        ::close(p[1]);
    }
    if (options.output_variable) ::close(stdout_pipe[1]);
    if (options.error_variable) ::close(stderr_pipe[1]);
    ::close(setup_pipe[1]);

    PipelineResult result;

    auto read_all = [](int fd) {
        std::string out;
        char buffer[4096];
        ssize_t bytes = 0;
        while ((bytes = ::read(fd, buffer, sizeof(buffer))) > 0) { out.append(buffer, bytes); }
        return out;
    };

    if (options.output_variable) {
        result.captured_stdout = read_all(stdout_pipe[0]);
        ::close(stdout_pipe[0]);
    }
    if (options.error_variable) {
        result.captured_stderr = read_all(stderr_pipe[0]);
        ::close(stderr_pipe[0]);
    }

    result.setup_error = read_all(setup_pipe[0]);
    ::close(setup_pipe[0]);

    auto start_time = std::chrono::steady_clock::now();
    std::vector<bool> finished(num_commands, false);
    size_t finished_count = 0;
    result.exit_codes.resize(num_commands, -1);

    while (finished_count < num_commands) {
        bool any_progress = false;
        for (size_t i = 0; i < num_commands; ++i) {
            if (!finished[i]) {
                int status = 0;
                pid_t res = ::waitpid(pids[i], &status, WNOHANG);
                if (res > 0) {
                    result.exit_codes[i] = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                    finished[i] = true;
                    finished_count++;
                    any_progress = true;
                    kiln::unregister_child_pid(pids[i]);
                } else if (res == -1 && errno != EINTR) {
                    finished[i] = true;
                    finished_count++;
                    kiln::unregister_child_pid(pids[i]);
                }
            }
        }

        if (finished_count == num_commands) break;

        if (options.timeout > 0.0) {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = now - start_time;
            if (elapsed.count() >= options.timeout) {
                for (size_t i = 0; i < num_commands; ++i) {
                    if (!finished[i]) {
                        ::kill(pids[i], SIGKILL);
                        int status = 0;
                        ::waitpid(pids[i], &status, 0);
                        kiln::unregister_child_pid(pids[i]);
                        result.exit_codes[i] = -1;
                    }
                }
                break;
            }
        }

        if (!any_progress) ::usleep(10000);
    }

    return result;
}

int run_foreground(const std::vector<std::string>& command, const ForegroundOptions& options) {
    if (command.empty()) return 1;

    pid_t pid = ::fork();
    if (pid < 0) return 1;

    if (pid == 0) {
        ::setpgid(0, 0);
        kiln::set_parent_death_signal(SIGTERM);
        reset_child_signals();
        apply_env_changes(options.environment);
        if (!options.working_dir.empty() && ::chdir(options.working_dir.c_str()) != 0) _exit(127);

        std::vector<char*> argv;
        for (const auto& arg : command) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    ::setpgid(pid, pid);
    kiln::register_child_pid(pid);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    kiln::unregister_child_pid(pid);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

int replace_current_process(const std::vector<std::string>& command) {
    if (command.empty()) return EINVAL;

    std::vector<char*> argv;
    for (const auto& arg : command) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    ::execvp(argv[0], argv.data());
    return errno;
}

bool terminate_process(int pid, int signal_number) {
    return ::kill(pid, signal_number) == 0;
}

} // namespace kiln::platform
