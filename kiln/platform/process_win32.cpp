#include "process.hpp"

namespace kiln::platform {

CommandResult run_command(const std::vector<std::string>& command, const std::string&) {
    if (command.empty()) return {-1, "Empty command"};
    return {-1, "Windows process execution is not implemented yet"};
}

PipelineResult execute_pipeline(const std::vector<std::vector<std::string>>& commands, const ProcessOptions&) {
    if (commands.empty()) return {};
    return {{1}, "", "", "Windows process execution is not implemented yet"};
}

int run_foreground(const std::vector<std::string>& command, const ForegroundOptions&) {
    return command.empty() ? 1 : 1;
}

int replace_current_process(const std::vector<std::string>& command) {
    return command.empty() ? 22 : 1;
}

bool terminate_process(int, int) {
    return false;
}

} // namespace kiln::platform
