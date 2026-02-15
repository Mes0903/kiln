#include "genex_evaluator.hpp"
#include "interperter.hpp"
#include "target.hpp"
#include "CMakeArray.hpp"
#include "language.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace dmake {

std::string GenexEvaluator::to_lower(const std::string& str) const {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Helper function for version comparison
// Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
// Note: This uses simplified lexicographic comparison
// Real CMake splits on '.' and compares components numerically
int GenexEvaluator::compare_versions(const std::string& v1, const std::string& v2) const {
    if (v1 == v2) return 0;
    return std::lexicographical_compare(v1.begin(), v1.end(), v2.begin(), v2.end()) ? -1 : 1;
}

Target* GenexEvaluator::find_target(const std::string& name) const {
    auto it = ctx_.all_targets->find(name);
    if (it != ctx_.all_targets->end()) return it->second.get();
    if (ctx_.target_aliases) {
        auto alias_it = ctx_.target_aliases->find(name);
        if (alias_it != ctx_.target_aliases->end()) {
            it = ctx_.all_targets->find(alias_it->second);
            if (it != ctx_.all_targets->end()) return it->second.get();
        }
    }
    return nullptr;
}

bool GenexEvaluator::is_truthy(const std::string& value) const {
    // Inverse of Interpreter::is_falsy — avoids to_lower allocation
    return !Interpreter::is_falsy(value);
}

std::expected<std::string, std::string> GenexEvaluator::evaluate_nodes(
    const std::vector<std::shared_ptr<GenexNode>>& nodes) {
    std::string result;
    for (const auto& node : nodes) {
        auto eval_result = evaluate_node(*node);
        if (!eval_result) {
            return eval_result;
        }
        result += *eval_result;
    }
    return result;
}

std::expected<std::string, std::string> GenexEvaluator::evaluate_node(const GenexNode& node) {
    switch (node.type) {
        case GenexNodeType::LITERAL:
            return node.raw_content;

        case GenexNodeType::BUILD_INTERFACE:
            if (ctx_.phase == GenexEvaluationContext::Phase::BUILD) {
                return evaluate_nodes(node.children);
            }
            return std::string("");  // Empty for INSTALL phase

        case GenexNodeType::INSTALL_INTERFACE:
            if (ctx_.phase == GenexEvaluationContext::Phase::INSTALL) {
                return evaluate_nodes(node.children);
            }
            return std::string("");  // Empty for BUILD phase

        case GenexNodeType::LINK_ONLY:
            // LINK_ONLY evaluates to its content - the semantic meaning
            // (skip INTERFACE propagation) is handled by evaluate_link_library()
            return evaluate_nodes(node.children);

        case GenexNodeType::INSTALL_PREFIX:
            // $<INSTALL_PREFIX> - resolves to CMAKE_INSTALL_PREFIX
            return ctx_.install_prefix.empty() ? "/usr/local" : ctx_.install_prefix;

        case GenexNodeType::CONFIG: {
            // $<CONFIG:cfg> returns 1 if CMAKE_BUILD_TYPE matches (case-insensitive), 0 otherwise
            std::string config = to_lower(node.raw_content);
            std::string build_type = to_lower(ctx_.build_type);
            return (config == build_type) ? "1" : "0";
        }

        case GenexNodeType::BOOL: {
            // $<BOOL:string> returns 1 if truthy, 0 otherwise
            // If there are children (nested genex), evaluate them first
            if (!node.children.empty()) {
                auto value_result = evaluate_nodes(node.children);
                if (!value_result) {
                    return value_result;
                }
                return is_truthy(*value_result) ? "1" : "0";
            }
            // No children, use raw_content directly
            return is_truthy(node.raw_content) ? "1" : "0";
        }

        case GenexNodeType::IF: {
            // $<IF:cond,true_val,false_val>
            // Children should be organized as: condition nodes, then true_val nodes, then false_val nodes
            // But we need to split by commas in the original content
            auto args = GenexParser().split_genex_args(node.raw_content);
            if (args.size() != 3) {
                return std::unexpected("$<IF:...> requires exactly 3 arguments");
            }

            // Evaluate condition
            GenexParser parser;
            auto cond_result = parser.parse(args[0]);
            if (!cond_result) {
                return std::unexpected(cond_result.error());
            }
            auto cond_val = evaluate_nodes(cond_result->nodes);
            if (!cond_val) {
                return cond_val;
            }

            // Evaluate true or false branch based on condition
            if (is_truthy(*cond_val)) {
                auto true_result = parser.parse(args[1]);
                if (!true_result) {
                    return std::unexpected(true_result.error());
                }
                return evaluate_nodes(true_result->nodes);
            } else {
                auto false_result = parser.parse(args[2]);
                if (!false_result) {
                    return std::unexpected(false_result.error());
                }
                return evaluate_nodes(false_result->nodes);
            }
        }

        case GenexNodeType::AND: {
            // $<AND:expr1,expr2,...> returns 1 if all expressions are truthy, 0 otherwise
            auto args = GenexParser().split_genex_args(node.raw_content);
            GenexParser parser;

            for (const auto& arg : args) {
                auto arg_result = parser.parse(arg);
                if (!arg_result) {
                    return std::unexpected(arg_result.error());
                }
                auto arg_val = evaluate_nodes(arg_result->nodes);
                if (!arg_val) {
                    return arg_val;
                }
                if (!is_truthy(*arg_val)) {
                    return "0";
                }
            }
            return "1";
        }

        case GenexNodeType::OR: {
            // $<OR:expr1,expr2,...> returns 1 if any expression is truthy, 0 otherwise
            auto args = GenexParser().split_genex_args(node.raw_content);
            GenexParser parser;

            for (const auto& arg : args) {
                auto arg_result = parser.parse(arg);
                if (!arg_result) {
                    return std::unexpected(arg_result.error());
                }
                auto arg_val = evaluate_nodes(arg_result->nodes);
                if (!arg_val) {
                    return arg_val;
                }
                if (is_truthy(*arg_val)) {
                    return "1";
                }
            }
            return "0";
        }

        case GenexNodeType::NOT: {
            // $<NOT:expr> returns 1 if expr is falsy, 0 if truthy
            if (!node.children.empty()) {
                auto val_result = evaluate_nodes(node.children);
                if (!val_result) {
                    return val_result;
                }
                return is_truthy(*val_result) ? "0" : "1";
            }
            // No children, use raw_content directly
            return is_truthy(node.raw_content) ? "0" : "1";
        }

        case GenexNodeType::STREQUAL: {
            // $<STREQUAL:a,b> returns 1 if strings are equal, 0 otherwise
            auto args = GenexParser().split_genex_args(node.raw_content);
            if (args.size() != 2) {
                return std::unexpected("$<STREQUAL:...> requires exactly 2 arguments");
            }

            GenexParser parser;
            auto arg1_result = parser.parse(args[0]);
            if (!arg1_result) {
                return std::unexpected(arg1_result.error());
            }
            auto arg1_val = evaluate_nodes(arg1_result->nodes);
            if (!arg1_val) {
                return arg1_val;
            }

            auto arg2_result = parser.parse(args[1]);
            if (!arg2_result) {
                return std::unexpected(arg2_result.error());
            }
            auto arg2_val = evaluate_nodes(arg2_result->nodes);
            if (!arg2_val) {
                return arg2_val;
            }

            return (*arg1_val == *arg2_val) ? "1" : "0";
        }

        case GenexNodeType::VERSION_LESS:
        case GenexNodeType::VERSION_GREATER:
        case GenexNodeType::VERSION_EQUAL:
        case GenexNodeType::VERSION_LESS_EQUAL:
        case GenexNodeType::VERSION_GREATER_EQUAL: {
            // Version comparison operators: $<VERSION_LESS:v1,v2> etc.
            auto args = GenexParser().split_genex_args(node.raw_content);
            if (args.size() != 2) {
                return std::unexpected("Version comparison requires exactly 2 arguments");
            }

            GenexParser parser;
            auto arg1_result = parser.parse(args[0]);
            if (!arg1_result) {
                return std::unexpected(arg1_result.error());
            }
            auto arg1_val = evaluate_nodes(arg1_result->nodes);
            if (!arg1_val) {
                return arg1_val;
            }

            auto arg2_result = parser.parse(args[1]);
            if (!arg2_result) {
                return std::unexpected(arg2_result.error());
            }
            auto arg2_val = evaluate_nodes(arg2_result->nodes);
            if (!arg2_val) {
                return arg2_val;
            }

            int cmp = compare_versions(*arg1_val, *arg2_val);

            switch (node.type) {
                case GenexNodeType::VERSION_LESS:
                    return (cmp < 0) ? "1" : "0";
                case GenexNodeType::VERSION_GREATER:
                    return (cmp > 0) ? "1" : "0";
                case GenexNodeType::VERSION_EQUAL:
                    return (cmp == 0) ? "1" : "0";
                case GenexNodeType::VERSION_LESS_EQUAL:
                    return (cmp <= 0) ? "1" : "0";
                case GenexNodeType::VERSION_GREATER_EQUAL:
                    return (cmp >= 0) ? "1" : "0";
                default:
                    return std::unexpected("Unknown version comparison operator");
            }
        }

        case GenexNodeType::TARGET_EXISTS: {
            // $<TARGET_EXISTS:target> returns 1 if target exists, 0 otherwise
            if (!ctx_.all_targets) {
                return std::unexpected("TARGET_EXISTS requires all_targets context");
            }
            return find_target(node.raw_content) ? "1" : "0";
        }

        case GenexNodeType::TARGET_NAME_IF_EXISTS: {
            // $<TARGET_NAME_IF_EXISTS:target> returns target name if exists, empty string otherwise
            if (!ctx_.all_targets) {
                return std::unexpected("TARGET_NAME_IF_EXISTS requires all_targets context");
            }
            return find_target(node.raw_content) ? node.raw_content : "";
        }

        case GenexNodeType::TARGET_FILE: {
            // $<TARGET_FILE:target> returns full path to target output
            if (!ctx_.all_targets) {
                return std::unexpected("TARGET_FILE requires all_targets context");
            }
            auto* target = find_target(node.raw_content);
            if (!target) {
                return std::unexpected("TARGET_FILE: target '" + node.raw_content + "' not found");
            }
            return target->get_output_path();
        }

        case GenexNodeType::TARGET_FILE_NAME: {
            // $<TARGET_FILE_NAME:target> returns filename of target output
            if (!ctx_.all_targets) {
                return std::unexpected("TARGET_FILE_NAME requires all_targets context");
            }
            auto* target = find_target(node.raw_content);
            if (!target) {
                return std::unexpected("TARGET_FILE_NAME: target '" + node.raw_content + "' not found");
            }
            std::filesystem::path output_path(target->get_output_path());
            return output_path.filename().string();
        }

        case GenexNodeType::TARGET_FILE_DIR: {
            // $<TARGET_FILE_DIR:target> returns directory of target output
            if (!ctx_.all_targets) {
                return std::unexpected("TARGET_FILE_DIR requires all_targets context");
            }
            auto* target = find_target(node.raw_content);
            if (!target) {
                return std::unexpected("TARGET_FILE_DIR: target '" + node.raw_content + "' not found");
            }
            std::filesystem::path output_path(target->get_output_path());
            return output_path.parent_path().string();
        }

        case GenexNodeType::TARGET_OBJECTS: {
            // $<TARGET_OBJECTS:target> returns object files from OBJECT_LIBRARY
            if (!ctx_.all_targets) {
                return std::unexpected("TARGET_OBJECTS requires all_targets context");
            }
            auto* target = find_target(node.raw_content);
            if (!target) {
                return std::unexpected("TARGET_OBJECTS: target '" + node.raw_content + "' not found");
            }
            // Since CMake 3.21, TARGET_OBJECTS works on any library with compiled sources
            auto ttype = target->get_type();
            if (ttype == TargetType::INTERFACE_LIBRARY || ttype == TargetType::CUSTOM) {
                return std::unexpected("TARGET_OBJECTS: target '" + node.raw_content + "' has no compiled sources");
            }

            // Collect object file paths for all sources in the target
            std::string result;
            auto sources = target->get_property_list("SOURCES", TargetPropertyScope::BUILD);
            std::string binary_dir = target->get_binary_dir();
            std::string target_name = target->get_name();
            std::string source_dir = target->get_source_dir();

            for (const auto& src : sources) {
                // Skip genex in source paths (they would need recursive evaluation)
                if (GenexParser::contains_genex(src)) {
                    continue;
                }

                // Skip header files - they don't produce object files
                auto lang_info = LanguageClassifier::from_path(src);
                if (lang_info.is_header || !lang_info.is_compileable) {
                    continue;
                }

                // Skip sources marked HEADER_FILE_ONLY (e.g. unity build originals)
                if (ctx_.source_properties) {
                    std::filesystem::path sp(src);
                    std::string abs = sp.is_absolute() ? sp.lexically_normal().string()
                        : (std::filesystem::path(source_dir) / sp).lexically_normal().string();
                    auto sp_it = ctx_.source_properties->find(abs);
                    if (sp_it != ctx_.source_properties->end()) {
                        auto hfo = sp_it->second.find("HEADER_FILE_ONLY");
                        if (hfo != sp_it->second.end() &&
                            hfo->second != "0" && hfo->second != "OFF" &&
                            hfo->second != "FALSE" && hfo->second != "NO" &&
                            hfo->second != "N" && !hfo->second.empty()) {
                            continue;
                        }
                    }
                }

                std::string obj_str = get_obj_path(binary_dir, target_name, src);

                if (!result.empty()) {
                    result += ";";
                }
                result += obj_str;
            }
            return result;
        }

        case GenexNodeType::TARGET_PROPERTY: {
            // $<TARGET_PROPERTY:tgt,prop> or $<TARGET_PROPERTY:prop>
            auto args = GenexParser().split_genex_args(node.raw_content);
            if (args.size() != 1 && args.size() != 2) {
                return std::unexpected("$<TARGET_PROPERTY:...> requires 1 or 2 arguments");
            }

            if (!ctx_.all_targets) {
                return std::unexpected("TARGET_PROPERTY requires all_targets context");
            }

            GenexParser parser;
            std::string target_name;
            std::string property_name;

            if (args.size() == 1) {
                // Single argument: property name, use current target
                if (!ctx_.current_target) {
                    return std::unexpected("TARGET_PROPERTY with 1 argument requires current_target context");
                }
                target_name = ctx_.current_target->get_name();

                auto prop_result = parser.parse(args[0]);
                if (!prop_result) {
                    return std::unexpected(prop_result.error());
                }
                auto prop_val = evaluate_nodes(prop_result->nodes);
                if (!prop_val) {
                    return prop_val;
                }
                property_name = *prop_val;
            } else {
                // Two arguments: target name, property name
                auto tgt_result = parser.parse(args[0]);
                if (!tgt_result) {
                    return std::unexpected(tgt_result.error());
                }
                auto tgt_val = evaluate_nodes(tgt_result->nodes);
                if (!tgt_val) {
                    return tgt_val;
                }
                target_name = *tgt_val;

                auto prop_result = parser.parse(args[1]);
                if (!prop_result) {
                    return std::unexpected(prop_result.error());
                }
                auto prop_val = evaluate_nodes(prop_result->nodes);
                if (!prop_val) {
                    return prop_val;
                }
                property_name = *prop_val;
            }

            // Look up the target (resolving aliases)
            auto* target = find_target(target_name);
            if (!target) {
                return std::unexpected("TARGET_PROPERTY: target '" + target_name + "' not found");
            }

            // Get the property value (checks list properties then generic)
            std::string prop_value = target->get_property_combined(property_name);

            // Recursively evaluate any nested genex in individual list elements
            if (prop_value.find("$<") != std::string::npos) {
                std::string result;
                for (auto sv : CMakeArrayView(prop_value)) {
                    auto eval = evaluate(std::string(sv));
                    if (!eval) return eval;
                    if (!eval->empty()) {
                        if (!result.empty()) result += ';';
                        result += *eval;
                    }
                }
                return result;
            }
            return prop_value;
        }

        case GenexNodeType::COMPILE_LANGUAGE: {
            // $<COMPILE_LANGUAGE:lang> returns 1 if compiling with specified language
            if (ctx_.allow_deferred_compile_language && !ctx_.compile_language) {
                // Return original expression for deferred evaluation
                return "$<COMPILE_LANGUAGE:" + node.raw_content + ">";
            }

            if (!ctx_.compile_language) {
                return std::unexpected("COMPILE_LANGUAGE requires compile_language context");
            }

            std::string lang_upper = node.raw_content;
            std::transform(lang_upper.begin(), lang_upper.end(), lang_upper.begin(),
                         [](unsigned char c) { return std::toupper(c); });

            std::string current_lang;
            switch (*ctx_.compile_language) {
                case Language::C: current_lang = "C"; break;
                case Language::CXX: current_lang = "CXX"; break;
                case Language::CUDA: current_lang = "CUDA"; break;
                default: current_lang = "UNKNOWN"; break;
            }

            return (lang_upper == current_lang) ? "1" : "0";
        }

        case GenexNodeType::COMPILE_LANG_AND_ID: {
            // $<COMPILE_LANG_AND_ID:language,compiler_id1,compiler_id2,...>
            // Returns 1 if:
            //   - compilation language matches the first argument
            //   - AND compiler ID for that language matches any of the remaining arguments
            if (ctx_.allow_deferred_compile_language && !ctx_.compile_language) {
                // Return original expression for deferred evaluation
                return "$<COMPILE_LANG_AND_ID:" + node.raw_content + ">";
            }

            if (!ctx_.compile_language) {
                return std::unexpected("COMPILE_LANG_AND_ID requires compile_language context");
            }

            auto args = GenexParser().split_genex_args(node.raw_content);
            if (args.size() < 2) {
                return std::unexpected("$<COMPILE_LANG_AND_ID:...> requires at least 2 arguments (language,compiler_id)");
            }

            // First argument is the language
            std::string lang_upper = args[0];
            std::transform(lang_upper.begin(), lang_upper.end(), lang_upper.begin(),
                         [](unsigned char c) { return std::toupper(c); });

            std::string current_lang;
            switch (*ctx_.compile_language) {
                case Language::C: current_lang = "C"; break;
                case Language::CXX: current_lang = "CXX"; break;
                case Language::CUDA: current_lang = "CUDA"; break;
                default: current_lang = "UNKNOWN"; break;
            }

            // If language doesn't match, return 0
            if (lang_upper != current_lang) {
                return "0";
            }

            // Get the compiler ID for the matched language
            std::string compiler_id;
            if (current_lang == "C") {
                compiler_id = ctx_.c_compiler_id;
            } else if (current_lang == "CXX") {
                compiler_id = ctx_.cxx_compiler_id;
            } else {
                // For other languages, we don't have compiler ID in context yet
                return "0";
            }

            // Check if compiler ID matches any of the provided IDs
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == compiler_id) {
                    return "1";
                }
            }

            return "0";
        }

        case GenexNodeType::PLATFORM_ID: {
            // $<PLATFORM_ID:platform> returns 1 if CMAKE_SYSTEM_NAME matches
            return (node.raw_content == ctx_.system_name) ? "1" : "0";
        }

        case GenexNodeType::CXX_COMPILER_ID: {
            // $<CXX_COMPILER_ID:id> returns 1 if CMAKE_CXX_COMPILER_ID matches
            return (node.raw_content == ctx_.cxx_compiler_id) ? "1" : "0";
        }

        case GenexNodeType::C_COMPILER_ID: {
            // $<C_COMPILER_ID:id> returns 1 if CMAKE_C_COMPILER_ID matches
            return (node.raw_content == ctx_.c_compiler_id) ? "1" : "0";
        }

        case GenexNodeType::CXX_COMPILER_VERSION: {
            // $<CXX_COMPILER_VERSION> returns CMAKE_CXX_COMPILER_VERSION
            return ctx_.cxx_compiler_version;
        }

        case GenexNodeType::C_COMPILER_VERSION: {
            // $<C_COMPILER_VERSION> returns CMAKE_C_COMPILER_VERSION
            return ctx_.c_compiler_version;
        }

        case GenexNodeType::CONDITIONAL: {
            // $<cond:text> - first child is condition, remaining children are value.
            if (node.children.empty()) {
                return std::unexpected("CONDITIONAL genex requires condition and value");
            }

            auto cond_val = evaluate_node(*node.children[0]);
            if (!cond_val) {
                return cond_val;
            }

            // Deferred genex in condition (e.g. $<COMPILE_LANG_AND_ID:...>):
            // reconstruct the expression for per-source evaluation later.
            if (cond_val->find("$<") != std::string::npos) {
                return "$<" + *cond_val + ":" + node.raw_content + ">";
            }

            if (is_truthy(*cond_val)) {
                std::string result;
                for (size_t i = 1; i < node.children.size(); ++i) {
                    auto val_result = evaluate_node(*node.children[i]);
                    if (!val_result) {
                        return val_result;
                    }
                    result += *val_result;
                }
                return result;
            }

            return std::string("");
        }

        case GenexNodeType::UNSUPPORTED:
            return std::unexpected("Unsupported generator expression: " + node.raw_content);

        default:
            return std::unexpected("Unknown generator expression type");
    }
}

std::expected<std::string, std::string> GenexEvaluator::evaluate(const std::string& input) {
    GenexParser parser;
    auto parse_result = parser.parse(input);
    if (!parse_result) {
        return std::unexpected(parse_result.error());
    }

    // If no genex, return as-is
    if (!parse_result->has_genex) {
        return input;
    }

    return evaluate_nodes(parse_result->nodes);
}

std::expected<std::vector<std::string>, std::string> GenexEvaluator::evaluate_property_list(
    const std::vector<std::string>& values) {
    std::vector<std::string> result;

    // Reassemble fragmented genex: multi-line genex like $<$<BOOL:TRUE>:\n-Wall\n>
    // are stored as separate list items. Join fragments with semicolons until
    // angle brackets balance, matching CMake's behavior.
    std::vector<std::string> reassembled;
    std::string pending;
    int depth = 0;
    for (const auto& value : values) {
        if (depth > 0) {
            // We're inside a fragmented genex, join with semicolon
            pending += ';';
            pending += value;
        } else {
            pending = value;
        }
        // Count unbalanced $< and > in this item
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '$' && i + 1 < value.size() && value[i + 1] == '<') {
                depth++;
                i++; // skip '<'
            } else if (value[i] == '>' && depth > 0) {
                depth--;
            }
        }
        if (depth == 0) {
            reassembled.push_back(std::move(pending));
            pending.clear();
        }
    }
    // If there's still a pending fragment (truly unbalanced), add it as-is
    if (!pending.empty()) {
        reassembled.push_back(std::move(pending));
    }

    for (const auto& value : reassembled) {
        // Parse to check if this is a pure generator expression
        GenexParser parser;
        auto parse_result = parser.parse(value);
        if (!parse_result) {
            return std::unexpected(parse_result.error());
        }

        // Check if the value is ONLY a generator expression (single non-literal node)
        // If so, split the result by whitespace (like unquoted arguments in CMake)
        // Otherwise, keep it as a single value (like quoted arguments or mixed content)
        bool is_pure_genex = parse_result->has_genex &&
                             parse_result->nodes.size() == 1 &&
                             parse_result->nodes[0]->type != GenexNodeType::LITERAL;

        auto eval_result = evaluate(value);
        if (!eval_result) {
            return std::unexpected(eval_result.error());
        }

        // Only add non-empty results
        if (!eval_result->empty()) {
            // If result still contains deferred genex (e.g., $<COMPILE_LANG_AND_ID:...>),
            // don't split it - keep it intact for per-source evaluation
            if (eval_result->find("$<") != std::string::npos) {
                result.push_back(*eval_result);
            } else if (is_pure_genex) {
                // Split the evaluated result by semicolons first (CMake list separator),
                // then by whitespace (treats genex output like unquoted arguments).
                // This allows:
                //   - $<1:-Wall -Wextra> to expand into multiple separate flags
                //   - $<$<BOOL:ON>:${LIST_VAR}> to expand semicolon-separated lists
                for (auto sv : CMakeArrayView(*eval_result)) {
                    // Further split by whitespace
                    std::istringstream iss{std::string(sv)};
                    std::string token;
                    while (iss >> token) {
                        result.push_back(token);
                    }
                }
            } else {
                // Keep as a single value (like a quoted argument or mixed content)
                result.push_back(*eval_result);
            }
        }
    }

    return result;
}

std::expected<LinkLibraryResult, std::string> GenexEvaluator::evaluate_link_library(const std::string& input) {
    GenexParser parser;
    auto parse_result = parser.parse(input);
    if (!parse_result) {
        return std::unexpected(parse_result.error());
    }

    LinkLibraryResult result;

    // Check if the top-level is a LINK_ONLY wrapper
    if (parse_result->nodes.size() == 1 &&
        parse_result->nodes[0]->type == GenexNodeType::LINK_ONLY) {
        result.link_only = true;
        // Evaluate the inner content
        auto inner_result = evaluate_nodes(parse_result->nodes[0]->children);
        if (!inner_result) {
            return std::unexpected(inner_result.error());
        }
        result.value = *inner_result;
    } else {
        // Normal evaluation
        auto eval_result = evaluate_nodes(parse_result->nodes);
        if (!eval_result) {
            return std::unexpected(eval_result.error());
        }
        result.value = *eval_result;
    }

    return result;
}

} // namespace dmake
