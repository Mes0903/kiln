#include "condition_evaluator.hpp"
#include "interperter.hpp"
#include "regex.hpp"
#include "CMakeArray.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
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

    // Set of keywords that should not be dereferenced as variables
    // All keywords are in uppercase; comparisons are done case-insensitively
    // We use a sorted static array and linear search because it is more branch-prediction
    // friendly for small sets of strings compared to std::set or binary search.
    static constexpr std::array keywords = {
        "(", ")", "AND", "COMMAND", "DEFINED", "EQUAL", "EXISTS", "GREATER",
        "GREATER_EQUAL", "IN_LIST", "IS_ABSOLUTE", "IS_DIRECTORY", "IS_NEWER_THAN",
        "IS_SYMLINK", "LESS", "LESS_EQUAL", "MATCHES", "NOT", "NOT_EQUAL", "OR",
        "POLICY", "STREQUAL", "STRGREATER", "STRGREATER_EQUAL", "STRLESS",
        "STRLESS_EQUAL", "TARGET", "TEST", "VERSION_EQUAL", "VERSION_GREATER",
        "VERSION_GREATER_EQUAL", "VERSION_LESS", "VERSION_LESS_EQUAL"
    };

    // Boolean constants that have fixed truthiness values (case-insensitive)
    static constexpr std::array boolean_constants = {
        "FALSE", "IGNORE", "N", "NO", "NOTFOUND", "OFF", "ON", "TRUE", "Y", "YES"
    };

    // Helper to get the string value of an argument token
    // For unquoted arguments that are not keywords, dereference as variable
    auto get_token_string = [&](const Argument& arg) -> std::string {
        if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0])) {
            return std::get<std::string>(arg.parts[0]);
        }
        return interp.evaluate_argument(arg);
    };

    // Helper to check if a string looks like a numeric constant
    auto is_numeric_constant = [](const std::string& s) -> bool {
        if (s.empty()) return false;
        size_t start = 0;
        if (s[0] == '-' || s[0] == '+') start = 1;
        if (start >= s.length()) return false;

        // Check if rest of string is numeric
        for (size_t i = start; i < s.length(); ++i) {
            if (!std::isdigit(s[i]) && s[i] != '.') return false;
        }
        return true;
    };

    // Helper to evaluate an argument, dereferencing variables unless it's a keyword or constant
    // CMake behavior (pre-CMP0054):
    // - Keywords and quoted strings are returned as-is
    // - Numeric constants are returned as-is
    // - Everything else is dereferenced as a variable:
    //   - If defined, return the variable's value (even if empty)
    //   - If undefined, return empty string
    auto evaluate_token = [&](const Argument& arg) -> std::string {
        std::string token = get_token_string(arg);

        // Don't dereference keywords (operators) or boolean constants or quoted strings
        // Check case-insensitively for both
        std::string upper_token = dmake::to_upper(token);
        if (arg.quoted ||
            std::find(keywords.begin(), keywords.end(), upper_token) != keywords.end() ||
            std::find(boolean_constants.begin(), boolean_constants.end(), upper_token) != boolean_constants.end()) {
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
        auto opt = interp.get_optional_variable(token);
        return opt.has_value() ? *opt : token;
    };

    // Recursive descent parser with proper CMake precedence
    // Precedence (high to low): unary tests > binary tests > NOT > AND/OR
    size_t pos = 0;
    std::string error_msg;  // Track parsing errors

    std::function<bool()> parse_or;
    std::function<bool()> parse_and;
    std::function<bool()> parse_not;
    std::function<bool()> parse_comparison;
    std::function<bool()> parse_unary_or_primary;

    // AND/OR have lowest precedence and evaluate left-to-right
    // NOTE: CMake does NOT short-circuit - both sides are always evaluated
    parse_or = [&]() -> bool {
        bool left = parse_and();

        while (pos < condition.size() && error_msg.empty()) {
            std::string token = get_token_string(condition[pos]);
            if (token == "OR") {
                pos++;
                if (pos >= condition.size()) {
                    error_msg = "OR operator requires a right operand";
                    return false;
                }
                bool right = parse_and();  // Always evaluate right side (no short-circuit)
                left = left || right;
            } else {
                break;
            }
        }
        return left;
    };

    parse_and = [&]() -> bool {
        bool left = parse_not();

        while (pos < condition.size() && error_msg.empty()) {
            std::string token = get_token_string(condition[pos]);
            if (token == "AND") {
                pos++;
                if (pos >= condition.size()) {
                    error_msg = "AND operator requires a right operand";
                    return false;
                }
                bool right = parse_not();  // Always evaluate right side (no short-circuit)
                left = left && right;
            } else {
                break;
            }
        }
        return left;
    };

    // NOT has higher precedence than AND/OR but lower than comparisons
    parse_not = [&]() -> bool {
        if (pos >= condition.size()) {
            error_msg = "Unexpected end of condition";
            return false;
        }

        std::string token = get_token_string(condition[pos]);
        if (token == "NOT") {
            // Check if there's a valid operand after NOT
            // NOT followed by nothing or by AND/OR should be treated as a primary value
            // (CMake compatibility - NOT without operand evaluates to false)
            if (pos + 1 < condition.size()) {
                std::string next_token = get_token_string(condition[pos + 1]);
                if (next_token != "AND" && next_token != "OR") {
                    // Valid operand exists - NOT is an operator
                    pos++;
                    return !parse_not();  // Right-associative
                }
            }
            // No valid operand - fall through to treat "NOT" as a primary value
        }
        return parse_comparison();
    };

    // Binary comparison operators (EQUAL, LESS, STREQUAL, etc.)
    parse_comparison = [&]() -> bool {
        if (pos >= condition.size()) return false;

        // Save start position in case this isn't a comparison
        size_t start_pos = pos;

        // Try to parse left operand
        bool unary_result = parse_unary_or_primary();

        // Check if next token is a binary operator
        if (pos >= condition.size()) {
            return unary_result;
        }

        std::string op = get_token_string(condition[pos]);

        // Numeric comparisons
        if (op == "EQUAL" || op == "LESS" || op == "GREATER" ||
            op == "LESS_EQUAL" || op == "GREATER_EQUAL" || op == "NOT_EQUAL") {
            pos++;
            if (pos >= condition.size()) {
                error_msg = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition[start_pos]);
            std::string right = evaluate_token(condition[pos++]);

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
            pos++;
            if (pos >= condition.size()) {
                error_msg = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition[start_pos]);
            std::string right = evaluate_token(condition[pos++]);

            if (op == "STREQUAL") return left == right;
            if (op == "STRLESS") return left < right;
            if (op == "STRGREATER") return left > right;
            if (op == "STRLESS_EQUAL") return left <= right;
            if (op == "STRGREATER_EQUAL") return left >= right;
        }
        // Version comparisons - component-wise numeric comparison (CMake behavior)
        else if (op.starts_with("VERSION_")) {
            pos++;
            if (pos >= condition.size()) {
                error_msg = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition[start_pos]);
            std::string right = evaluate_token(condition[pos++]);

            int cmp = compare_versions(left, right);

            if (op == "VERSION_EQUAL") return cmp == 0;
            if (op == "VERSION_LESS") return cmp < 0;
            if (op == "VERSION_GREATER") return cmp > 0;
            if (op == "VERSION_LESS_EQUAL") return cmp <= 0;
            if (op == "VERSION_GREATER_EQUAL") return cmp >= 0;
        }
        // regex
        else if(op == "MATCHES") {
            pos++; // Consume operator
            if (pos >= condition.size()) {
                error_msg = "MATCHES operator requires a right operand";
                return false;
            }
            std::string pattern = evaluate_token(condition[pos++]);
            auto re = Regex::compile(pattern);
            if (!re) {
                error_msg = "MATCHES: invalid regex: " + re.error();
                return false;
            }
            std::string left = evaluate_token(condition[start_pos]);
            std::vector<std::string> captures;
            bool result = re->search(left, captures);

            if (result) {
                interp.set_variable("CMAKE_MATCH_COUNT", std::to_string(captures.size() - 1));
                for (size_t i = 0; i < captures.size() && i < 10; ++i) {
                    interp.set_variable("CMAKE_MATCH_" + std::to_string(i), captures[i]);
                }
                for (size_t i = captures.size(); i < 10; ++i) {
                    interp.set_variable("CMAKE_MATCH_" + std::to_string(i), "");
                }
            } else {
                 interp.set_variable("CMAKE_MATCH_COUNT", "0");
                 for (size_t i = 0; i < 10; ++i) {
                    interp.set_variable("CMAKE_MATCH_" + std::to_string(i), "");
                }
            }
            return result;
        }
        // IN_LIST operator: checks if value is in a list variable
        else if (op == "IN_LIST") {
            pos++; // Consume operator
            if (pos >= condition.size()) {
                error_msg = "IN_LIST operator requires a right operand";
                return false;
            }

            std::string value = evaluate_token(condition[start_pos]);
            std::string list_str = evaluate_token(condition[pos++]);

            // Parse the list (semicolon-separated) and check if value is in it
            return CMakeArrayView(list_str).contains(value);
        }
        // IS_NEWER_THAN - file timestamp comparison
        else if (op == "IS_NEWER_THAN") {
            pos++;
            if (pos >= condition.size()) {
                error_msg = "IS_NEWER_THAN operator requires a right operand";
                return false;
            }

            std::string left = interp.evaluate_argument(condition[start_pos]);
            std::string right = interp.evaluate_argument(condition[pos++]);

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
    };

    // Unary operators (highest precedence) and primary values
    parse_unary_or_primary = [&]() -> bool {
        if (pos >= condition.size()) return false;

        std::string token = get_token_string(condition[pos]);

        // Parentheses for grouping
        if (token == "(") {
            pos++;
            bool result = parse_or();
            if (pos >= condition.size() || get_token_string(condition[pos]) != ")") {
                error_msg = "Expected ')' to close group";
                return false;
            }
            pos++;
            return result;
        }

        // Unary operators that take one argument
        // If there's no next token, treat the keyword as a primary value instead
        if (token == "DEFINED" && pos + 1 < condition.size()) {
            pos++;
            // DEFINED takes a variable name (don't dereference it)
            std::string var_name = get_token_string(condition[pos++]);

            // Check if variable is defined in any scope
            return interp.get_variables().is_defined(var_name);
        } else if (token == "TARGET" && pos + 1 < condition.size()) {
            pos++;
            std::string target_name = get_token_string(condition[pos++]);
            return interp.get_targets().contains(target_name);
        } else if (token == "EXISTS" && pos + 1 < condition.size()) {
            pos++;
            // File test operators take paths literally (with variable expansion)
            // but do NOT dereference the entire path as a variable name
            std::string path = interp.evaluate_argument(condition[pos++]);
            return std::filesystem::exists(path);
        } else if (token == "IS_DIRECTORY" && pos + 1 < condition.size()) {
            pos++;
            std::string path = interp.evaluate_argument(condition[pos++]);
            return std::filesystem::is_directory(path);
        } else if (token == "IS_ABSOLUTE" && pos + 1 < condition.size()) {
            pos++;
            std::string path = interp.evaluate_argument(condition[pos++]);
            return std::filesystem::path(path).is_absolute();
        } else if (token == "IS_SYMLINK" && pos + 1 < condition.size()) {
            pos++;
            std::string path = interp.evaluate_argument(condition[pos++]);
            return std::filesystem::is_symlink(path);
        } else if (token == "COMMAND" && pos + 1 < condition.size()) {
            pos++;
            std::string name = evaluate_token(condition[pos++]);
            return interp.has_user_function(name);
        } else if (token == "POLICY" && pos + 1 < condition.size()) {
            pos++;
            pos++;  // Consume the policy name/number
            // Always return true - dmake doesn't implement policies but we want
            // scripts to think we support the latest policies
            return true;
        }

        // Primary value - evaluate and check truthiness
        // For keywords that aren't being used as operators (like standalone DEFINED, TARGET, etc.),
        // we need to dereference them as variables, not treat them as keywords
        const Argument& arg = condition[pos++];
        std::string token_str = get_token_string(arg);

        // If it's quoted or a numeric constant, use it as-is
        if (arg.quoted || is_numeric_constant(token_str)) {
            return !Interpreter::is_falsy(token_str);
        }

        // Check if this is a boolean constant (case-insensitive)
        // These have fixed truthiness and should not be dereferenced as variables
        std::string upper_token = token_str;
        std::transform(upper_token.begin(), upper_token.end(), upper_token.begin(), ::toupper);
        if (std::find(boolean_constants.begin(), boolean_constants.end(), upper_token) != boolean_constants.end()) {
            return !Interpreter::is_falsy(token_str);
        }

        // For all other cases (plain literals or concatenations like Prefix_${Suffix}),
        // dereference the result as a variable name
        // Example: if(VAR) should look up the value of variable VAR
        // Example: if(VAR_${suffix}) where suffix="" should expand to "VAR_", then look up VAR_
        std::string val = interp.get_variable(token_str);
        return !Interpreter::is_falsy(val);
    };

    // Start parsing at the lowest precedence level (OR)
    bool result = parse_or();

    // Check if there was an error during parsing
    if (!error_msg.empty()) {
        interp.set_fatal_error(error_msg);
        return std::unexpected(*interp.get_fatal_error());
    }

    // Lenient handling of leftover tokens (CMake compatibility)
    // CMake returns FALSE for malformed conditions instead of erroring
    if (pos < condition.size()) {
        std::string remaining;
        for (size_t i = pos; i < condition.size(); ++i) {
            if (!remaining.empty()) remaining += " ";
            remaining += get_token_string(condition[i]);
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

        // Only warn if there are actual non-whitespace tokens
        if (!remaining.empty()) {
            interp.print_message("AUTHOR_WARNING",
                          "Malformed if() condition - unexpected tokens: " + remaining +
                          "\n  Condition evaluates to FALSE (CMake compatibility mode)" +
                          "\n  in " + interp.get_current_file() + ":" + std::to_string(row + 1));
        }
        return false;
    }

    return result;
}

} // namespace dmake
