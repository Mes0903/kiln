#pragma once

#include <string>
#include <vector>

namespace kiln::platform {

std::string argv_to_windows_command_line(const std::vector<std::string>& argv);

} // namespace kiln::platform
