#pragma once
#include "printing.hpp"
#include <expected>
#include <string>
#include <memory>
#include <vector>

namespace kiln {

class Interpreter;
struct InstallRule;

std::expected<void, std::string> execute_install_rules(
    Interpreter* interp,
    const std::vector<std::shared_ptr<InstallRule>>& rules,
    const std::string& install_prefix,
    const std::string& current_config,
    const std::string& component_filter = "",
    const OutputCtx& out = stdout_output_ctx()
);

} // namespace kiln
