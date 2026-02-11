#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <expected>
#include <optional>
#include <functional>

namespace dmake {

// Transparent hash/equal for heterogeneous lookup with string_view
// Allows unordered_map<string,...>::find(string_view) without constructing a string
struct TransparentStringHash {
    using is_transparent = void;

    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

struct TransparentStringEqual {
    using is_transparent = void;

    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

/**
 * Shadow Map for efficient variable scoping with O(1) access.
 *
 * Variables are stored with depth-tagged version histories. Each variable
 * maps to a vector of (depth, value) pairs where only the topmost entry
 * is visible. This provides:
 * - O(1) variable access (no parent traversal)
 * - Automatic cleanup on scope exit
 * - Minimal allocations (only when variables are shadowed)
 * - Exception-safe RAII pattern
 */
class ShadowMap {
public:
    ShadowMap() = default;

    /**
     * Get the current value of a variable.
     * Returns empty string if variable is not defined.
     * O(1) complexity.
     */
    const std::string& get(std::string_view name) const {
        static const std::string empty;
        auto it = variables_.find(name);
        if (it == variables_.end() || it->second.empty()) {
            return empty;
        }
        // Check for tombstone (nullopt = unset)
        const auto& val = it->second.back().value;
        if (!val.has_value()) {
            return empty;
        }
        return *val;
    }

    /**
     * Set a variable at the current depth.
     * If the variable already exists at this depth, modifies it.
     * Otherwise, pushes a new version.
     * O(1) amortized complexity.
     */
    void set(const std::string& name, const std::string& value) {
        auto& versions = variables_[name];

        // If already set at current depth, modify in place (no tracking needed)
        if (!versions.empty() && versions.back().depth == current_depth_) {
            versions.back().value = value;  // Assigns to optional
            return;
        }

        // Push new version at current depth - track for cleanup
        versions.push_back({std::optional<std::string>(value), current_depth_});
        if (current_depth_ > 0 && current_depth_ < static_cast<int>(modified_per_depth_.size())) {
            modified_per_depth_[current_depth_].insert(name);
        }
    }

    /**
     * Set a variable at the parent scope (depth - 1).
     * Used for CMake's PARENT_SCOPE modifier.
     *
     * CMake semantics: PARENT_SCOPE modifies the parent scope but does NOT
     * affect the current scope's view of the variable. To achieve this, if
     * the variable is visible from the parent but has no local entry, we
     * first "snapshot" the current value into a local entry before modifying
     * the parent.
     *
     * Returns: expected<bool, string>
     *   - true: replaced existing variable
     *   - false: created new variable
     *   - error string: operation failed (no parent scope)
     * O(n) where n is the number of versions (typically small).
     */
    std::expected<bool, std::string> set_parent_scope(const std::string& name, const std::string& value) {
        if (current_depth_ == 0) {
            return std::unexpected("PARENT_SCOPE requires a parent scope (must be called from a function)");
        }

        int target_depth = current_depth_ - 1;
        auto& versions = variables_[name];

        // CMake semantics: PARENT_SCOPE should NOT affect current scope's view.
        // If the variable is visible from a parent depth but has no local entry,
        // we need to "snapshot" the current visible value into a local entry first.
        bool has_local_entry = !versions.empty() && versions.back().depth == current_depth_;
        if (!has_local_entry && !versions.empty() && versions.back().value.has_value()) {
            // Variable is visible from parent - snapshot current value to preserve local view
            std::string current_value = *versions.back().value;
            versions.push_back({std::optional<std::string>(current_value), current_depth_});
            if (current_depth_ < static_cast<int>(modified_per_depth_.size())) {
                modified_per_depth_[current_depth_].insert(name);
            }
        }

        // Search for existing entry at parent depth and modify
        for (auto& ver : versions) {
            if (ver.depth == target_depth) {
                ver.value = value;
                return true;  // Replaced existing
            }
        }

        // No existing entry at parent depth - this is a NEW variable being created
        // CMake semantics: a variable created via PARENT_SCOPE is NOT visible in
        // the current scope until we exit.

        // Only insert tombstone if there's no local entry already
        // (If there's a local entry, the current scope's view is already established)
        if (!has_local_entry) {
            // Insert tombstone at current depth to hide the new parent variable
            versions.push_back({std::nullopt, current_depth_});
            if (current_depth_ < static_cast<int>(modified_per_depth_.size())) {
                modified_per_depth_[current_depth_].insert(name);
            }
        }

        // Insert the actual value at parent depth
        // Find the insertion point to maintain depth ordering
        auto insert_pos = versions.begin();
        while (insert_pos != versions.end() && insert_pos->depth < target_depth) {
            ++insert_pos;
        }

        versions.insert(insert_pos, {std::optional<std::string>(value), target_depth});

        // Track modification at parent depth
        if (target_depth < static_cast<int>(modified_per_depth_.size())) {
            modified_per_depth_[target_depth].insert(name);
        }

        return false;  // Created new
    }

    /**
     * Unset a variable at the current depth.
     * If the variable exists at this depth, removes that version.
     * If the variable exists at a parent depth, inserts a tombstone to mask it.
     * O(1) complexity.
     */
    void unset(const std::string& name) {
        auto it = variables_.find(name);
        if (it == variables_.end() || it->second.empty()) {
            return;
        }

        auto& versions = it->second;
        if (versions.back().depth == current_depth_) {
            // Variable is at current depth - remove it
            versions.pop_back();
            // If a parent version exists, insert a tombstone to mask it
            if (!versions.empty() && versions.back().value.has_value()) {
                versions.push_back({std::nullopt, current_depth_});
                if (current_depth_ > 0 && current_depth_ < static_cast<int>(modified_per_depth_.size())) {
                    modified_per_depth_[current_depth_].insert(name);
                }
            }
        } else if (versions.back().depth < current_depth_ && versions.back().value.has_value()) {
            // Variable exists at parent depth and is visible - insert tombstone to mask it
            versions.push_back({std::nullopt, current_depth_});
            if (current_depth_ > 0 && current_depth_ < static_cast<int>(modified_per_depth_.size())) {
                modified_per_depth_[current_depth_].insert(name);
            }
        }
        // If variable is already a tombstone at parent depth, do nothing
    }

    /**
     * Unset a variable at the parent scope (depth - 1).
     * Used for CMake's unset(VAR PARENT_SCOPE).
     *
     * CMake semantics: unset(VAR PARENT_SCOPE) removes the variable from the
     * parent scope but does NOT affect the current scope's view. To achieve
     * this, if the variable is visible from the parent but has no local entry,
     * we first "snapshot" the current value into a local entry before unsetting
     * the parent.
     *
     * Returns: expected<bool, string>
     *   - true: variable was found and removed
     *   - false: variable was not found at parent scope
     *   - error string: operation failed (no parent scope)
     * O(n) where n is the number of versions (typically small).
     */
    std::expected<bool, std::string> unset_parent_scope(const std::string& name) {
        if (current_depth_ == 0) {
            return std::unexpected("PARENT_SCOPE requires a parent scope (must be called from a function)");
        }

        int target_depth = current_depth_ - 1;
        auto it = variables_.find(name);
        if (it == variables_.end() || it->second.empty()) {
            return false;  // Variable not found
        }

        auto& versions = it->second;

        // CMake semantics: unset PARENT_SCOPE should NOT affect current scope's view.
        // If the variable is visible from a parent depth but has no local entry,
        // we need to "snapshot" the current visible value into a local entry first.
        bool has_local_entry = !versions.empty() && versions.back().depth == current_depth_;
        if (!has_local_entry && !versions.empty() && versions.back().value.has_value()) {
            // Variable is visible from parent - snapshot current value to preserve local view
            std::string current_value = *versions.back().value;
            versions.push_back({std::optional<std::string>(current_value), current_depth_});
            if (current_depth_ < static_cast<int>(modified_per_depth_.size())) {
                modified_per_depth_[current_depth_].insert(name);
            }
        }

        // Find and remove the entry at parent depth
        for (auto ver_it = versions.begin(); ver_it != versions.end(); ++ver_it) {
            if (ver_it->depth == target_depth) {
                versions.erase(ver_it);
                return true;  // Found and removed
            }
        }
        return false;  // Not found at parent depth
    }

    /**
     * Check if a variable is defined at any visible depth.
     * O(1) complexity.
     */
    bool is_defined(std::string_view name) const {
        auto it = variables_.find(name);
        if (it == variables_.end() || it->second.empty()) {
            return false;
        }
        // Check for tombstone
        return it->second.back().value.has_value();
    }

    /**
     * Enter a new scope (increment depth).
     * O(1) complexity.
     */
    void push_scope() {
        ++current_depth_;

        // Ensure tracking vector is large enough
        if (current_depth_ >= static_cast<int>(modified_per_depth_.size())) {
            modified_per_depth_.resize(current_depth_ + 1);
        }
    }

    /**
     * Exit current scope (decrement depth).
     * Automatically removes all variables modified at this depth.
     * O(modified_vars) complexity.
     */
    void pop_scope() {
        if (current_depth_ == 0) {
            return;  // Already at root
        }

        // Remove all modifications at current depth
        if (current_depth_ < static_cast<int>(modified_per_depth_.size())) {
            for (const auto& name : modified_per_depth_[current_depth_]) {
                auto it = variables_.find(name);
                if (it != variables_.end() && !it->second.empty()) {
                    auto& versions = it->second;
                    // Remove all versions at current depth (should be at most one)
                    while (!versions.empty() && versions.back().depth == current_depth_) {
                        versions.pop_back();
                    }
                }
            }
            modified_per_depth_[current_depth_].clear();
        }

        --current_depth_;
    }

    /**
     * Get current scope depth.
     * Used for debugging and validation.
     */
    int depth() const {
        return current_depth_;
    }

    /**
     * Return a snapshot of all currently visible variables.
     * Useful for caching/restoring state.
     */
    std::unordered_map<std::string, std::string> snapshot() const {
        std::unordered_map<std::string, std::string> result;
        for (const auto& [name, versions] : variables_) {
            if (!versions.empty() && versions.back().value.has_value()) {
                result[name] = *versions.back().value;
            }
        }
        return result;
    }

    /**
     * Merge all variables from a map into the current scope.
     */
    void merge(const std::unordered_map<std::string, std::string>& vars) {
        for (const auto& [name, value] : vars) {
            set(name, value);
        }
    }

    /**
     * Get all currently visible variable names.
     * O(n) where n is total number of variables.
     */
    std::vector<std::string> get_all_names() const {
        std::vector<std::string> result;
        for (const auto& [name, versions] : variables_) {
            if (!versions.empty() && versions.back().value.has_value()) {
                result.push_back(name);
            }
        }
        return result;
    }

private:
    struct VariableVersion {
        std::optional<std::string> value;  // nullopt = tombstone (unset masking parent)
        int depth;
    };

    // Variable name -> [version history sorted by depth]
    // Uses transparent hash/equal for string_view lookup without allocation
    std::unordered_map<std::string, std::vector<VariableVersion>,
                       TransparentStringHash, TransparentStringEqual> variables_;

    // Current scope depth (0 = root)
    int current_depth_ = 0;

    // Track which variables were modified at each depth (for cleanup)
    std::vector<std::unordered_set<std::string>> modified_per_depth_;
};

}  // namespace dmake
