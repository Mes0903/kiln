#pragma once
#include "compiler.hpp"
#include "language.hpp"
#include <map>
#include <memory>

namespace dmake {

class Toolchain {
public:
    void set_compiler(Language lang, std::unique_ptr<Compiler> compiler) {
        compilers_[lang] = std::move(compiler);
    }

    const Compiler* get_compiler(Language lang) const {
        auto it = compilers_.find(lang);
        if (it == compilers_.end()) return nullptr;
        return it->second.get();
    }

    const Compiler* get_compiler_ptr(Language lang) const {
        return get_compiler(lang);
    }

private:
    std::map<Language, std::unique_ptr<Compiler>> compilers_;
};

} // namespace dmake