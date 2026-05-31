#pragma once

#include <string>

namespace kiln {

enum class TargetArtifactRole {
    runtime,
    linker,
    archive,
    import_library,
};

struct TargetArtifacts {
    std::string runtime;
    std::string linker;
    std::string archive;
    std::string import_library;

    std::string path(TargetArtifactRole role) const {
        switch (role) {
        case TargetArtifactRole::runtime:
            return runtime;
        case TargetArtifactRole::linker:
            return linker;
        case TargetArtifactRole::archive:
            return archive;
        case TargetArtifactRole::import_library:
            return import_library;
        }
        return {};
    }

    std::string default_path() const {
        if (!runtime.empty()) return runtime;
        if (!archive.empty()) return archive;
        return linker;
    }
};

} // namespace kiln
