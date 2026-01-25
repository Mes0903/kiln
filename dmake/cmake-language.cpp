#include "cmake-language.hpp"

#include <cctype>
#include <algorithm>

namespace dmake {

Parser::Parser(std::string_view content) : content_(content) {}

// Helper to peek at the next identifier without consuming it
std::string Parser::peek_identifier() {
    auto original_pos = pos_;
    auto original_row = row_;
    auto original_col = col_;

    consume_whitespace();
    
    size_t start_pos = pos_;
    while (pos_ < content_.length() && (std::isalnum(content_[pos_]) || content_[pos_] == '_')) {
        pos_++;
    }
    std::string identifier(content_.substr(start_pos, pos_ - start_pos));
    
    // Restore parser state
    pos_ = original_pos;
    row_ = original_row;
    col_ = original_col;

    return identifier;
}


std::expected<std::vector<AstNode>, ParseError> Parser::parse_block(const std::vector<std::string>& terminators) {
    std::vector<AstNode> ast;
    while (pos_ < content_.length()) {
        consume_whitespace();
        if (pos_ >= content_.length()) {
            break;
        }

        auto next_command = peek_identifier();
        if (std::find(terminators.begin(), terminators.end(), next_command) != terminators.end()) {
            break; 
        }

        auto command_or_error = parse_command_invocation();
        if (!command_or_error) {
            return std::unexpected(command_or_error.error());
        }
        
        std::string identifier_lower = command_or_error.value().identifier;
        std::transform(identifier_lower.begin(), identifier_lower.end(), identifier_lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        if (identifier_lower == "if") {
            auto if_block_or_error = parse_if_block(command_or_error.value());
            if(!if_block_or_error) {
                return std::unexpected(if_block_or_error.error());
            }
            ast.emplace_back(std::move(if_block_or_error.value()));
        } else if (identifier_lower == "function") {
            auto function_block_or_error = parse_function_block(command_or_error.value());
            if(!function_block_or_error) {
                return std::unexpected(function_block_or_error.error());
            }
            ast.emplace_back(std::move(function_block_or_error.value()));
        } else if (identifier_lower == "macro") {
            auto macro_block_or_error = parse_macro_block(command_or_error.value());
            if(!macro_block_or_error) {
                return std::unexpected(macro_block_or_error.error());
            }
            ast.emplace_back(std::move(macro_block_or_error.value()));
        } else if (identifier_lower == "foreach") {
            auto foreach_block_or_error = parse_foreach_block(command_or_error.value());
            if(!foreach_block_or_error) {
                return std::unexpected(foreach_block_or_error.error());
            }
            ast.emplace_back(std::move(foreach_block_or_error.value()));
        } else {
            ast.emplace_back(std::move(command_or_error.value()));
        }
    }
    return ast;
}

std::expected<IfBlock, ParseError> Parser::parse_if_block(const CommandInvocation& if_command) {
    IfBlock if_block;
    if_block.condition = if_command.arguments;
    if_block.row = if_command.row;
    if_block.col = if_command.col;

    auto then_branch_or_error = parse_block({"else", "endif"});
    if (!then_branch_or_error) {
        return std::unexpected(then_branch_or_error.error());
    }
    if_block.then_branch = std::move(then_branch_or_error.value());
    
    auto next_command = peek_identifier();
    if(next_command == "else") {
        auto else_command_or_error = parse_command_invocation(); // consume "else"
        if (!else_command_or_error) {
            return std::unexpected(else_command_or_error.error());
        }

        auto else_branch_or_error = parse_block({"endif"});
        if (!else_branch_or_error) {
            return std::unexpected(else_branch_or_error.error());
        }
        if_block.else_branch = std::move(else_branch_or_error.value());
    }
    
    auto endif_command_or_error = parse_command_invocation(); // consume "endif"
    if (!endif_command_or_error) {
         return std::unexpected(endif_command_or_error.error());
    }
    if (endif_command_or_error.value().identifier != "endif") {
        return std::unexpected(ParseError{row_, col_, "Expected 'endif'"});
    }

    return if_block;
}

std::expected<FunctionBlock, ParseError> Parser::parse_function_block(const CommandInvocation& function_command) {
    FunctionBlock function_block;

    if (function_command.arguments.empty()) {
        return std::unexpected(ParseError{function_command.row, function_command.col, "function() requires a name"});
    }

    // First argument is the function name
    if (!function_command.arguments[0].parts.empty() &&
        std::holds_alternative<std::string>(function_command.arguments[0].parts[0])) {
        function_block.name = std::get<std::string>(function_command.arguments[0].parts[0]);
    } else {
        return std::unexpected(ParseError{function_command.row, function_command.col, "function() name must be a simple identifier"});
    }

    // Remaining arguments are parameter names
    for (size_t i = 1; i < function_command.arguments.size(); ++i) {
        if (!function_command.arguments[i].parts.empty() &&
            std::holds_alternative<std::string>(function_command.arguments[i].parts[0])) {
            function_block.parameters.push_back(std::get<std::string>(function_command.arguments[i].parts[0]));
        } else {
            return std::unexpected(ParseError{function_command.row, function_command.col, "function() parameters must be simple identifiers"});
        }
    }

    // Parse the body
    auto body_or_error = parse_block({"endfunction"});
    if (!body_or_error) {
        return std::unexpected(body_or_error.error());
    }
    function_block.body = std::move(body_or_error.value());

    // Consume endfunction
    auto endfunction_command_or_error = parse_command_invocation();
    if (!endfunction_command_or_error) {
        return std::unexpected(endfunction_command_or_error.error());
    }

    std::string identifier_lower = endfunction_command_or_error.value().identifier;
    std::transform(identifier_lower.begin(), identifier_lower.end(), identifier_lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (identifier_lower != "endfunction") {
        return std::unexpected(ParseError{row_, col_, "Expected 'endfunction'"});
    }

    return function_block;
}

std::expected<MacroBlock, ParseError> Parser::parse_macro_block(const CommandInvocation& macro_command) {
    MacroBlock macro_block;

    if (macro_command.arguments.empty()) {
        return std::unexpected(ParseError{macro_command.row, macro_command.col, "macro() requires a name"});
    }

    // First argument is the macro name
    if (!macro_command.arguments[0].parts.empty() &&
        std::holds_alternative<std::string>(macro_command.arguments[0].parts[0])) {
        macro_block.name = std::get<std::string>(macro_command.arguments[0].parts[0]);
    } else {
        return std::unexpected(ParseError{macro_command.row, macro_command.col, "macro() name must be a simple identifier"});
    }

    // Remaining arguments are parameter names
    for (size_t i = 1; i < macro_command.arguments.size(); ++i) {
        if (!macro_command.arguments[i].parts.empty() &&
            std::holds_alternative<std::string>(macro_command.arguments[i].parts[0])) {
            macro_block.parameters.push_back(std::get<std::string>(macro_command.arguments[i].parts[0]));
        } else {
            return std::unexpected(ParseError{macro_command.row, macro_command.col, "macro() parameters must be simple identifiers"});
        }
    }

    // Parse the body
    auto body_or_error = parse_block({"endmacro"});
    if (!body_or_error) {
        return std::unexpected(body_or_error.error());
    }
    macro_block.body = std::move(body_or_error.value());

    // Consume endmacro
    auto endmacro_command_or_error = parse_command_invocation();
    if (!endmacro_command_or_error) {
        return std::unexpected(endmacro_command_or_error.error());
    }

    std::string identifier_lower = endmacro_command_or_error.value().identifier;
    std::transform(identifier_lower.begin(), identifier_lower.end(), identifier_lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (identifier_lower != "endmacro") {
        return std::unexpected(ParseError{row_, col_, "Expected 'endmacro'"});
    }

    return macro_block;
}

std::expected<ForeachBlock, ParseError> Parser::parse_foreach_block(const CommandInvocation& foreach_command) {
    ForeachBlock foreach_block;
    foreach_block.row = foreach_command.row;
    foreach_block.col = foreach_command.col;

    if (foreach_command.arguments.empty()) {
        return std::unexpected(ParseError{foreach_command.row, foreach_command.col, "foreach() requires a loop variable"});
    }

    // Extract loop variable (must be simple identifier)
    if (!foreach_command.arguments[0].parts.empty() &&
        std::holds_alternative<std::string>(foreach_command.arguments[0].parts[0])) {
        foreach_block.loop_var = std::get<std::string>(foreach_command.arguments[0].parts[0]);
    } else {
        return std::unexpected(ParseError{foreach_command.row, foreach_command.col, "foreach() loop variable must be a simple identifier"});
    }

    if (foreach_command.arguments.size() == 1) {
        return std::unexpected(ParseError{foreach_command.row, foreach_command.col, "foreach() requires items to iterate over"});
    }

    // Determine mode by checking second argument
    std::string mode_keyword;
    if (!foreach_command.arguments[1].quoted &&
        foreach_command.arguments[1].parts.size() == 1 &&
        std::holds_alternative<std::string>(foreach_command.arguments[1].parts[0])) {
        mode_keyword = std::get<std::string>(foreach_command.arguments[1].parts[0]);
        std::transform(mode_keyword.begin(), mode_keyword.end(), mode_keyword.begin(), ::toupper);
    }

    if (mode_keyword == "RANGE") {
        // RANGE mode: foreach(i RANGE <stop>) or foreach(i RANGE <start> <stop> [<step>])
        if (foreach_command.arguments.size() < 3) {
            return std::unexpected(ParseError{foreach_command.row, foreach_command.col, "foreach(RANGE) requires at least a stop value"});
        }
        if (foreach_command.arguments.size() > 5) {
            return std::unexpected(ParseError{foreach_command.row, foreach_command.col, "foreach(RANGE) accepts at most 3 range values (start, stop, step)"});
        }

        ForeachRange range;
        if (foreach_command.arguments.size() == 3) {
            // RANGE <stop>
            range.stop = foreach_command.arguments[2];
        } else if (foreach_command.arguments.size() == 4) {
            // RANGE <start> <stop>
            range.start = foreach_command.arguments[2];
            range.stop = foreach_command.arguments[3];
        } else { // size == 5
            // RANGE <start> <stop> <step>
            range.start = foreach_command.arguments[2];
            range.stop = foreach_command.arguments[3];
            range.step = foreach_command.arguments[4];
        }
        foreach_block.params = range;

    } else if (mode_keyword == "IN") {
        // IN mode: foreach(i IN [LISTS [<lists>]] [ITEMS [<items>]])
        ForeachIn in_params;

        size_t idx = 2;  // Start after "IN"
        while (idx < foreach_command.arguments.size()) {
            std::string keyword;
            if (!foreach_command.arguments[idx].quoted &&
                foreach_command.arguments[idx].parts.size() == 1 &&
                std::holds_alternative<std::string>(foreach_command.arguments[idx].parts[0])) {
                keyword = std::get<std::string>(foreach_command.arguments[idx].parts[0]);
                std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::toupper);
            }

            if (keyword == "LISTS") {
                // Collect list variable names until we hit ITEMS or end
                idx++;
                while (idx < foreach_command.arguments.size()) {
                    // Check if this is the ITEMS keyword
                    std::string next_keyword;
                    if (!foreach_command.arguments[idx].quoted &&
                        foreach_command.arguments[idx].parts.size() == 1 &&
                        std::holds_alternative<std::string>(foreach_command.arguments[idx].parts[0])) {
                        next_keyword = std::get<std::string>(foreach_command.arguments[idx].parts[0]);
                        std::transform(next_keyword.begin(), next_keyword.end(), next_keyword.begin(), ::toupper);
                    }
                    if (next_keyword == "ITEMS") {
                        break;  // Stop collecting, let the outer loop handle ITEMS
                    }
                    in_params.lists.push_back(foreach_command.arguments[idx]);
                    idx++;
                }
            } else if (keyword == "ITEMS") {
                // Collect item values until end
                idx++;
                while (idx < foreach_command.arguments.size()) {
                    in_params.items.push_back(foreach_command.arguments[idx]);
                    idx++;
                }
            } else {
                return std::unexpected(ParseError{foreach_command.row, foreach_command.col, "foreach(IN) expected LISTS or ITEMS keyword"});
            }
        }

        if (in_params.lists.empty() && in_params.items.empty()) {
            return std::unexpected(ParseError{foreach_command.row, foreach_command.col, "foreach(IN) requires at least one LISTS or ITEMS argument"});
        }

        foreach_block.params = in_params;

    } else {
        // Simple mode: foreach(i item1 item2 ...)
        // All arguments from index 1 onwards are items
        ForeachSimple simple;
        simple.items.insert(simple.items.end(),
                           foreach_command.arguments.begin() + 1,
                           foreach_command.arguments.end());
        foreach_block.params = simple;
    }

    // Parse the body
    auto body_or_error = parse_block({"endforeach"});
    if (!body_or_error) {
        return std::unexpected(body_or_error.error());
    }
    foreach_block.body = std::move(body_or_error.value());

    // Consume endforeach
    auto endforeach_command_or_error = parse_command_invocation();
    if (!endforeach_command_or_error) {
        return std::unexpected(endforeach_command_or_error.error());
    }

    std::string identifier_lower = endforeach_command_or_error.value().identifier;
    std::transform(identifier_lower.begin(), identifier_lower.end(), identifier_lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (identifier_lower != "endforeach") {
        return std::unexpected(ParseError{row_, col_, "Expected 'endforeach'"});
    }

    return foreach_block;
}

std::expected<std::vector<AstNode>, ParseError> Parser::parse() {
    return parse_block({});
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
    // Save location at start of command
    size_t cmd_row = row_;
    size_t cmd_col = col_;

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

    CommandInvocation cmd_inv{std::move(identifier), {}, cmd_row, cmd_col};

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
