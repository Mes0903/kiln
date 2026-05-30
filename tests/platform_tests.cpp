#include "kiln/platform/env.hpp"
#include "kiln/platform/host.hpp"
#include "kiln/platform/process.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include "kiln/platform/command_line.hpp"
#include <process.h>
#else
#include <unistd.h>
#endif

TEST_CASE("platform environment helpers mutate the current process environment", "[platform]") {
    const std::string name = "KILN_PLATFORM_ENV_TEST_VALUE";

    kiln::platform::unset_env(name);
    REQUIRE_FALSE(kiln::platform::get_env(name).has_value());

    REQUIRE(kiln::platform::set_env(name, "first"));
    REQUIRE(kiln::platform::get_env(name) == "first");

    REQUIRE(kiln::platform::set_env(name, "second"));
    REQUIRE(kiln::platform::get_env(name) == "second");

    REQUIRE(kiln::platform::unset_env(name));
    REQUIRE_FALSE(kiln::platform::get_env(name).has_value());
}

namespace {

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view name) {
        path_ = std::filesystem::temp_directory_path()
              / (std::string(name) + "-" + std::to_string(static_cast<long long>(current_process_id())));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    static int current_process_id() {
#ifdef _WIN32
        return ::_getpid();
#else
        return ::getpid();
#endif
    }

    std::filesystem::path path_;
};

} // namespace

#ifdef _WIN32
TEST_CASE("Windows argv_to_windows_command_line follows MSVC CRT quoting rules", "[platform][win32]") {
    using kiln::platform::argv_to_windows_command_line;

    REQUIRE(argv_to_windows_command_line({"program", "simple"}) == "program simple");
    REQUIRE(argv_to_windows_command_line({"program", "two words", ""}) == R"(program "two words" "")");
    REQUIRE(argv_to_windows_command_line({"program", R"(say"hi)"}) == R"(program "say\"hi")");
    REQUIRE(argv_to_windows_command_line({"program", R"(a\"b)"}) == R"(program "a\\\"b")");
    REQUIRE(argv_to_windows_command_line({"program", R"(C:\path with slash\)"}) == R"(program "C:\path with slash\\")");
}

TEST_CASE("Windows platform run_command captures output from the requested working directory", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-run-command");
    {
        std::ofstream file(temp.path() / "message.txt");
        file << "from working directory\r\n";
    }

    auto result = kiln::platform::run_command({"cmd.exe", "/D", "/S", "/C", "type message.txt"}, temp.path().string());

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.output == "from working directory\r\n");
}

TEST_CASE("Windows platform execute_pipeline captures stdout", "[platform][win32]") {
    std::string output;
    kiln::platform::ProcessOptions options;
    options.output_variable = &output;

    auto result = kiln::platform::execute_pipeline({{"cmd.exe", "/D", "/S", "/C", "echo hello"}}, options);

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{0});
    REQUIRE(result.captured_stdout == "hello\r\n");
}

TEST_CASE("Windows platform execute_pipeline routes shell operators through COMSPEC", "[platform][win32]") {
    std::string output;
    kiln::platform::ProcessOptions options;
    options.output_variable = &output;

    auto result = kiln::platform::execute_pipeline({{"echo", "first", "&", "echo", "second"}}, options);

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{0});
    REQUIRE(result.captured_stdout.find("first") != std::string::npos);
    REQUIRE(result.captured_stdout.find("second") != std::string::npos);
}

TEST_CASE("Windows platform execute_pipeline quotes cmd shell words with embedded quotes", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-pipeline-cmd-quote");
    kiln::platform::ProcessOptions options;
    options.working_dir = temp.path().string();

    auto result = kiln::platform::execute_pipeline({{"echo", R"(safe" & copy nul injected.txt & rem ")", "&", "echo", "done", ">",
                                                     "done.txt"}},
                                                   options);

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{0});
    REQUIRE(std::filesystem::exists(temp.path() / "done.txt"));
    REQUIRE_FALSE(std::filesystem::exists(temp.path() / "injected.txt"));
}

TEST_CASE("Windows platform execute_pipeline prevents percent expansion in cmd shell words", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-pipeline-cmd-percent");
    const std::string name = "KILN_PLATFORM_CMD_PERCENT_INJECT";
    REQUIRE(kiln::platform::set_env(name, R"(" & copy nul injected.txt & rem ")"));

    std::string output;
    kiln::platform::ProcessOptions options;
    options.working_dir = temp.path().string();
    options.output_variable = &output;

    auto result = kiln::platform::execute_pipeline({{"echo", "prefix %" + name + "% suffix", "&", "echo", "done"}}, options);
    kiln::platform::unset_env(name);

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{0});
    REQUIRE(output.find("prefix %" + name + "% suffix") != std::string::npos);
    REQUIRE(output.find("^%" + name + "^%") == std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(temp.path() / "injected.txt"));
}

TEST_CASE("Windows platform execute_pipeline supports shell pipe operators inside a command", "[platform][win32]") {
    std::string output;
    kiln::platform::ProcessOptions options;
    options.output_variable = &output;

    auto result = kiln::platform::execute_pipeline({{"echo", "kiln-pipe-token", "|", "findstr", "kiln-pipe-token"}}, options);

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{0});
    REQUIRE(result.captured_stdout.find("kiln-pipe-token") != std::string::npos);
}

TEST_CASE("Windows platform execute_pipeline supports shell file redirects inside a command", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-pipeline-redirect");
    auto out_path = temp.path() / "redirect.txt";

    kiln::platform::ProcessOptions options;
    options.working_dir = temp.path().string();

    auto result = kiln::platform::execute_pipeline({{"echo", "kiln-redirect-token", ">", out_path.filename().string()}}, options);

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{0});
    std::ifstream file(out_path);
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(contents.find("kiln-redirect-token") != std::string::npos);
}

TEST_CASE("Windows platform run_command resolves native redirects against working directory", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-run-command-native-redirect");
    auto out_path = temp.path() / "native.txt";

    auto result = kiln::platform::run_command({"cmd.exe", "/D", "/S", "/C", "echo native-redirect-token", ">", "native.txt"},
                                             temp.path().string());

    REQUIRE(result.exit_code == 0);
    REQUIRE(std::filesystem::exists(out_path));
    std::ifstream file(out_path);
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(contents.find("native-redirect-token") != std::string::npos);
}

TEST_CASE("Windows platform execute_pipeline resolves output files against working directory", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-pipeline-output-file");
    auto out_path = temp.path() / "pipeline-output.txt";

    kiln::platform::ProcessOptions options;
    options.working_dir = temp.path().string();
    options.output_file = "pipeline-output.txt";

    auto result = kiln::platform::execute_pipeline({{"cmd.exe", "/D", "/S", "/C", "echo pipeline-output-token"}}, options);

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{0});
    REQUIRE(std::filesystem::exists(out_path));
    std::ifstream file(out_path);
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(contents.find("pipeline-output-token") != std::string::npos);
}

TEST_CASE("Windows platform execute_pipeline preserves batch routing with shell operators", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-pipeline-batch");
    auto script_path = temp.path() / "script.cmd";
    {
        std::ofstream script(script_path);
        script << "@echo batch-%1\r\n";
    }

    std::string output;
    kiln::platform::ProcessOptions options;
    options.output_variable = &output;

    auto result = kiln::platform::execute_pipeline({{script_path.string(), "value", "&&", "echo", "after-batch"}}, options);

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{0});
    REQUIRE(result.captured_stdout.find("batch-value") != std::string::npos);
    REQUIRE(result.captured_stdout.find("after-batch") != std::string::npos);
}

TEST_CASE("Windows platform execute_pipeline applies timeout to the whole pipeline", "[platform][win32]") {
    kiln::platform::ProcessOptions options;
    options.timeout = 0.25;

    auto start = std::chrono::steady_clock::now();
    auto result = kiln::platform::execute_pipeline({{"ping.exe", "-n", "6", "127.0.0.1"},
                                                    {"ping.exe", "-n", "6", "127.0.0.1"},
                                                    {"ping.exe", "-n", "6", "127.0.0.1"},
                                                    {"ping.exe", "-n", "6", "127.0.0.1"}},
                                                   options);
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{-1, -1, -1, -1});
    REQUIRE(elapsed < 0.8);
}

TEST_CASE("Windows platform execute_pipeline reports setup errors for missing commands", "[platform][win32]") {
    auto result = kiln::platform::execute_pipeline({{"kiln-platform-command-that-does-not-exist.exe"}});

    REQUIRE(result.exit_codes == std::vector<int>{1});
    REQUIRE(result.setup_error.find("kiln-platform-command-that-does-not-exist.exe") != std::string::npos);
}

TEST_CASE("Windows platform run_foreground prints setup errors for missing commands", "[platform][win32]") {
    std::ostringstream errors;
    auto* previous = std::cerr.rdbuf(errors.rdbuf());
    int exit_code = kiln::platform::run_foreground({"kiln-platform-foreground-missing.exe"});
    std::cerr.rdbuf(previous);

    REQUIRE(exit_code == 1);
    REQUIRE(errors.str().find("kiln-platform-foreground-missing.exe") != std::string::npos);
}

TEST_CASE("Windows platform run_foreground does not treat non-cmd argv as raw shell argv", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-run-foreground-non-cmd-shell-shape");
    auto copied_cmd = temp.path() / "not-cmd.exe";
    auto script_path = temp.path() / "arg with spaces.cmd";

    auto comspec = kiln::platform::get_env("COMSPEC");
    REQUIRE(comspec.has_value());
    REQUIRE(std::filesystem::exists(*comspec));
    std::filesystem::copy_file(*comspec, copied_cmd, std::filesystem::copy_options::overwrite_existing);

    {
        std::ofstream script(script_path);
        script << "@echo off\r\n";
        script << "echo quoted>non-cmd.txt\r\n";
    }

    kiln::platform::ForegroundOptions options;
    options.working_dir = temp.path().string();

    int exit_code = kiln::platform::run_foreground({copied_cmd.string(), "/D", "/S", "/C", script_path.string()}, options);

    REQUIRE(exit_code == 0);
    REQUIRE(std::filesystem::exists(temp.path() / "non-cmd.txt"));
}

TEST_CASE("Windows platform run_foreground applies child environment overrides", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-run-foreground-env");

    kiln::platform::ForegroundOptions options;
    options.working_dir = temp.path().string();
    options.environment.push_back({"KILN_PLATFORM_CHILD_ENV", std::string{"child-value"}});

    int exit_code = kiln::platform::run_foreground({"cmd.exe", "/D", "/S", "/C", "echo %kiln_platform_child_env%>env.txt"}, options);

    REQUIRE(exit_code == 0);
    std::ifstream file(temp.path() / "env.txt");
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(contents == "child-value\r\n");
}

TEST_CASE("Windows platform run_foreground routes shell operators through COMSPEC", "[platform][win32]") {
    TemporaryDirectory temp("kiln-platform-run-foreground-shell");

    kiln::platform::ForegroundOptions options;
    options.working_dir = temp.path().string();

    int exit_code = kiln::platform::run_foreground({"echo", "first", ">", "foreground.txt", "&", "echo", "second", ">>", "foreground.txt"},
                                                   options);

    REQUIRE(exit_code == 0);
    std::ifstream file(temp.path() / "foreground.txt");
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(contents.find("first") != std::string::npos);
    REQUIRE(contents.find("second") != std::string::npos);
}
#else
TEST_CASE("POSIX platform run_command captures output from the requested working directory", "[platform][posix]") {
    TemporaryDirectory temp("kiln-platform-run-command");
    {
        std::ofstream file(temp.path() / "message.txt");
        file << "from working directory\n";
    }

    auto result = kiln::platform::run_command({"cat", "message.txt"}, temp.path().string());

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.output == "from working directory\n");
}

TEST_CASE("POSIX platform execute_pipeline captures stdout from a pipeline", "[platform][posix]") {
    std::string output;
    kiln::platform::ProcessOptions options;
    options.output_variable = &output;

    auto result = kiln::platform::execute_pipeline({{"printf", "hello\n"}, {"tr", "a-z", "A-Z"}}, options);

    REQUIRE(result.setup_error.empty());
    REQUIRE(result.exit_codes == std::vector<int>{0, 0});
    REQUIRE(result.captured_stdout == "HELLO\n");
}

TEST_CASE("POSIX platform execute_pipeline reports setup errors for missing commands", "[platform][posix]") {
    auto result = kiln::platform::execute_pipeline({{"kiln-platform-command-that-does-not-exist"}});

    REQUIRE(result.exit_codes == std::vector<int>{127});
    REQUIRE(result.setup_error.find("exec kiln-platform-command-that-does-not-exist") != std::string::npos);
}

TEST_CASE("POSIX platform host_info is populated", "[platform][posix]") {
    auto info = kiln::platform::host_info();

    REQUIRE_FALSE(info.system_name.empty());
    REQUIRE_FALSE(info.system_release.empty());
    REQUIRE_FALSE(info.system_version.empty());
    REQUIRE_FALSE(info.machine.empty());
}
#endif
