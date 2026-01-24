#pragma once

#include <string>
#include <vector>
#include <string_view>
#include <variant>
#include <expected>

namespace dmake {

struct ParseError {
    size_t row;
    size_t col;
    std::string reason;
};

struct VariableReference {
    std::string name;
};

using ArgumentPart = std::variant<std::string, VariableReference>;

struct Argument {
    std::vector<ArgumentPart> parts;
    bool quoted;
};

struct CommandInvocation;

using AstNode = std::variant<CommandInvocation>;

struct CommandInvocation {
    std::string identifier;
    std::vector<Argument> arguments;
};

class Parser {
public:
    explicit Parser(std::string_view content);

    std::expected<std::vector<AstNode>, ParseError> parse();

private:
    std::string_view content_;
    size_t pos_ = 0;
    size_t row_ = 1;
    size_t col_ = 1;

    void consume_whitespace();
    std::expected<CommandInvocation, ParseError> parse_command_invocation();
    std::expected<Argument, ParseError> parse_argument();
    std::expected<std::vector<ArgumentPart>, ParseError> parse_unquoted_argument_value();
    std::expected<std::vector<ArgumentPart>, ParseError> parse_quoted_argument_value();
    std::expected<std::string, ParseError> parse_bracket_argument();
};

} // namespace dmake
