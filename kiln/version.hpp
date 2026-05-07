#pragma once

#include <string_view>

namespace kiln {

// Semantic version "MAJOR.MINOR.PATCH" parsed from the nearest git tag,
// or "0.0.0" when no tag is reachable.
std::string_view version() noexcept;

// Full descriptor from `git describe --tags --dirty --always`, e.g.
// "1.2.3", "1.2.3-5-gabcdef", "1.2.3-dirty", or commit hash / "0.0.0".
std::string_view version_full() noexcept;

} // namespace kiln
