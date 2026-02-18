#include "condition_evaluator.hpp"
#include "interperter.hpp"
#include "regex.hpp"
#include "clock_cache.hpp"
#include "CMakeArray.hpp"
#include "parse_number.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <filesystem>
#include <limits>
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
        parts_a.push_back(parse_number_partial<int>(component, 0));
    }

    // Parse version b
    std::istringstream iss_b(b);
    while (std::getline(iss_b, component, '.')) {
        parts_b.push_back(parse_number_partial<int>(component, 0));
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

// Case-insensitive check against boolean_constants using length-based dispatch.
// All constants are pure ASCII letters so (c | 0x20) gives case-insensitive comparison.
bool is_boolean_constant_ci(std::string_view token) {
    auto ci_eq = [](std::string_view a, const char* b) {
        for (size_t i = 0; i < a.size(); ++i)
            if ((a[i] | 0x20) != (b[i] | 0x20)) return false;
        return true;
    };
    switch (token.size()) {
    case 1: return (token[0] | 0x20) == 'n' || (token[0] | 0x20) == 'y';
    case 2: return ci_eq(token, "NO") || ci_eq(token, "ON");
    case 3: return ci_eq(token, "OFF") || ci_eq(token, "YES");
    case 4: return ci_eq(token, "TRUE");
    case 5: return ci_eq(token, "FALSE");
    case 6: return ci_eq(token, "IGNORE");
    case 8: return ci_eq(token, "NOTFOUND");
    default: return false;
    }
}

// Set of binary operator keywords
constexpr std::array binary_operators = {
    "EQUAL", "GREATER", "GREATER_EQUAL", "IN_LIST", "IS_NEWER_THAN",
    "LESS", "LESS_EQUAL", "MATCHES", "NOT_EQUAL", "STREQUAL",
    "STRGREATER", "STRGREATER_EQUAL", "STRLESS", "STRLESS_EQUAL",
    "VERSION_EQUAL", "VERSION_GREATER", "VERSION_GREATER_EQUAL",
    "VERSION_LESS", "VERSION_LESS_EQUAL"
};

// Unary operator keywords
constexpr std::array unary_keywords = {
    "COMMAND", "DEFINED", "EXISTS", "IS_ABSOLUTE", "IS_DIRECTORY",
    "IS_SYMLINK", "POLICY", "TARGET", "TEST"
};

// --- Shared helpers used by both ConditionParser and fast-path ---

bool is_numeric_constant(std::string_view s) {
    if (s.empty()) return false;
    size_t start = 0;
    if (s[0] == '-' || s[0] == '+') start = 1;
    if (start >= s.length()) return false;
    for (size_t i = start; i < s.length(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i])) && s[i] != '.') return false;
    }
    return true;
}

// Returns a reference to the token string. Fast path returns reference into
// the AST (stable); slow path evaluates into caller-provided buffer.
// WARNING: reference is invalidated by the next call with the same buffer.
const std::string& get_token_string(Interpreter& interp, const Argument& arg, std::string& buffer) {
    if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0])) {
        return std::get<std::string>(arg.parts[0]);
    }
    buffer = interp.evaluate_argument(arg);
    return buffer;
}

bool is_keyword(std::string_view token) {
    // Arrays are sorted — use binary search instead of linear scan.
    return std::binary_search(keywords.begin(), keywords.end(), token,
        [](std::string_view a, std::string_view b) { return a < b; });
}

bool is_binary_operator(std::string_view token) {
    // Length-based dispatch eliminates most comparisons for non-operator tokens.
    switch (token.size()) {
    case 4: return token == "LESS";
    case 5: return token == "EQUAL";
    case 7: return token == "GREATER" || token == "IN_LIST" || token == "MATCHES" || token == "STRLESS";
    case 8: return token == "STREQUAL";
    case 9: return token == "NOT_EQUAL";
    case 10: return token == "LESS_EQUAL" || token == "STRGREATER";
    case 12: return token == "VERSION_LESS";
    case 13: return token == "GREATER_EQUAL" || token == "IS_NEWER_THAN" || token == "STRLESS_EQUAL" || token == "VERSION_EQUAL";
    case 15: return token == "VERSION_GREATER";
    case 16: return token == "STRGREATER_EQUAL";
    case 18: return token == "VERSION_LESS_EQUAL";
    case 21: return token == "VERSION_GREATER_EQUAL";
    default: return false;
    }
}

bool is_unary_keyword(std::string_view token) {
    switch (token.size()) {
    case 4: return token == "TEST";
    case 6: return token == "EXISTS" || token == "POLICY" || token == "TARGET";
    case 7: return token == "COMMAND" || token == "DEFINED";
    case 10: return token == "IS_SYMLINK";
    case 11: return token == "IS_ABSOLUTE";
    case 12: return token == "IS_DIRECTORY";
    default: return false;
    }
}

// Evaluate MATCHES with CMAKE_MATCH_* side effects. Returns error string on failure.
struct MatchesResult {
    bool matched = false;
    std::string error;
};

MatchesResult evaluate_matches(Interpreter& interp, const std::string& left, const std::string& pattern) {
    thread_local ClockCache<std::string, Regex> cache(8, [](const std::string& p) {
        return Regex::from_cmake_regex(p);
    });
    auto re = cache.get(pattern);
    if (!re) {
        return {false, "MATCHES: invalid regex: " + re.error()};
    }
    std::vector<std::string> captures;
    bool result = (*re)->search(left, captures);

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
    return {result, {}};
}

// --- classify_condition helpers ---

// Check if argument is a single unquoted bare string literal (no variable references)
bool is_bare_literal(const Argument& arg) {
    return !arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0]);
}

// Get the bare literal string from an argument. Only valid if is_bare_literal() is true.
std::string_view get_bare_literal(const Argument& arg) {
    return std::get<std::string>(arg.parts[0]);
}

// Check if argument contains any VariableReference parts
bool arg_has_varref(const Argument& arg) {
    for (const auto& part : arg.parts) {
        if (std::holds_alternative<VariableReference>(part)) return true;
    }
    return false;
}

// --- eval_operand for fast-path ---
// Simplified evaluate_token that skips keyword linear scan (safe because classify_condition
// guards ensure operands aren't keywords).
// Returns a string_view valid until the next variable mutation or scope change.
// When dereference produces a result, it points into the variable store; otherwise
// it points into the AST or the caller-provided buffer.
std::string_view eval_operand_sv(Interpreter& interp, const Argument& arg, std::string& buffer) {
    const std::string& token = get_token_string(interp, arg, buffer);

    // Quoted strings returned as-is
    if (arg.quoted) return token;

    // Single variable reference → already expanded
    if (arg.parts.size() == 1 && std::holds_alternative<VariableReference>(arg.parts[0])) {
        return token;
    }

    // Boolean constant → return as-is
    if (is_boolean_constant_ci(token)) return token;

    // Numeric constant → return as-is
    if (is_numeric_constant(token)) return token;

    // Dereference as variable. Undefined → keep literal.
    auto view = interp.get_variable_view(token);
    return view.has_value() ? *view : std::string_view(token);
}

// Fast numeric comparison: try integer first (most common in CMake), then double.
// Returns true if both strings were successfully parsed as numbers, with result in cmp.
// cmp is <0, 0, or >0 like strcmp.
bool try_numeric_compare(std::string_view left, std::string_view right, int& cmp) {
    int64_t li, ri;
    auto lr = std::from_chars(left.data(), left.data() + left.size(), li);
    auto rr = std::from_chars(right.data(), right.data() + right.size(), ri);
    if (lr.ec == std::errc{} && lr.ptr == left.data() + left.size() &&
        rr.ec == std::errc{} && rr.ptr == right.data() + right.size()) {
        cmp = (li > ri) - (li < ri);
        return true;
    }
    // Fall back to double
    double ld, rd;
    auto lrd = std::from_chars(left.data(), left.data() + left.size(), ld);
    auto rrd = std::from_chars(right.data(), right.data() + right.size(), rd);
    if (lrd.ec == std::errc{} && lrd.ptr == left.data() + left.size() &&
        rrd.ec == std::errc{} && rrd.ptr == right.data() + right.size()) {
        cmp = (ld > rd) - (ld < rd);
        return true;
    }
    return false;
}

// Apply a numeric comparison operator given a three-way cmp result.
bool apply_numeric_op(ConditionOp op, int cmp) {
    switch (op) {
    case ConditionOp::BinaryEqual: return cmp == 0;
    case ConditionOp::BinaryNotEqual: return cmp != 0;
    case ConditionOp::BinaryLess: return cmp < 0;
    case ConditionOp::BinaryGreater: return cmp > 0;
    case ConditionOp::BinaryLessEqual: return cmp <= 0;
    case ConditionOp::BinaryGreaterEqual: return cmp >= 0;
    default: return false;
    }
}

// --- Filter + fallback for dynamic args that expanded to empty/semicolons ---
std::expected<bool, InterpreterError> evaluate_condition_with_filtering(
    Interpreter& interp,
    const std::vector<Argument>& condition,
    size_t row, size_t col, size_t offset, size_t length)
{
    // CMake argument elision and list expansion
    std::vector<Argument> filtered;
    for (const auto& arg : condition) {
        bool has_var_ref = false;
        for (const auto& part : arg.parts) {
            if (std::holds_alternative<VariableReference>(part)) {
                has_var_ref = true;
                break;
            }
        }

        if (has_var_ref && !arg.quoted) {
            std::string val = interp.evaluate_argument(arg);
            if (val.empty()) continue;  // elision
            for (auto item : CMakeArrayIterator(val)) {
                if (item.empty()) continue;
                Argument new_arg;
                new_arg.quoted = false;
                new_arg.parts.push_back(std::string(item));
                filtered.push_back(std::move(new_arg));
            }
        } else {
            filtered.push_back(arg);
        }
    }
    return evaluate_condition(interp, filtered, row, col, offset, length);
}

// ConditionParser (full recursive descent — fallback path)
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
    std::string eval_buffer_;

    const std::string& parser_get_token_string(const Argument& arg) {
        return get_token_string(interp_, arg, eval_buffer_);
    }

    std::string_view peek_bare() const {
        if (pos_ >= condition_.size()) return {};
        const auto& arg = condition_[pos_];
        if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0]))
            return std::get<std::string>(arg.parts[0]);
        return {};
    }

    std::string evaluate_token(const Argument& arg) {
        const std::string& token = parser_get_token_string(arg);

        if (arg.quoted ||
            std::find(keywords.begin(), keywords.end(), token) != keywords.end() ||
            is_boolean_constant_ci(token)) {
            return token;
        }

        if (is_numeric_constant(token)) {
            return token;
        }

        if (arg.parts.size() == 1 && std::holds_alternative<VariableReference>(arg.parts[0])) {
            return token;
        }

        auto view = interp_.get_variable_view(token);
        return view.has_value() ? std::string(*view) : std::string(token);
    }

    bool parse_or() {
        bool left = parse_and();
        while (pos_ < condition_.size() && error_msg_.empty()) {
            if (peek_bare() == "OR") {
                pos_++;
                if (pos_ >= condition_.size()) {
                    error_msg_ = "OR operator requires a right operand";
                    return false;
                }
                bool right = parse_and();
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
                bool right = parse_not();
                left = left && right;
            } else {
                break;
            }
        }
        return left;
    }

    bool parse_not() {
        if (pos_ >= condition_.size()) {
            error_msg_ = "Unexpected end of condition";
            return false;
        }

        if (peek_bare() == "NOT") {
            if (pos_ + 1 < condition_.size()) {
                const std::string& next_token = parser_get_token_string(condition_[pos_ + 1]);
                if (is_binary_operator(next_token)) {
                    return parse_comparison();
                }
                if (next_token != "AND" && next_token != "OR") {
                    pos_++;
                    return !parse_not();
                }
            }
        }
        return parse_comparison();
    }

    bool parse_comparison() {
        if (pos_ >= condition_.size()) return false;

        if (peek_bare() == "MATCHES" && pos_ + 1 < condition_.size()) {
            pos_ += 2;
            return false;
        }

        if (!condition_[pos_].quoted) {
            const std::string& current_token = parser_get_token_string(condition_[pos_]);
            if (is_binary_operator(current_token) && pos_ + 1 < condition_.size()) {
                error_msg_ = "if given arguments: \"" + current_token + "\" - missing left operand";
                return false;
            }
        }

        size_t start_pos = pos_;
        bool unary_result = parse_unary_or_primary();

        if (pos_ >= condition_.size()) {
            return unary_result;
        }

        std::string op = parser_get_token_string(condition_[pos_]);

        if (op == "EQUAL" || op == "LESS" || op == "GREATER" ||
            op == "LESS_EQUAL" || op == "GREATER_EQUAL" || op == "NOT_EQUAL") {
            pos_++;
            if (pos_ >= condition_.size()) {
                error_msg_ = op + " operator requires a right operand";
                return false;
            }
            std::string left = evaluate_token(condition_[start_pos]);
            std::string right = evaluate_token(condition_[pos_++]);
            int cmp;
            if (try_numeric_compare(left, right, cmp)) {
                if (op == "EQUAL") return cmp == 0;
                if (op == "NOT_EQUAL") return cmp != 0;
                if (op == "LESS") return cmp < 0;
                if (op == "GREATER") return cmp > 0;
                if (op == "LESS_EQUAL") return cmp <= 0;
                if (op == "GREATER_EQUAL") return cmp >= 0;
            } else {
                if (op == "EQUAL") return left == right;
                if (op == "NOT_EQUAL") return left != right;
                return false;
            }
        }
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
        else if(op == "MATCHES") {
            pos_++;
            if (pos_ >= condition_.size()) {
                error_msg_ = "MATCHES operator requires a right operand";
                return false;
            }
            std::string pattern = evaluate_token(condition_[pos_++]);
            std::string left = evaluate_token(condition_[start_pos]);
            auto mr = evaluate_matches(interp_, left, pattern);
            if (!mr.error.empty()) {
                error_msg_ = mr.error;
                return false;
            }
            return mr.matched;
        }
        else if (op == "IN_LIST") {
            pos_++;
            if (pos_ >= condition_.size()) {
                error_msg_ = "IN_LIST operator requires a right operand";
                return false;
            }
            std::string value = evaluate_token(condition_[start_pos]);
            std::string list_str = evaluate_token(condition_[pos_++]);
            return cmake_list_contains(list_str, value);
        }
        else if (op == "IS_NEWER_THAN") {
            pos_++;
            if (pos_ >= condition_.size()) {
                error_msg_ = "IS_NEWER_THAN operator requires a right operand";
                return false;
            }
            std::string left = interp_.evaluate_argument(condition_[start_pos]);
            std::string right = interp_.evaluate_argument(condition_[pos_++]);
            std::error_code ec1, ec2;
            auto time1 = std::filesystem::last_write_time(left, ec1);
            auto time2 = std::filesystem::last_write_time(right, ec2);
            if (ec1 || ec2) return true;
            return time1 >= time2;
        }

        return unary_result;
    }

    bool parse_unary_or_primary() {
        if (pos_ >= condition_.size()) return false;

        if (peek_bare() == "(") {
            pos_++;
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

        std::string token = parser_get_token_string(condition_[pos_]);

        if (!condition_[pos_].quoted && token == "DEFINED" && pos_ + 1 < condition_.size()) {
            pos_++;
            std::string var_name = parser_get_token_string(condition_[pos_++]);
            if (var_name.size() > 6 &&
                var_name.compare(0, 6, "CACHE{") == 0 &&
                var_name.back() == '}') {
                std::string cache_var = var_name.substr(6, var_name.size() - 7);
                return interp_.get_cache_variables().contains(cache_var);
            }
            return interp_.is_variable_set(var_name);
        } else if (!condition_[pos_].quoted && token == "TARGET" && pos_ + 1 < condition_.size()) {
            pos_++;
            std::string target_name = parser_get_token_string(condition_[pos_++]);
            return interp_.find_target(target_name) != nullptr;
        } else if (!condition_[pos_].quoted && token == "EXISTS" && pos_ + 1 < condition_.size()) {
            pos_++;
            std::string path = interp_.evaluate_argument(condition_[pos_++]);
            return interp_.cached_file_exists(path);
        } else if (!condition_[pos_].quoted && token == "IS_DIRECTORY" && pos_ + 1 < condition_.size()) {
            pos_++;
            std::string path = interp_.evaluate_argument(condition_[pos_++]);
            return interp_.cached_is_directory(path);
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
            pos_++;
            return true;
        }

        const Argument& arg = condition_[pos_++];
        std::string token_str = parser_get_token_string(arg);

        if (is_boolean_constant_ci(token_str)) {
            return !Interpreter::is_falsy(token_str);
        }

        if (is_numeric_constant(token_str)) {
            return !Interpreter::is_falsy(token_str);
        }

        if (arg.quoted) {
            return false;
        }

        std::string val = interp_.get_variable(token_str);
        return !Interpreter::is_falsy(val);
    }
};

} // anonymous namespace

// --- classify_condition (parse-time) ---

PreParsedCondition classify_condition(const std::vector<Argument>& condition) {
    PreParsedCondition pp;
    const size_t n = condition.size();

    // Indices are uint8_t (max 255). We only handle n <= 4, so max index is 3.
    static_assert(4 <= std::numeric_limits<uint8_t>::max());
    if (n == 0 || n > 4) return pp;  // Fallback for empty or 5+ tokens

    // Helper: check if an operand-position argument is a bare keyword literal.
    // If so, we can't fast-path because the full parser has special keyword handling.
    auto operand_is_keyword = [&](size_t idx) -> bool {
        if (!is_bare_literal(condition[idx])) return false;
        auto lit = get_bare_literal(condition[idx]);
        return is_keyword(lit);
    };

    // Helper: check dynamic args flag
    auto compute_dynamic_flags = [&](uint8_t& flags, std::initializer_list<size_t> indices) {
        for (size_t idx : indices) {
            if (idx < n && !condition[idx].quoted && arg_has_varref(condition[idx])) {
                flags |= 2;  // has_dynamic_args
                return;
            }
        }
    };

    // Map operator string to ConditionOp for binary operators
    auto binary_op_from_string = [](std::string_view s) -> ConditionOp {
        if (s == "EQUAL") return ConditionOp::BinaryEqual;
        if (s == "NOT_EQUAL") return ConditionOp::BinaryNotEqual;
        if (s == "LESS") return ConditionOp::BinaryLess;
        if (s == "GREATER") return ConditionOp::BinaryGreater;
        if (s == "LESS_EQUAL") return ConditionOp::BinaryLessEqual;
        if (s == "GREATER_EQUAL") return ConditionOp::BinaryGreaterEqual;
        if (s == "STREQUAL") return ConditionOp::BinaryStrEqual;
        if (s == "STRLESS") return ConditionOp::BinaryStrLess;
        if (s == "STRGREATER") return ConditionOp::BinaryStrGreater;
        if (s == "STRLESS_EQUAL") return ConditionOp::BinaryStrLessEqual;
        if (s == "STRGREATER_EQUAL") return ConditionOp::BinaryStrGreaterEqual;
        if (s == "VERSION_EQUAL") return ConditionOp::BinaryVersionEqual;
        if (s == "VERSION_LESS") return ConditionOp::BinaryVersionLess;
        if (s == "VERSION_GREATER") return ConditionOp::BinaryVersionGreater;
        if (s == "VERSION_LESS_EQUAL") return ConditionOp::BinaryVersionLessEqual;
        if (s == "VERSION_GREATER_EQUAL") return ConditionOp::BinaryVersionGreaterEqual;
        if (s == "MATCHES") return ConditionOp::BinaryMatches;
        if (s == "IN_LIST") return ConditionOp::BinaryInList;
        if (s == "IS_NEWER_THAN") return ConditionOp::BinaryIsNewerThan;
        return ConditionOp::Fallback;
    };

    auto unary_op_from_string = [](std::string_view s) -> ConditionOp {
        if (s == "DEFINED") return ConditionOp::Defined;
        if (s == "TARGET") return ConditionOp::Target;
        if (s == "EXISTS") return ConditionOp::Exists;
        if (s == "IS_DIRECTORY") return ConditionOp::IsDirectory;
        if (s == "IS_ABSOLUTE") return ConditionOp::IsAbsolute;
        if (s == "IS_SYMLINK") return ConditionOp::IsSymlink;
        if (s == "COMMAND") return ConditionOp::Command;
        return ConditionOp::Fallback;
    };

    if (n == 1) {
        // if(X) → BoolCheck
        pp.op = ConditionOp::BoolCheck;
        pp.left_idx = 0;
        compute_dynamic_flags(pp.flags, {0});
        return pp;
    }

    if (n == 2) {
        if (!is_bare_literal(condition[0])) return pp;  // Fallback
        auto kw = get_bare_literal(condition[0]);

        if (kw == "NOT") {
            // NOT X → BoolCheck negated
            pp.op = ConditionOp::BoolCheck;
            pp.flags |= 1;  // negated
            pp.left_idx = 1;
            compute_dynamic_flags(pp.flags, {1});
            return pp;
        }

        ConditionOp uop = unary_op_from_string(kw);
        if (uop != ConditionOp::Fallback) {
            // DEFINED/TARGET/EXISTS/... X
            pp.op = uop;
            pp.left_idx = 1;
            compute_dynamic_flags(pp.flags, {1});
            return pp;
        }

        return pp;  // Fallback
    }

    if (n == 3) {
        // Check for NOT + unary-keyword + X
        if (is_bare_literal(condition[0]) && get_bare_literal(condition[0]) == "NOT" &&
            is_bare_literal(condition[1])) {
            auto kw = get_bare_literal(condition[1]);
            ConditionOp uop = unary_op_from_string(kw);
            if (uop != ConditionOp::Fallback) {
                pp.op = uop;
                pp.flags |= 1;  // negated
                pp.left_idx = 2;
                compute_dynamic_flags(pp.flags, {2});
                return pp;
            }
        }

        // Check for X binary-op Y
        if (is_bare_literal(condition[1])) {
            auto op_str = get_bare_literal(condition[1]);
            ConditionOp bop = binary_op_from_string(op_str);
            if (bop != ConditionOp::Fallback) {
                // Guard: left operand must not be a bare keyword
                if (operand_is_keyword(0)) return pp;
                // Guard: left operand must not be a unary keyword
                if (is_bare_literal(condition[0]) && is_unary_keyword(get_bare_literal(condition[0]))) return pp;
                // Guard: right operand must not be a bare keyword (except it's fine for some)
                // Actually the full parser evaluates both sides as evaluate_token, which handles keywords
                // But we need to guard against operands that ARE binary operators (would confuse full parser)
                // The classify guards are: operands must not be keywords. But actually, the full parser
                // handles keywords fine in evaluate_token (returns them as-is). The real concern is that
                // our fast-path eval_operand skips the keyword check. So we must guard.
                if (operand_is_keyword(2)) return pp;

                pp.op = bop;
                pp.left_idx = 0;
                pp.right_idx = 2;
                compute_dynamic_flags(pp.flags, {0, 2});
                return pp;
            }
        }

        return pp;  // Fallback
    }

    if (n == 4) {
        // NOT X binary-op Y
        if (!is_bare_literal(condition[0]) || get_bare_literal(condition[0]) != "NOT") return pp;
        if (!is_bare_literal(condition[2])) return pp;

        auto op_str = get_bare_literal(condition[2]);
        ConditionOp bop = binary_op_from_string(op_str);
        if (bop == ConditionOp::Fallback) return pp;

        // Guard: left operand (token[1]) must not be a bare keyword
        if (operand_is_keyword(1)) return pp;
        // Guard: token[1] must not be a unary keyword
        if (is_bare_literal(condition[1]) && is_unary_keyword(get_bare_literal(condition[1]))) return pp;
        // NOT-before-binary guard: token[1] must not be a binary operator
        if (is_bare_literal(condition[1]) && is_binary_operator(get_bare_literal(condition[1]))) return pp;
        // Guard: right operand must not be a keyword
        if (operand_is_keyword(3)) return pp;

        pp.op = bop;
        pp.flags |= 1;  // negated
        pp.left_idx = 1;
        pp.right_idx = 3;
        compute_dynamic_flags(pp.flags, {1, 3});
        return pp;
    }

    return pp;  // Fallback
}

// --- Full parser fallback ---

std::expected<bool, InterpreterError> evaluate_condition(
    Interpreter& interp,
    const std::vector<Argument>& condition,
    size_t row, size_t col, size_t offset, size_t length)
{
    if (condition.empty()) {
        return false;
    }

    ConditionParser parser(interp, condition);
    bool result = parser.parse();

    if (!parser.error().empty()) {
        interp.set_fatal_error(parser.error());
        return std::unexpected(*interp.get_fatal_error());
    }

    if (parser.pos() < condition.size()) {
        std::string remaining;
        for (size_t i = parser.pos(); i < condition.size(); ++i) {
            if (!remaining.empty()) remaining += " ";
            if (!condition[i].quoted && condition[i].parts.size() == 1 &&
                std::holds_alternative<std::string>(condition[i].parts[0])) {
                remaining += std::get<std::string>(condition[i].parts[0]);
            } else {
                remaining += interp.evaluate_argument(condition[i]);
            }
        }

        auto is_whitespace = [](char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        };
        size_t start = 0;
        while (start < remaining.size() && is_whitespace(remaining[start])) ++start;
        size_t end = remaining.size();
        while (end > start && is_whitespace(remaining[end - 1])) --end;
        remaining = remaining.substr(start, end - start);

        if (!remaining.empty()) {
            interp.set_fatal_error("if() condition has unexpected tokens: " + remaining);
            return std::unexpected(*interp.get_fatal_error());
        }
    }

    return result;
}

// --- Fast-path evaluator ---

std::expected<bool, InterpreterError> evaluate_condition(
    Interpreter& interp,
    const std::vector<Argument>& condition,
    const PreParsedCondition& pp,
    size_t row, size_t col, size_t offset, size_t length)
{
    if (condition.empty()) return false;

    // Fallback: use full parser with filtering
    if (pp.op == ConditionOp::Fallback) {
        return evaluate_condition_with_filtering(interp, condition, row, col, offset, length);
    }

    // Safety: indices are set to literal 0-3 by classify_condition (which only
    // accepts n <= 4), so uint8_t overflow is impossible. But verify bounds at
    // runtime in case the condition vector was modified after classification.
    bool has_binary = (static_cast<uint8_t>(pp.op) >= static_cast<uint8_t>(ConditionOp::BinaryEqual));
    if (pp.left_idx >= condition.size() ||
        (has_binary && pp.right_idx >= condition.size())) {
        return evaluate_condition_with_filtering(interp, condition, row, col, offset, length);
    }

    // Handle dynamic args: expand, check for empty/semicolons, fall back if dirty
    if (pp.has_dynamic_args()) {
        // We need to check operands that have varrefs
        auto check_operand = [&](uint8_t idx) -> std::pair<bool, std::string> {
            // Returns {needs_fallback, expanded_value}
            const auto& arg = condition[idx];
            if (arg.quoted || !arg_has_varref(arg)) return {false, {}};
            std::string val = interp.evaluate_argument(arg);
            if (val.empty() || val.find(';') != std::string::npos) return {true, {}};
            return {false, std::move(val)};
        };

        // Check left operand
        bool left_dirty = false;
        std::string left_expanded;
        if (pp.left_idx < condition.size() && !condition[pp.left_idx].quoted &&
            arg_has_varref(condition[pp.left_idx])) {
            auto [dirty, val] = check_operand(pp.left_idx);
            if (dirty) {
                return evaluate_condition_with_filtering(interp, condition, row, col, offset, length);
            }
            left_expanded = std::move(val);
            left_dirty = false;  // not dirty, but we have expanded value
        }

        // Check right operand (binary ops only)
        std::string right_expanded;
        if (has_binary && pp.right_idx < condition.size() && !condition[pp.right_idx].quoted &&
            arg_has_varref(condition[pp.right_idx])) {
            auto [dirty, val] = check_operand(pp.right_idx);
            if (dirty) {
                return evaluate_condition_with_filtering(interp, condition, row, col, offset, length);
            }
            right_expanded = std::move(val);
        }

        // Create temp arguments for expanded values (Rule 4: must outlive refs into them)
        Argument left_temp, right_temp;
        const Argument* left_arg = &condition[pp.left_idx];
        const Argument* right_arg = has_binary ? &condition[pp.right_idx] : nullptr;

        if (!left_expanded.empty()) {
            left_temp.quoted = false;
            left_temp.parts.push_back(std::move(left_expanded));
            left_arg = &left_temp;
        }
        if (has_binary && !right_expanded.empty()) {
            right_temp.quoted = false;
            right_temp.parts.push_back(std::move(right_expanded));
            right_arg = &right_temp;
        }

        // Now dispatch with the (possibly replaced) arguments
        bool result = false;

        switch (pp.op) {
        case ConditionOp::BoolCheck: {
            // BoolCheck uses get_variable_view (avoids string copy) — Rule 3
            std::string buffer;
            const std::string& token = get_token_string(interp, *left_arg, buffer);

            if (is_boolean_constant_ci(token)) {
                result = !Interpreter::is_falsy(token);
            } else if (is_numeric_constant(token)) {
                result = !Interpreter::is_falsy(token);
            } else if (left_arg->quoted) {
                result = false;
            } else {
                auto view = interp.get_variable_view(token);
                result = view.has_value() && !Interpreter::is_falsy(*view);
            }
            break;
        }

        case ConditionOp::Defined: {
            std::string buffer;
            const std::string& var_name = get_token_string(interp, *left_arg, buffer);
            if (var_name.size() > 6 && var_name.compare(0, 6, "CACHE{") == 0 && var_name.back() == '}') {
                std::string cache_var = var_name.substr(6, var_name.size() - 7);
                result = interp.get_cache_variables().contains(cache_var);
            } else {
                result = interp.is_variable_set(var_name);
            }
            break;
        }

        case ConditionOp::Target: {
            std::string buffer;
            const std::string& name = get_token_string(interp, *left_arg, buffer);
            result = interp.find_target(name) != nullptr;
            break;
        }

        case ConditionOp::Exists: {
            std::string path = interp.evaluate_argument(*left_arg);
            result = interp.cached_file_exists(path);
            break;
        }

        case ConditionOp::IsDirectory: {
            std::string path = interp.evaluate_argument(*left_arg);
            result = interp.cached_is_directory(path);
            break;
        }

        case ConditionOp::IsAbsolute: {
            std::string path = interp.evaluate_argument(*left_arg);
            result = std::filesystem::path(path).is_absolute();
            break;
        }

        case ConditionOp::IsSymlink: {
            std::string path = interp.evaluate_argument(*left_arg);
            result = std::filesystem::is_symlink(path);
            break;
        }

        case ConditionOp::Command: {
            std::string buffer;
            auto name = eval_operand_sv(interp, *left_arg, buffer);
            result = interp.has_user_function(std::string(name));
            break;
        }

        default: {
            // Binary ops
            std::string buf_l, buf_r;
            auto left_val = eval_operand_sv(interp, *left_arg, buf_l);
            auto right_val = eval_operand_sv(interp, *right_arg, buf_r);

            switch (pp.op) {
            case ConditionOp::BinaryEqual:
            case ConditionOp::BinaryNotEqual:
            case ConditionOp::BinaryLess:
            case ConditionOp::BinaryGreater:
            case ConditionOp::BinaryLessEqual:
            case ConditionOp::BinaryGreaterEqual: {
                int cmp;
                if (try_numeric_compare(left_val, right_val, cmp)) {
                    result = apply_numeric_op(pp.op, cmp);
                } else {
                    if (pp.op == ConditionOp::BinaryEqual) result = left_val == right_val;
                    else if (pp.op == ConditionOp::BinaryNotEqual) result = left_val != right_val;
                    else result = false;
                }
                break;
            }
            case ConditionOp::BinaryStrEqual: result = left_val == right_val; break;
            case ConditionOp::BinaryStrLess: result = left_val < right_val; break;
            case ConditionOp::BinaryStrGreater: result = left_val > right_val; break;
            case ConditionOp::BinaryStrLessEqual: result = left_val <= right_val; break;
            case ConditionOp::BinaryStrGreaterEqual: result = left_val >= right_val; break;

            case ConditionOp::BinaryVersionEqual:
            case ConditionOp::BinaryVersionLess:
            case ConditionOp::BinaryVersionGreater:
            case ConditionOp::BinaryVersionLessEqual:
            case ConditionOp::BinaryVersionGreaterEqual: {
                int cmp = compare_versions(std::string(left_val), std::string(right_val));
                switch (pp.op) {
                case ConditionOp::BinaryVersionEqual: result = cmp == 0; break;
                case ConditionOp::BinaryVersionLess: result = cmp < 0; break;
                case ConditionOp::BinaryVersionGreater: result = cmp > 0; break;
                case ConditionOp::BinaryVersionLessEqual: result = cmp <= 0; break;
                case ConditionOp::BinaryVersionGreaterEqual: result = cmp >= 0; break;
                default: break;
                }
                break;
            }

            case ConditionOp::BinaryMatches: {
                auto mr = evaluate_matches(interp, std::string(left_val), std::string(right_val));
                if (!mr.error.empty()) {
                    interp.set_fatal_error(mr.error);
                    return std::unexpected(*interp.get_fatal_error());
                }
                result = mr.matched;
                break;
            }

            case ConditionOp::BinaryInList:
                result = cmake_list_contains(std::string(right_val), std::string(left_val));
                break;

            case ConditionOp::BinaryIsNewerThan: {
                std::string lpath = interp.evaluate_argument(*left_arg);
                std::string rpath = interp.evaluate_argument(*right_arg);
                std::error_code ec1, ec2;
                auto time1 = std::filesystem::last_write_time(lpath, ec1);
                auto time2 = std::filesystem::last_write_time(rpath, ec2);
                if (ec1 || ec2) result = true;
                else result = time1 >= time2;
                break;
            }

            default: break;
            }
            break;
        }
        }

        if (pp.negated()) result = !result;
        return result;
    }

    // Non-dynamic fast path (no varrefs in operands — most common)
    bool result = false;

    switch (pp.op) {
    case ConditionOp::BoolCheck: {
        const Argument& arg = condition[pp.left_idx];
        std::string buffer;
        const std::string& token = get_token_string(interp, arg, buffer);

        if (is_boolean_constant_ci(token)) {
            result = !Interpreter::is_falsy(token);
        } else if (is_numeric_constant(token)) {
            result = !Interpreter::is_falsy(token);
        } else if (arg.quoted) {
            result = false;
        } else {
            // BoolCheck: undefined → falsy. Use get_variable_view to avoid string copy.
            auto view = interp.get_variable_view(token);
            result = view.has_value() && !Interpreter::is_falsy(*view);
        }
        break;
    }

    case ConditionOp::Defined: {
        std::string buffer;
        const std::string& var_name = get_token_string(interp, condition[pp.left_idx], buffer);
        if (var_name.size() > 6 && var_name.compare(0, 6, "CACHE{") == 0 && var_name.back() == '}') {
            std::string cache_var = var_name.substr(6, var_name.size() - 7);
            result = interp.get_cache_variables().contains(cache_var);
        } else {
            result = interp.is_variable_set(var_name);
        }
        break;
    }

    case ConditionOp::Target: {
        std::string buffer;
        const std::string& name = get_token_string(interp, condition[pp.left_idx], buffer);
        result = interp.find_target(name) != nullptr;
        break;
    }

    case ConditionOp::Exists: {
        std::string path = interp.evaluate_argument(condition[pp.left_idx]);
        result = interp.cached_file_exists(path);
        break;
    }

    case ConditionOp::IsDirectory: {
        std::string path = interp.evaluate_argument(condition[pp.left_idx]);
        result = interp.cached_is_directory(path);
        break;
    }

    case ConditionOp::IsAbsolute: {
        std::string path = interp.evaluate_argument(condition[pp.left_idx]);
        result = std::filesystem::path(path).is_absolute();
        break;
    }

    case ConditionOp::IsSymlink: {
        std::string path = interp.evaluate_argument(condition[pp.left_idx]);
        result = std::filesystem::is_symlink(path);
        break;
    }

    case ConditionOp::Command: {
        std::string buffer;
        auto name = eval_operand_sv(interp, condition[pp.left_idx], buffer);
        result = interp.has_user_function(std::string(name));
        break;
    }

    default: {
        // Binary ops — use separate buffers (Rule 2)
        std::string buf_l, buf_r;
        auto left = eval_operand_sv(interp, condition[pp.left_idx], buf_l);
        auto right = eval_operand_sv(interp, condition[pp.right_idx], buf_r);

        switch (pp.op) {
        case ConditionOp::BinaryEqual:
        case ConditionOp::BinaryNotEqual:
        case ConditionOp::BinaryLess:
        case ConditionOp::BinaryGreater:
        case ConditionOp::BinaryLessEqual:
        case ConditionOp::BinaryGreaterEqual: {
            int cmp;
            if (try_numeric_compare(left, right, cmp)) {
                result = apply_numeric_op(pp.op, cmp);
            } else {
                if (pp.op == ConditionOp::BinaryEqual) result = left == right;
                else if (pp.op == ConditionOp::BinaryNotEqual) result = left != right;
                else result = false;
            }
            break;
        }
        case ConditionOp::BinaryStrEqual: result = left == right; break;
        case ConditionOp::BinaryStrLess: result = left < right; break;
        case ConditionOp::BinaryStrGreater: result = left > right; break;
        case ConditionOp::BinaryStrLessEqual: result = left <= right; break;
        case ConditionOp::BinaryStrGreaterEqual: result = left >= right; break;

        case ConditionOp::BinaryVersionEqual:
        case ConditionOp::BinaryVersionLess:
        case ConditionOp::BinaryVersionGreater:
        case ConditionOp::BinaryVersionLessEqual:
        case ConditionOp::BinaryVersionGreaterEqual: {
            int cmp = compare_versions(std::string(left), std::string(right));
            switch (pp.op) {
            case ConditionOp::BinaryVersionEqual: result = cmp == 0; break;
            case ConditionOp::BinaryVersionLess: result = cmp < 0; break;
            case ConditionOp::BinaryVersionGreater: result = cmp > 0; break;
            case ConditionOp::BinaryVersionLessEqual: result = cmp <= 0; break;
            case ConditionOp::BinaryVersionGreaterEqual: result = cmp >= 0; break;
            default: break;
            }
            break;
        }

        case ConditionOp::BinaryMatches: {
            auto mr = evaluate_matches(interp, std::string(left), std::string(right));
            if (!mr.error.empty()) {
                interp.set_fatal_error(mr.error);
                return std::unexpected(*interp.get_fatal_error());
            }
            result = mr.matched;
            break;
        }

        case ConditionOp::BinaryInList:
            result = cmake_list_contains(std::string(right), std::string(left));
            break;

        case ConditionOp::BinaryIsNewerThan: {
            std::string lpath = interp.evaluate_argument(condition[pp.left_idx]);
            std::string rpath = interp.evaluate_argument(condition[pp.right_idx]);
            std::error_code ec1, ec2;
            auto time1 = std::filesystem::last_write_time(lpath, ec1);
            auto time2 = std::filesystem::last_write_time(rpath, ec2);
            if (ec1 || ec2) result = true;
            else result = time1 >= time2;
            break;
        }

        default: break;
        }
        break;
    }
    }

    if (pp.negated()) result = !result;
    return result;
}

} // namespace dmake
