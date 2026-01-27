#pragma once

#include <vector>
#include <string>

namespace dmake {

class Interpreter;

// Helper class for CMake list operations
class CMakeList {
public:
    CMakeList() = default;
    explicit CMakeList(const std::string& semicolon_separated);
    explicit CMakeList(const std::vector<std::string>& items);
    CMakeList(std::initializer_list<std::string> items);

    std::string to_string() const;
    std::vector<std::string> to_vector() const;

    size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    const std::string& operator[](size_t idx) const { return items_[idx]; }
    const std::string& at(size_t idx) const { return items_.at(idx); }

    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }
    auto begin() { return items_.begin(); }
    auto end() { return items_.end(); }

    void append(const std::string& item);
    void append(const CMakeList& other);
    void push_back(const std::string& item) { append(item); }

    void reverse();
    void sort();
    void remove_duplicates();
    CMakeList sublist(size_t begin_idx, size_t length) const;
    bool contains(const std::string& item) const;

private:
    std::vector<std::string> items_;
    static std::vector<std::string> split_by_semicolon(const std::string& str);
};

}
