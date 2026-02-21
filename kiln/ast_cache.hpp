#pragma once

#include "cmake-language.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace kiln {

// In-memory cache for parsed ASTs of system modules that get included repeatedly.
// Only caches a hardcoded whitelist of shared infrastructure modules that every
// Find*.cmake module includes (e.g., FindPackageHandleStandardArgs.cmake).
class AstCache {
public:
    // Returns true if the file (by basename) is eligible for AST caching.
    static bool is_cacheable(const std::string& abs_path);

    // Look up a cached AST. Returns nullptr if not cached.
    const std::vector<AstNode>* get(const std::string& abs_path) const;

    // Store an AST in the cache. Only stores if is_cacheable().
    void put(const std::string& abs_path, std::vector<AstNode> ast);

private:
    std::unordered_map<std::string, std::vector<AstNode>> cache_;
};

} // namespace kiln
