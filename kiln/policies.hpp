#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace kiln {

enum class CMakePolicy : uint8_t {
    CMP0126,  // 3.21: set(CACHE) removes local variables
    CMP0148,  // 3.27: FindPythonInterp/FindPythonLibs removed
    CMP0167,  // 3.30: Boost find_package config-first
    COUNT
};

enum class PolicyState : uint8_t { OLD, NEW };

// Mark sites that use OLD behavior — grep KILN_POLICY_OLD to find all deviations
#define KILN_POLICY_OLD(policy) /* using OLD behavior for policy */

struct PolicyStack {
    using StateArray = std::array<PolicyState, size_t(CMakePolicy::COUNT)>;
    StateArray current_;
    std::vector<StateArray> stack_;

    PolicyState get(CMakePolicy p) const { return current_[size_t(p)]; }
    void set(CMakePolicy p, PolicyState s) { current_[size_t(p)] = s; }
    void push() { stack_.push_back(current_); }
    void pop() {
        if (!stack_.empty()) {
            current_ = stack_.back();
            stack_.pop_back();
        }
    }

    static PolicyStack make_defaults() {
        PolicyStack ps;
        ps.current_.fill(PolicyState::NEW);
        // Known OLD defaults — grep KILN_POLICY_OLD to find usage sites
        ps.current_[size_t(CMakePolicy::CMP0126)] = PolicyState::OLD;
        ps.current_[size_t(CMakePolicy::CMP0148)] = PolicyState::OLD;
        return ps;
    }
};

// Parse "CMP0126" → enum. Returns nullopt for unknown policies.
inline std::optional<CMakePolicy> parse_cmake_policy(std::string_view name) {
    if (name == "CMP0126") return CMakePolicy::CMP0126;
    if (name == "CMP0148") return CMakePolicy::CMP0148;
    if (name == "CMP0167") return CMakePolicy::CMP0167;
    return std::nullopt;
}

} // namespace kiln
