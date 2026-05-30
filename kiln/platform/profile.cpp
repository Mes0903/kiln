#include "kiln/platform/profile.hpp"

#include "kiln/platform/host.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace kiln::platform {
namespace {

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

PlatformProfile unix_profile(std::string profile_id) {
    PlatformProfile profile;
    profile.profile_id = std::move(profile_id);
    profile.executable_suffix = "";
    profile.object_suffix = ".o";
    profile.static_library_prefix = "lib";
    profile.static_library_suffix = ".a";
    profile.shared_library_prefix = "lib";
    profile.shared_library_suffix = ".so";
    profile.shared_module_prefix = "";
    profile.shared_module_suffix = ".so";
    profile.import_library_prefix = "";
    profile.import_library_suffix = "";
    profile.link_library_suffix = "";
    profile.path_list_separator = ":";
    profile.null_device = "/dev/null";
    profile.default_install_prefix = "/usr/local";
    profile.system_prefix_paths = {"/usr/local", "/usr", "/"};
    profile.system_prefix_post_paths = {"/usr/X11R6", "/usr/pkg", "/opt"};
    profile.system_include_paths = {"/usr/include/X11"};
    profile.system_library_paths = {"/usr/lib/X11"};
    profile.platform_implicit_link_directories = {"/lib", "/lib32", "/lib64", "/usr/lib", "/usr/lib32", "/usr/lib64"};
    profile.find_library_prefixes = {"lib", ""};
    profile.find_library_suffixes = {".so", ".a"};
    profile.shared_library_c_flags = "-fPIC";
    profile.shared_library_cxx_flags = "-fPIC";
    profile.dl_libs = "";
    return profile;
}

PlatformProfile linux_profile() {
    auto profile = unix_profile("Linux");
    profile.dl_libs = "dl";
    return profile;
}

PlatformProfile darwin_profile() {
    auto profile = unix_profile("Darwin");
    profile.shared_library_suffix = ".dylib";
    profile.find_library_suffixes = {".tbd", ".dylib", ".so", ".a"};
    return profile;
}

PlatformProfile windows_msvc_profile() {
    PlatformProfile profile;
    profile.profile_id = "Windows-MSVC";
    profile.executable_suffix = ".exe";
    profile.object_suffix = ".obj";
    profile.static_library_prefix = "";
    profile.static_library_suffix = ".lib";
    profile.shared_library_prefix = "";
    profile.shared_library_suffix = ".dll";
    profile.shared_module_prefix = "";
    profile.shared_module_suffix = ".dll";
    profile.import_library_prefix = "";
    profile.import_library_suffix = ".lib";
    profile.link_library_suffix = ".lib";
    profile.path_list_separator = ";";
    profile.null_device = "NUL";
    // CMake's project-specific default is "C:/Program Files/${PROJECT_NAME}".
    // The interpreter profile is applied before project() has a reliable name,
    // so keep the stable root prefix and let later project-aware code refine it.
    profile.default_install_prefix = "C:/Program Files";
    profile.system_prefix_paths = {"C:/Program Files", "C:/Program Files (x86)"};
    profile.system_prefix_post_paths = {};
    profile.system_include_paths = {};
    profile.system_library_paths = {};
    profile.platform_implicit_link_directories = {};
    profile.find_library_prefixes = {"", "lib"};
    profile.find_library_suffixes = {".dll.lib", ".lib", ".a"};
    profile.shared_library_c_flags = "";
    profile.shared_library_cxx_flags = "";
    profile.dl_libs = "";
    return profile;
}

PlatformProfile windows_gnu_profile() {
    auto profile = windows_msvc_profile();
    profile.profile_id = "Windows-GNU";
    profile.static_library_prefix = "lib";
    profile.static_library_suffix = ".a";
    profile.shared_library_prefix = "lib";
    profile.shared_module_prefix = "lib";
    profile.import_library_prefix = "lib";
    profile.import_library_suffix = ".dll.a";
    profile.link_library_suffix = "";
    profile.find_library_prefixes = {"lib", ""};
    profile.find_library_suffixes = {".dll.a", ".a", ".lib"};
    return profile;
}

bool is_msvc_like(std::string_view compiler_id) {
    const auto compiler = lowercase(compiler_id);
    return compiler.empty() || compiler == "msvc";
}

bool is_gnu_like(std::string_view compiler_id) {
    const auto compiler = lowercase(compiler_id);
    return compiler == "gnu" || compiler == "mingw";
}

} // namespace

PlatformProfile profile_for(std::string_view system_name, std::string_view compiler_id) {
    const auto system = lowercase(system_name);
    if (system == "mingw" || system == "msys") { return windows_gnu_profile(); }
    if (system == "windows" || system == "windowsstore") {
        if (is_gnu_like(compiler_id) && !is_msvc_like(compiler_id)) return windows_gnu_profile();
        return windows_msvc_profile();
    }
    if (system == "darwin") { return darwin_profile(); }
    if (system == "linux") { return linux_profile(); }
    return unix_profile(system.empty() ? "Unix" : std::string(system_name));
}

PlatformProfile host_profile() {
#ifdef _WIN32
#if defined(_MSC_VER)
    return windows_msvc_profile();
#elif defined(__GNUC__)
    return windows_gnu_profile();
#else
    return windows_msvc_profile();
#endif
#elif defined(__APPLE__)
    return darwin_profile();
#elif defined(__linux__)
    return linux_profile();
#else
    auto info = host_info();
    return profile_for(info.system_name, "");
#endif
}

} // namespace kiln::platform
