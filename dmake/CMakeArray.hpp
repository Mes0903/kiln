#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <stdexcept>

namespace dmake {

class Interpreter;

// Owning CMake semicolon-separated list with mutation support
class CMakeArray {
public:
    CMakeArray() = default;
    explicit CMakeArray(const std::string& semicolon_separated);
    explicit CMakeArray(const std::vector<std::string>& items);
    CMakeArray(std::initializer_list<std::string> items);

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
    void append(const CMakeArray& other);
    void push_back(const std::string& item) { append(item); }
    void erase(size_t idx) { items_.erase(items_.begin()+idx);  }
    void insert(size_t idx, const std::vector<std::string>& items);

    void reverse();
    void sort(bool natural = false, bool descending = false);
    void remove_duplicates();
    CMakeArray sublist(size_t begin_idx, size_t length) const;
    bool contains(const std::string& item) const;

    // Count list elements without allocating (for LENGTH)
    static size_t count_elements(std::string_view str);

private:
    std::vector<std::string> items_;
    static std::vector<std::string> split_by_semicolon(const std::string& str);
};

// Non-owning read-only view over a semicolon-separated string.
// Avoids heap-allocating N strings when you only need to iterate.
class CMakeArrayView {
public:
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;
        using pointer = const std::string_view*;
        using reference = std::string_view;

        iterator() = default;
        iterator(const CMakeArrayView* parent, size_t idx) : parent_(parent), idx_(idx) {}

        std::string_view operator*() const { return parent_->element_at(idx_); }
        iterator& operator++() { ++idx_; return *this; }
        iterator operator++(int) { auto tmp = *this; ++idx_; return tmp; }
        bool operator==(const iterator& other) const { return idx_ == other.idx_; }
        bool operator!=(const iterator& other) const { return idx_ != other.idx_; }

    private:
        const CMakeArrayView* parent_ = nullptr;
        size_t idx_ = 0;
    };

    CMakeArrayView() = default;
    explicit CMakeArrayView(std::string_view semicolon_separated);

    size_t size() const;
    bool empty() const { return size() == 0; }
    std::string_view operator[](size_t idx) const { return element_at(idx); }
    std::string_view at(size_t idx) const;
    bool contains(std::string_view item) const;
    std::string to_string() const { return std::string(source_); }

    iterator begin() const { return iterator(this, 0); }
    iterator end() const { return iterator(this, size()); }

private:
    std::string_view source_;
    std::vector<size_t> separators_; // positions of ';' outside genex

    std::string_view element_at(size_t i) const;
};

// Zero-allocation forward range for iterating semicolon-separated lists.
// Unlike CMakeArrayView, does no pre-scanning or heap allocation.
// Use this when you only need sequential iteration (95% of cases).
class CMakeArrayIterator {
public:
    struct sentinel {};

    class iterator {
    public:
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;

        explicit iterator(std::string_view source)
            : source_(source) {
            if (source_.empty()) {
                done_ = true;
            } else {
                find_end();
            }
        }

        std::string_view operator*() const {
            return source_.substr(pos_, end_ - pos_);
        }

        iterator& operator++() {
            if (end_ >= source_.size()) {
                done_ = true;
            } else {
                pos_ = end_ + 1;
                find_end();
            }
            return *this;
        }

        friend bool operator==(const iterator& it, sentinel) { return it.done_; }
        friend bool operator!=(const iterator& it, sentinel) { return !it.done_; }

    private:
        void find_end() {
            end_ = pos_;
            while (end_ < source_.size()) {
                if (source_[end_] == '\\' && end_ + 1 < source_.size() && source_[end_ + 1] == ';') {
                    end_ += 2;
                } else if (source_[end_] == ';') {
                    break;
                } else {
                    ++end_;
                }
            }
        }

        std::string_view source_;
        size_t pos_ = 0;
        size_t end_ = 0;
        bool done_ = false;
    };

    explicit CMakeArrayIterator(std::string_view source) : source_(source) {}

    iterator begin() const { return iterator(source_); }
    sentinel end() const { return {}; }

private:
    std::string_view source_;
};

}
