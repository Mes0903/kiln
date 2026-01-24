#include "cmake-language.hpp"

#include <cctype>

namespace dmake {

Parser::Parser(std::string_view content) : content_(content) {}

std::expected<std::vector<AstNode>, ParseError> Parser::parse() {
    std::vector<AstNode> ast;
    while (pos_ < content_.length()) {
        consume_whitespace();
        if (pos_ >= content_.length()) {
            break;
        }

        auto command_or_error = parse_command_invocation();
        if (command_or_error) {
            ast.emplace_back(std::move(command_or_error.value()));
        } else {
            return std::unexpected(command_or_error.error());
        }
    }
    return ast;
}

void Parser::consume_whitespace() {
    while (pos_ < content_.length()) {
        if (std::isspace(content_[pos_])) {
            if (content_[pos_] == '\n') {
                row_++;
                col_ = 1;
            } else {
                col_++;
            }
            pos_++;
        } else if (content_[pos_] == '#') {
            if (pos_ + 1 < content_.length() && content_[pos_ + 1] == '[') {
                // Bracket comment
                pos_++; // #
                col_++;
                pos_++; // [
                col_++;

                size_t equals_count = 0;
                while (pos_ < content_.length() && content_[pos_] == '=') {
                    equals_count++;
                    pos_++;
                    col_++;
                }

                if (pos_ >= content_.length() || content_[pos_] != '[') {
                    // This is not a valid bracket comment, so we treat it as a line comment
                    while (pos_ < content_.length() && content_[pos_] != '\n') {
                        pos_++;
                        col_++;
                    }
                } else {
                    pos_++; // [
                    col_++;

                    while (pos_ + equals_count + 1 < content_.length()) {
                        if (content_[pos_] == ']' && content_.substr(pos_ + 1, equals_count) == std::string(equals_count, '=') && content_[pos_ + equals_count + 1] == ']') {
                            pos_ += equals_count + 2;
                            col_ += equals_count + 2;
                            break;
                        }
                        if (content_[pos_] == '\n') {
                            row_++;
                            col_ = 1;
                        } else {
                            col_++;
                        }
                        pos_++;
                    }
                }
            } else {
                // Line comment
                while (pos_ < content_.length() && content_[pos_] != '\n') {
                    pos_++;
                    col_++;
                }
            }
        } else {
            break;
        }
    }
}

std::expected<CommandInvocation, ParseError> Parser::parse_command_invocation() {
    // Parse identifier
    size_t start_pos = pos_;
    while (pos_ < content_.length() && (std::isalnum(content_[pos_]) || content_[pos_] == '_')) {
        pos_++;
        col_++;
    }
    std::string identifier(content_.substr(start_pos, pos_ - start_pos));

    if (identifier.empty()) {
        return std::unexpected(ParseError{row_, col_, "Expected an identifier"});
    }

    consume_whitespace();

    // Parse opening parenthesis
    if (pos_ >= content_.length() || content_[pos_] != '(') {
        return std::unexpected(ParseError{row_, col_, "Expected '(' after identifier"});
    }
    pos_++;
    col_++;

    CommandInvocation cmd_inv{std::move(identifier), {}};

    // Parse arguments
    while (pos_ < content_.length()) {
        consume_whitespace();
        if (pos_ < content_.length() && content_[pos_] == ')') {
            break;
        }

        auto arg_or_error = parse_argument();
        if (arg_or_error) {
            cmd_inv.arguments.push_back(std::move(arg_or_error.value()));
        } else {
            return std::unexpected(arg_or_error.error());
        }
    }

    // Parse closing parenthesis
    if (pos_ >= content_.length() || content_[pos_] != ')') {
        return std::unexpected(ParseError{row_, col_, "Expected ')' to close argument list"});
    }
    pos_++;
    col_++;

    return cmd_inv;
}

std::expected<Argument, ParseError> Parser::parse_argument() {
    if (pos_ >= content_.length()) {
        return std::unexpected(ParseError{row_, col_, "Unexpected end of input"});
    }

    if (content_[pos_] == '"') {
        auto value_or_error = parse_quoted_argument_value();
        if (!value_or_error) {
            return std::unexpected(value_or_error.error());
        }
        return Argument{std::move(value_or_error.value()), true};
    } else if (content_[pos_] == '[') {
        auto value_or_error = parse_bracket_argument();
        if (!value_or_error) {
            return std::unexpected(value_or_error.error());
        }
        return Argument{{std::move(value_or_error.value())}, false};
    } else {
        auto value_or_error = parse_unquoted_argument_value();
        if (!value_or_error) {
            return std::unexpected(value_or_error.error());
        }
        return Argument{std::move(value_or_error.value()), false};
    }
}

std::expected<std::vector<ArgumentPart>, ParseError> Parser::parse_unquoted_argument_value() {
    std::vector<ArgumentPart> parts;
    size_t start_pos = pos_;

    while (pos_ < content_.length() && !std::isspace(content_[pos_]) && content_[pos_] != ')') {
        if (content_[pos_] == '$' && pos_ + 1 < content_.length() && content_[pos_ + 1] == '{') {
            if (start_pos < pos_) {
                parts.emplace_back(std::string(content_.substr(start_pos, pos_ - start_pos)));
            }

            pos_ += 2; // Consume ${
            col_ += 2;
            size_t var_start = pos_;
            while (pos_ < content_.length() && content_[pos_] != '}') {
                pos_++;
                col_++;
            }
            if (pos_ >= content_.length()) {
                return std::unexpected(ParseError{row_, col_, "Unterminated variable reference"});
            }
            parts.emplace_back(VariableReference{std::string(content_.substr(var_start, pos_ - var_start))});
            pos_++; // Consume }
            col_++;
            start_pos = pos_;
            continue;
        }
        pos_++;
        col_++;
    }

    if (start_pos < pos_) {
        parts.emplace_back(std::string(content_.substr(start_pos, pos_ - start_pos)));
    }

    if (parts.empty()) {
        return std::unexpected(ParseError{row_, col_, "Expected an unquoted argument"});
    }

    return parts;
}

std::expected<std::vector<ArgumentPart>, ParseError> Parser::parse_quoted_argument_value() {
    pos_++; // Consume opening quote
    col_++;

    std::vector<ArgumentPart> parts;
    std::string current_literal;
    bool escaped = false;

    while (pos_ < content_.length()) {
        char current = content_[pos_];
        if (escaped) {
            current_literal += current;
            escaped = false;
            pos_++;
            col_++;
            continue;
        }
        if (current == '\\') {
            escaped = true;
            pos_++;
            col_++;
            continue;
        }
        if (current == '"') {
            pos_++; // Consume closing quote
            col_++;
            if (!current_literal.empty()) {
                parts.emplace_back(current_literal);
            }
            return parts;
        }
        if (current == '$' && pos_ + 1 < content_.length() && content_[pos_ + 1] == '{') {
            if (!current_literal.empty()) {
                parts.emplace_back(current_literal);
                current_literal.clear();
            }

            pos_ += 2; // Consume ${
            col_ += 2;
            size_t var_start = pos_;
            while (pos_ < content_.length() && content_[pos_] != '}') {
                pos_++;
                col_++;
            }
            if (pos_ >= content_.length()) {
                return std::unexpected(ParseError{row_, col_, "Unterminated variable reference"});
            }
            parts.emplace_back(VariableReference{std::string(content_.substr(var_start, pos_ - var_start))});
            pos_++; // Consume }
            col_++;
            continue;
        }
        
        current_literal += current;
        pos_++;
        if (current == '\n') {
            row_++;
            col_ = 1;
        } else {
            col_++;
        }
    }

    return std::unexpected(ParseError{row_, col_, "Unterminated quoted argument"});
}

std::expected<std::string, ParseError> Parser::parse_bracket_argument() {
    pos_++; // Consume opening bracket
    col_++;

    size_t equals_count = 0;
    while (pos_ < content_.length() && content_[pos_] == '=') {
        equals_count++;
        pos_++;
        col_++;
    }

    if (pos_ >= content_.length() || content_[pos_] != '[') {
        return std::unexpected(ParseError{row_, col_, "Invalid bracket argument"});
    }
    pos_++; // Consume opening bracket
    col_++;

    size_t start_pos = pos_;
    while (pos_ + equals_count + 1 < content_.length()) {
        if (content_[pos_] == ']' && content_.substr(pos_ + 1, equals_count) == std::string(equals_count, '=') && content_[pos_ + equals_count + 1] == ']') {
            std::string value(content_.substr(start_pos, pos_ - start_pos));
            pos_ += equals_count + 2;
            col_ += equals_count + 2;
            return value;
        }
        if (content_[pos_] == '\n') {
            row_++;
            col_ = 1;
        } else {
            col_++;
        }
        pos_++;
    }

    return std::unexpected(ParseError{row_, col_, "Unterminated bracket argument"});
}

} // namespace dmake
