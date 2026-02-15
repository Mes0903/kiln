#include "registry.hpp"
#include "../interperter.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <stdexcept>

namespace dmake {

// Single-pass recursive descent math evaluator operating directly on string_view.
// No intermediate tokenization step.
class MathEvaluator {
public:
    explicit MathEvaluator(std::string_view expr) : expr_(expr), pos_(0) {}

    int64_t evaluate() {
        skip_whitespace();
        if (pos_ >= expr_.size()) return 0;
        int64_t result = parse_bitwise_or();
        skip_whitespace();
        if (pos_ < expr_.size()) {
            throw std::runtime_error(std::string("Unexpected character in expression: '") + expr_[pos_] + "'");
        }
        return result;
    }

private:
    std::string_view expr_;
    size_t pos_;

    void skip_whitespace() {
        while (pos_ < expr_.size() && expr_[pos_] == ' ') ++pos_;
    }

    char peek() const {
        return pos_ < expr_.size() ? expr_[pos_] : '\0';
    }

    int64_t parse_bitwise_or() {
        int64_t left = parse_bitwise_xor();
        while (peek() == '|') {
            ++pos_;
            left |= parse_bitwise_xor();
        }
        return left;
    }

    int64_t parse_bitwise_xor() {
        int64_t left = parse_bitwise_and();
        while (peek() == '^') {
            ++pos_;
            left ^= parse_bitwise_and();
        }
        return left;
    }

    int64_t parse_bitwise_and() {
        int64_t left = parse_shift();
        while (peek() == '&') {
            ++pos_;
            left &= parse_shift();
        }
        return left;
    }

    int64_t parse_shift() {
        int64_t left = parse_additive();
        for (;;) {
            skip_whitespace();
            if (pos_ + 1 < expr_.size() && expr_[pos_] == '<' && expr_[pos_ + 1] == '<') {
                pos_ += 2;
                left <<= parse_additive();
            } else if (pos_ + 1 < expr_.size() && expr_[pos_] == '>' && expr_[pos_ + 1] == '>') {
                pos_ += 2;
                left >>= parse_additive();
            } else {
                break;
            }
        }
        return left;
    }

    int64_t parse_additive() {
        int64_t left = parse_multiplicative();
        for (;;) {
            skip_whitespace();
            char c = peek();
            if (c == '+') { ++pos_; left += parse_multiplicative(); }
            else if (c == '-') { ++pos_; left -= parse_multiplicative(); }
            else break;
        }
        return left;
    }

    int64_t parse_multiplicative() {
        int64_t left = parse_unary();
        for (;;) {
            skip_whitespace();
            char c = peek();
            if (c == '*') {
                ++pos_;
                left *= parse_unary();
            } else if (c == '/') {
                ++pos_;
                int64_t right = parse_unary();
                if (right == 0) throw std::runtime_error("Division by zero");
                left /= right;
            } else if (c == '%') {
                ++pos_;
                int64_t right = parse_unary();
                if (right == 0) throw std::runtime_error("Modulo by zero");
                left %= right;
            } else {
                break;
            }
        }
        return left;
    }

    int64_t parse_unary() {
        skip_whitespace();
        char c = peek();
        if (c == '+') { ++pos_; return parse_unary(); }
        if (c == '-') { ++pos_; return -parse_unary(); }
        if (c == '~') { ++pos_; return ~parse_unary(); }
        return parse_primary();
    }

    int64_t parse_primary() {
        skip_whitespace();
        char c = peek();

        if (c == '(') {
            ++pos_;
            int64_t result = parse_bitwise_or();
            skip_whitespace();
            if (peek() != ')') throw std::runtime_error("Expected ')'");
            ++pos_;
            return result;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            return parse_number();
        }

        if (c == '\0') {
            throw std::runtime_error("Unexpected end of expression");
        }
        throw std::runtime_error(std::string("Invalid character in expression: '") + c + "'");
    }

    int64_t parse_number() {
        size_t start = pos_;

        // Check for hex prefix
        if (pos_ + 1 < expr_.size() && expr_[pos_] == '0' &&
            (expr_[pos_ + 1] == 'x' || expr_[pos_ + 1] == 'X')) {
            pos_ += 2;
            while (pos_ < expr_.size() && std::isxdigit(static_cast<unsigned char>(expr_[pos_]))) {
                ++pos_;
            }
            std::string num_str(expr_.substr(start, pos_ - start));
            try {
                return std::stoll(num_str, nullptr, 16);
            } catch (...) {
                throw std::runtime_error("Invalid hex number: " + num_str);
            }
        }

        // Decimal
        while (pos_ < expr_.size() && std::isdigit(static_cast<unsigned char>(expr_[pos_]))) {
            ++pos_;
        }
        std::string num_str(expr_.substr(start, pos_ - start));
        try {
            return std::stoll(num_str, nullptr, 10);
        } catch (...) {
            throw std::runtime_error("Invalid number: " + num_str);
        }
    }
};

void register_math_builtins(Interpreter& interp) {
    interp.add_builtin("math", [](Interpreter& interp, const std::vector<std::string>& args) {
        // math(EXPR <variable> "<expression>" [OUTPUT_FORMAT <DECIMAL|HEXADECIMAL>])
        if (args.size() < 3) {
            interp.set_fatal_error("math() requires at least 3 arguments: math(EXPR <variable> <expression>)");
            return;
        }

        if (args[0] != "EXPR") {
            interp.set_fatal_error("math() only supports EXPR mode, got: " + args[0]);
            return;
        }

        const std::string& var_name = args[1];
        const std::string& expression = args[2];

        // Parse optional OUTPUT_FORMAT
        std::string_view output_format = "DECIMAL";
        if (args.size() >= 5) {
            if (args[3] == "OUTPUT_FORMAT") {
                output_format = args[4];
            } else {
                interp.set_fatal_error("math(EXPR): unexpected argument '" + args[3] + "', expected OUTPUT_FORMAT");
                return;
            }
            if (args.size() > 5) {
                interp.set_fatal_error("math(EXPR): too many arguments");
                return;
            }
        } else if (args.size() == 4) {
            interp.set_fatal_error("math(EXPR): OUTPUT_FORMAT requires a value (DECIMAL or HEXADECIMAL)");
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
                interp.set_fatal_error("Invalid OUTPUT_FORMAT: " + std::string(output_format) + ". Must be DECIMAL or HEXADECIMAL.");
                return;
            }

            interp.set_variable(var_name, result_str);
        } catch (const std::exception& e) {
            interp.set_fatal_error(std::string("math(EXPR) error: ") + e.what());
        }
    });
}

} // namespace dmake
