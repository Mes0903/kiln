#include <catch2/catch_test_macros.hpp>
#include "dmake/gnu_compiler.hpp"

using namespace dmake;

TEST_CASE("GnuCompiler: CXX compile command", "[compiler]") {
    GnuCompiler compiler("g++", Language::CXX);
    CompileContext ctx;
    ctx.source = "main.cpp";
    ctx.output = "main.o";
    ctx.standard = "23";
    ctx.includes = {"include", "/usr/local/include"};
    ctx.definitions = {"DEBUG", "VERSION=1"};
    ctx.is_shared = true;
    
    std::string cmd = compiler.get_compile_command(ctx);
    
    CHECK(cmd.find("g++") == 0);
    CHECK(cmd.find("-std=c++23") != std::string::npos);
    CHECK(cmd.find("-Iinclude") != std::string::npos);
    CHECK(cmd.find("-I/usr/local/include") != std::string::npos);
    CHECK(cmd.find("-DDEBUG") != std::string::npos);
    CHECK(cmd.find("-DVERSION=1") != std::string::npos);
    CHECK(cmd.find("-fPIC") != std::string::npos);
    CHECK(cmd.find("-c -o main.o") != std::string::npos);
    CHECK(cmd.find("main.cpp") != std::string::npos);
}

TEST_CASE("GnuCompiler: C compile command", "[compiler]") {
    GnuCompiler compiler("gcc", Language::C);
    CompileContext ctx;
    ctx.source = "main.c";
    ctx.output = "main.o";
    ctx.standard = "11";
    
    std::string cmd = compiler.get_compile_command(ctx);
    
    CHECK(cmd.find("gcc") == 0);
    CHECK(cmd.find("-std=c11") != std::string::npos);
    CHECK(cmd.find("main.c") != std::string::npos);
}

TEST_CASE("GnuCompiler: Link command", "[compiler]") {
    GnuCompiler compiler("g++", Language::CXX);
    LinkContext ctx;
    ctx.output = "app";
    ctx.objects = {"main.o", "lib.o"};
    ctx.lib_dirs = {"build/lib"};
    ctx.libs = {"m", "pthread"};
    ctx.is_shared = false;
    
    std::string cmd = compiler.get_link_command(ctx);
    
    CHECK(cmd.find("g++") == 0);
    CHECK(cmd.find("-o app") != std::string::npos);
    CHECK(cmd.find("main.o lib.o") != std::string::npos);
    CHECK(cmd.find("-Lbuild/lib") != std::string::npos);
    CHECK(cmd.find("-lm") != std::string::npos);
    CHECK(cmd.find("-lpthread") != std::string::npos);
}
