#include "language.hpp"
#include <filesystem>
#include <map>

namespace dmake {

LanguageInfo LanguageClassifier::from_extension(std::string_view ext) {
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".C" || ext == ".c++" || ext == ".c+") {
        return {Language::CXX, "CXX", true, false};
    }
    if (ext == ".c") {
        return {Language::C, "C", true, false};
    }
    if (ext == ".cu") {
        return {Language::CUDA, "CUDA", true, false};
    }
    if (ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh") {
        return {Language::HEADER, "HEADER", false, true};
    }
    return {Language::UNKNOWN, "UNKNOWN", false, false};
}

LanguageInfo LanguageClassifier::from_path(std::string_view path) {
    std::filesystem::path p(path);
    return from_extension(p.extension().string());
}

} // namespace dmake
