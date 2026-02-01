#pragma once

#include <expected>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace dmake {

// Generator expression node types
enum class GenexNodeType {
    LITERAL,              // Plain text, no genex
    BUILD_INTERFACE,      // $<BUILD_INTERFACE:...>
    INSTALL_INTERFACE,    // $<INSTALL_INTERFACE:...>
    LINK_ONLY,            // $<LINK_ONLY:...> - link but don't propagate INTERFACE properties
    CONFIG,               // $<CONFIG:cfg>
    BOOL,                 // $<BOOL:string>
    IF,                   // $<IF:cond,true_val,false_val>
    AND,                  // $<AND:...>
    OR,                   // $<OR:...>
    NOT,                  // $<NOT:...>
    STREQUAL,             // $<STREQUAL:a,b>
    VERSION_LESS,         // $<VERSION_LESS:v1,v2>
    VERSION_GREATER,      // $<VERSION_GREATER:v1,v2>
    VERSION_EQUAL,        // $<VERSION_EQUAL:v1,v2>
    VERSION_LESS_EQUAL,   // $<VERSION_LESS_EQUAL:v1,v2>
    VERSION_GREATER_EQUAL, // $<VERSION_GREATER_EQUAL:v1,v2>
    TARGET_EXISTS,        // $<TARGET_EXISTS:target>
    TARGET_FILE,          // $<TARGET_FILE:target> - full path to target output
    TARGET_FILE_NAME,     // $<TARGET_FILE_NAME:target> - filename of target output
    TARGET_FILE_DIR,      // $<TARGET_FILE_DIR:target> - directory of target output
    TARGET_OBJECTS,       // $<TARGET_OBJECTS:target> - object files from OBJECT_LIBRARY
    TARGET_PROPERTY,      // $<TARGET_PROPERTY:tgt,prop> or $<TARGET_PROPERTY:prop>
    COMPILE_LANGUAGE,     // $<COMPILE_LANGUAGE:lang>
    COMPILE_LANG_AND_ID,  // $<COMPILE_LANG_AND_ID:lang,id1,id2,...>
    PLATFORM_ID,          // $<PLATFORM_ID:platform>
    CXX_COMPILER_ID,      // $<CXX_COMPILER_ID:id>
    C_COMPILER_ID,        // $<C_COMPILER_ID:id>
    CONDITIONAL,          // $<cond:text> where cond is a genex
    UNSUPPORTED           // Unknown or unsupported genex type
};

// AST node for generator expressions
struct GenexNode {
    GenexNodeType type;
    std::string raw_content;  // For literals, or original genex string if UNSUPPORTED
    std::vector<std::shared_ptr<GenexNode>> children;
    size_t start_pos = 0;
    size_t end_pos = 0;

    GenexNode(GenexNodeType t, std::string content = "")
        : type(t), raw_content(std::move(content)) {}
};

// Result of parsing a property value
struct GenexParseResult {
    std::vector<std::shared_ptr<GenexNode>> nodes;  // Mix of literals + genex
    bool has_genex = false;  // Optimization: skip evaluation if false
};

// Parser for CMake generator expressions
class GenexParser {
public:
    GenexParser() = default;

    // Quick check if string contains any generator expression (without parsing)
    // Use this to skip validation/processing for strings that can't contain genex
    static bool contains_genex(const std::string& input) {
        return input.find("$<") != std::string::npos;
    }

    // Parse a property value that may contain genex
    std::expected<GenexParseResult, std::string> parse(const std::string& input);

    // Validation API (shared by both error handling layers)
    // Returns error if input contains unsupported genex types or malformed syntax
    static std::expected<void, std::string> validate_genex_support(const std::string& input);

    // Extract all genex types found in input (for introspection)
    static std::expected<std::set<GenexNodeType>, std::string> extract_genex_types(const std::string& input);

    // Split comma-separated arguments, respecting nested genex (public for evaluator)
    std::vector<std::string> split_genex_args(const std::string& content);

private:
    std::string input_;
    size_t pos_ = 0;

    // Parse a single genex starting at current position (expects to be at '$<')
    std::expected<std::shared_ptr<GenexNode>, std::string> parse_genex();

    // Parse the interior of a genex (after the keyword and ':')
    // Returns the balanced content up to the closing '>'
    std::expected<std::string, std::string> parse_genex_content();

    // Determine genex type from keyword
    GenexNodeType classify_genex_type(const std::string& keyword) const;

    // Check if we're at end of input
    bool at_end() const { return pos_ >= input_.size(); }

    // Peek at current character
    char peek() const { return at_end() ? '\0' : input_[pos_]; }

    // Advance position
    void advance() { if (!at_end()) ++pos_; }

    // Get substring from start to current position
    std::string substr_from(size_t start) const {
        return input_.substr(start, pos_ - start);
    }
};

} // namespace dmake
