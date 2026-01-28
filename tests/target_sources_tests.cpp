#include <catch2/catch_test_macros.hpp>
#include "dmake/build_system.hpp"
#include "dmake/target.hpp"
#include "dmake/toolchain.hpp"
#include "dmake/gnu_compiler.hpp"
#include "dmake/language.hpp"
#include "dmake/interperter.hpp"
#include "dmake/cmake-language.hpp"
#include "dmake/builtins/registry.hpp"
#include <filesystem>
#include <string>
#include <fstream>

using namespace dmake;

TEST_CASE("target_sources basic", "[target][target_sources]") {
    std::string temp_dir = "build_test_target_sources";
    std::filesystem::create_directories(temp_dir);

    // Create dummy source files
    {
        std::ofstream f1("source1.cpp");
        f1 << "void foo() {}\n";
        std::ofstream f2("source2.cpp");
        f2 << "void bar() {}\n";
    }

    Interpreter interp(".", &std::cout, &std::cerr, nullptr, temp_dir);
    register_target_builtins(interp);

    std::string script = R"(
        add_executable(myapp)
        target_sources(myapp PRIVATE source1.cpp source2.cpp)
    )";

    Parser parser(script);
    auto ast_or_error = parser.parse();
    REQUIRE(ast_or_error.has_value());

    auto result = interp.interpret(ast_or_error.value());
    REQUIRE(result.has_value());

    auto& targets = interp.get_targets();
    REQUIRE(targets.count("myapp") > 0);
    auto app = targets["myapp"];

    // Check that sources were added
    const auto& sources = app->get_property_list("SOURCES", PropertyVisibility::PRIVATE);
    REQUIRE(sources.size() == 2);
    REQUIRE(std::find(sources.begin(), sources.end(), "source1.cpp") != sources.end());
    REQUIRE(std::find(sources.begin(), sources.end(), "source2.cpp") != sources.end());

    // Cleanup
    std::filesystem::remove_all(temp_dir);
    std::filesystem::remove("source1.cpp");
    std::filesystem::remove("source2.cpp");
}

TEST_CASE("target_sources with FILE_SET HEADERS", "[target][target_sources][file_set]") {
    std::string temp_dir = "build_test_target_sources_headers";
    std::filesystem::create_directories(temp_dir);

    // Create dummy files
    {
        std::ofstream f1("lib.cpp");
        f1 << "void lib_func() {}\n";
        std::ofstream f2("lib.hpp");
        f2 << "#pragma once\nvoid lib_func();\n";
    }

    Interpreter interp(".", &std::cout, &std::cerr, nullptr, temp_dir);
    register_target_builtins(interp);

    std::string script = R"(
        add_library(mylib lib.cpp)
        target_sources(mylib
            PUBLIC
                FILE_SET headers
                TYPE HEADERS
                FILES lib.hpp
        )
    )";

    Parser parser(script);
    auto ast_or_error = parser.parse();
    REQUIRE(ast_or_error.has_value());

    auto result = interp.interpret(ast_or_error.value());
    REQUIRE(result.has_value());

    auto& targets = interp.get_targets();
    REQUIRE(targets.count("mylib") > 0);
    auto lib = targets["mylib"];

    // Check that header was added to SOURCES with PUBLIC visibility
    const auto& public_sources = lib->get_property_list("SOURCES", PropertyVisibility::PUBLIC);
    REQUIRE(std::find(public_sources.begin(), public_sources.end(), "lib.hpp") != public_sources.end());

    // Check file set was stored
    const auto& file_sets = lib->get_file_sets();
    REQUIRE(file_sets.size() == 1);
    REQUIRE(file_sets[0].name == "headers");
    REQUIRE(file_sets[0].type == "HEADERS");
    REQUIRE(file_sets[0].visibility == PropertyVisibility::PUBLIC);
    REQUIRE(file_sets[0].files.size() == 1);
    REQUIRE(file_sets[0].files[0] == "lib.hpp");

    // Cleanup
    std::filesystem::remove_all(temp_dir);
    std::filesystem::remove("lib.cpp");
    std::filesystem::remove("lib.hpp");
}

TEST_CASE("target_sources with FILE_SET CXX_MODULES", "[target][target_sources][file_set][modules]") {
    std::string temp_dir = "build_test_target_sources_modules";
    std::filesystem::create_directories(temp_dir);

    // Create dummy files
    {
        std::ofstream f1("main.cpp");
        f1 << "int main() { return 0; }\n";
        std::ofstream f2("math.cppm");
        f2 << "export module math;\nexport int add(int a, int b) { return a + b; }\n";
    }

    Interpreter interp(".", &std::cout, &std::cerr, nullptr, temp_dir);
    register_target_builtins(interp);

    std::string script = R"(
        add_executable(myapp main.cpp)
        target_sources(myapp
            PRIVATE
                FILE_SET cxx_modules
                TYPE CXX_MODULES
                FILES math.cppm
        )
    )";

    Parser parser(script);
    auto ast_or_error = parser.parse();
    REQUIRE(ast_or_error.has_value());

    auto result = interp.interpret(ast_or_error.value());
    REQUIRE(result.has_value());

    auto& targets = interp.get_targets();
    REQUIRE(targets.count("myapp") > 0);
    auto app = targets["myapp"];

    // Check that module was added to SOURCES
    const auto& sources = app->get_property_list("SOURCES", PropertyVisibility::PRIVATE);
    REQUIRE(std::find(sources.begin(), sources.end(), "math.cppm") != sources.end());

    // Check file set was stored
    const auto& file_sets = app->get_file_sets();
    REQUIRE(file_sets.size() == 1);
    REQUIRE(file_sets[0].name == "cxx_modules");
    REQUIRE(file_sets[0].type == "CXX_MODULES");
    REQUIRE(file_sets[0].visibility == PropertyVisibility::PRIVATE);

    // Cleanup
    std::filesystem::remove_all(temp_dir);
    std::filesystem::remove("main.cpp");
    std::filesystem::remove("math.cppm");
}

TEST_CASE("target_sources with multiple visibility scopes", "[target][target_sources]") {
    std::string temp_dir = "build_test_target_sources_multi";
    std::filesystem::create_directories(temp_dir);

    // Create dummy files
    {
        std::ofstream f1("impl.cpp");
        f1 << "void impl() {}\n";
        std::ofstream f2("pub.cpp");
        f2 << "void pub() {}\n";
        std::ofstream f3("iface.cpp");
        f3 << "void iface() {}\n";
    }

    Interpreter interp(".", &std::cout, &std::cerr, nullptr, temp_dir);
    register_target_builtins(interp);

    std::string script = R"(
        add_library(mylib STATIC)
        target_sources(mylib
            PRIVATE impl.cpp
            PUBLIC pub.cpp
            INTERFACE iface.cpp
        )
    )";

    Parser parser(script);
    auto ast_or_error = parser.parse();
    REQUIRE(ast_or_error.has_value());

    auto result = interp.interpret(ast_or_error.value());
    REQUIRE(result.has_value());

    auto& targets = interp.get_targets();
    REQUIRE(targets.count("mylib") > 0);
    auto lib = targets["mylib"];

    // Check visibility
    const auto& priv = lib->get_property_list("SOURCES", PropertyVisibility::PRIVATE);
    const auto& pub = lib->get_property_list("SOURCES", PropertyVisibility::PUBLIC);
    const auto& iface = lib->get_property_list("SOURCES", PropertyVisibility::INTERFACE);

    REQUIRE(std::find(priv.begin(), priv.end(), "impl.cpp") != priv.end());
    REQUIRE(std::find(pub.begin(), pub.end(), "pub.cpp") != pub.end());
    REQUIRE(std::find(iface.begin(), iface.end(), "iface.cpp") != iface.end());

    // Cleanup
    std::filesystem::remove_all(temp_dir);
    std::filesystem::remove("impl.cpp");
    std::filesystem::remove("pub.cpp");
    std::filesystem::remove("iface.cpp");
}
