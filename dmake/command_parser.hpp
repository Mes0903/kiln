#pragma once
#include <string>
#include <vector>
#include <map>
#include <expected>
#include <span>

namespace dmake {

class Interpreter;

/**
 * @brief Helper to parse CMake command arguments.
 * 
 * Supports positional arguments, flags, single values, lists, and multi-lists.
 * Handles error reporting via return value.
 */
class CommandParser {
public:
    explicit CommandParser(std::string cmd_name, std::string subcommand = "");

    /**
     * @brief Add a positional argument. Must be called in order.
     * @param var Target string to store the value.
     * @param label Name of the argument for error messages.
     * @param required If true, parse() fails if this argument is missing.
     */
    void add_positional(std::string& var, std::string label, bool required = true);

    /**
     * @brief Add a boolean flag (e.g., VERBATIM).
     */
    void add_flag(std::string keyword, bool& var);

    /**
     * @brief Add a single value keyword (e.g., WORKING_DIRECTORY <dir>).
     */
    void add_value(std::string keyword, std::string& var);

    /**
     * @brief Add a list keyword (e.g., OUTPUT <file1> <file2>).
     * Subsequent occurrences of the same keyword append to the same list.
     */
    void add_list(std::string keyword, std::vector<std::string>& var);

    /**
     * @brief Add a multi-list keyword (e.g., COMMAND <cmd1> COMMAND <cmd2>).
     * Each occurrence of the keyword starts a new sub-vector.
     */
    void add_multi_list(std::string keyword, std::vector<std::vector<std::string>>& var);

    /**
     * @brief Add a target for arguments not associated with any keyword.
     * These are arguments seen after positionals but before any keyword,
     * or arguments seen after positionals if no keyword is ever active.
     */
    void add_default_list(std::vector<std::string>& var);

    /**
     * @brief Parse the arguments.
     * @return Success or an error message.
     */
    std::expected<void, std::string> parse(std::span<const std::string> args);

    /**
     * @brief Get the expected syntax of the command.
     */
    std::string get_syntax() const;

private:
    enum class ArgType { FLAG, VALUE, LIST, MULTI_LIST };
    struct KeywordInfo {
        ArgType type;
        void* target;
        std::string keyword;
    };

    struct Positional {
        std::string* var;
        std::string label;
        bool required;
        bool set = false;
    };

    std::string cmd_name_;
    std::string subcommand_;
    std::vector<Positional> positionals_;
    std::map<std::string, KeywordInfo> keywords_;
    std::vector<std::string>* default_list_ = nullptr;
};

#define PARSE_OR_RETURN(parser, interp, args) \
    if (auto res = (parser).parse(args); !res) { \
        (interp).set_fatal_error(res.error() + "\nExpected syntax: " + (parser).get_syntax()); \
        return; \
    }

} // namespace dmake
