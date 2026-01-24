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
struct IfBlock;

using AstNode = std::variant<CommandInvocation, IfBlock>;

struct IfBlock {
    std::vector<Argument> condition;
    std::vector<AstNode> then_branch;
    std::vector<AstNode> else_branch;
};

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

    std::string peek_identifier();
    std::expected<std::vector<AstNode>, ParseError> parse_block(const std::vector<std::string>& terminators);
    std::expected<IfBlock, ParseError> parse_if_block(const CommandInvocation& if_command);
    void consume_whitespace();
    std::expected<CommandInvocation, ParseError> parse_command_invocation();
    std::expected<Argument, ParseError> parse_argument();
    std::expected<std::vector<ArgumentPart>, ParseError> parse_unquoted_argument_value();
    std::expected<std::vector<ArgumentPart>, ParseError> parse_quoted_argument_value();
    std::expected<std::string, ParseError> parse_bracket_argument();
};

} // namespace dmake
