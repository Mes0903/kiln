#include "host.hpp"

#if defined __FreeBSD__
#include <sys/sysctl.h>
#endif
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <array>
#include <limits.h>

namespace kiln::platform {

namespace {

int stream_fd(StandardStream stream) {
    switch (stream) {
    case StandardStream::input:
        return STDIN_FILENO;
    case StandardStream::output:
        return STDOUT_FILENO;
    case StandardStream::error:
        return STDERR_FILENO;
    }
    return STDOUT_FILENO;
}

} // namespace

bool is_terminal(StandardStream stream) {
    return ::isatty(stream_fd(stream)) != 0;
}

int terminal_width(StandardStream stream) {
    struct winsize ws;
    if (::ioctl(stream_fd(stream), TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 80;
}

std::string executable_path() {
    std::array<char, PATH_MAX> result{};
#ifdef __linux__
    ssize_t count = ::readlink("/proc/self/exe", result.data(), result.size());
    if (count != -1) return std::string(result.data(), static_cast<size_t>(count));
#elif defined __FreeBSD__
    size_t len = result.size();
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, getpid()};
    if (::sysctl(mib, 4, result.data(), &len, nullptr, 0) == 0) return result.data();
#endif
    return "kiln";
}

std::string current_working_directory() {
    std::array<char, 4096> buf{};
    if (!::getcwd(buf.data(), buf.size())) return {};
    return buf.data();
}

std::string host_name() {
    std::array<char, 256> buf{};
    if (::gethostname(buf.data(), buf.size()) == 0) return buf.data();
    return {};
}

long logical_core_count() {
#ifdef _SC_NPROCESSORS_ONLN
    long n = ::sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? n : 1;
#else
    return 1;
#endif
}

HostInfo host_info() {
    HostInfo info;
    struct utsname u;
    if (::uname(&u) == 0) {
        info.system_name = u.sysname;
        info.system_release = u.release;
        info.system_version = u.version;
        info.machine = u.machine;
    }
    return info;
}

} // namespace kiln::platform
