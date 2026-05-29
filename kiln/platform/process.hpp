#pragma once

#include <optional>
#include <string>
#include <vector>

namespace kiln::platform {

struct CommandResult {
    int exit_code;
    std::string output;
};

struct ProcessOptions {
    std::string working_dir;
    std::string input_file;
    std::string output_file;
    std::string error_file;
    bool output_quiet = false;
    bool error_quiet = false;
    double timeout = 0.0; // Seconds, 0 means no timeout
    std::string* output_variable = nullptr;
    std::string* error_variable = nullptr;
};

struct PipelineResult {
    std::vector<int> exit_codes;
    std::string captured_stdout;
    std::string captured_stderr;
    std::string setup_error;
};

struct EnvChange {
    std::string name;
    std::optional<std::string> value;
};

struct ForegroundOptions {
    std::string working_dir;
    std::vector<EnvChange> environment;
};

CommandResult run_command(const std::vector<std::string>& command, const std::string& working_dir = "");
PipelineResult execute_pipeline(const std::vector<std::vector<std::string>>& commands, const ProcessOptions& options = {});
int run_foreground(const std::vector<std::string>& command, const ForegroundOptions& options = {});
int replace_current_process(const std::vector<std::string>& command);
bool terminate_process(int pid, int signal_number);

} // namespace kiln::platform
