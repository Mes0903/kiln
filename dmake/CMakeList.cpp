#include "CMakeList.hpp"

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

}
