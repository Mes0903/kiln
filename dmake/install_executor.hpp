#pragma once
#include <expected>
#include <string>
#include <memory>
#include <vector>

namespace dmake {

class Interpreter;
struct InstallRule;

std::expected<void, std::string> execute_install_rules(
    Interpreter* interp,
    const std::vector<std::shared_ptr<InstallRule>>& rules,
    const std::string& install_prefix,
    const std::string& current_config,
    const std::string& component_filter = ""
);

} // namespace dmake
