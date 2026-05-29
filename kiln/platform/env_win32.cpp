#include "env.hpp"

#include <cstdlib>

namespace kiln::platform {

std::optional<std::string> get_env(const std::string& name) {
    size_t required = 0;
    getenv_s(&required, nullptr, 0, name.c_str());
    if (required == 0) return std::nullopt;

    std::string value(required, '\0');
    getenv_s(&required, value.data(), value.size(), name.c_str());
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

bool set_env(const std::string& name, const std::string& value) {
    return _putenv_s(name.c_str(), value.c_str()) == 0;
}

bool unset_env(const std::string& name) {
    return _putenv_s(name.c_str(), "") == 0;
}

std::vector<std::string> current_environment() {
    std::vector<std::string> result;
    char** env = _environ;
    for (; env && *env; ++env) { result.emplace_back(*env); }
    return result;
}

} // namespace kiln::platform
