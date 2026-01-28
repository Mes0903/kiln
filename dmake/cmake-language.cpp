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
        std::string next_command_lower = next_command;
        std::transform(next_command_lower.begin(), next_command_lower.end(), next_command_lower.begin(), ::tolower);

        bool terminated = false;
        for (const auto& term : terminators) {
            std::string term_lower = term;
            std::transform(term_lower.begin(), term_lower.end(), term_lower.begin(), ::tolower);
            if (next_command_lower == term_lower) {
                terminated = true;
                break;
            }
        }
        if (terminated) {
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
        } else if (identifier_lower == "while") {
            auto while_block_or_error = parse_while_block(command_or_error.value());
            if(!while_block_or_error) {
                return std::unexpected(while_block_or_error.error());
            }
            ast.emplace_back(std::move(while_block_or_error.value()));
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
    if_block.offset = if_command.offset;

    auto then_branch_or_error = parse_block({"elseif", "else", "endif"});
    if (!then_branch_or_error) {
        return std::unexpected(then_branch_or_error.error());
    }
    if_block.then_branch = std::move(then_branch_or_error.value());
    
    while (true) {
        auto next_command = peek_identifier();
        std::string next_command_lower = next_command;
        std::transform(next_command_lower.begin(), next_command_lower.end(), next_command_lower.begin(), ::tolower);

        if (next_command_lower == "elseif") {
            auto elseif_command_or_error = parse_command_invocation();
            if (!elseif_command_or_error) return std::unexpected(elseif_command_or_error.error());
            
            ElseIfBlock elseif_branch;
            elseif_branch.condition = std::move(elseif_command_or_error.value().arguments);
            elseif_branch.row = elseif_command_or_error.value().row;
            elseif_branch.col = elseif_command_or_error.value().col;
            elseif_branch.offset = elseif_command_or_error.value().offset;
            
            auto body_or_error = parse_block({"elseif", "else", "endif"});
            if (!body_or_error) return std::unexpected(body_or_error.error());
            elseif_branch.body = std::move(body_or_error.value());
            elseif_branch.length = pos_ - elseif_branch.offset;
            
            if_block.elseif_branches.push_back(std::move(elseif_branch));
        } else if (next_command_lower == "else") {
            auto else_command_or_error = parse_command_invocation();
            if (!else_command_or_error) return std::unexpected(else_command_or_error.error());

            auto else_branch_or_error = parse_block({"endif"});
            if (!else_branch_or_error) return std::unexpected(else_branch_or_error.error());
            if_block.else_branch = std::move(else_branch_or_error.value());
            break; 
        } else if (next_command_lower == "endif") {
            break;
        } else {
             break;
        }
    }
    
    auto endif_command_or_error = parse_command_invocation(); // consume "endif"
    if (!endif_command_or_error) {
         return std::unexpected(endif_command_or_error.error());
    }
    std::string endif_lower = endif_command_or_error.value().identifier;
    std::transform(endif_lower.begin(), endif_lower.end(), endif_lower.begin(), ::tolower);
    if (endif_lower != "endif") {
        return std::unexpected(ParseError{endif_command_or_error.value().row, endif_command_or_error.value().col, endif_command_or_error.value().offset, endif_command_or_error.value().length, "Expected 'endif'"});
    }

    if_block.length = pos_ - if_block.offset;
    return if_block;
}

std::expected<FunctionBlock, ParseError> Parser::parse_function_block(const CommandInvocation& function_command) {
    FunctionBlock function_block;

    if (function_command.arguments.empty()) {
        return std::unexpected(ParseError{function_command.row, function_command.col, function_command.offset, function_command.length, "function() requires a name"});
    }

    // First argument is the function name
    if (!function_command.arguments[0].parts.empty() &&
        std::holds_alternative<std::string>(function_command.arguments[0].parts[0])) {
        function_block.name = std::get<std::string>(function_command.arguments[0].parts[0]);
    } else {
        return std::unexpected(ParseError{function_command.row, function_command.col, function_command.offset, function_command.length, "function() name must be a simple identifier"});
    }

    // Remaining arguments are parameter names
    for (size_t i = 1; i < function_command.arguments.size(); ++i) {
        if (!function_command.arguments[i].parts.empty() &&
            std::holds_alternative<std::string>(function_command.arguments[i].parts[0])) {
            function_block.parameters.push_back(std::get<std::string>(function_command.arguments[i].parts[0]));
        } else {
            return std::unexpected(ParseError{function_command.row, function_command.col, function_command.offset, function_command.length, "function() parameters must be simple identifiers"});
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
        return std::unexpected(ParseError{row_, col_, pos_, 11, "Expected 'endfunction'"});
    }

    return function_block;
}

std::expected<MacroBlock, ParseError> Parser::parse_macro_block(const CommandInvocation& macro_command) {
    MacroBlock macro_block;

    if (macro_command.arguments.empty()) {
        return std::unexpected(ParseError{macro_command.row, macro_command.col, macro_command.offset, macro_command.length, "macro() requires a name"});
    }

    // First argument is the macro name
    if (!macro_command.arguments[0].parts.empty() &&
        std::holds_alternative<std::string>(macro_command.arguments[0].parts[0])) {
        macro_block.name = std::get<std::string>(macro_command.arguments[0].parts[0]);
    } else {
        return std::unexpected(ParseError{macro_command.row, macro_command.col, macro_command.offset, macro_command.length, "macro() name must be a simple identifier"});
    }

    // Remaining arguments are parameter names
    for (size_t i = 1; i < macro_command.arguments.size(); ++i) {
        if (!macro_command.arguments[i].parts.empty() &&
            std::holds_alternative<std::string>(macro_command.arguments[i].parts[0])) {
            macro_block.parameters.push_back(std::get<std::string>(macro_command.arguments[i].parts[0]));
        } else {
            return std::unexpected(ParseError{macro_command.row, macro_command.col, macro_command.offset, macro_command.length, "macro() parameters must be simple identifiers"});
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
        return std::unexpected(ParseError{row_, col_, pos_, 8, "Expected 'endmacro'"});
    }

    return macro_block;
}

std::expected<ForeachBlock, ParseError> Parser::parse_foreach_block(const CommandInvocation& foreach_command) {
    ForeachBlock foreach_block;
    foreach_block.row = foreach_command.row;
    foreach_block.col = foreach_command.col;
    foreach_block.offset = foreach_command.offset;

    if (foreach_command.arguments.empty()) {
        return std::unexpected(ParseError{foreach_command.row, foreach_command.col, foreach_command.offset, foreach_command.length, "foreach() requires a loop variable"});
    }

    // Extract loop variable (must be simple identifier)
    if (!foreach_command.arguments[0].parts.empty() &&
        std::holds_alternative<std::string>(foreach_command.arguments[0].parts[0])) {
        foreach_block.loop_var = std::get<std::string>(foreach_command.arguments[0].parts[0]);
    } else {
        return std::unexpected(ParseError{foreach_command.row, foreach_command.col, foreach_command.offset, foreach_command.length, "foreach() loop variable must be a simple identifier"});
    }

    if (foreach_command.arguments.size() == 1) {
        return std::unexpected(ParseError{foreach_command.row, foreach_command.col, foreach_command.offset, foreach_command.length, "foreach() requires items to iterate over"});
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
            return std::unexpected(ParseError{foreach_command.row, foreach_command.col, foreach_command.offset, foreach_command.length, "foreach(RANGE) requires at least a stop value"});
        }
        if (foreach_command.arguments.size() > 5) {
            return std::unexpected(ParseError{foreach_command.row, foreach_command.col, foreach_command.offset, foreach_command.length, "foreach(RANGE) accepts at most 3 range values (start, stop, step)"});
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
                return std::unexpected(ParseError{foreach_command.row, foreach_command.col, foreach_command.offset, foreach_command.length, "foreach(IN) expected LISTS or ITEMS keyword"});
            }
        }

        if (in_params.lists.empty() && in_params.items.empty()) {
            return std::unexpected(ParseError{foreach_command.row, foreach_command.col, foreach_command.offset, foreach_command.length, "foreach(IN) requires at least one LISTS or ITEMS argument"});
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
        return std::unexpected(ParseError{row_, col_, pos_, 10, "Expected 'endforeach'"});
    }

    foreach_block.length = pos_ - foreach_block.offset;
    return foreach_block;
}

std::expected<WhileBlock, ParseError> Parser::parse_while_block(const CommandInvocation& while_command) {
    WhileBlock while_block;
    while_block.row = while_command.row;
    while_block.col = while_command.col;
    while_block.offset = while_command.offset;
    while_block.condition = while_command.arguments;

    // Parse the body
    auto body_or_error = parse_block({"endwhile"});
    if (!body_or_error) {
        return std::unexpected(body_or_error.error());
    }
    while_block.body = std::move(body_or_error.value());

    // Consume endwhile
    auto endwhile_command_or_error = parse_command_invocation();
    if (!endwhile_command_or_error) {
        return std::unexpected(endwhile_command_or_error.error());
    }

    std::string identifier_lower = endwhile_command_or_error.value().identifier;
    std::transform(identifier_lower.begin(), identifier_lower.end(), identifier_lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (identifier_lower != "endwhile") {
        return std::unexpected(ParseError{row_, col_, pos_, 8, "Expected 'endwhile'"});
    }

    while_block.length = pos_ - while_block.offset;
    return while_block;
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
    consume_whitespace();

    // Save location at start of command
    size_t cmd_row = row_;
    size_t cmd_col = col_;
    size_t cmd_offset = pos_;

    // Parse identifier
    size_t start_pos = pos_;
    while (pos_ < content_.length() && (std::isalnum(content_[pos_]) || content_[pos_] == '_')) {
        pos_++;
        col_++;
    }
    std::string identifier(content_.substr(start_pos, pos_ - start_pos));

    if (identifier.empty()) {
        return std::unexpected(ParseError{row_, col_, pos_, 0, "Expected an identifier"});
    }

    consume_whitespace();

    // Parse opening parenthesis
    if (pos_ >= content_.length() || content_[pos_] != '(') {
        return std::unexpected(ParseError{row_, col_, pos_, 1, "Expected '(' after identifier"});
    }
    pos_++;
    col_++;

    CommandInvocation cmd_inv{std::move(identifier), {}, cmd_row, cmd_col, cmd_offset, 0};

    // Parse arguments
    int nesting = 0;
    while (pos_ < content_.length()) {
        consume_whitespace();
        if (pos_ < content_.length() && content_[pos_] == ')' && nesting == 0) {
            break;
        }

        auto arg_or_error = parse_argument();
        if (arg_or_error) {
            const auto& arg = arg_or_error.value();
            if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0])) {
                std::string s = std::get<std::string>(arg.parts[0]);
                if (s == "(") nesting++;
                else if (s == ")") nesting--;
            }
            cmd_inv.arguments.push_back(std::move(arg_or_error.value()));
        } else {
            return std::unexpected(arg_or_error.error());
        }
    }

    // Parse closing parenthesis
    if (pos_ >= content_.length() || content_[pos_] != ')') {
        return std::unexpected(ParseError{row_, col_, pos_, 1, "Expected ')' to close argument list"});
    }
    pos_++;
    col_++;

    cmd_inv.length = pos_ - cmd_offset;

    return cmd_inv;
}

std::expected<Argument, ParseError> Parser::parse_argument() {
    if (pos_ >= content_.length()) {
        return std::unexpected(ParseError{row_, col_, pos_, 0, "Unexpected end of input"});
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
        return Argument{{std::move(value_or_error.value())}, true};
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

    // Handle '(' or ')' as single-character unquoted arguments.
    // In CMake, these are separate tokens in if() conditions.
    if (pos_ < content_.length() && (content_[pos_] == '(' || content_[pos_] == ')')) {
        parts.emplace_back(std::string(1, content_[pos_]));
        pos_++;
        col_++;
        return parts;
    }

    while (pos_ < content_.length() && !std::isspace(content_[pos_]) &&
           content_[pos_] != '(' && content_[pos_] != ')' &&
           content_[pos_] != '#' && content_[pos_] != '"' && content_[pos_] != '[') {
        if (content_[pos_] == '\\' && pos_ + 1 < content_.length()) {
            // Handle escape sequence - skip the backslash and include the next character
            pos_++;
            col_++;
            pos_++;
            if (content_[pos_ - 1] == '\n') {
                row_++;
                col_ = 1;
            } else {
                col_++;
            }
            continue;
        }
        if (content_[pos_] == '$' && pos_ + 1 < content_.length() &&
            (content_[pos_ + 1] == '{' || std::isalpha(content_[pos_ + 1]) || content_[pos_ + 1] == '_')) {
            if (start_pos < pos_) {
                parts.emplace_back(std::string(content_.substr(start_pos, pos_ - start_pos)));
            }

            pos_++; // Move past '$'
            col_++;

            auto var_ref = parse_variable_reference(false);
            if (!var_ref) {
                return std::unexpected(var_ref.error());
            }
            parts.emplace_back(std::move(var_ref.value()));
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
        return std::unexpected(ParseError{row_, col_, pos_, 0, "Expected an unquoted argument"});
    }

    return parts;
}

std::expected<std::vector<ArgumentPart>, ParseError> Parser::parse_quoted_argument_value() {
    size_t quote_start = pos_;
    pos_++; // Consume opening quote
    col_++;

    std::vector<ArgumentPart> parts;
    std::string current_literal;
    bool escaped = false;

    while (pos_ < content_.length()) {
        char current = content_[pos_];
        if (escaped) {
            // Interpret CMake escape sequences
            switch (current) {
                case 'n':  current_literal += '\n'; break;
                case 't':  current_literal += '\t'; break;
                case 'r':  current_literal += '\r'; break;
                case '\\': current_literal += '\\'; break;
                case '"':  current_literal += '"'; break;
                case ')':  current_literal += ')'; break;
                case '(':  current_literal += '('; break;
                case '#':  current_literal += '#'; break;
                case ' ':  current_literal += ' '; break;
                case ';':  current_literal += ';'; break;
                default:   current_literal += current; break; // Unknown escape, keep as-is
            }
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
        if (current == '$' && pos_ + 1 < content_.length() &&
            (content_[pos_ + 1] == '{' || std::isalpha(content_[pos_ + 1]) || content_[pos_ + 1] == '_')) {
            if (!current_literal.empty()) {
                parts.emplace_back(current_literal);
                current_literal.clear();
            }

            pos_++; // Move past '$'
            col_++;

            auto var_ref = parse_variable_reference(true);
            if (!var_ref) {
                return std::unexpected(var_ref.error());
            }
            parts.emplace_back(std::move(var_ref.value()));
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

    return std::unexpected(ParseError{row_, col_, quote_start, pos_ - quote_start, "Unterminated quoted argument"});
}

std::expected<std::string, ParseError> Parser::parse_bracket_argument() {
    size_t start_pos_outer = pos_;
    pos_++; // Consume opening bracket
    col_++;

    size_t equals_count = 0;
    while (pos_ < content_.length() && content_[pos_] == '=') {
        equals_count++;
        pos_++;
        col_++;
    }

    if (pos_ >= content_.length() || content_[pos_] != '[') {
        return std::unexpected(ParseError{row_, col_, start_pos_outer, pos_ - start_pos_outer, "Invalid bracket argument"});
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

    return std::unexpected(ParseError{row_, col_, start_pos_outer, pos_ - start_pos_outer, "Unterminated bracket argument"});
}

std::expected<VariableReference, ParseError> Parser::parse_variable_reference(bool inside_quotes) {
    // We're positioned right after '$'
    // For now, we only support ${...}, not $ENV{...} or $CACHE{...}

    std::string namespace_prefix;
    size_t ref_start_pos = pos_ - 1; // Position of '$'

    // Check what comes after '$'
    if (pos_ >= content_.length()) {
        return std::unexpected(ParseError{row_, col_, pos_, 0, "Unexpected end after '$'"});
    }

    // Detect namespace prefix: $ENV{...}, $CACHE{...}, or ${...}
    if (content_[pos_] == '{') {
        // Regular variable: ${...}
        namespace_prefix = "";
    } else if (std::isalpha(content_[pos_]) || content_[pos_] == '_') {
        // Parse identifier (namespace prefix)
        size_t prefix_start = pos_;
        while (pos_ < content_.length() &&
               (std::isalnum(content_[pos_]) || content_[pos_] == '_')) {
            pos_++;
            col_++;
        }

        std::string prefix(content_.substr(prefix_start, pos_ - prefix_start));

        // Normalize to uppercase (CMake is case-insensitive)
        std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                      [](unsigned char c){ return std::toupper(c); });

        // Expect '{'
        if (pos_ >= content_.length() || content_[pos_] != '{') {
            return std::unexpected(ParseError{row_, col_, pos_, 1,
                "Expected '{' after '$" + prefix + "'"});
        }

        namespace_prefix = prefix;
    } else {
        return std::unexpected(ParseError{row_, col_, pos_, 1,
            "Expected '{' or identifier after '$'"});
    }

    // Consume '{'
    pos_++;
    col_++;

    // Parse the content inside ${...} as argument parts
    std::vector<ArgumentPart> name_parts;
    std::string current_literal;

    while (pos_ < content_.length()) {
        char c = content_[pos_];

        // Check for closing brace
        if (c == '}') {
            // Save any accumulated literal
            if (!current_literal.empty()) {
                name_parts.emplace_back(std::move(current_literal));
                current_literal.clear();
            }
            pos_++; // Consume '}'
            col_++;
            return VariableReference{namespace_prefix, std::move(name_parts)};
        }

        // Check for nested variable reference
        if (c == '$' && pos_ + 1 < content_.length() && content_[pos_ + 1] == '{') {
            // Save any accumulated literal
            if (!current_literal.empty()) {
                name_parts.emplace_back(std::move(current_literal));
                current_literal.clear();
            }

            pos_++; // Move past '$'
            col_++;

            // Recursively parse nested variable reference
            auto nested_ref = parse_variable_reference(inside_quotes);
            if (!nested_ref) {
                return std::unexpected(nested_ref.error());
            }
            name_parts.emplace_back(std::move(nested_ref.value()));
            continue;
        }

        // Handle quotes inside variable references (for quoted argument context)
        if (inside_quotes && c == '"') {
            return std::unexpected(ParseError{row_, col_, pos_, 1, "Unexpected '\"' inside variable reference"});
        }

        // Regular character - add to literal
        current_literal += c;
        pos_++;
        if (c == '\n') {
            row_++;
            col_ = 1;
        } else {
            col_++;
        }
    }

    return std::unexpected(ParseError{row_, col_, ref_start_pos, pos_ - ref_start_pos, "Unterminated variable reference"});
}

} // namespace dmake
