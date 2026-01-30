#pragma once

#include "genex_parser.hpp"
#include "language.hpp"
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dmake {

// Forward declaration
class Target;

// Result of evaluating a link library entry
// Carries semantic metadata alongside the evaluated value
struct LinkLibraryResult {
    std::string value;           // The evaluated library name/path
    bool link_only = false;      // $<LINK_ONLY:...> - don't propagate INTERFACE properties
    // Future extensibility:
    // bool compile_only = false;  // $<COMPILE_ONLY:...> if CMake adds it
};

// Context for evaluating generator expressions
struct GenexEvaluationContext {
    std::string build_type;           // CMAKE_BUILD_TYPE
    std::string system_name;          // CMAKE_SYSTEM_NAME
    std::string cxx_compiler_id;      // CMAKE_CXX_COMPILER_ID
    std::string c_compiler_id;        // CMAKE_C_COMPILER_ID
    std::optional<Language> compile_language;  // For per-source evaluation
    const std::map<std::string, std::shared_ptr<Target>>* all_targets = nullptr;
    const Target* current_target = nullptr;     // For error messages
    enum class Phase { BUILD, INSTALL } phase = Phase::BUILD;
    bool allow_deferred_compile_language = false;  // For deferred evaluation
};

// Evaluator for generator expressions
class GenexEvaluator {
public:
    explicit GenexEvaluator(const GenexEvaluationContext& ctx) : ctx_(ctx) {}

    // Evaluate a single property value that may contain genex
    std::expected<std::string, std::string> evaluate(const std::string& input);

    // Evaluate a list of property values
    std::expected<std::vector<std::string>, std::string> evaluate_property_list(
        const std::vector<std::string>& values);

    // Evaluate a link library entry, returning structured result with metadata
    // Use this for LINK_LIBRARIES to properly handle $<LINK_ONLY:...>
    std::expected<LinkLibraryResult, std::string> evaluate_link_library(const std::string& input);

private:
    GenexEvaluationContext ctx_;

    // Evaluate a parsed genex node
    std::expected<std::string, std::string> evaluate_node(const GenexNode& node);

    // Helper: Evaluate nodes and concatenate results
    std::expected<std::string, std::string> evaluate_nodes(
        const std::vector<std::shared_ptr<GenexNode>>& nodes);

    // Helper: Check if a string is "truthy" using CMake semantics
    bool is_truthy(const std::string& value) const;

    // Helper: Normalize case for case-insensitive comparisons
    std::string to_lower(const std::string& str) const;

    // Helper: Compare versions (-1 if v1 < v2, 0 if equal, 1 if v1 > v2)
    int compare_versions(const std::string& v1, const std::string& v2) const;
};

} // namespace dmake
