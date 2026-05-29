#pragma once

#include <optional>
#include <string>
#include <vector>

namespace kiln::platform {

std::optional<std::string> get_env(const std::string& name);
bool set_env(const std::string& name, const std::string& value);
bool unset_env(const std::string& name);
std::vector<std::string> current_environment();

} // namespace kiln::platform
