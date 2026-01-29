#include "genex_parser.hpp"
#include <algorithm>
#include <functional>
#include <sstream>

namespace dmake {

GenexNodeType GenexParser::classify_genex_type(const std::string& keyword) const {
    // Check if keyword is itself a genex (conditional pattern)
    if (keyword.starts_with("$<")) {
        return GenexNodeType::CONDITIONAL;
    }

    if (keyword == "BUILD_INTERFACE") return GenexNodeType::BUILD_INTERFACE;
    if (keyword == "INSTALL_INTERFACE") return GenexNodeType::INSTALL_INTERFACE;
    if (keyword == "LINK_ONLY") return GenexNodeType::LINK_ONLY;
    if (keyword == "CONFIG") return GenexNodeType::CONFIG;
    if (keyword == "BOOL") return GenexNodeType::BOOL;
    if (keyword == "IF") return GenexNodeType::IF;
    if (keyword == "AND") return GenexNodeType::AND;
    if (keyword == "OR") return GenexNodeType::OR;
    if (keyword == "NOT") return GenexNodeType::NOT;
    if (keyword == "STREQUAL") return GenexNodeType::STREQUAL;
    if (keyword == "TARGET_EXISTS") return GenexNodeType::TARGET_EXISTS;
    if (keyword == "COMPILE_LANGUAGE") return GenexNodeType::COMPILE_LANGUAGE;
    if (keyword == "PLATFORM_ID") return GenexNodeType::PLATFORM_ID;
    if (keyword == "CXX_COMPILER_ID") return GenexNodeType::CXX_COMPILER_ID;
    if (keyword == "C_COMPILER_ID") return GenexNodeType::C_COMPILER_ID;

    return GenexNodeType::UNSUPPORTED;
}

std::expected<std::string, std::string> GenexParser::parse_genex_content() {
    // Parse content until we hit the matching '>', balancing angle brackets
    std::string content;
    int depth = 1;  // We're already inside one '<'
    size_t start_pos = pos_;

    while (!at_end() && depth > 0) {
        char c = peek();

        if (c == '<') {
            ++depth;
            content += c;
            advance();
        } else if (c == '>') {
            --depth;
            if (depth > 0) {
                content += c;
            }
            advance();
        } else {
            content += c;
            advance();
        }
    }

    if (depth != 0) {
        return std::unexpected("Unmatched '<' in generator expression at position " + std::to_string(start_pos));
    }

    return content;
}

std::vector<std::string> GenexParser::split_genex_args(const std::string& content) {
    std::vector<std::string> args;
    std::string current;
    int depth = 0;

    for (char c : content) {
        if (c == '<') {
            ++depth;
            current += c;
        } else if (c == '>') {
            --depth;
            current += c;
        } else if (c == ',' && depth == 0) {
            args.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }

    // Always add the last part, even if empty (important for cases like "a," or ",")
    args.push_back(current);

    return args;
}

std::expected<std::shared_ptr<GenexNode>, std::string> GenexParser::parse_genex() {
    size_t start = pos_;

    // Expect "$<"
    if (peek() != '$') {
        return std::unexpected("Expected '$' at position " + std::to_string(pos_));
    }
    advance();

    if (peek() != '<') {
        return std::unexpected("Expected '<' after '$' at position " + std::to_string(pos_));
    }
    advance();

    // Parse until we find ':' or '>' (but balance angle brackets for nested genex)
    std::string keyword;
    int depth = 0;
    while (!at_end()) {
        char c = peek();
        if (c == '<') {
            depth++;
            keyword += c;
            advance();
        } else if (c == '>') {
            if (depth > 0) {
                depth--;
                keyword += c;
                advance();
            } else {
                // This is the closing '>' for our genex
                break;
            }
        } else if (c == ':' && depth == 0) {
            // Found the separator at our level
            break;
        } else {
            keyword += c;
            advance();
        }
    }

    if (at_end()) {
        return std::unexpected("Unexpected end of input in generator expression");
    }

    GenexNodeType type = classify_genex_type(keyword);
    auto node = std::make_shared<GenexNode>(type);
    node->start_pos = start;

    // Handle CONDITIONAL type (keyword is itself a genex)
    if (type == GenexNodeType::CONDITIONAL) {
        // keyword contains the condition genex (e.g., "$<CONFIG:Debug>")
        // Parse it as a nested genex
        GenexParser cond_parser;
        auto cond_result = cond_parser.parse(keyword);
        if (!cond_result) {
            return std::unexpected(cond_result.error());
        }
        // Store condition in children (first set of nodes)
        node->children = cond_result->nodes;

        // Now parse the value part after ':'
        if (peek() == ':') {
            advance();
            auto content_result = parse_genex_content();
            if (!content_result) {
                return std::unexpected(content_result.error());
            }

            // Parse the content as potentially containing more genex
            GenexParser value_parser;
            auto value_result = value_parser.parse(*content_result);
            if (!value_result) {
                return std::unexpected(value_result.error());
            }
            // Append value nodes to children
            node->children.insert(node->children.end(),
                                value_result->nodes.begin(),
                                value_result->nodes.end());
            node->raw_content = *content_result;
        } else if (peek() == '>') {
            advance();
            // No value part, just condition
        }
        node->end_pos = pos_;
        return node;
    }

    // If it's UNSUPPORTED, store the original string for error reporting
    if (type == GenexNodeType::UNSUPPORTED) {
        // Need to capture the full genex string
        size_t content_start = pos_;
        if (peek() == ':') {
            advance();
            auto content_result = parse_genex_content();
            if (!content_result) {
                return std::unexpected(content_result.error());
            }
        } else if (peek() == '>') {
            advance();
        }
        node->end_pos = pos_;
        node->raw_content = "$<" + keyword + ">";  // Simplified representation
        return node;
    }

    // Parse content if present
    if (peek() == ':') {
        advance();
        auto content_result = parse_genex_content();
        if (!content_result) {
            return std::unexpected(content_result.error());
        }

        std::string content = *content_result;
        node->raw_content = content;

        // For certain types, parse arguments recursively
        if (type == GenexNodeType::BUILD_INTERFACE ||
            type == GenexNodeType::INSTALL_INTERFACE ||
            type == GenexNodeType::LINK_ONLY ||
            type == GenexNodeType::NOT) {
            // Single argument that may contain nested genex
            GenexParser inner_parser;
            inner_parser.input_ = content;
            inner_parser.pos_ = 0;
            auto inner_result = inner_parser.parse(content);
            if (!inner_result) {
                return std::unexpected(inner_result.error());
            }
            node->children = inner_result->nodes;
        } else if (type == GenexNodeType::IF) {
            // Three comma-separated arguments
            auto args = split_genex_args(content);
            if (args.size() != 3) {
                return std::unexpected("$<IF:...> requires exactly 3 arguments (condition,true_value,false_value)");
            }

            for (const auto& arg : args) {
                GenexParser inner_parser;
                auto inner_result = inner_parser.parse(arg);
                if (!inner_result) {
                    return std::unexpected(inner_result.error());
                }
                node->children.insert(node->children.end(),
                                    inner_result->nodes.begin(),
                                    inner_result->nodes.end());
            }
        } else if (type == GenexNodeType::AND || type == GenexNodeType::OR) {
            // Multiple comma-separated arguments
            auto args = split_genex_args(content);
            for (const auto& arg : args) {
                GenexParser inner_parser;
                auto inner_result = inner_parser.parse(arg);
                if (!inner_result) {
                    return std::unexpected(inner_result.error());
                }
                node->children.insert(node->children.end(),
                                    inner_result->nodes.begin(),
                                    inner_result->nodes.end());
            }
        } else if (type == GenexNodeType::STREQUAL) {
            // Two comma-separated arguments
            auto args = split_genex_args(content);
            if (args.size() != 2) {
                return std::unexpected("$<STREQUAL:...> requires exactly 2 arguments");
            }

            for (const auto& arg : args) {
                GenexParser inner_parser;
                auto inner_result = inner_parser.parse(arg);
                if (!inner_result) {
                    return std::unexpected(inner_result.error());
                }
                node->children.insert(node->children.end(),
                                    inner_result->nodes.begin(),
                                    inner_result->nodes.end());
            }
        }
        // For simple types (CONFIG, BOOL, etc.), raw_content is sufficient
    } else if (peek() == '>') {
        advance();
        // Empty genex like $<AND> or $<OR> - valid for some types
    } else {
        return std::unexpected("Expected ':' or '>' in generator expression at position " + std::to_string(pos_));
    }

    node->end_pos = pos_;
    return node;
}

std::expected<GenexParseResult, std::string> GenexParser::parse(const std::string& input) {
    input_ = input;
    pos_ = 0;

    GenexParseResult result;
    std::string literal;

    while (!at_end()) {
        // Look for "$<"
        if (peek() == '$' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '<') {
            // Save any accumulated literal
            if (!literal.empty()) {
                auto lit_node = std::make_shared<GenexNode>(GenexNodeType::LITERAL, literal);
                result.nodes.push_back(lit_node);
                literal.clear();
            }

            // Parse genex
            auto genex_result = parse_genex();
            if (!genex_result) {
                return std::unexpected(genex_result.error());
            }

            result.nodes.push_back(*genex_result);
            result.has_genex = true;
        } else {
            literal += peek();
            advance();
        }
    }

    // Save any remaining literal
    if (!literal.empty()) {
        auto lit_node = std::make_shared<GenexNode>(GenexNodeType::LITERAL, literal);
        result.nodes.push_back(lit_node);
    }

    // If no nodes, add empty literal
    if (result.nodes.empty()) {
        auto lit_node = std::make_shared<GenexNode>(GenexNodeType::LITERAL, "");
        result.nodes.push_back(lit_node);
    }

    return result;
}

std::expected<void, std::string> GenexParser::validate_genex_support(const std::string& input) {
    GenexParser parser;
    auto result = parser.parse(input);
    if (!result) {
        return std::unexpected(result.error());  // Syntax error
    }

    // Check for unsupported types recursively
    std::function<std::expected<void, std::string>(const GenexNode&)> check_node =
        [&](const GenexNode& node) -> std::expected<void, std::string> {
        if (node.type == GenexNodeType::UNSUPPORTED) {
            return std::unexpected("Unsupported generator expression: " + node.raw_content);
        }
        for (const auto& child : node.children) {
            auto child_result = check_node(*child);
            if (!child_result) {
                return child_result;
            }
        }
        return {};
    };

    for (const auto& node : result->nodes) {
        auto node_result = check_node(*node);
        if (!node_result) {
            return node_result;
        }
    }

    return {};  // All genex are supported
}

std::expected<std::set<GenexNodeType>, std::string> GenexParser::extract_genex_types(const std::string& input) {
    GenexParser parser;
    auto result = parser.parse(input);
    if (!result) {
        return std::unexpected(result.error());
    }

    std::set<GenexNodeType> types;

    std::function<void(const GenexNode&)> collect_types = [&](const GenexNode& node) {
        if (node.type != GenexNodeType::LITERAL) {
            types.insert(node.type);
        }
        for (const auto& child : node.children) {
            collect_types(*child);
        }
    };

    for (const auto& node : result->nodes) {
        collect_types(*node);
    }

    return types;
}

} // namespace dmake
