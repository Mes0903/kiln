#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kiln::platform {

struct PlatformProfile {
    std::string profile_id;
    std::string executable_suffix;
    std::string object_suffix;
    std::string static_library_prefix;
    std::string static_library_suffix;
    std::string shared_library_prefix;
    std::string shared_library_suffix;
    std::string shared_module_prefix;
    std::string shared_module_suffix;
    std::string import_library_prefix;
    std::string import_library_suffix;
    std::string link_library_suffix;
    std::string path_list_separator;
    std::string null_device;
    std::string default_install_prefix;
    std::vector<std::string> system_prefix_paths;
    std::vector<std::string> system_prefix_post_paths;
    std::vector<std::string> system_include_paths;
    std::vector<std::string> system_library_paths;
    std::vector<std::string> platform_implicit_link_directories;
    std::vector<std::string> find_library_prefixes;
    std::vector<std::string> find_library_suffixes;
    std::string shared_library_c_flags;
    std::string shared_library_cxx_flags;
    std::string dl_libs;
};

PlatformProfile host_profile();
PlatformProfile profile_for(std::string_view system_name, std::string_view compiler_id);

} // namespace kiln::platform
