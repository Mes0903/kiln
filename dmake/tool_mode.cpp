#include "dmake/tool_mode.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <archive.h>
#include <archive_entry.h>

namespace fs = std::filesystem;

static char** get_environ_ptr() {
    extern char** environ;
    return environ;
}

namespace dmake {

namespace {

using Args = std::span<const std::string>;
using CmdFn = std::function<int(Args)>;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int exec_command(const std::vector<std::string>& cmd, const std::string& working_dir = "") {
    if (cmd.empty()) return 1;

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Error: fork failed: " << strerror(errno) << std::endl;
        return 1;
    }
    if (pid == 0) {
        if (!working_dir.empty()) {
            if (chdir(working_dir.c_str()) != 0) {
                _exit(127);
            }
        }
        std::vector<char*> argv;
        for (auto& s : cmd) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

bool copy_file_impl(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    auto actual_dst = dst;
    if (fs::is_directory(dst)) {
        actual_dst = dst / src.filename();
    }
    fs::copy_file(src, actual_dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Error: Failed to copy " << src << " to " << actual_dst << ": " << ec.message() << std::endl;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Command implementations
// ---------------------------------------------------------------------------

int cmd_echo(Args args) {
    for (size_t i = 0; i < args.size(); ++i) {
        std::cout << args[i];
        if (i + 1 < args.size()) std::cout << ' ';
    }
    std::cout << '\n';
    return 0;
}

int cmd_echo_append(Args args) {
    for (size_t i = 0; i < args.size(); ++i) {
        std::cout << args[i];
        if (i + 1 < args.size()) std::cout << ' ';
    }
    return 0;
}

int cmd_touch(Args args) {
    for (auto& a : args) {
        std::ofstream f(a, std::ios::app);
    }
    return 0;
}

int cmd_touch_nocreate(Args args) {
    for (auto& a : args) {
        if (fs::exists(a)) {
            std::ofstream f(a, std::ios::app);
        }
    }
    return 0;
}

int cmd_remove(Args args) {
    for (auto& a : args) {
        std::error_code ec;
        fs::remove_all(a, ec);
    }
    return 0;
}

int cmd_remove_directory(Args args) {
    for (auto& a : args) {
        std::error_code ec;
        fs::remove_all(a, ec);
        if (ec) {
            std::cerr << "Error: Failed to remove directory " << a << ": " << ec.message() << std::endl;
        }
    }
    return 0;
}

int cmd_make_directory(Args args) {
    for (auto& a : args) {
        std::error_code ec;
        fs::create_directories(a, ec);
        if (ec) {
            std::cerr << "Error: Failed to create directory " << a << ": " << ec.message() << std::endl;
            return 1;
        }
    }
    return 0;
}

int cmd_create_symlink(Args args) {
    if (args.size() != 2) {
        std::cerr << "Error: create_symlink requires exactly two arguments" << std::endl;
        return 1;
    }
    auto src = args[0];
    auto dst = args[1];
    if (fs::exists(dst) && !fs::is_symlink(dst)) {
        std::cerr << "Error: Link destination " << dst << " already exists and is not a symlink" << std::endl;
        return 1;
    }
    if (fs::is_symlink(dst)) return 0;
    std::error_code ec;
    fs::create_symlink(src, dst, ec);
    if (ec) {
        std::cerr << "Error: Failed to create symlink: " << ec.message() << std::endl;
        return 1;
    }
    return 0;
}

int cmd_create_hardlink(Args args) {
    if (args.size() != 2) {
        std::cerr << "Error: create_hardlink requires exactly two arguments" << std::endl;
        return 1;
    }
    std::error_code ec;
    fs::create_hard_link(args[0], args[1], ec);
    if (ec) {
        std::cerr << "Error: Failed to create hard link: " << ec.message() << std::endl;
        return 1;
    }
    return 0;
}

int cmd_rename(Args args) {
    if (args.size() != 2) {
        std::cerr << "Error: rename requires exactly two arguments" << std::endl;
        return 1;
    }
    std::error_code ec;
    fs::rename(args[0], args[1], ec);
    if (ec) {
        std::cerr << "Error: Failed to rename " << args[0] << " to " << args[1] << ": " << ec.message() << std::endl;
        return 1;
    }
    return 0;
}

int cmd_copy(Args args) {
    if (args.size() < 2) {
        std::cerr << "Error: copy requires at least two arguments" << std::endl;
        return 1;
    }
    auto dest = args.back();
    if (args.size() > 2 && !fs::is_directory(dest)) {
        std::cerr << "Error: Destination " << dest << " is not a directory" << std::endl;
        return 1;
    }
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (!copy_file_impl(args[i], dest)) return 1;
    }
    return 0;
}

int cmd_copy_if_different(Args args) {
    if (args.size() < 2) {
        std::cerr << "Error: copy_if_different requires at least two arguments" << std::endl;
        return 1;
    }
    auto dest = args.back();
    if (args.size() > 2 && !fs::is_directory(dest)) {
        std::cerr << "Error: Destination " << dest << " is not a directory" << std::endl;
        return 1;
    }
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        fs::path src_path = args[i];
        fs::path dst_path = fs::is_directory(dest) ? fs::path(dest) / src_path.filename() : fs::path(dest);
        if (fs::exists(dst_path) && fs::file_size(src_path) == fs::file_size(dst_path)) {
            std::ifstream f1(src_path, std::ios::binary);
            std::ifstream f2(dst_path, std::ios::binary);
            if (std::equal(std::istreambuf_iterator<char>(f1), std::istreambuf_iterator<char>(),
                           std::istreambuf_iterator<char>(f2), std::istreambuf_iterator<char>())) {
                continue;
            }
        }
        if (!copy_file_impl(src_path, dest)) return 1;
    }
    return 0;
}

int cmd_copy_if_newer(Args args) {
    if (args.size() < 2) {
        std::cerr << "Error: copy_if_newer requires at least two arguments" << std::endl;
        return 1;
    }
    auto dest = args.back();
    if (args.size() > 2 && !fs::is_directory(dest)) {
        std::cerr << "Error: Destination " << dest << " is not a directory" << std::endl;
        return 1;
    }
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        fs::path src_path = args[i];
        fs::path dst_path = fs::is_directory(dest) ? fs::path(dest) / src_path.filename() : fs::path(dest);
        if (fs::exists(dst_path)) {
            auto src_time = fs::last_write_time(src_path);
            auto dst_time = fs::last_write_time(dst_path);
            if (src_time <= dst_time) continue;
        }
        if (!copy_file_impl(src_path, dest)) return 1;
    }
    return 0;
}

int cmd_copy_directory(Args args) {
    if (args.size() < 2) {
        std::cerr << "Error: copy_directory requires at least two arguments" << std::endl;
        return 1;
    }
    auto dest = fs::path(args.back());
    std::error_code ec;
    fs::create_directories(dest, ec);
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        fs::copy(args[i], dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "Error: Failed to copy directory " << args[i] << " to " << dest << ": " << ec.message() << std::endl;
            return 1;
        }
    }
    return 0;
}

int cmd_copy_directory_if_different(Args args) {
    // For simplicity, same as copy_directory (CMake also doesn't deeply diff)
    return cmd_copy_directory(args);
}

int cmd_copy_directory_if_newer(Args args) {
    return cmd_copy_directory(args);
}

int cmd_cat(Args args) {
    size_t start = 0;
    if (!args.empty() && args[0] == "--") start = 1;
    if (start >= args.size()) {
        std::cerr << "Error: cat requires at least one file" << std::endl;
        return 1;
    }
    for (size_t i = start; i < args.size(); ++i) {
        std::ifstream f(args[i], std::ios::binary);
        if (!f) {
            std::cerr << "Error: Cannot open " << args[i] << std::endl;
            return 1;
        }
        std::cout << f.rdbuf();
    }
    return 0;
}

int cmd_compare_files(Args args) {
    bool ignore_eol = false;
    size_t start = 0;
    if (!args.empty() && args[0] == "--ignore-eol") {
        ignore_eol = true;
        start = 1;
    }
    if (args.size() - start != 2) {
        std::cerr << "Error: compare_files requires exactly two file arguments" << std::endl;
        return 1;
    }
    auto& f1_path = args[start];
    auto& f2_path = args[start + 1];

    std::ifstream f1(f1_path, std::ios::binary);
    std::ifstream f2(f2_path, std::ios::binary);
    if (!f1) { std::cerr << "Error: Cannot open " << f1_path << std::endl; return 1; }
    if (!f2) { std::cerr << "Error: Cannot open " << f2_path << std::endl; return 1; }

    if (ignore_eol) {
        std::string line1, line2;
        while (true) {
            bool got1 = bool(std::getline(f1, line1));
            bool got2 = bool(std::getline(f2, line2));
            if (!got1 && !got2) return 0;
            if (got1 != got2) return 1;
            // Strip \r
            if (!line1.empty() && line1.back() == '\r') line1.pop_back();
            if (!line2.empty() && line2.back() == '\r') line2.pop_back();
            if (line1 != line2) return 1;
        }
    }

    if (std::equal(std::istreambuf_iterator<char>(f1), std::istreambuf_iterator<char>(),
                   std::istreambuf_iterator<char>(f2), std::istreambuf_iterator<char>())) {
        return 0;
    }
    return 1;
}

int cmd_chdir(Args args) {
    if (args.size() < 2) {
        std::cerr << "Error: chdir requires a directory and command" << std::endl;
        return 1;
    }
    std::string dir = args[0];
    std::vector<std::string> cmd(args.begin() + 1, args.end());
    return exec_command(cmd, dir);
}

int cmd_env(Args args) {
    std::vector<std::string> unsets;
    std::vector<std::pair<std::string, std::string>> sets;
    size_t i = 0;

    for (; i < args.size(); ++i) {
        if (args[i] == "--") { ++i; break; }
        if (args[i].starts_with("--unset=")) {
            unsets.push_back(args[i].substr(8));
        } else if (args[i].find('=') != std::string::npos) {
            auto eq = args[i].find('=');
            sets.emplace_back(args[i].substr(0, eq), args[i].substr(eq + 1));
        } else {
            break;
        }
    }

    if (i >= args.size()) {
        std::cerr << "Error: env requires a command to execute" << std::endl;
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Error: fork failed: " << strerror(errno) << std::endl;
        return 1;
    }
    if (pid == 0) {
        for (auto& name : unsets) unsetenv(name.c_str());
        for (auto& [name, val] : sets) setenv(name.c_str(), val.c_str(), 1);
        std::vector<char*> argv;
        for (size_t j = i; j < args.size(); ++j)
            argv.push_back(const_cast<char*>(args[j].c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

int cmd_environment(Args) {
    for (char** env = get_environ_ptr(); *env; ++env) {
        std::cout << *env << '\n';
    }
    return 0;
}

int cmd_sleep(Args args) {
    if (args.empty()) {
        std::cerr << "Error: sleep requires at least one number" << std::endl;
        return 1;
    }
    double total = 0;
    for (auto& a : args) {
        try {
            total += std::stod(a);
        } catch (...) {
            std::cerr << "Error: Invalid sleep duration: " << a << std::endl;
            return 1;
        }
    }
    if (total > 0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(total));
    }
    return 0;
}

int cmd_time(Args args) {
    if (args.empty()) {
        std::cerr << "Error: time requires a command" << std::endl;
        return 1;
    }
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::string> cmd(args.begin(), args.end());
    int rc = exec_command(cmd);
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "Elapsed time: " << elapsed << " s" << std::endl;
    return rc;
}

int cmd_rm(Args args) {
    bool recursive = false;
    bool force = false;
    size_t start = 0;

    for (; start < args.size(); ++start) {
        if (args[start] == "--") { ++start; break; }
        if (!args[start].starts_with("-")) break;
        for (size_t j = 1; j < args[start].size(); ++j) {
            char c = args[start][j];
            if (c == 'r' || c == 'R') recursive = true;
            else if (c == 'f') force = true;
            else {
                std::cerr << "Error: Unknown flag -" << c << std::endl;
                return 1;
            }
        }
    }

    if (start >= args.size()) {
        if (force) return 0;
        std::cerr << "Error: rm requires at least one path" << std::endl;
        return 1;
    }

    for (size_t i = start; i < args.size(); ++i) {
        std::error_code ec;
        if (!fs::exists(args[i], ec)) {
            if (!force) {
                std::cerr << "Error: " << args[i] << " does not exist" << std::endl;
                return 1;
            }
            continue;
        }
        if (fs::is_directory(args[i], ec) && !recursive) {
            std::cerr << "Error: " << args[i] << " is a directory (use -r to remove)" << std::endl;
            return 1;
        }
        if (recursive) {
            fs::remove_all(args[i], ec);
        } else {
            fs::remove(args[i], ec);
        }
        if (ec && !force) {
            std::cerr << "Error: Failed to remove " << args[i] << ": " << ec.message() << std::endl;
            return 1;
        }
    }
    return 0;
}

int cmd_capabilities(Args) {
    std::cout << "{}" << std::endl;
    return 0;
}

int cmd_true(Args) { return 0; }
int cmd_false(Args) { return 1; }

int cmd_tar(Args args) {
    if (args.size() < 2) {
        std::cerr << "Error: tar requires at least flags and archive name" << std::endl;
        return 1;
    }

    std::string flags = args[0];
    std::string archive_path = args[1];

    bool create = flags.find('c') != std::string::npos;
    bool extract = flags.find('x') != std::string::npos;
    bool list = flags.find('t') != std::string::npos;
    bool verbose = flags.find('v') != std::string::npos;
    bool gzip = flags.find('z') != std::string::npos;
    bool bzip2 = flags.find('j') != std::string::npos;
    bool xz = flags.find('J') != std::string::npos;

    int mode_count = (int)create + (int)extract + (int)list;
    if (mode_count != 1) {
        std::cerr << "Error: tar requires exactly one of c, x, or t" << std::endl;
        return 1;
    }

    if (extract || list) {
        struct archive* a = archive_read_new();
        archive_read_support_format_all(a);
        archive_read_support_filter_all(a);

        if (archive_read_open_filename(a, archive_path.c_str(), 16384) != ARCHIVE_OK) {
            std::cerr << "Error: could not open archive: " << archive_path << ": " << archive_error_string(a) << std::endl;
            archive_read_free(a);
            return 1;
        }

        struct archive* ext = nullptr;
        if (extract) {
            ext = archive_write_disk_new();
            archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
            archive_write_disk_set_standard_lookup(ext);
        }

        struct archive_entry* entry;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            if (verbose || list) {
                std::cout << archive_entry_pathname(entry) << std::endl;
            }
            if (extract) {
                int r = archive_write_header(ext, entry);
                if (r != ARCHIVE_OK) {
                    std::cerr << "Error: " << archive_error_string(ext) << std::endl;
                } else {
                    const void* buff;
                    size_t size;
                    la_int64_t offset;
                    while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                        archive_write_data_block(ext, buff, size, offset);
                    }
                }
                archive_write_finish_entry(ext);
            } else {
                archive_read_data_skip(a);
            }
        }

        if (ext) {
            archive_write_close(ext);
            archive_write_free(ext);
        }
        archive_read_close(a);
        archive_read_free(a);
        return 0;
    }

    // create mode
    struct archive* a = archive_write_new();
    if (gzip) archive_write_add_filter_gzip(a);
    else if (bzip2) archive_write_add_filter_bzip2(a);
    else if (xz) archive_write_add_filter_xz(a);
    else archive_write_add_filter_none(a);
    archive_write_set_format_pax_restricted(a);

    if (archive_write_open_filename(a, archive_path.c_str()) != ARCHIVE_OK) {
        std::cerr << "Error: could not create archive: " << archive_path << ": " << archive_error_string(a) << std::endl;
        archive_write_free(a);
        return 1;
    }

    struct archive* disk = archive_read_disk_new();
    archive_read_disk_set_standard_lookup(disk);

    for (size_t i = 2; i < args.size(); ++i) {
        archive_read_disk_open(disk, args[i].c_str());
        struct archive_entry* entry;
        while (archive_read_next_header2(disk, entry = archive_entry_new()) == ARCHIVE_OK) {
            archive_read_disk_descend(disk);
            if (verbose) {
                std::cout << archive_entry_pathname(entry) << std::endl;
            }
            archive_write_header(a, entry);
            int fd = open(archive_entry_sourcepath(entry), O_RDONLY);
            if (fd >= 0) {
                char buff[16384];
                ssize_t len;
                while ((len = read(fd, buff, sizeof(buff))) > 0) {
                    archive_write_data(a, buff, len);
                }
                close(fd);
            }
            archive_entry_free(entry);
        }
        archive_read_close(disk);
    }
    archive_read_free(disk);
    archive_write_close(a);
    archive_write_free(a);
    return 0;
}

// ---------------------------------------------------------------------------
// Dispatch table
// ---------------------------------------------------------------------------

struct CommandEntry {
    std::string_view name;
    CmdFn fn;
};

const CommandEntry commands[] = {
    {"echo", cmd_echo},
    {"echo_append", cmd_echo_append},
    {"touch", cmd_touch},
    {"touch_nocreate", cmd_touch_nocreate},
    {"remove", cmd_remove},
    {"remove_directory", cmd_remove_directory},
    {"make_directory", cmd_make_directory},
    {"create_symlink", cmd_create_symlink},
    {"create_hardlink", cmd_create_hardlink},
    {"rename", cmd_rename},
    {"copy", cmd_copy},
    {"copy_if_different", cmd_copy_if_different},
    {"copy_if_newer", cmd_copy_if_newer},
    {"copy_directory", cmd_copy_directory},
    {"copy_directory_if_different", cmd_copy_directory_if_different},
    {"copy_directory_if_newer", cmd_copy_directory_if_newer},
    {"cat", cmd_cat},
    {"compare_files", cmd_compare_files},
    {"chdir", cmd_chdir},
    {"env", cmd_env},
    {"environment", cmd_environment},
    {"sleep", cmd_sleep},
    {"time", cmd_time},
    {"rm", cmd_rm},
    {"tar", cmd_tar},
    {"capabilities", cmd_capabilities},
    {"true", cmd_true},
    {"false", cmd_false},
};

} // namespace

int run_tool_mode(std::span<const std::string> args) {
    if (args.empty()) {
        std::cerr << "Error: -E requires a command" << std::endl;
        return 1;
    }

    auto cmd_name = args[0];
    auto cmd_args = args.subspan(1);

    for (auto& entry : commands) {
        if (entry.name == cmd_name) {
            return entry.fn(cmd_args);
        }
    }

    std::cerr << "Error: Unknown -E command: " << cmd_name << std::endl;
    return 1;
}

} // namespace dmake
