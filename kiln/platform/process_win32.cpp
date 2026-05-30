#include "process.hpp"

#include "command_line.hpp"
#include "kiln/build_system.hpp"
#include "kiln/utils.hpp"
#include "env.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace kiln::platform {

namespace {

class unique_handle {
public:
    unique_handle() = default;
    explicit unique_handle(HANDLE handle) : handle_(handle) {}
    ~unique_handle() { reset(); }

    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;

    unique_handle(unique_handle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    HANDLE get() const { return handle_; }
    HANDLE* put() {
        reset();
        return &handle_;
    }
    HANDLE release() { return std::exchange(handle_, nullptr); }

    void reset(HANDLE handle = nullptr) {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(handle_);
        handle_ = handle;
    }

    explicit operator bool() const { return handle_ && handle_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE handle_ = nullptr;
};

struct Pipe {
    unique_handle read;
    unique_handle write;
};

struct ResolvedCommand {
    std::vector<std::string> argv;
    std::string stdin_file;
    std::string stdout_file;
    std::string stderr_file;
    bool stdout_append = false;
    bool stderr_append = false;
};

struct ProcessInstance {
    unique_handle process;
    unique_handle thread;
    unique_handle job;
    DWORD pid = 0;
};

std::wstring utf8_to_wide(const std::string& input) {
    if (input.empty()) return {};
    int size = ::MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "MultiByteToWideChar");
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), size);
    return output;
}

std::string wide_to_utf8(std::wstring_view input) {
    if (input.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "WideCharToMultiByte");
    std::string output(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), size, nullptr, nullptr);
    return output;
}

std::string windows_error_message(DWORD error) {
    wchar_t* message = nullptr;
    DWORD len = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                 error, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::string result = len > 0 ? wide_to_utf8(std::wstring_view(message, len)) : ("Windows error " + std::to_string(error));
    if (message) ::LocalFree(message);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) result.pop_back();
    return result;
}

std::string last_error_message() {
    return windows_error_message(::GetLastError());
}

struct ChildStdioHandles {
    unique_handle stdin_handle;
    unique_handle stdout_handle;
    unique_handle stderr_handle;
    std::array<HANDLE, 3> startup_handles{};
    std::vector<HANDLE> inherited_handles;
};

class unique_proc_thread_attribute_list {
public:
    unique_proc_thread_attribute_list() = default;
    ~unique_proc_thread_attribute_list() {
        if (list_) ::DeleteProcThreadAttributeList(list_);
    }

    unique_proc_thread_attribute_list(const unique_proc_thread_attribute_list&) = delete;
    unique_proc_thread_attribute_list& operator=(const unique_proc_thread_attribute_list&) = delete;

    bool initialize(DWORD attribute_count, std::string& setup_error) {
        SIZE_T bytes = 0;
        ::InitializeProcThreadAttributeList(nullptr, attribute_count, 0, &bytes);
        if (bytes == 0) {
            setup_error = "InitializeProcThreadAttributeList: " + last_error_message();
            return false;
        }

        buffer_.resize(bytes);
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer_.data());
        if (!::InitializeProcThreadAttributeList(list_, attribute_count, 0, &bytes)) {
            setup_error = "InitializeProcThreadAttributeList: " + last_error_message();
            list_ = nullptr;
            buffer_.clear();
            return false;
        }
        return true;
    }

    LPPROC_THREAD_ATTRIBUTE_LIST get() const { return list_; }

private:
    std::vector<char> buffer_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
};

std::string lowercase_ascii(std::string value) {
    for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

std::string path_extension_lower(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return lowercase_ascii(path.substr(dot));
}

std::string path_basename_lower(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    return lowercase_ascii(slash == std::string::npos ? path : path.substr(slash + 1));
}

bool is_batch_file(const std::string& path) {
    std::string ext = path_extension_lower(path);
    return ext == ".bat" || ext == ".cmd";
}

std::string comspec() {
    if (auto value = get_env("COMSPEC"); value && !value->empty()) return *value;
    return "cmd.exe";
}

size_t shell_redirect_prefix_len(const std::string& arg) {
    if (arg.starts_with("2>>")) return 3;
    if (arg.starts_with("1>>")) return 3;
    if (arg.starts_with(">>")) return 2;
    if (arg.starts_with("2>")) return 2;
    if (arg.starts_with("1>")) return 2;
    if (arg.starts_with(">")) return 1;
    if (arg.starts_with("<")) return 1;
    return 0;
}

std::pair<size_t, size_t> find_embedded_redirect(const std::string& arg) {
    for (size_t i = 1; i < arg.size(); ++i) {
        if (arg[i] == '>') {
            if (i + 1 < arg.size() && arg[i + 1] == '>') return {i, 2};
            return {i, 1};
        }
        if (arg[i] == '<') return {i, 1};
    }
    return {std::string::npos, 0};
}

bool is_shell_operator(const std::string& arg) {
    return arg == "|" || arg == "&" || arg == "&&" || arg == "||" || arg == "2>&1";
}

std::string quote_for_cmd(std::string_view arg) {
    // Quote a cmd.exe shell word here, not an MSVC CRT argv word.
    std::string quoted = "\"";
    for (char ch : arg) {
        if (ch == '"') quoted += "\"\"";
        else if (ch == '%' || ch == '!') {
            quoted.push_back('"');
            quoted.push_back('^');
            quoted.push_back(ch);
            quoted.push_back('"');
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
}

std::string escape_unquoted_cmd_word(std::string_view arg) {
    std::string escaped;
    for (char ch : arg) {
        if (ch == '^' || ch == '%' || ch == '!') escaped.push_back('^');
        escaped.push_back(ch);
    }
    return escaped;
}

bool needs_cmd_outer_quotes(std::string_view arg) {
    if (arg.empty()) return true;
    for (char ch : arg) {
        if (std::isspace(static_cast<unsigned char>(ch)) || std::string_view("&|<>()\"").find(ch) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

std::string quote_cmd_shell_word(std::string_view arg) {
    if (needs_cmd_outer_quotes(arg)) return quote_for_cmd(arg);
    return escape_unquoted_cmd_word(arg);
}

std::string join_windows_shell_command(const std::vector<std::string>& command) {
    std::string result;
    for (std::size_t i = 0; i < command.size(); ++i) {
        const auto& arg = command[i];
        if (i > 0) result.push_back(' ');

        if (is_shell_operator(arg)) {
            result += arg;
        } else if (size_t pfx = shell_redirect_prefix_len(arg); pfx > 0) {
            result += arg.substr(0, pfx);
            std::string path = arg.substr(pfx);
            if (!path.empty()) result += quote_cmd_shell_word(kiln::strip_shell_quoting(path));
        } else if (auto [pos, len] = find_embedded_redirect(arg); pos != std::string::npos) {
            result += quote_cmd_shell_word(arg.substr(0, pos));
            result.push_back(' ');
            result += arg.substr(pos, len);
            std::string path = arg.substr(pos + len);
            if (!path.empty()) result += quote_cmd_shell_word(kiln::strip_shell_quoting(path));
        } else {
            if (i == 0 && is_batch_file(kiln::strip_shell_quoting(arg))) result += "call ";
            result += quote_cmd_shell_word(kiln::strip_shell_quoting(arg));
        }
    }
    return result;
}

std::vector<std::string> shell_command_argv(const std::string& command) {
    return {comspec(), "/D", "/S", "/C", command};
}

std::string join_cmd_words(const std::vector<std::string>& argv) {
    std::string result;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) result.push_back(' ');
        result += quote_cmd_shell_word(argv[i]);
    }
    return result;
}

std::vector<std::string> command_for_launch(const std::vector<std::string>& argv) {
    if (argv.empty()) return argv;
    if (is_batch_file(argv[0])) return shell_command_argv("call " + join_cmd_words(argv));
    return argv;
}

bool needs_shell_launch(const std::vector<std::string>& argv) {
    for (const auto& arg : argv) {
        if (is_shell_operator(arg)) return true;
        if (shell_redirect_prefix_len(arg) > 0) return true;
        if (find_embedded_redirect(arg).first != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> command_for_pipeline_launch(const std::vector<std::string>& argv) {
    if (needs_shell_launch(argv)) return shell_command_argv(join_windows_shell_command(argv));
    return argv;
}

bool is_cmd_executable(const std::string& argv0) {
    std::string path = kiln::strip_shell_quoting(argv0);
    std::string lower = lowercase_ascii(path);
    std::string basename = path_basename_lower(path);

    if (lower == "cmd" || lower == "cmd.exe" || basename == "cmd.exe") return true;

    std::string spec = kiln::strip_shell_quoting(comspec());
    if (spec.empty()) return false;
    return lower == lowercase_ascii(spec) || basename == path_basename_lower(spec);
}

bool is_cmd_shell_argv(const std::vector<std::string>& argv) {
    return argv.size() == 5 && is_cmd_executable(argv[0]) && lowercase_ascii(argv[1]) == "/d" && lowercase_ascii(argv[2]) == "/s"
        && lowercase_ascii(argv[3]) == "/c";
}

std::string command_line_for_launch(const std::vector<std::string>& argv) {
    if (is_cmd_shell_argv(argv)) {
        std::vector<std::string> prefix(argv.begin(), argv.begin() + 4);
        return argv_to_windows_command_line(prefix) + " " + argv[4];
    }
    return argv_to_windows_command_line(argv);
}

SECURITY_ATTRIBUTES non_inheritable_security_attributes() {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    return sa;
}

std::optional<Pipe> create_pipe() {
    SECURITY_ATTRIBUTES sa = non_inheritable_security_attributes();
    Pipe pipe;
    if (!::CreatePipe(pipe.read.put(), pipe.write.put(), &sa, 0)) return std::nullopt;
    return pipe;
}

unique_handle open_file_for_child(const std::string& path, DWORD access, DWORD creation, bool append = false) {
    SECURITY_ATTRIBUTES sa = non_inheritable_security_attributes();
    unique_handle file(::CreateFileW(utf8_to_wide(path).c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa,
                                     creation, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (append && file) ::SetFilePointer(file.get(), 0, nullptr, FILE_END);
    return file;
}

unique_handle open_nul_for_child(DWORD access) {
    SECURITY_ATTRIBUTES sa = non_inheritable_security_attributes();
    return unique_handle(::CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                       nullptr));
}

std::string child_file_path(const std::string& path, const std::string& working_dir) {
    if (path.empty() || working_dir.empty()) return path;
    std::filesystem::path file_path(path);
    if (file_path.is_absolute()) return path;
    return (std::filesystem::path(working_dir) / file_path).lexically_normal().string();
}

std::string read_all(HANDLE handle) {
    std::string output;
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
        output.append(buffer.data(), read);
    }
    return output;
}

bool is_valid_handle(HANDLE handle) {
    return handle && handle != INVALID_HANDLE_VALUE;
}

bool duplicate_child_handle(HANDLE source, unique_handle& target, const char* name, std::string& setup_error) {
    if (!is_valid_handle(source)) return true;

    HANDLE duplicate = nullptr;
    if (!::DuplicateHandle(::GetCurrentProcess(), source, ::GetCurrentProcess(), &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        setup_error = std::string("DuplicateHandle ") + name + ": " + last_error_message();
        return false;
    }
    target.reset(duplicate);
    return true;
}

std::optional<ChildStdioHandles> duplicate_child_stdio_handles(HANDLE stdin_source, HANDLE stdout_source, HANDLE stderr_source,
                                                               std::string& setup_error) {
    ChildStdioHandles handles;
    handles.startup_handles = {stdin_source, stdout_source, stderr_source};

    if (!duplicate_child_handle(stdin_source, handles.stdin_handle, "stdin", setup_error)) return std::nullopt;
    if (!duplicate_child_handle(stdout_source, handles.stdout_handle, "stdout", setup_error)) return std::nullopt;
    if (!duplicate_child_handle(stderr_source, handles.stderr_handle, "stderr", setup_error)) return std::nullopt;

    if (handles.stdin_handle) {
        handles.startup_handles[0] = handles.stdin_handle.get();
        handles.inherited_handles.push_back(handles.stdin_handle.get());
    }
    if (handles.stdout_handle) {
        handles.startup_handles[1] = handles.stdout_handle.get();
        handles.inherited_handles.push_back(handles.stdout_handle.get());
    }
    if (handles.stderr_handle) {
        handles.startup_handles[2] = handles.stderr_handle.get();
        handles.inherited_handles.push_back(handles.stderr_handle.get());
    }
    return handles;
}

bool current_process_is_in_job() {
    BOOL in_job = FALSE;
    if (!::IsProcessInJob(::GetCurrentProcess(), nullptr, &in_job)) return false;
    return in_job != FALSE;
}

void register_process(DWORD pid) {
    kiln::register_child_pid(static_cast<int>(pid));
}

void unregister_process(DWORD pid) {
    kiln::unregister_child_pid(static_cast<int>(pid));
}

std::optional<ProcessInstance> launch_process(const std::vector<std::string>& original_argv, const std::string& working_dir, HANDLE stdin_h,
                                              HANDLE stdout_h, HANDLE stderr_h, void* environment, std::string& setup_error) {
    std::vector<std::string> argv = command_for_launch(original_argv);
    if (argv.empty()) {
        setup_error = "Empty command";
        return std::nullopt;
    }

    std::wstring command_line = utf8_to_wide(command_line_for_launch(argv));
    std::wstring cwd = utf8_to_wide(working_dir);

    auto child_handles = duplicate_child_stdio_handles(stdin_h ? stdin_h : ::GetStdHandle(STD_INPUT_HANDLE),
                                                       stdout_h ? stdout_h : ::GetStdHandle(STD_OUTPUT_HANDLE),
                                                       stderr_h ? stderr_h : ::GetStdHandle(STD_ERROR_HANDLE), setup_error);
    if (!child_handles) return std::nullopt;

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_handles->startup_handles[0];
    startup.StartupInfo.hStdOutput = child_handles->startup_handles[1];
    startup.StartupInfo.hStdError = child_handles->startup_handles[2];

    unique_proc_thread_attribute_list attributes;
    BOOL inherit_handles = FALSE;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;
    if (!child_handles->inherited_handles.empty()) {
        if (!attributes.initialize(1, setup_error)) return std::nullopt;
        if (!::UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, child_handles->inherited_handles.data(),
                                         child_handles->inherited_handles.size() * sizeof(HANDLE), nullptr, nullptr)) {
            setup_error = "UpdateProcThreadAttribute handle list: " + last_error_message();
            return std::nullopt;
        }
        startup.lpAttributeList = attributes.get();
        inherit_handles = TRUE;
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }

    PROCESS_INFORMATION pi{};

    auto try_create_process = [&](DWORD extra_flags, PROCESS_INFORMATION& info) -> DWORD {
        std::wstring mutable_command_line = command_line;
        if (::CreateProcessW(nullptr, mutable_command_line.data(), nullptr, nullptr, inherit_handles, flags | extra_flags, environment,
                             working_dir.empty() ? nullptr : cwd.c_str(), &startup.StartupInfo, &info)) {
            return ERROR_SUCCESS;
        }
        return ::GetLastError();
    };

    std::optional<DWORD> breakaway_error;
    bool created_after_breakaway_failed = false;
    DWORD create_error = ERROR_SUCCESS;
    if (current_process_is_in_job()) {
        create_error = try_create_process(CREATE_BREAKAWAY_FROM_JOB, pi);
        if (create_error != ERROR_SUCCESS) {
            breakaway_error = create_error;
            create_error = try_create_process(0, pi);
            created_after_breakaway_failed = create_error == ERROR_SUCCESS;
        }
    } else {
        create_error = try_create_process(0, pi);
    }

    if (create_error != ERROR_SUCCESS) {
        setup_error = "CreateProcessW " + original_argv[0] + ": " + windows_error_message(create_error);
        if (breakaway_error) {
            setup_error = "CreateProcessW " + original_argv[0] + " with CREATE_BREAKAWAY_FROM_JOB: "
                        + windows_error_message(*breakaway_error) + "; retry without breakaway: " + windows_error_message(create_error);
        }
        return std::nullopt;
    }

    ProcessInstance process;
    process.process.reset(pi.hProcess);
    process.thread.reset(pi.hThread);
    process.pid = pi.dwProcessId;
    process.job.reset(::CreateJobObjectW(nullptr, nullptr));
    if (!process.job) {
        setup_error = "CreateJobObjectW: " + last_error_message();
        ::TerminateProcess(process.process.get(), 1);
        return std::nullopt;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(process.job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        setup_error = "SetInformationJobObject: " + last_error_message();
        ::TerminateProcess(process.process.get(), 1);
        return std::nullopt;
    }

    if (!::AssignProcessToJobObject(process.job.get(), process.process.get())) {
        setup_error = "AssignProcessToJobObject";
        if (created_after_breakaway_failed) {
            setup_error += " after CREATE_BREAKAWAY_FROM_JOB failed (" + windows_error_message(*breakaway_error) + ")";
        }
        setup_error += ": " + last_error_message();
        ::TerminateProcess(process.process.get(), 1);
        return std::nullopt;
    }

    register_process(process.pid);
    ::ResumeThread(process.thread.get());
    return process;
}

int wait_for_process(ProcessInstance& process, double timeout_seconds) {
    DWORD timeout_ms = INFINITE;
    if (timeout_seconds > 0.0) {
        timeout_ms = static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::duration<double>(timeout_seconds))
                                            .count());
    }

    DWORD wait_result = ::WaitForSingleObject(process.process.get(), timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        ::TerminateJobObject(process.job.get(), 1);
        ::WaitForSingleObject(process.process.get(), INFINITE);
        unregister_process(process.pid);
        return -1;
    }

    DWORD exit_code = 1;
    ::GetExitCodeProcess(process.process.get(), &exit_code);
    unregister_process(process.pid);
    return static_cast<int>(exit_code);
}

void unregister_finished_process(ProcessInstance& process, int& exit_code) {
    DWORD process_exit_code = 1;
    ::GetExitCodeProcess(process.process.get(), &process_exit_code);
    unregister_process(process.pid);
    exit_code = static_cast<int>(process_exit_code);
}

void terminate_unfinished_process(ProcessInstance& process, int& exit_code) {
    ::TerminateJobObject(process.job.get(), 1);
    ::WaitForSingleObject(process.process.get(), INFINITE);
    unregister_process(process.pid);
    exit_code = -1;
}

void wait_for_pipeline_processes(std::vector<ProcessInstance>& processes, std::vector<int>& exit_codes, double timeout_seconds) {
    auto deadline = std::chrono::steady_clock::time_point::max();
    if (timeout_seconds > 0.0) {
        auto timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(timeout_seconds));
        deadline = std::chrono::steady_clock::now() + timeout;
    }

    std::vector<bool> finished(processes.size(), false);
    std::size_t unfinished = processes.size();
    while (unfinished > 0) {
        for (std::size_t i = 0; i < processes.size(); ++i) {
            if (finished[i]) continue;
            DWORD wait_result = ::WaitForSingleObject(processes[i].process.get(), 0);
            if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_FAILED) {
                unregister_finished_process(processes[i], exit_codes[i]);
                finished[i] = true;
                --unfinished;
            }
        }
        if (unfinished == 0) return;

        if (timeout_seconds > 0.0 && std::chrono::steady_clock::now() >= deadline) {
            for (std::size_t i = 0; i < processes.size(); ++i) {
                if (finished[i]) continue;
                terminate_unfinished_process(processes[i], exit_codes[i]);
                finished[i] = true;
                --unfinished;
            }
            return;
        }

        DWORD wait_ms = 10;
        if (timeout_seconds > 0.0) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            wait_ms = static_cast<DWORD>(std::clamp<long long>(remaining, 1, 10));
        }
        ::Sleep(wait_ms);
    }
}

void terminate_launched_processes(std::vector<ProcessInstance>& processes, std::vector<int>& exit_codes) {
    for (std::size_t i = 0; i < processes.size(); ++i) { terminate_unfinished_process(processes[i], exit_codes[i]); }
}

void apply_redirect(ResolvedCommand& rc, const std::string& op, std::string path) {
    path = kiln::strip_shell_quoting(path);
    if (op == "<") {
        rc.stdin_file = std::move(path);
    } else if (op == ">>" || op == "1>>") {
        rc.stdout_file = std::move(path);
        rc.stdout_append = true;
    } else if (op == ">" || op == "1>") {
        rc.stdout_file = std::move(path);
        rc.stdout_append = false;
    } else if (op == "2>>") {
        rc.stderr_file = std::move(path);
        rc.stderr_append = true;
    } else if (op == "2>") {
        rc.stderr_file = std::move(path);
        rc.stderr_append = false;
    }
}

std::optional<ResolvedCommand> resolve_command_redirects(const std::vector<std::string>& command, bool& needs_shell) {
    ResolvedCommand rc;
    needs_shell = false;

    for (size_t i = 0; i < command.size(); ++i) {
        const auto& arg = command[i];
        if (is_shell_operator(arg)) {
            needs_shell = true;
            return std::nullopt;
        }

        if (size_t pfx = shell_redirect_prefix_len(arg); pfx > 0) {
            std::string op = arg.substr(0, pfx);
            std::string path = arg.substr(pfx);
            if (path.empty() && i + 1 < command.size()) path = command[++i];
            apply_redirect(rc, op, std::move(path));
        } else if (auto [pos, len] = find_embedded_redirect(arg); pos != std::string::npos) {
            std::string pre = arg.substr(0, pos);
            std::string op = arg.substr(pos, len);
            std::string path = arg.substr(pos + len);
            if (path.empty() && i + 1 < command.size()) path = command[++i];
            if (!pre.empty()) rc.argv.push_back(pre);
            apply_redirect(rc, op, std::move(path));
        } else {
            rc.argv.push_back(arg);
        }
    }

    if (rc.argv.empty()) needs_shell = true;
    return rc;
}

CommandResult run_resolved_command(const ResolvedCommand& rc, const std::string& working_dir) {
    Pipe capture;
    auto pipe = create_pipe();
    if (!pipe) return {-1, "Failed to create pipe: " + last_error_message()};
    capture = std::move(*pipe);

    unique_handle stdin_file;
    unique_handle stdout_file;
    unique_handle stderr_file;

    HANDLE stdin_h = ::GetStdHandle(STD_INPUT_HANDLE);
    HANDLE stdout_h = capture.write.get();
    HANDLE stderr_h = capture.write.get();

    if (!rc.stdin_file.empty()) {
        std::string path = child_file_path(rc.stdin_file, working_dir);
        stdin_file = open_file_for_child(path, GENERIC_READ, OPEN_EXISTING);
        if (!stdin_file) return {-1, "Failed to open input file " + rc.stdin_file + ": " + last_error_message()};
        stdin_h = stdin_file.get();
    }
    if (!rc.stdout_file.empty()) {
        std::string path = child_file_path(rc.stdout_file, working_dir);
        stdout_file = open_file_for_child(path, GENERIC_WRITE, OPEN_ALWAYS, rc.stdout_append);
        if (!stdout_file) return {-1, "Failed to open output file " + rc.stdout_file + ": " + last_error_message()};
        if (!rc.stdout_append) ::SetEndOfFile(stdout_file.get());
        stdout_h = stdout_file.get();
    }
    if (!rc.stderr_file.empty()) {
        std::string path = child_file_path(rc.stderr_file, working_dir);
        stderr_file = open_file_for_child(path, GENERIC_WRITE, OPEN_ALWAYS, rc.stderr_append);
        if (!stderr_file) return {-1, "Failed to open error file " + rc.stderr_file + ": " + last_error_message()};
        if (!rc.stderr_append) ::SetEndOfFile(stderr_file.get());
        stderr_h = stderr_file.get();
    }

    std::string setup_error;
    auto process = launch_process(rc.argv, working_dir, stdin_h, stdout_h, stderr_h, nullptr, setup_error);
    capture.write.reset();
    if (!process) return {-1, setup_error};

    auto reader = std::async(std::launch::async, [&] { return read_all(capture.read.get()); });
    int exit_code = wait_for_process(*process, 0.0);
    return {exit_code, reader.get()};
}

std::wstring env_name_key(std::wstring_view entry) {
    std::size_t start = entry.starts_with(L"=") ? 1 : 0;
    std::size_t pos = entry.find(L'=', start);
    std::wstring key(entry.substr(0, pos));
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return key;
}

std::vector<wchar_t> build_environment_block(const std::vector<EnvChange>& changes) {
    if (changes.empty()) return {};

    std::map<std::wstring, std::wstring> merged;
    LPWCH raw_env = ::GetEnvironmentStringsW();
    if (raw_env) {
        for (LPWCH cur = raw_env; *cur; cur += std::wcslen(cur) + 1) {
            std::wstring entry(cur);
            merged[env_name_key(entry)] = std::move(entry);
        }
        ::FreeEnvironmentStringsW(raw_env);
    }

    for (const auto& change : changes) {
        std::wstring name = utf8_to_wide(change.name);
        std::wstring key = env_name_key(name + L"=");
        if (change.value) {
            merged[key] = name + L"=" + utf8_to_wide(*change.value);
        } else {
            merged.erase(key);
        }
    }

    std::vector<std::wstring> entries;
    entries.reserve(merged.size());
    for (auto& [_, entry] : merged) entries.push_back(std::move(entry));
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });

    std::vector<wchar_t> block;
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

} // namespace

CommandResult run_command(const std::vector<std::string>& command, const std::string& working_dir) {
    if (command.empty()) return {-1, "Empty command"};

    bool needs_shell = false;
    auto rc = resolve_command_redirects(command, needs_shell);
    if (needs_shell) {
        ResolvedCommand shell_rc;
        shell_rc.argv = shell_command_argv(join_windows_shell_command(command));
        return run_resolved_command(shell_rc, working_dir);
    }

    return run_resolved_command(*rc, working_dir);
}

PipelineResult execute_pipeline(const std::vector<std::vector<std::string>>& commands, const ProcessOptions& options) {
    if (commands.empty()) return {};

    PipelineResult result;
    result.exit_codes.assign(commands.size(), -1);

    std::vector<Pipe> pipes;
    for (std::size_t i = 0; i + 1 < commands.size(); ++i) {
        auto pipe = create_pipe();
        if (!pipe) {
            result.exit_codes = {1};
            result.setup_error = "Failed to create pipe: " + last_error_message();
            return result;
        }
        pipes.push_back(std::move(*pipe));
    }

    std::optional<Pipe> stdout_capture;
    std::optional<Pipe> stderr_capture;
    if (options.output_variable) {
        stdout_capture = create_pipe();
        if (!stdout_capture) {
            result.exit_codes = {1};
            result.setup_error = "Failed to create stdout pipe: " + last_error_message();
            return result;
        }
    }
    if (options.error_variable) {
        stderr_capture = create_pipe();
        if (!stderr_capture) {
            result.exit_codes = {1};
            result.setup_error = "Failed to create stderr pipe: " + last_error_message();
            return result;
        }
    }

    unique_handle input_file;
    unique_handle output_file;
    unique_handle error_file;
    unique_handle output_nul;
    unique_handle error_nul;

    if (!options.input_file.empty()) {
        std::string path = child_file_path(options.input_file, options.working_dir);
        input_file = open_file_for_child(path, GENERIC_READ, OPEN_EXISTING);
        if (!input_file) {
            result.exit_codes = {1};
            result.setup_error = "Failed to open INPUT_FILE " + options.input_file + ": " + last_error_message();
            return result;
        }
    }
    if (!options.output_file.empty()) {
        std::string path = child_file_path(options.output_file, options.working_dir);
        output_file = open_file_for_child(path, GENERIC_WRITE, CREATE_ALWAYS);
        if (!output_file) {
            result.exit_codes = {1};
            result.setup_error = "Failed to open OUTPUT_FILE " + options.output_file + ": " + last_error_message();
            return result;
        }
    }
    if (!options.error_file.empty()) {
        std::string path = child_file_path(options.error_file, options.working_dir);
        error_file = open_file_for_child(path, GENERIC_WRITE, CREATE_ALWAYS);
        if (!error_file) {
            result.exit_codes = {1};
            result.setup_error = "Failed to open ERROR_FILE " + options.error_file + ": " + last_error_message();
            return result;
        }
    }
    if (options.output_quiet) output_nul = open_nul_for_child(GENERIC_WRITE);
    if (options.error_quiet) error_nul = open_nul_for_child(GENERIC_WRITE);

    std::vector<ProcessInstance> processes;
    processes.reserve(commands.size());

    for (std::size_t i = 0; i < commands.size(); ++i) {
        HANDLE stdin_h = ::GetStdHandle(STD_INPUT_HANDLE);
        HANDLE stdout_h = ::GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE stderr_h = ::GetStdHandle(STD_ERROR_HANDLE);

        if (i == 0) {
            if (input_file) stdin_h = input_file.get();
        } else {
            stdin_h = pipes[i - 1].read.get();
        }

        if (i + 1 < commands.size()) {
            stdout_h = pipes[i].write.get();
        } else if (stdout_capture) {
            stdout_h = stdout_capture->write.get();
        } else if (output_file) {
            stdout_h = output_file.get();
        } else if (output_nul) {
            stdout_h = output_nul.get();
        }

        if (stderr_capture) {
            stderr_h = stderr_capture->write.get();
        } else if (error_file) {
            stderr_h = error_file.get();
        } else if (error_nul) {
            stderr_h = error_nul.get();
        }

        auto launch_argv = command_for_pipeline_launch(commands[i]);
        std::string setup_error;
        auto process = launch_process(launch_argv, options.working_dir, stdin_h, stdout_h, stderr_h, nullptr, setup_error);
        if (!process) {
            terminate_launched_processes(processes, result.exit_codes);
            result.exit_codes = {1};
            result.setup_error = setup_error;
            return result;
        }
        processes.push_back(std::move(*process));
    }

    for (auto& pipe : pipes) {
        pipe.read.reset();
        pipe.write.reset();
    }
    if (stdout_capture) stdout_capture->write.reset();
    if (stderr_capture) stderr_capture->write.reset();

    auto stdout_reader = stdout_capture ? std::async(std::launch::async, [&] { return read_all(stdout_capture->read.get()); })
                                        : std::future<std::string>{};
    auto stderr_reader = stderr_capture ? std::async(std::launch::async, [&] { return read_all(stderr_capture->read.get()); })
                                        : std::future<std::string>{};

    wait_for_pipeline_processes(processes, result.exit_codes, options.timeout);

    if (stdout_capture) result.captured_stdout = stdout_reader.get();
    if (stderr_capture) result.captured_stderr = stderr_reader.get();

    if (options.output_variable) *options.output_variable = result.captured_stdout;
    if (options.error_variable) *options.error_variable = result.captured_stderr;
    return result;
}

int run_foreground(const std::vector<std::string>& command, const ForegroundOptions& options) {
    if (command.empty()) return 1;

    std::vector<wchar_t> env_block = build_environment_block(options.environment);
    auto launch_argv = is_cmd_shell_argv(command) ? command : command_for_pipeline_launch(command);
    std::string setup_error;
    auto process = launch_process(launch_argv, options.working_dir, ::GetStdHandle(STD_INPUT_HANDLE), ::GetStdHandle(STD_OUTPUT_HANDLE),
                                  ::GetStdHandle(STD_ERROR_HANDLE), env_block.empty() ? nullptr : env_block.data(), setup_error);
    if (!process) {
        std::cerr << setup_error << '\n';
        return 1;
    }
    return wait_for_process(*process, 0.0);
}

int replace_current_process(const std::vector<std::string>& command) {
    return run_foreground(command);
}

bool terminate_process(int pid, int) {
    unique_handle process(::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid)));
    if (!process) return false;
    return ::TerminateProcess(process.get(), 1) != 0;
}

} // namespace kiln::platform
