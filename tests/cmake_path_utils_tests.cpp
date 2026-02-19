#include <catch2/catch_test_macros.hpp>
#include "dmake/cmake_path_utils.hpp"
#include <filesystem>

using namespace dmake::path_utils;

// --- is_absolute ---

TEST_CASE("path_utils::is_absolute", "[cmake_path_utils]") {
    CHECK(is_absolute("/foo/bar"));
    CHECK(is_absolute("/"));
    CHECK(is_absolute("C:/Windows"));
    CHECK(is_absolute("c:/lower"));
    CHECK(is_absolute("//server/share"));
    CHECK_FALSE(is_absolute(""));
    CHECK_FALSE(is_absolute("foo/bar"));
    CHECK_FALSE(is_absolute("./foo"));
    CHECK_FALSE(is_absolute("../foo"));
    CHECK_FALSE(is_absolute("C:noslash")); // no slash after colon
}

// --- join ---

TEST_CASE("path_utils::join basics", "[cmake_path_utils]") {
    CHECK(join("/usr", "lib") == "/usr/lib");
    CHECK(join("/usr/", "lib") == "/usr/lib");
    CHECK(join("base", "rel") == "base/rel");
    CHECK(join("", "rel") == "rel");
    CHECK(join("base", "") == "base");
    // Absolute rel replaces base
    CHECK(join("/usr/lib", "/etc/conf") == "/etc/conf");
    CHECK(join("anything", "C:/Windows") == "C:/Windows");
}

// --- lexically_normal ---

TEST_CASE("path_utils::lexically_normal removes dots", "[cmake_path_utils]") {
    CHECK(lexically_normal("/foo/./bar") == "/foo/bar");
    CHECK(lexically_normal("/foo/bar/.") == "/foo/bar");
    CHECK(lexically_normal("./foo") == "foo");
    CHECK(lexically_normal(".") == ".");
}

TEST_CASE("path_utils::lexically_normal collapses dotdot", "[cmake_path_utils]") {
    CHECK(lexically_normal("/foo/bar/../baz") == "/foo/baz");
    CHECK(lexically_normal("/foo/bar/../../baz") == "/baz");
    CHECK(lexically_normal("foo/bar/../baz") == "foo/baz");
    CHECK(lexically_normal("foo/../..") == "..");
}

TEST_CASE("path_utils::lexically_normal can't go past root", "[cmake_path_utils]") {
    CHECK(lexically_normal("/..") == "/");
    CHECK(lexically_normal("/../foo") == "/foo");
    CHECK(lexically_normal("C:/../foo") == "C:/foo");
}

TEST_CASE("path_utils::lexically_normal deduplicates slashes", "[cmake_path_utils]") {
    CHECK(lexically_normal("/foo//bar///baz") == "/foo/bar/baz");
    CHECK(lexically_normal("foo//bar") == "foo/bar");
}

TEST_CASE("path_utils::lexically_normal preserves root forms", "[cmake_path_utils]") {
    CHECK(lexically_normal("/") == "/");
    CHECK(lexically_normal("C:/") == "C:/");
    CHECK(lexically_normal("//server/share") == "//server/share");
    CHECK(lexically_normal("//server/share/foo/..") == "//server/share");
}

TEST_CASE("path_utils::lexically_normal empty path", "[cmake_path_utils]") {
    CHECK(lexically_normal("") == ".");
}

TEST_CASE("path_utils::lexically_normal relative dotdot preservation", "[cmake_path_utils]") {
    CHECK(lexically_normal("..") == "..");
    CHECK(lexically_normal("../..") == "../..");
    CHECK(lexically_normal("../foo/bar") == "../foo/bar");
    CHECK(lexically_normal("a/b/../../c") == "c");
    CHECK(lexically_normal("a/b/../../../c") == "../c");
}

// --- parent_path ---

TEST_CASE("path_utils::parent_path", "[cmake_path_utils]") {
    CHECK(parent_path("/foo/bar") == "/foo");
    CHECK(parent_path("/foo") == "/");
    CHECK(parent_path("/") == "/");
    CHECK(parent_path("foo/bar") == "foo");
    CHECK(parent_path("foo") == "");
    CHECK(parent_path("") == "");
    CHECK(parent_path("C:/foo") == "C:/");
    CHECK(parent_path("C:/") == "C:/");
}

// --- filename ---

TEST_CASE("path_utils::filename", "[cmake_path_utils]") {
    CHECK(filename("/foo/bar.txt") == "bar.txt");
    CHECK(filename("/foo/bar/") == "bar");
    CHECK(filename("bar.txt") == "bar.txt");
    CHECK(filename("/") == "");
    CHECK(filename("") == "");
}

// --- make_absolute_and_normal ---

TEST_CASE("path_utils::make_absolute_and_normal", "[cmake_path_utils]") {
    CHECK(make_absolute_and_normal("/base", "rel/file") == "/base/rel/file");
    CHECK(make_absolute_and_normal("/base", "../file") == "/file");
    CHECK(make_absolute_and_normal("/base", "/abs/file") == "/abs/file");
    CHECK(make_absolute_and_normal("/base/sub", "./file") == "/base/sub/file");
}

// --- Equivalence with std::filesystem::path ---

TEST_CASE("path_utils matches std::filesystem for common operations", "[cmake_path_utils]") {
    auto check_normal = [](const char* input) {
        auto ours = lexically_normal(input);
        auto theirs = std::filesystem::path(input).lexically_normal().string();
        // std::filesystem may include trailing '/' for directories — we don't
        // Just compare core semantics
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check_normal("/foo/bar");
    check_normal("/foo/./bar");
    check_normal("/foo/bar/../baz");
    check_normal("/foo/bar/../../baz");
    check_normal("foo/bar/../baz");
    check_normal("/foo//bar");
    check_normal("/");
}

TEST_CASE("path_utils::parent matches std::filesystem", "[cmake_path_utils]") {
    auto check_parent = [](const char* input) {
        auto ours = parent_path(input);
        auto theirs = std::filesystem::path(input).parent_path().string();
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check_parent("/foo/bar");
    check_parent("/foo");
    check_parent("foo/bar");
    check_parent("foo");
}

TEST_CASE("path_utils::filename matches std::filesystem", "[cmake_path_utils]") {
    auto check_fn = [](const char* input) {
        auto ours = filename(input);
        auto theirs = std::filesystem::path(input).filename().string();
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check_fn("/foo/bar.txt");
    check_fn("bar.txt");
    check_fn("/foo/bar");
}
