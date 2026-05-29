#include "kiln/platform/env.hpp"
#include "kiln/platform/host.hpp"
#include "kiln/platform/process.hpp"

#include <catch2/catch_test_macros.hpp>

#ifndef _WIN32
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
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

#ifndef _WIN32
namespace {

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view name) {
        path_ = std::filesystem::temp_directory_path()
              / (std::string(name) + "-" + std::to_string(static_cast<long long>(::getpid())));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

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
