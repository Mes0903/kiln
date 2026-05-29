#pragma once

#include <string>

namespace kiln::platform {

enum class StandardStream {
    input,
    output,
    error,
};

struct HostInfo {
    std::string system_name;
    std::string system_release;
    std::string system_version;
    std::string machine;
};

bool is_terminal(StandardStream stream);
int terminal_width(StandardStream stream = StandardStream::output);
std::string executable_path();
std::string current_working_directory();
std::string host_name();
long logical_core_count();
HostInfo host_info();

} // namespace kiln::platform
