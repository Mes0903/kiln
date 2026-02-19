#include <catch2/catch_test_macros.hpp>
#include "dmake/path.hpp"
#include <filesystem>

using dmake::Path;
namespace fs = std::filesystem;

// --- Construction and access ---

TEST_CASE("Path construction and access", "[path]") {
    Path from_string(std::string("/foo/bar"));
    CHECK(from_string.str() == "/foo/bar");

    Path from_sv(std::string_view("/foo/bar"));
    CHECK(from_sv.str() == "/foo/bar");

    Path from_cstr("/foo/bar");
    CHECK(from_cstr.c_str() == std::string("/foo/bar"));

    Path empty;
    CHECK(empty.empty());
    CHECK(empty.str().empty());

    Path nonempty("/x");
    CHECK_FALSE(nonempty.empty());
}

TEST_CASE("Path::view returns correct view", "[path]") {
    Path p("/foo/bar");
    std::string_view v = p.view();
    CHECK(v == "/foo/bar");
    CHECK(v.data() == p.str().data());
}

TEST_CASE("Path::fs_path round-trips", "[path]") {
    Path p("/foo/bar/baz.txt");
    auto fsp = p.fs_path();
    CHECK(fsp.string() == p.str());
}

TEST_CASE("Path::release moves string out", "[path]") {
    Path p("/foo/bar");
    std::string s = std::move(p).release();
    CHECK(s == "/foo/bar");
}

// --- Semantic equivalence with std::filesystem::path ---

TEST_CASE("Path::filename matches std::filesystem", "[path]") {
    auto check = [](const char* input) {
        Path p(input);
        auto ours = p.filename();
        auto theirs = fs::path(input).filename().string();
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check("/foo/bar.txt");
    check("bar.txt");
    check("/foo/bar");
    check("/foo/");
    check("/");
    check("");
    check(".");
    check("..");
    check(".gitignore");
    check("/path/to/.gitignore");
    check("foo.tar.gz");
    check("/a/b/c/");
}

TEST_CASE("Path::parent_path matches std::filesystem", "[path]") {
    auto check = [](const char* input) {
        Path p(input);
        auto ours = p.parent_path();
        auto theirs = fs::path(input).parent_path().string();
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check("/foo/bar");
    check("/foo");
    check("foo/bar");
    check("foo");
    check("/foo/bar.txt");
    check("/a/b/c");
}

TEST_CASE("Path::stem matches std::filesystem", "[path]") {
    auto check = [](const char* input) {
        Path p(input);
        auto ours = p.stem();
        auto theirs = fs::path(input).stem().string();
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check("/foo/bar.txt");
    check("bar.txt");
    check("bar");
    check(".gitignore");
    check("/path/to/.gitignore");
    check("foo.tar.gz");
    check(".");
    check("..");
    check("");
    check("/foo/bar.cpp");
    check("no_ext");
}

TEST_CASE("Path::extension matches std::filesystem", "[path]") {
    auto check = [](const char* input) {
        Path p(input);
        auto ours = p.extension();
        auto theirs = fs::path(input).extension().string();
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check("/foo/bar.txt");
    check("bar.txt");
    check("bar");
    check(".gitignore");
    check("/path/to/.gitignore");
    check("foo.tar.gz");
    check(".");
    check("..");
    check("");
    check("/foo/bar.cpp");
    check("no_ext");
    check(".hidden");
    check("a.b.c.d");
}

TEST_CASE("Path::has_extension matches std::filesystem", "[path]") {
    auto check = [](const char* input) {
        Path p(input);
        auto ours = p.has_extension();
        auto theirs = fs::path(input).has_extension();
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check("/foo/bar.txt");
    check("bar");
    check(".gitignore");
    check("foo.tar.gz");
    check(".");
    check("..");
    check("");
}

TEST_CASE("Path::is_absolute matches std::filesystem", "[path]") {
    auto check = [](const char* input) {
        Path p(input);
        auto ours = p.is_absolute();
        auto theirs = fs::path(input).is_absolute();
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check("/foo/bar");
    check("/");
    check("");
    check("foo/bar");
    check("./foo");
    check("../foo");
}

TEST_CASE("Path::is_absolute Windows drive letters", "[path]") {
    CHECK(Path("C:/Windows").is_absolute());
    CHECK(Path("c:/lower").is_absolute());
    CHECK_FALSE(Path("C:noslash").is_absolute());
}

TEST_CASE("Path::is_relative", "[path]") {
    CHECK(Path("foo/bar").is_relative());
    CHECK_FALSE(Path("/foo/bar").is_relative());
}

TEST_CASE("Path::lexically_normal matches std::filesystem", "[path]") {
    auto check = [](const char* input) {
        Path p(input);
        auto ours = p.lexically_normal().str();
        auto theirs = fs::path(input).lexically_normal().string();
        INFO("input: " << input << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check("/foo/bar");
    check("/foo/./bar");
    check("/foo/bar/../baz");
    check("/foo/bar/../../baz");
    check("foo/bar/../baz");
    check("/foo//bar");
    check("/");
    check(".");
    check("..");
    check("../foo/bar");
    check("a/b/../../c");
    check("a/b/../../../c");
    check("foo/../..");
}

TEST_CASE("Path::lexically_normal edge cases", "[path]") {
    CHECK(Path("").lexically_normal().str() == ".");
    CHECK(Path("/..").lexically_normal().str() == "/");
    CHECK(Path("/../foo").lexically_normal().str() == "/foo");
    CHECK(Path("/foo//bar///baz").lexically_normal().str() == "/foo/bar/baz");
    CHECK(Path("../..").lexically_normal().str() == "../..");
}

TEST_CASE("Path::operator/ matches std::filesystem", "[path]") {
    auto check = [](const char* a, const char* b) {
        Path p(a);
        auto ours = (p / b).str();
        auto theirs = (fs::path(a) / b).string();
        INFO("a: " << a << " b: " << b << " ours: " << ours << " theirs: " << theirs);
        CHECK(ours == theirs);
    };

    check("/usr", "lib");
    check("/usr/", "lib");
    check("base", "rel");
    check("", "rel");
    // Note: ("base", "") differs — fs::path appends trailing slash, we don't.
    // This is intentional; trailing slashes cause issues in our path ops.
    // Absolute rhs replaces
    check("/usr/lib", "/etc/conf");
}

// --- join static helper ---

TEST_CASE("Path::join basics", "[path]") {
    CHECK(Path::join("/usr", "lib") == "/usr/lib");
    CHECK(Path::join("/usr/", "lib") == "/usr/lib");
    CHECK(Path::join("base", "rel") == "base/rel");
    CHECK(Path::join("", "rel") == "rel");
    CHECK(Path::join("base", "") == "base");
    CHECK(Path::join("/usr/lib", "/etc/conf") == "/etc/conf");
    CHECK(Path::join("anything", "C:/Windows") == "C:/Windows");
}

// --- make_absolute_and_normal ---

TEST_CASE("Path::make_absolute_and_normal", "[path]") {
    CHECK(Path::make_absolute_and_normal("/base", "rel/file") == "/base/rel/file");
    CHECK(Path::make_absolute_and_normal("/base", "../file") == "/file");
    CHECK(Path::make_absolute_and_normal("/base", "/abs/file") == "/abs/file");
    CHECK(Path::make_absolute_and_normal("/base/sub", "./file") == "/base/sub/file");
}

// --- replace_extension ---

TEST_CASE("Path::replace_extension", "[path]") {
    CHECK(Path("foo.cpp").replace_extension(".o").str() == "foo.o");
    CHECK(Path("foo.tar.gz").replace_extension(".xz").str() == "foo.tar.xz");
    CHECK(Path("foo").replace_extension(".o").str() == "foo.o");
    CHECK(Path("/a/b/foo.cpp").replace_extension(".o").str() == "/a/b/foo.o");
    CHECK(Path("foo.cpp").replace_extension("").str() == "foo");
}

// --- relative_path ---

TEST_CASE("Path::relative_path", "[path]") {
    CHECK(Path("/foo/bar").relative_path() == "foo/bar");
    CHECK(Path("foo/bar").relative_path() == "foo/bar");
    CHECK(Path("/").relative_path() == "");
    CHECK(Path("").relative_path() == "");
    CHECK(Path("C:/foo/bar").relative_path() == "foo/bar");
}

// --- Comparison operators ---

TEST_CASE("Path comparison", "[path]") {
    Path a("/foo/bar");
    Path b("/foo/bar");
    Path c("/foo/baz");

    CHECK(a == b);
    CHECK(a != c);
    CHECK(a == std::string_view("/foo/bar"));
    CHECK(a != std::string_view("/foo/baz"));
}
