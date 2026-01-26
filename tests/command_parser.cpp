#include <catch2/catch_test_macros.hpp>
#include "dmake/command_parser.hpp"
#include <vector>
#include <string>

using namespace dmake;

TEST_CASE("CommandParser: basic positional", "[command_parser]") {
    std::vector<std::string> args = {"my_target", "src1.cpp", "src2.cpp"};
    CommandParser parser("test_cmd");
    
    std::string target;
    std::vector<std::string> sources;
    
    parser.add_positional(target, "target");
    parser.add_default_list(sources);
    
    auto res = parser.parse(args);
    REQUIRE(res.has_value());
    CHECK(target == "my_target");
    CHECK(sources == std::vector<std::string>{"src1.cpp", "src2.cpp"});
}

TEST_CASE("CommandParser: flags and keywords", "[command_parser]") {
    std::vector<std::string> args = {"my_lib", "SHARED", "SOURCES", "s1.cpp", "s2.cpp", "VERBOSE"};
    CommandParser parser("add_library");
    
    std::string name;
    bool shared = false;
    bool verbose = false;
    std::vector<std::string> sources;
    
    parser.add_positional(name, "name");
    parser.add_flag("SHARED", shared);
    parser.add_flag("VERBOSE", verbose);
    parser.add_list("SOURCES", sources);
    
    auto res = parser.parse(args);
    REQUIRE(res.has_value());
    CHECK(name == "my_lib");
    CHECK(shared == true);
    CHECK(verbose == true);
    CHECK(sources == std::vector<std::string>{"s1.cpp", "s2.cpp"});
}

TEST_CASE("CommandParser: single values", "[command_parser]") {
    std::vector<std::string> args = {"DESTINATION", "bin", "COMPONENT", "runtime"};
    CommandParser parser("install");
    
    std::string dest;
    std::string comp;
    
    parser.add_value("DESTINATION", dest);
    parser.add_value("COMPONENT", comp);
    
    auto res = parser.parse(args);
    REQUIRE(res.has_value());
    CHECK(dest == "bin");
    CHECK(comp == "runtime");
}

TEST_CASE("CommandParser: multi-lists", "[command_parser]") {
    std::vector<std::string> args = {
        "COMMAND", "echo", "hello",
        "COMMAND", "ls", "-l",
        "WORKING_DIRECTORY", "/tmp"
    };
    CommandParser parser("execute_process");
    
    std::vector<std::vector<std::string>> commands;
    std::string working_dir;
    
    parser.add_multi_list("COMMAND", commands);
    parser.add_value("WORKING_DIRECTORY", working_dir);
    
    auto res = parser.parse(args);
    REQUIRE(res.has_value());
    REQUIRE(commands.size() == 2);
    CHECK(commands[0] == std::vector<std::string>{"echo", "hello"});
    CHECK(commands[1] == std::vector<std::string>{"ls", "-l"});
    CHECK(working_dir == "/tmp");
}

TEST_CASE("CommandParser: missing required positional", "[command_parser]") {
    std::vector<std::string> args = {};
    CommandParser parser("test_cmd");
    
    std::string target;
    parser.add_positional(target, "target", true);
    
    auto res = parser.parse(args);
    REQUIRE_FALSE(res.has_value());
    CHECK(res.error().find("missing required positional argument: target") != std::string::npos);
}

TEST_CASE("CommandParser: optional positional", "[command_parser]") {
    std::vector<std::string> args = {};
    CommandParser parser("test_cmd");
    
    std::string target = "default";
    parser.add_positional(target, "target", false);
    
    auto res = parser.parse(args);
    REQUIRE(res.has_value());
    CHECK(target == "default");
}

TEST_CASE("CommandParser: unknown keyword", "[command_parser]") {
    std::vector<std::string> args = {"UNKNOWN_KEY", "value"};
    CommandParser parser("test_cmd");
    
    auto res = parser.parse(args);
    REQUIRE_FALSE(res.has_value());
    CHECK(res.error().find("unknown argument: UNKNOWN_KEY") != std::string::npos);
}

TEST_CASE("CommandParser: positional stops at keyword", "[command_parser]") {
    std::vector<std::string> args = {"pos1", "KEY", "val"};
    CommandParser parser("test_cmd");
    
    std::string p1, p2;
    std::string key_val;
    
    parser.add_positional(p1, "p1");
    parser.add_positional(p2, "p2", false);
    parser.add_value("KEY", key_val);
    
    auto res = parser.parse(args);
    REQUIRE(res.has_value());
    CHECK(p1 == "pos1");
    CHECK(p2 == "");
    CHECK(key_val == "val");
}

TEST_CASE("CommandParser: default list after positionals", "[command_parser]") {
    std::vector<std::string> args = {"target", "extra1", "extra2"};
    CommandParser parser("test_cmd");
    
    std::string target;
    std::vector<std::string> extras;
    
    parser.add_positional(target, "target");
    parser.add_default_list(extras);
    
    auto res = parser.parse(args);
    REQUIRE(res.has_value());
    CHECK(target == "target");
    CHECK(extras == std::vector<std::string>{"extra1", "extra2"});
}

TEST_CASE("CommandParser: flags do not interrupt positional parsing", "[command_parser]") {
    // Actually, according to docs/command_parser.md:
    // "If a recognized keyword is encountered during this phase, it terminates positional parsing (unless the keyword is a flag)."
    
    std::vector<std::string> args = {"pos1", "MY_FLAG", "pos2"};
    CommandParser parser("test_cmd");
    
    std::string p1, p2;
    bool flag = false;
    
    parser.add_positional(p1, "p1");
    parser.add_positional(p2, "p2");
    parser.add_flag("MY_FLAG", flag);
    
    auto res = parser.parse(args);
    REQUIRE(res.has_value());
    CHECK(p1 == "pos1");
    CHECK(p2 == "pos2");
    CHECK(flag == true);
}
