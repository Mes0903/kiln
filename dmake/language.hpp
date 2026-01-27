#pragma once
#include <string>
#include <string_view>

namespace dmake {

enum class Language {
    C,
    CXX,
    CUDA,
    HEADER,
    UNKNOWN
};

struct LanguageInfo {
    Language lang;
    std::string_view name;
    bool is_compileable;
    bool is_header;
    bool is_module_interface = false;  // True for .ixx, .cppm, etc.
};

class LanguageClassifier {
public:
    static LanguageInfo from_extension(std::string_view ext);
    static LanguageInfo from_path(std::string_view path);
};

} // namespace dmake
