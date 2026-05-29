#include "host.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>

#include <array>
#include <filesystem>
#include <thread>

namespace kiln::platform {

namespace {

int stream_fd(StandardStream stream) {
    switch (stream) {
    case StandardStream::input:
        return 0;
    case StandardStream::output:
        return 1;
    case StandardStream::error:
        return 2;
    }
    return 1;
}

DWORD stream_handle(StandardStream stream) {
    switch (stream) {
    case StandardStream::input:
        return STD_INPUT_HANDLE;
    case StandardStream::output:
        return STD_OUTPUT_HANDLE;
    case StandardStream::error:
        return STD_ERROR_HANDLE;
    }
    return STD_OUTPUT_HANDLE;
}

} // namespace

bool is_terminal(StandardStream stream) {
    return ::_isatty(stream_fd(stream)) != 0;
}

int terminal_width(StandardStream stream) {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    HANDLE handle = ::GetStdHandle(stream_handle(stream));
    if (handle != INVALID_HANDLE_VALUE && ::GetConsoleScreenBufferInfo(handle, &info)) {
        return info.srWindow.Right - info.srWindow.Left + 1;
    }
    return 80;
}

std::string executable_path() {
    std::array<char, MAX_PATH> path{};
    DWORD len = ::GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (len > 0 && len < path.size()) return std::string(path.data(), len);
    return "kiln";
}

std::string current_working_directory() {
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if (ec) return {};
    return cwd.string();
}

std::string host_name() {
    std::array<char, MAX_COMPUTERNAME_LENGTH + 1> name{};
    DWORD size = static_cast<DWORD>(name.size());
    if (::GetComputerNameA(name.data(), &size)) return std::string(name.data(), size);
    return {};
}

long logical_core_count() {
    auto count = std::thread::hardware_concurrency();
    return count > 0 ? static_cast<long>(count) : 1;
}

HostInfo host_info() {
    HostInfo info;
    info.system_name = "Windows";
#if defined(_M_X64)
    info.machine = "AMD64";
#elif defined(_M_ARM64)
    info.machine = "ARM64";
#elif defined(_M_IX86)
    info.machine = "x86";
#endif
    return info;
}

} // namespace kiln::platform
