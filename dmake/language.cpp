#include "language.hpp"
#include <filesystem>
#include <map>

namespace dmake {

LanguageInfo LanguageClassifier::from_extension(std::string_view ext) {
    // C++20 module interface files
    if (ext == ".ixx" || ext == ".cppm" || ext == ".ccm" || ext == ".cxxm" || ext == ".mpp") {
        return {Language::CXX, "CXX", true, false, true};
    }
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".C" || ext == ".c++" || ext == ".c+") {
        return {Language::CXX, "CXX", true, false, false};
    }
    if (ext == ".c") {
        return {Language::C, "C", true, false, false};
    }
    if (ext == ".cu") {
        return {Language::CUDA, "CUDA", true, false, false};
    }
    if (ext == ".s" || ext == ".S" || ext == ".asm" || ext == ".sx") {
        return {Language::ASM, "ASM", true, false, false};
    }
    if (ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh") {
        return {Language::HEADER, "HEADER", false, true, false};
    }
    return {Language::UNKNOWN, "UNKNOWN", false, false, false};
}

LanguageInfo LanguageClassifier::from_path(std::string_view path) {
    std::filesystem::path p(path);
    return from_extension(p.extension().string());
}

} // namespace dmake
