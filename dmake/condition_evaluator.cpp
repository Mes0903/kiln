#include "condition_evaluator.hpp"
#include "interperter.hpp"
#include "regex.hpp"
#include "clock_cache.hpp"
#include "CMakeArray.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>

namespace dmake {

namespace {

// Compare version strings component-wise (CMake behavior)
// Returns: -1 if a < b, 0 if a == b, 1 if a > b
// CMake behavior: split by '.', parse each component as integer (non-numeric suffix stripped),
// missing components treated as 0
int compare_versions(const std::string& a, const std::string& b) {
    std::vector<int> parts_a, parts_b;

    // Parse version a - split by '.' and parse each component
    std::istringstream iss_a(a);
    std::string component;
    while (std::getline(iss_a, component, '.')) {
        // CMake strips non-numeric suffixes: "1a" -> 1, "1-suffix" -> 1
        try {
            parts_a.push_back(std::stoi(component));
        } catch (...) {
            parts_a.push_back(0);
        }
    }

    // Parse version b
    std::istringstream iss_b(b);
    while (std::getline(iss_b, component, '.')) {
        try {
            parts_b.push_back(std::stoi(component));
        } catch (...) {
            parts_b.push_back(0);
        }
    }

    // Pad shorter vector with zeros (missing components = 0)
    size_t max_len = std::max(parts_a.size(), parts_b.size());
    parts_a.resize(max_len, 0);
    parts_b.resize(max_len, 0);

    // Compare component by component
    for (size_t i = 0; i < max_len; ++i) {
        if (parts_a[i] < parts_b[i]) return -1;
        if (parts_a[i] > parts_b[i]) return 1;
    }
    return 0;
}

// Set of keywords that should not be dereferenced as variables
// All keywords are in uppercase; comparisons are done case-insensitively
// We use a sorted static array and linear search because it is more branch-prediction
// friendly for small sets of strings compared to std::set or binary search.
constexpr std::array keywords = {
    "(", ")", "AND", "COMMAND", "DEFINED", "EQUAL", "EXISTS", "GREATER",
    "GREATER_EQUAL", "IN_LIST", "IS_ABSOLUTE", "IS_DIRECTORY", "IS_NEWER_THAN",
    "IS_SYMLINK", "LESS", "LESS_EQUAL", "MATCHES", "NOT", "NOT_EQUAL", "OR",
    "POLICY", "STREQUAL", "STRGREATER", "STRGREATER_EQUAL", "STRLESS",
    "STRLESS_EQUAL", "TARGET", "TEST", "VERSION_EQUAL", "VERSION_GREATER",
    "VERSION_GREATER_EQUAL", "VERSION_LESS", "VERSION_LESS_EQUAL"
};

// Boolean constants that have fixed truthiness values (case-insensitive)
constexpr std::array boolean_constants = {
    "FALSE", "IGNORE", "N", "NO", "NOTFOUND", "OFF", "ON", "TRUE", "Y", "YES"
};

// Case-insensitive check against boolean_constants without allocating a to_upper copy.
bool is_boolean_constant_ci(std::string_view token) {
    for (const auto& bc : boolean_constants) {
        std::string_view b(bc);
        if (token.size() != b.size()) continue;
        bool match = true;
        for (size_t i = 0; i < token.size(); ++i) {
            if (static_cast<char>(std::toupper(static_cast<unsigned char>(token[i]))) != b[i]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Set of binary operator keywords
constexpr std::array binary_operators = {
    "EQUAL", "GREATER", "GREATER_EQUAL", "IN_LIST", "IS_NEWER_THAN",
    "LESS", "LESS_EQUAL", "MATCHES", "NOT_EQUAL", "STREQUAL",
    "STRGREATER", "STRGREATER_EQUAL", "STRLESS", "STRLESS_EQUAL",
    "VERSION_EQUAL", "VERSION_GREATER", "VERSION_GREATER_EQUAL",
    "VERSION_LESS", "VERSION_LESS_EQUAL"
};

// Recursive descent parser for CMake if() conditions.
// Replaces std::function<bool()> lambdas with direct member function calls.
class ConditionParser {
public:
    ConditionParser(Interpreter& interp, const std::vector<Argument>& condition)
        : interp_(interp), condition_(condition) {}

    bool parse() {
        return parse_or();
    }

    const std::string& error() const { return error_msg_; }
    size_t pos() const { return pos_; }

private:
    Interpreter& interp_;
    const std::vector<Argument>& condition_;
    size_t pos_ = 0;
    std::string error_msg_;
    std::string eval_buffer_;  // Reusable buffer for get_token_string slow path

    // Returns a reference to the token string. Fast path returns reference into
    // the AST; slow path evaluates into eval_buffer_ and returns reference to that.
    // WARNING: reference is invalidated by the next call to get_token_string.
    const std::string& get_token_string(const Argument& arg) {
        if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0])) {
            return std::get<std::string>(arg.parts[0]);
        }
        eval_buffer_ = interp_.evaluate_argument(arg);
        return eval_buffer_;
    }

    // Return the literal text of the current token without expanding or allocating.
    // Returns empty string_view if pos_ is out of range or the token is quoted/composite.
    std::string_view peek_bare() const {
        if (pos_ >= condition_.size()) return {};
        const auto& arg = condition_[pos_];
        if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0]))
            return std::get<std::string>(arg.parts[0]);
        return {};
    }

    static bool is_numeric_constant(const std::string& s) {
        if (s.empty()) return false;
        size_t start = 0;
        if (s[0] == '-' || s[0] == '+') start = 1;
        if (start >= s.length()) return false;

        // Check if rest of string is numeric
        for (size_t i = start; i < s.length(); ++i) {
            if (!std::isdigit(s[i]) && s[i] != '.') return false;
        }
        return true;
    }

    // Helper to evaluate an argument, dereferencing variables unless it's a keyword or constant
    // CMake behavior (pre-CMP0054):
    // - Keywords and quoted strings are returned as-is
    // - Numeric constants are returned as-is
    // - Everything else is dereferenced as a variable:
    //   - If defined, return the variable's value (even if empty)
    //   - If undefined, return empty string
    std::string evaluate_token(const Argument& arg) {
        const std::string& token = get_token_string(arg);

        // Don't dereference keywords (operators), boolean constants, or quoted strings.
        // CMake keywords are case-sensitive (must be uppercase: NOT, AND, TARGET, etc.)
        // Boolean constants are case-insensitive (OFF, off, Off all work)
        if (arg.quoted ||
            std::find(keywords.begin(), keywords.end(), token) != keywords.end() ||
            is_boolean_constant_ci(token)) {
            return token;
        }

        // Don't dereference numeric constants (0, 1, -5, 3.14, etc.)
        if (is_numeric_constant(token)) {
            return token;
        }

        // If the argument is ONLY a single variable reference (e.g., ${VAR}), the expansion
        // already happened in get_token_string() and we have the value. Don't dereference again.
        // But if the argument contains mixed parts (e.g., _${PREFIX}_SUFFIX), the expansion
        // gives us a variable NAME that should be dereferenced to get its value.
        if (arg.parts.size() == 1 && std::holds_alternative<VariableReference>(arg.parts[0])) {
            return token;  // Already have the value from the variable reference
        }

        // Dereference as variable (CMake if() semantics)
        // If defined: return value (even if empty)
        // If undefined: return the literal token string
        // This matches CMake behavior where bare words that don't name a defined
        // variable are kept as literal strings (e.g., "AIX" stays "AIX" in MATCHES)
        auto opt = interp_.get_optional_variable(token);
        return opt.has_value() ? *opt : std::string(token);
    }

    static bool is_binary_operator(const std::string& token) {
        // CMake keywords are case-sensitive (must be uppercase)
        return std::find(binary_operators.begin(), binary_operators.end(), token) != binary_operators.end();
    }

    // AND/OR have lowest precedence and evaluate left-to-right
    // NOTE: CMake does NOT short-circuit - both sides are always evaluated
    bool parse_or() {
        bool left = parse_and();

        while (pos_ < condition_.size() && error_msg_.empty()) {
            if (peek_bare() == "OR") {
                pos_++;
                if (pos_ >= condition_.size()) {
                    error_msg_ = "OR operator requires a right operand";
                    return false;
                }
                bool right = parse_and();  // Always evaluate right side (no short-circuit)
                left = left || right;
            } else {
                break;
            }
        }
        return left;
    }

    bool parse_and() {
        bool left = parse_not();

        while (pos_ < condition_.size() && error_msg_.empty()) {
            if (peek_bare() == "AND") {
                pos_++;
                if (pos_ >= condition_.size()) {
                    error_msg_ = "AND operator requires a right operand";
                    return false;
                }
                bool right = parse_not();  // Always evaluate right side (no short-circuit)
                left = left && right;
            } else {
                break;
            }
        }
        return left;
    }

    // NOT has higher precedence than AND/OR but lower than comparisons
    bool parse_not() {
        if (pos_ >= condition_.size()) {
            error_msg_ = "Unexpected end of condition";
            return false;
        }

        if (peek_bare() == "NOT") {
            // CMake compatibility: If NOT is followed by a binary operator,
            // treat NOT as a value (left operand), not as a negation operator.
            // Example: if(NOT STREQUAL "X") -> "NOT" STREQUAL "X" = false
            if (pos_ + 1 < condition_.size()) {
                const std::string& next_token = get_token_string(condition_[pos_ + 1]);
                if (is_binary_operator(next_token)) {
                    // NOT is actually a left operand here, not a negation
                    return parse_comparison();
                }
                if (next_token != "AND" && next_token != "OR") {
                    // Valid operand exists - NOT is an operator
                    pos_++;
                    return !parse_not();  // Right-associative
                }
            }
            // No valid operand - fall through to treat "NOT" as a primary value
        }
        return parse_comparison();
    }

    // Binary comparison operators (EQUAL, LESS, STREQUAL, etc.)
    bool parse_comparison() {
        if (pos_ >= condition_.size()) return false;

        // CMake special case: MATCHES without left operand returns false
        // Example: if(${UNDEFINED} MATCHES "pat") where UNDEFINED expands to nothing
        // becomes if(MATCHES "pat") which CMake evaluates as false
        if (peek_bare() == "MATCHES" && pos_ + 1 < condition_.size()) {
            // Consume MATCHES and the pattern, return false
            pos_ += 2;
            return false;
        }

        // CMake error case: other binary operators without left operand
        // Example: if(STREQUAL "") -> error "Unknown arguments specified"
        // Note: This does NOT apply when NOT precedes the operator (handled in parse_not)
        if (!condition_[pos_].quoted) {
            const std::string& current_token = get_token_string(condition_[pos_]);
            if (is_binary_operator(current_token) && pos_ + 1 < condition_.size()) {
                error_msg_ = "if given arguments: \"" + current_token + "\" - missing left operand";
                return false;
            }
        }

        // Save start position in case this isn't a comparison
        size_t start_pos = pos_;

        // Try to parse left operand
        bool unary_result = parse_unary_or_primary();

        // Check if next token is a binary operator
        if (pos_ >= condition_.size()) {
            return unary_result;
        }

        // Copy needed: op is used in error messages after get_token_string may be called again
        std::string op = get_token_string(condition_[pos_]);

        // Numeric comparisons
        if (op == "EQUAL" || op == "LESS" || op == "GREATER" ||
            op == "LESS_EQUAL" || op == "GREATER_EQUAL" || op == "NOT_EQUAL") {
            pos_++;
            if (pos_ >= condition_.size()) {
                error_msg_ = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition_[start_pos]);
            std::string right = evaluate_token(condition_[pos_++]);

            try {
                double left_num = std::stod(left);
                double right_num = std::stod(right);

                if (op == "EQUAL") return left_num == right_num;
                if (op == "NOT_EQUAL") return left_num != right_num;
                if (op == "LESS") return left_num < right_num;
                if (op == "GREATER") return left_num > right_num;
                if (op == "LESS_EQUAL") return left_num <= right_num;
                if (op == "GREATER_EQUAL") return left_num >= right_num;
            } catch (...) {
                // Fallback to string comparison for EQUAL/NOT_EQUAL
                if (op == "EQUAL") return left == right;
                if (op == "NOT_EQUAL") return left != right;
                return false;
            }
        }
        // String comparisons
        else if (op == "STREQUAL" || op == "STRLESS" || op == "STRGREATER" ||
                 op == "STRLESS_EQUAL" || op == "STRGREATER_EQUAL") {
            pos_++;
            if (pos_ >= condition_.size()) {
                error_msg_ = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition_[start_pos]);
            std::string right = evaluate_token(condition_[pos_++]);

            if (op == "STREQUAL") return left == right;
            if (op == "STRLESS") return left < right;
            if (op == "STRGREATER") return left > right;
            if (op == "STRLESS_EQUAL") return left <= right;
            if (op == "STRGREATER_EQUAL") return left >= right;
        }
        // Version comparisons - component-wise numeric comparison (CMake behavior)
        else if (op.starts_with("VERSION_")) {
            pos_++;
            if (pos_ >= condition_.size()) {
                error_msg_ = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition_[start_pos]);
            std::string right = evaluate_token(condition_[pos_++]);

            int cmp = compare_versions(left, right);

            if (op == "VERSION_EQUAL") return cmp == 0;
            if (op == "VERSION_LESS") return cmp < 0;
            if (op == "VERSION_GREATER") return cmp > 0;
            if (op == "VERSION_LESS_EQUAL") return cmp <= 0;
            if (op == "VERSION_GREATER_EQUAL") return cmp >= 0;
        }
        // regex
        else if(op == "MATCHES") {
            pos_++; // Consume operator
            if (pos_ >= condition_.size()) {
                error_msg_ = "MATCHES operator requires a right operand";
                return false;
            }
            std::string pattern = evaluate_token(condition_[pos_++]);
            thread_local ClockCache<std::string, Regex> cache(8, [](const std::string& p) {
                return Regex::from_cmake_regex(p);
            });
            auto re = cache.get(pattern);
            if (!re) {
                error_msg_ = "MATCHES: invalid regex: " + re.error();
                return false;
            }
            std::string left = evaluate_token(condition_[start_pos]);
            std::vector<std::string> captures;
            bool result = (*re)->search(left, captures);

            if (result) {
                interp_.set_variable("CMAKE_MATCH_COUNT", std::to_string(captures.size() - 1));
                for (size_t i = 0; i < captures.size() && i < 10; ++i) {
                    interp_.set_variable("CMAKE_MATCH_" + std::to_string(i), captures[i]);
                }
                for (size_t i = captures.size(); i < 10; ++i) {
                    interp_.set_variable("CMAKE_MATCH_" + std::to_string(i), "");
                }
            } else {
                 interp_.set_variable("CMAKE_MATCH_COUNT", "0");
                 for (size_t i = 0; i < 10; ++i) {
                    interp_.set_variable("CMAKE_MATCH_" + std::to_string(i), "");
                }
            }
            return result;
        }
        // IN_LIST operator: checks if value is in a list variable
        else if (op == "IN_LIST") {
            pos_++; // Consume operator
            if (pos_ >= condition_.size()) {
                error_msg_ = "IN_LIST operator requires a right operand";
                return false;
            }

            std::string value = evaluate_token(condition_[start_pos]);
            std::string list_str = evaluate_token(condition_[pos_++]);

            // Parse the list (semicolon-separated) and check if value is in it
            return CMakeArrayView(list_str).contains(value);
        }
        // IS_NEWER_THAN - file timestamp comparison
        else if (op == "IS_NEWER_THAN") {
            pos_++;
            if (pos_ >= condition_.size()) {
                error_msg_ = "IS_NEWER_THAN operator requires a right operand";
                return false;
            }

            std::string left = interp_.evaluate_argument(condition_[start_pos]);
            std::string right = interp_.evaluate_argument(condition_[pos_++]);

            // CMake behavior: returns true if file1 >= file2 OR either doesn't exist
            std::error_code ec1, ec2;
            auto time1 = std::filesystem::last_write_time(left, ec1);
            auto time2 = std::filesystem::last_write_time(right, ec2);

            if (ec1 || ec2) {
                // Either file doesn't exist - return true
                return true;
            }
            return time1 >= time2;
        }

        // Not a comparison operator - return the unary/primary result
        return unary_result;
    }

    // Unary operators (highest precedence) and primary values
    bool parse_unary_or_primary() {
        if (pos_ >= condition_.size()) return false;

        // Parentheses for grouping - peek at AST directly
        if (peek_bare() == "(") {
            pos_++;
            // CMake behavior: empty parentheses () evaluate to false
            if (peek_bare() == ")") {
                pos_++;
                return false;
            }
            bool result = parse_or();
            if (peek_bare() != ")") {
                error_msg_ = "Expected ')' to close group";
                return false;
            }
            pos_++;
            return result;
        }

        // Need the token string for unary operator checks
        // Copy needed: token is used after get_token_string may be called again below
        std::string token = get_token_string(condition_[pos_]);

        // Unary operators that take one argument
        // If there's no next token, treat the keyword as a primary value instead
        // Quoted arguments are never keywords — if("${VAR}" ...) where VAR expands
        // to "TARGET" etc. must be treated as a primary value, not a unary operator.
        if (!condition_[pos_].quoted && token == "DEFINED" && pos_ + 1 < condition_.size()) {
            pos_++;
            // DEFINED takes a variable name (don't dereference it)
            std::string var_name = get_token_string(condition_[pos_++]);

            // Check DEFINED CACHE{VAR} syntax - checks only cache variables
            if (var_name.size() > 6 &&
                var_name.compare(0, 6, "CACHE{") == 0 &&
                var_name.back() == '}') {
                std::string cache_var = var_name.substr(6, var_name.size() - 7);
                return interp_.get_cache_variables().contains(cache_var);
            }

            // Check if variable is defined in any scope (local + cache)
            return interp_.is_variable_set(var_name);
        } else if (!condition_[pos_].quoted && token == "TARGET" && pos_ + 1 < condition_.size()) {
            pos_++;
            std::string target_name = get_token_string(condition_[pos_++]);
            // Use find_target() to handle aliases (CMake's if(TARGET) returns true for aliases)
            return interp_.find_target(target_name) != nullptr;
        } else if (!condition_[pos_].quoted && token == "EXISTS" && pos_ + 1 < condition_.size()) {
            pos_++;
            // File test operators take paths literally (with variable expansion)
            // but do NOT dereference the entire path as a variable name
            std::string path = interp_.evaluate_argument(condition_[pos_++]);
            return std::filesystem::exists(path);
        } else if (!condition_[pos_].quoted && token == "IS_DIRECTORY" && pos_ + 1 < condition_.size()) {
            pos_++;
            std::string path = interp_.evaluate_argument(condition_[pos_++]);
            return std::filesystem::is_directory(path);
        } else if (!condition_[pos_].quoted && token == "IS_ABSOLUTE" && pos_ + 1 < condition_.size()) {
            pos_++;
            std::string path = interp_.evaluate_argument(condition_[pos_++]);
            return std::filesystem::path(path).is_absolute();
        } else if (!condition_[pos_].quoted && token == "IS_SYMLINK" && pos_ + 1 < condition_.size()) {
            pos_++;
            std::string path = interp_.evaluate_argument(condition_[pos_++]);
            return std::filesystem::is_symlink(path);
        } else if (!condition_[pos_].quoted && token == "COMMAND" && pos_ + 1 < condition_.size()) {
            pos_++;
            std::string name = evaluate_token(condition_[pos_++]);
            return interp_.has_user_function(name);
        } else if (!condition_[pos_].quoted && token == "POLICY" && pos_ + 1 < condition_.size()) {
            pos_++;
            pos_++;  // Consume the policy name/number
            // Always return true - dmake doesn't implement policies but we want
            // scripts to think we support the latest policies
            return true;
        }

        // Primary value - evaluate and check truthiness
        // For keywords that aren't being used as operators (like standalone DEFINED, TARGET, etc.),
        // we need to dereference them as variables, not treat them as keywords
        const Argument& arg = condition_[pos_++];
        std::string token_str = get_token_string(arg);

        // Check if this is a boolean constant (case-insensitive)
        // These have fixed truthiness regardless of quoting
        if (is_boolean_constant_ci(token_str)) {
            return !Interpreter::is_falsy(token_str);
        }

        // Check if it's a numeric constant
        if (is_numeric_constant(token_str)) {
            return !Interpreter::is_falsy(token_str);
        }

        // CMake behavior for quoted strings: if not a boolean constant or number,
        // return FALSE. Quoted strings cannot be variable lookups.
        if (arg.quoted) {
            return false;
        }

        // For all other cases (plain literals or concatenations like Prefix_${Suffix}),
        // dereference the result as a variable name
        // Example: if(VAR) should look up the value of variable VAR
        // Example: if(VAR_${suffix}) where suffix="" should expand to "VAR_", then look up VAR_
        std::string val = interp_.get_variable(token_str);
        return !Interpreter::is_falsy(val);
    }
};

} // anonymous namespace

std::expected<bool, InterpreterError> evaluate_condition(
    Interpreter& interp,
    const std::vector<Argument>& condition,
    size_t row, size_t col, size_t offset, size_t length)
{
    // Empty condition evaluates to FALSE (CMake behavior)
    if (condition.empty()) {
        return false;
    }

    ConditionParser parser(interp, condition);
    bool result = parser.parse();

    // Check if there was an error during parsing
    if (!parser.error().empty()) {
        interp.set_fatal_error(parser.error());
        return std::unexpected(*interp.get_fatal_error());
    }

    // CMake treats leftover tokens in conditions as an error
    if (parser.pos() < condition.size()) {
        std::string remaining;
        for (size_t i = parser.pos(); i < condition.size(); ++i) {
            if (!remaining.empty()) remaining += " ";
            // Use evaluate_argument for remaining tokens
            if (!condition[i].quoted && condition[i].parts.size() == 1 &&
                std::holds_alternative<std::string>(condition[i].parts[0])) {
                remaining += std::get<std::string>(condition[i].parts[0]);
            } else {
                remaining += interp.evaluate_argument(condition[i]);
            }
        }

        // Trim whitespace (space, tab, CR, LF)
        auto is_whitespace = [](char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        };

        // Trim leading whitespace
        size_t start = 0;
        while (start < remaining.size() && is_whitespace(remaining[start])) {
            ++start;
        }

        // Trim trailing whitespace
        size_t end = remaining.size();
        while (end > start && is_whitespace(remaining[end - 1])) {
            --end;
        }

        remaining = remaining.substr(start, end - start);

        // Error if there are actual non-whitespace tokens (CMake behavior)
        if (!remaining.empty()) {
            interp.set_fatal_error("if() condition has unexpected tokens: " + remaining);
            return std::unexpected(*interp.get_fatal_error());
        }
    }

    return result;
}

} // namespace dmake
