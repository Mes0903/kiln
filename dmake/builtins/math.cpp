#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

namespace dmake {

class MathEvaluator {
public:
    explicit MathEvaluator(std::string expr) : expr_(std::move(expr)) {
        tokenize();
    }

    int64_t evaluate() {
        if (tokens_.empty()) return 0;
        token_pos_ = 0;
        int64_t result = parse_bitwise_or();
        if (token_pos_ < tokens_.size()) {
            throw std::runtime_error("Unexpected token: " + tokens_[token_pos_]);
        }
        return result;
    }

private:
    void tokenize() {
        size_t i = 0;
        while (i < expr_.size()) {
            char c = expr_[i];
            if (std::isspace(static_cast<unsigned char>(c))) {
                i++;
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c))) {
                size_t start = i;
                if (i + 1 < expr_.size() && (expr_[i+1] == 'x' || expr_[i+1] == 'X')) {
                    i += 2;
                    while (i < expr_.size() && std::isxdigit(static_cast<unsigned char>(expr_[i]))) {
                        i++;
                    }
                } else {
                    while (i < expr_.size() && std::isdigit(static_cast<unsigned char>(expr_[i]))) {
                        i++;
                    }
                }
                tokens_.push_back(expr_.substr(start, i - start));
            } else if (c == '(' || c == ')' || c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '|' || c == '&' || c == '^' || c == '~') {
                tokens_.push_back(std::string(1, c));
                i++;
            } else if (c == '<' || c == '>') {
                if (i + 1 < expr_.size() && expr_[i+1] == c) {
                    tokens_.push_back(expr_.substr(i, 2));
                    i += 2;
                } else {
                    throw std::runtime_error(std::string("Invalid operator: ") + c);
                }
            } else {
                throw std::runtime_error(std::string("Invalid character in expression '" + expr_ + "': ") + c);
            }
        }
    }

    int64_t parse_bitwise_or() {
        int64_t left = parse_bitwise_xor();
        while (peek() == "|") {
            consume();
            left |= parse_bitwise_xor();
        }
        return left;
    }

    int64_t parse_bitwise_xor() {
        int64_t left = parse_bitwise_and();
        while (peek() == "^") {
            consume();
            left ^= parse_bitwise_and();
        }
        return left;
    }

    int64_t parse_bitwise_and() {
        int64_t left = parse_shift();
        while (peek() == "&") {
            consume();
            left &= parse_shift();
        }
        return left;
    }

    int64_t parse_shift() {
        int64_t left = parse_additive();
        while (peek() == "<<" || peek() == ">>") {
            std::string op = consume();
            int64_t right = parse_additive();
            if (op == "<<") left <<= right;
            else left >>= right;
        }
        return left;
    }

    int64_t parse_additive() {
        int64_t left = parse_multiplicative();
        while (peek() == "+" || peek() == "-") {
            std::string op = consume();
            int64_t right = parse_multiplicative();
            if (op == "+") left += right;
            else left -= right;
        }
        return left;
    }

    int64_t parse_multiplicative() {
        int64_t left = parse_unary();
        while (peek() == "*" || peek() == "/" || peek() == "%") {
            std::string op = consume();
            int64_t right = parse_unary();
            if (op == "*") left *= right;
            else if (op == "/") {
                if (right == 0) throw std::runtime_error("Division by zero");
                left /= right;
            } else if (op == "%") {
                if (right == 0) throw std::runtime_error("Modulo by zero");
                left %= right;
            }
        }
        return left;
    }

    int64_t parse_unary() {
        if (peek() == "+") {
            consume();
            return parse_unary();
        } else if (peek() == "-") {
            consume();
            return -parse_unary();
        } else if (peek() == "~") {
            consume();
            return ~parse_unary();
        }
        return parse_primary();
    }

    int64_t parse_primary() {
        std::string t = consume();
        if (t == "(") {
            int64_t result = parse_bitwise_or();
            if (consume() != ")") throw std::runtime_error("Expected ')'");
            return result;
        }

        try {
            if (t.size() > 2 && (t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))) {
                return std::stoll(t, nullptr, 16);
            }
            return std::stoll(t, nullptr, 10);
        } catch (...) {
            throw std::runtime_error("Invalid number: " + t);
        }
    }

    std::string peek() {
        if (token_pos_ < tokens_.size()) return tokens_[token_pos_];
        return "";
    }

    std::string consume() {
        if (token_pos_ < tokens_.size()) return tokens_[token_pos_++];
        throw std::runtime_error("Unexpected end of expression");
    }

    std::string expr_;
    std::vector<std::string> tokens_;
    size_t token_pos_ = 0;
};

void register_math_builtins(Interpreter& interp) {
    interp.add_builtin("math", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("math");
        std::string mode;
        std::string var_name;
        std::string expression;
        std::string output_format;

        parser.positional(mode, "mode (EXPR)");
        parser.positional(var_name, "variable name");
        parser.positional(expression, "expression");
        parser.value("OUTPUT_FORMAT", output_format);

        PARSE_OR_RETURN(parser, interp, args);

        if (output_format.empty()) {
            output_format = "DECIMAL";
        }

        if (mode != "EXPR") {
            interp.set_fatal_error("math() only supports EXPR mode");
            return;
        }

        try {
            MathEvaluator eval(expression);
            int64_t result = eval.evaluate();

            std::string result_str;
            if (output_format == "HEXADECIMAL") {
                std::stringstream ss;
                ss << "0x" << std::hex << result;
                result_str = ss.str();
            } else if (output_format == "DECIMAL") {
                result_str = std::to_string(result);
            } else {
                interp.set_fatal_error("Invalid OUTPUT_FORMAT: " + output_format + ". Must be DECIMAL or HEXADECIMAL.");
                return;
            }

            interp.set_variable(var_name, result_str);
        } catch (const std::exception& e) {
            interp.set_fatal_error(std::string("math(EXPR) error: ") + e.what());
        }
    });
}

} // namespace dmake
