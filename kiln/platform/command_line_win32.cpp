#include "command_line.hpp"

namespace kiln::platform {

namespace {

bool needs_quotes(const std::string& arg) {
    if (arg.empty()) return true;
    for (char ch : arg) {
        if (ch == ' ' || ch == '\t' || ch == '"') return true;
    }
    return false;
}

std::string quote_arg(const std::string& arg) {
    if (!needs_quotes(arg)) return arg;

    std::string result;
    result.push_back('"');

    std::size_t backslashes = 0;
    for (char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }

        if (ch == '"') {
            result.append(backslashes * 2 + 1, '\\');
            result.push_back('"');
        } else {
            result.append(backslashes, '\\');
            result.push_back(ch);
        }
        backslashes = 0;
    }

    result.append(backslashes * 2, '\\');
    result.push_back('"');
    return result;
}

} // namespace

std::string argv_to_windows_command_line(const std::vector<std::string>& argv) {
    std::string result;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) result.push_back(' ');
        result += quote_arg(argv[i]);
    }
    return result;
}

} // namespace kiln::platform
