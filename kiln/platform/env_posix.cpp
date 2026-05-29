#include "env.hpp"

#include <cstdlib>

extern char** environ;

namespace kiln::platform {

std::optional<std::string> get_env(const std::string& name) {
    if (const char* value = std::getenv(name.c_str())) return std::string(value);
    return std::nullopt;
}

bool set_env(const std::string& name, const std::string& value) {
    return ::setenv(name.c_str(), value.c_str(), /*overwrite=*/1) == 0;
}

bool unset_env(const std::string& name) {
    return ::unsetenv(name.c_str()) == 0;
}

std::vector<std::string> current_environment() {
    std::vector<std::string> result;
    for (char** env = environ; env && *env; ++env) { result.emplace_back(*env); }
    return result;
}

} // namespace kiln::platform
