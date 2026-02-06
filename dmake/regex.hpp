#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <expected>

namespace dmake {

class Regex {
public:
    struct Impl;

private:
    Impl* impl_ = nullptr;

    explicit Regex(Impl* impl) : impl_(impl) {}

public:
    // Compile pattern, returns error string on failure
    static std::expected<Regex, std::string> compile(std::string_view pattern);

    // Compile with full-match semantics (anchors pattern with ^(?:...)$)
    static std::expected<Regex, std::string> compile_match(std::string_view pattern);

    ~Regex();
    Regex(Regex&& other) noexcept;
    Regex& operator=(Regex&& other) noexcept;

    Regex(const Regex&) = delete;
    Regex& operator=(const Regex&) = delete;

    // Search for first match (like std::regex_search)
    // captures[0] = full match, captures[1..N] = groups
    bool search(std::string_view input, std::vector<std::string>& captures) const;
    bool search(std::string_view input) const;

    // Full match (like std::regex_match) - uses anchored pattern from compile_match
    bool match(std::string_view input, std::vector<std::string>& captures) const;
    bool match(std::string_view input) const;

    // Find all non-overlapping matches (replaces sregex_iterator)
    // Each element is [full_match, group1, group2, ...]
    std::vector<std::vector<std::string>> match_all(std::string_view input) const;

    // Replace all occurrences. Replacement uses \1, \2, etc.
    std::string replace_all(std::string_view input, std::string_view replacement) const;
};

} // namespace dmake
