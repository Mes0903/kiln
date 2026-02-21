#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <expected>
#include <span>

namespace kiln {

class Interpreter;

/**
 * @brief Helper to parse CMake command arguments.
 *
 * CMake commands follow the pattern:
 *   command(positionals... [KEYWORD values...] [FLAG] ...)
 *
 * Positionals are collected until the first keyword is encountered.
 * Keywords can be flags (no values), single values, or lists.
 */
class CommandParser {
public:
    explicit CommandParser(std::string cmd_name);
    CommandParser(std::string cmd_name, std::string subcommand);

    /**
     * @brief Add a single positional argument (filled in order before keywords).
     * @param required If false, this positional is optional.
     */
    void positional(std::string& var, std::string label, bool required = true);

    /**
     * @brief Add a positional list (collects remaining args until first keyword).
     * @param required If true, parse() fails if the list is empty.
     */
    void positionals(std::vector<std::string>& var, std::string label, bool required = false);

    /**
     * @brief Add a boolean flag keyword (e.g., VERBATIM).
     */
    void flag(std::string keyword, bool& var);

    /**
     * @brief Add a single-value keyword (e.g., WORKING_DIRECTORY <dir>).
     */
    void value(std::string keyword, std::string& var);

    /**
     * @brief Add a list keyword (e.g., DEPENDS <dep1> <dep2>).
     * Values are collected until the next keyword.
     */
    void list(std::string keyword, std::vector<std::string>& var);

    /**
     * @brief Add a multi-list keyword (e.g., COMMAND <cmd1> COMMAND <cmd2>).
     * Each occurrence of the keyword starts a new sub-vector.
     */
    void multi_list(std::string keyword, std::vector<std::vector<std::string>>& var);

    /**
     * @brief Collect any unrecognized arguments instead of erroring.
     * Useful for commands like find_package where bare args are implicit components.
     */
    void unparsed(std::vector<std::string>& var);

    /**
     * @brief Parse the arguments.
     * @return Success or an error message.
     */
    std::expected<std::vector<std::string>, std::string> parse(std::span<const std::string> args);

    /**
     * @brief Get the expected syntax of the command.
     */
    std::string get_syntax() const;

private:
    enum class KeywordType { FLAG, VALUE, LIST, MULTI_LIST };

    struct KeywordInfo {
        KeywordType type;
        void* target;
        std::string keyword;
    };

    struct SinglePositional {
        std::string* var;
        std::string label;
        bool required;
        bool filled = false;
    };

    struct PositionalList {
        std::vector<std::string>* var;
        std::string label;
        bool required;
    };

    std::string cmd_name_;
    std::string subcommand_;
    std::vector<SinglePositional> single_positionals_;
    std::optional<PositionalList> positional_list_;
    std::map<std::string, KeywordInfo> keywords_;
    std::vector<std::string>* unparsed_ = nullptr;
};

#define PARSE_OR_RETURN(parser, interp, args) \
    if (auto res = (parser).parse(args); !res) { \
        (interp).set_fatal_error(res.error() + "\nExpected syntax: " + (parser).get_syntax()); \
        return; \
    } else { \
        for (const auto& w : *res) (interp).print_message("WARNING", w); \
    }

} // namespace kiln
