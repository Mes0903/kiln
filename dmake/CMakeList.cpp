#include "CMakeList.hpp"
#include <sstream>
#include <algorithm>
#include <set>

namespace dmake {

std::vector<std::string> CMakeList::split_by_semicolon(const std::string& str) {
    if (str.empty()) {
        return {};
    }

    std::vector<std::string> result;
    std::string current;
    int genex_depth = 0;  // Track $<...> nesting

    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];

        // Track genex nesting
        if (c == '$' && i + 1 < str.size() && str[i + 1] == '<') {
            genex_depth++;
            current += c;
        } else if (c == '>' && genex_depth > 0) {
            genex_depth--;
            current += c;
        } else if (c == ';' && genex_depth == 0) {
            // Only split on semicolons outside of genex
            result.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }

    // Push final element (even if empty)
    result.push_back(current);

    return result;
}

CMakeList::CMakeList(const std::string& semicolon_separated)
    : items_(split_by_semicolon(semicolon_separated)) {
}

CMakeList::CMakeList(const std::vector<std::string>& items)
    : items_(items) {
}

CMakeList::CMakeList(std::initializer_list<std::string> items)
    : items_(items) {
}

std::string CMakeList::to_string() const {
    std::string res;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (i > 0) res += ";";
        res += items_[i];
    }
    return res;
}

std::vector<std::string> CMakeList::to_vector() const {
    return items_;
}

void CMakeList::append(const std::string& item) {
    if (item.empty()) return;
    auto parts = split_by_semicolon(item);
    items_.insert(items_.end(), parts.begin(), parts.end());
}

void CMakeList::append(const CMakeList& other) {
    items_.insert(items_.end(), other.items_.begin(), other.items_.end());
}

void CMakeList::insert(size_t idx, const std::vector<std::string>& items) {
    items_.insert(items_.begin() + idx, items.begin(), items.end());
}

void CMakeList::reverse() {
    std::reverse(items_.begin(), items_.end());
}

// Natural sort comparison function
static bool natural_compare(const std::string& a, const std::string& b) {
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };

    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        // If both are digits, compare numerically
        if (is_digit(a[i]) && is_digit(b[j])) {
            // Extract the numeric parts
            size_t num_start_a = i, num_start_b = j;
            while (i < a.size() && is_digit(a[i])) i++;
            while (j < b.size() && is_digit(b[j])) j++;

            std::string num_a = a.substr(num_start_a, i - num_start_a);
            std::string num_b = b.substr(num_start_b, j - num_start_b);

            // Compare numerically
            try {
                unsigned long long val_a = std::stoull(num_a);
                unsigned long long val_b = std::stoull(num_b);
                if (val_a != val_b) {
                    return val_a < val_b;
                }
                // If numerically equal, longer strings (more leading zeros) come first
                // This handles "00" vs "0" - "00" (length 2) comes before "0" (length 1)
                if (num_a.size() != num_b.size()) {
                    return num_a.size() > num_b.size();  // Longer first!
                }
            } catch (...) {
                // If numeric conversion fails, fall back to string comparison
                if (num_a != num_b) {
                    return num_a < num_b;
                }
            }
        } else {
            // Lexicographic comparison
            if (a[i] != b[j]) {
                return a[i] < b[j];
            }
            i++;
            j++;
        }
    }

    // If we've exhausted one string, the shorter one comes first
    return a.size() < b.size();
}

void CMakeList::sort(bool natural, bool descending) {
    if (natural) {
        if (descending) {
            std::sort(items_.begin(), items_.end(), [](const std::string& a, const std::string& b) {
                return natural_compare(b, a);  // Reverse comparison for descending
            });
        } else {
            std::sort(items_.begin(), items_.end(), natural_compare);
        }
    } else {
        if (descending) {
            std::sort(items_.begin(), items_.end(), std::greater<std::string>());
        } else {
            std::sort(items_.begin(), items_.end());
        }
    }
}

void CMakeList::remove_duplicates() {
    std::set<std::string> seen;
    std::vector<std::string> unique_items;
    for (const auto& item : items_) {
        if (seen.find(item) == seen.end()) {
            unique_items.push_back(item);
            seen.insert(item);
        }
    }
    items_ = std::move(unique_items);
}

CMakeList CMakeList::sublist(size_t begin_idx, size_t length) const {
    if (begin_idx >= items_.size()) return CMakeList();
    size_t count = std::min(length, items_.size() - begin_idx);
    std::vector<std::string> sub(items_.begin() + begin_idx, items_.begin() + begin_idx + count);
    return CMakeList(sub);
}

bool CMakeList::contains(const std::string& item) const {
    return std::find(items_.begin(), items_.end(), item) != items_.end();
}

}