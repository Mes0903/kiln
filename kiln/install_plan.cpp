#include "install_plan.hpp"
#include <fstream>
#include "glaze/glaze.hpp"

template <> struct glz::meta<kiln::InstallOp> {
    using T = kiln::InstallOp;
    static constexpr auto value = glz::object(
        "kind", &T::kind, "dest", &T::dest, "component", &T::component, "configurations", &T::configurations, "exclude_from_all",
        &T::exclude_from_all, "optional", &T::optional, "src", &T::src, "perms", &T::perms, "content", &T::content, "symlink_target",
        &T::symlink_target, "patterns", &T::patterns, "excludes", &T::excludes, "use_source_permissions", &T::use_source_permissions,
        "preserve_dir_name", &T::preserve_dir_name, "file_perms", &T::file_perms, "dir_perms", &T::dir_perms);
};

template <> struct glz::meta<kiln::InstallPlan> {
    using T = kiln::InstallPlan;
    static constexpr auto value = glz::object("version", &T::version, "kiln_version", &T::kiln_version, "config", &T::config,
                                              "default_prefix", &T::default_prefix, "ops", &T::ops);
};

namespace kiln {

namespace {

constexpr PermissionBits kOwnerRead = 0400;
constexpr PermissionBits kOwnerWrite = 0200;
constexpr PermissionBits kOwnerExecute = 0100;
constexpr PermissionBits kGroupRead = 0040;
constexpr PermissionBits kGroupWrite = 0020;
constexpr PermissionBits kGroupExecute = 0010;
constexpr PermissionBits kOtherRead = 0004;
constexpr PermissionBits kOtherWrite = 0002;
constexpr PermissionBits kOtherExecute = 0001;

} // namespace

std::string mode_to_rwx(PermissionBits mode) {
    std::string s(9, '-');
    if (mode & kOwnerRead) s[0] = 'r';
    if (mode & kOwnerWrite) s[1] = 'w';
    if (mode & kOwnerExecute) s[2] = 'x';
    if (mode & kGroupRead) s[3] = 'r';
    if (mode & kGroupWrite) s[4] = 'w';
    if (mode & kGroupExecute) s[5] = 'x';
    if (mode & kOtherRead) s[6] = 'r';
    if (mode & kOtherWrite) s[7] = 'w';
    if (mode & kOtherExecute) s[8] = 'x';
    return s;
}

std::expected<PermissionBits, std::string> rwx_to_mode(const std::string& rwx) {
    if (rwx.size() != 9) { return std::unexpected("invalid rwx string (expected 9 chars): " + rwx); }
    PermissionBits mode = 0;
    constexpr char expected[9] = {'r', 'w', 'x', 'r', 'w', 'x', 'r', 'w', 'x'};
    constexpr PermissionBits bits[9] = {
        kOwnerRead, kOwnerWrite, kOwnerExecute, kGroupRead, kGroupWrite, kGroupExecute, kOtherRead, kOtherWrite, kOtherExecute,
    };
    for (size_t i = 0; i < 9; ++i) {
        if (rwx[i] == expected[i]) {
            mode |= bits[i];
        } else if (rwx[i] != '-') {
            return std::unexpected("invalid rwx char at position " + std::to_string(i) + ": " + rwx);
        }
    }
    return mode;
}

std::expected<void, std::string> save_install_plan(const InstallPlan& plan, const std::filesystem::path& path) {
    std::string json_str;
    auto write_result = glz::write<glz::opts{.prettify = true}>(plan, json_str);
    if (write_result) { return std::unexpected("failed to serialize install plan: " + glz::format_error(write_result)); }

    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) { return std::unexpected("failed to create plan directory: " + ec.message()); }
    }

    std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) { return std::unexpected("failed to open temp plan file: " + tmp.string()); }
        f << json_str;
        if (!f) { return std::unexpected("failed to write temp plan file: " + tmp.string()); }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) { return std::unexpected("failed to rename plan file: " + ec.message()); }
    return {};
}

std::expected<InstallPlan, std::string> load_install_plan(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::unexpected("install plan not found: " + path.string() + " (run `kiln build` first)");
    }
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f) { return std::unexpected("failed to open install plan: " + path.string()); }
    auto sz = f.tellg();
    f.seekg(0);
    std::string json_str(static_cast<size_t>(sz), '\0');
    f.read(json_str.data(), sz);

    InstallPlan plan;
    auto parse_result = glz::read_json(plan, json_str);
    if (parse_result) {
        return std::unexpected("install plan is corrupted: " + glz::format_error(parse_result, json_str)
                               + " (run `kiln build` to regenerate)");
    }

    if (plan.version != INSTALL_PLAN_VERSION) {
        return std::unexpected("install plan version " + std::to_string(plan.version) + ", kiln expects "
                               + std::to_string(INSTALL_PLAN_VERSION) + " (run `kiln build` to regenerate)");
    }
    return plan;
}

} // namespace kiln
