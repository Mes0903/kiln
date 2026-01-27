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

    for (char c : str) {
        if (c == ';') {
            result.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }

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

void CMakeList::reverse() {
    std::reverse(items_.begin(), items_.end());
}

void CMakeList::sort() {
    std::sort(items_.begin(), items_.end());
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