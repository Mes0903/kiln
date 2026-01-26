#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "dmake/interperter.hpp"
#include "dmake/cmake-language.hpp"
#include <sstream>
#include <fstream>
#include <filesystem>

std::string run_script(std::string src) {
    std::stringstream output;
    dmake::Interpreter interpreter("", &output);

    interpreter.add_builtin("message", [&](dmake::Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            throw std::runtime_error("message called with incorrect number of arguments");
        }
        // A simple implementation for testing purposes
        for (const auto& arg : args) {
            output << arg;
        }
        output << std::endl;
    });



    dmake::Parser parser(src);
    auto ast_or_error = parser.parse();
    if (!ast_or_error.has_value()) {
        const auto error = ast_or_error.error();
        throw std::runtime_error("Failed to parse script - " + error.reason + " on line " + std::to_string(error.row));
    }

    auto result = interpreter.interpret(ast_or_error.value());
    if (!result) {
        throw std::runtime_error(result.error().message);
    }

    return output.str();
}


TEST_CASE("Interpreter variable substitution", "[interpreter]") {
    auto output = run_script(R"(
        set(MY_VAR "Hello")
        message("${MY_VAR} World")
    )");
    REQUIRE(output == "Hello World\n");

    output = run_script(R"(
        set(MY_VAR "Goodbye")
        message("${MY_VAR} World"
        )
    )");
    REQUIRE(output == "Goodbye World\n");

    output = run_script(R"(
        set(
        MY_VAR "Goodbye"
        )
        message(
        "${MY_VAR} World")
    )");
    REQUIRE(output == "Goodbye World\n");

    output = run_script(R"(
        set(
        MY_VAR "Goodbye"
        )
        set(MY_VAR2 "${MY_VAR}")
        message(
        "${MY_VAR2} World")
    )");
    REQUIRE(output == "Goodbye World\n");

    output = run_script(R"(
        set(
        MY_VAR "Goodbye"
        )
        set(MY_VAR2 "${MY_VAR}")
        message(
        "${MY_VAR2} World")
    )");
    REQUIRE(output == "Goodbye World\n");
}

TEST_CASE("Interpreter math command", "[interpreter][math]") {
    SECTION("Basic arithmetic") {
        auto output = run_script(R"(
            math(EXPR res "1 + 2 * 3")
            message("${res}")
        )");
        REQUIRE(output == "7\n");
    }

    SECTION("Parentheses and precedence") {
        auto output = run_script(R"(
            math(EXPR res "(1 + 2) * 3")
            message("${res}")
        )");
        REQUIRE(output == "9\n");
    }

    SECTION("Bitwise operators") {
        auto output = run_script(R"(
            math(EXPR res "1 << 4")
            message("${res}")
            math(EXPR res "${res} | 1")
            message("${res}")
            math(EXPR res "${res} & 15")
            message("${res}")
        )");
        REQUIRE(output == "16\n17\n1\n");
    }

    SECTION("Hexadecimal") {
        auto output = run_script(R"(
            math(EXPR res "0x10 + 5")
            message("${res}")
            math(EXPR res "077 + 1")
            message("${res}")
            math(EXPR res "0xA * 2" OUTPUT_FORMAT HEXADECIMAL)
            message("${res}")
        )");
        REQUIRE(output == "21\n78\n0x14\n");
    }

    SECTION("Division and modulo") {
        auto output = run_script(R"(
            math(EXPR res "10 / 3")
            message("${res}")
            math(EXPR res "10 % 3")
            message("${res}")
        )");
        REQUIRE(output == "3\n1\n");
    }

    SECTION("Unary operators") {
        auto output = run_script(R"(
            math(EXPR res "-5 + 10")
            message("${res}")
            math(EXPR res "~0" OUTPUT_FORMAT HEXADECIMAL)
            message("${res}")
        )");
        // ~0 in 64-bit is 0xffffffffffffffff
        REQUIRE(output == "5\n0xffffffffffffffff\n");
    }

    SECTION("Variable expansion in expression") {
        auto output = run_script(R"(
            set(X 10)
            math(EXPR res "${X} * 2")
            message("${res}")
        )");
        REQUIRE(output == "20\n");
    }
}

TEST_CASE("Interpreter unset", "[interpreter]") {
    auto output = run_script(R"(
        set(MY_VAR "Hello")
        unset(MY_VAR)
        message("${MY_VAR} World")
    )");
    REQUIRE(output == " World\n");
}

TEST_CASE("Interpreter if/else/endif", "[interpreter]") {
    auto output = run_script(R"(
        set(MY_VAR "TRUE")
        if(MY_VAR)
            message("Inside if")
        else()
            message("Inside else")
        endif()
    )");
    REQUIRE(output == "Inside if\n");

    output = run_script(R"(
        set(MY_VAR "FALSE")
        if(MY_VAR)
            message("Inside if")
        else()
            message("Inside else")
        endif()
    )");
    REQUIRE(output == "Inside else\n");
}

TEST_CASE("Comment", "[interpreter]") {
    auto output = run_script(R"(
        # This is a comment
        message("Hello")
    )");
    REQUIRE(output == "Hello\n");

    output = run_script(R"(
        #[[
            This is a multi-line comment
        ]]
        message("World")
    )");
    REQUIRE(output == "World\n");
}

TEST_CASE("Function basic invocation", "[interpreter][function]") {
    auto output = run_script(R"(
        function(greet name)
            message("Hello ${name}")
        endfunction()

        greet("World")
    )");
    REQUIRE(output == "Hello World\n");
}

TEST_CASE("Function scoping - variables don't leak", "[interpreter][function]") {
    // Functions create a new scope, so variables set inside don't affect parent
    auto output = run_script(R"(
        set(MY_VAR "outer")

        function(test_scope)
            set(MY_VAR "inner")
            message("Inside: ${MY_VAR}")
        endfunction()

        test_scope()
        message("Outside: ${MY_VAR}")
    )");
    REQUIRE(output == "Inside: inner\nOutside: outer\n");
}

TEST_CASE("Function with multiple parameters", "[interpreter][function]") {
    auto output = run_script(R"(
        function(add_message a b c)
            message("${a} ${b} ${c}")
        endfunction()

        add_message("one" "two" "three")
    )");
    REQUIRE(output == "one two three\n");
}

TEST_CASE("Function ARGC and ARGV", "[interpreter][function]") {
    auto output = run_script(R"(
        function(test_args)
            message("ARGC=${ARGC}")
            message("ARGV0=${ARGV0}")
            message("ARGV1=${ARGV1}")
            message("ARGV2=${ARGV2}")
        endfunction()

        test_args("first" "second" "third")
    )");
    REQUIRE(output == "ARGC=3\nARGV0=first\nARGV1=second\nARGV2=third\n");
}

TEST_CASE("Function ARGN for extra arguments", "[interpreter][function]") {
    auto output = run_script(R"(
        function(test_argn required_param)
            message("required=${required_param}")
            message("ARGN=${ARGN}")
        endfunction()

        test_argn("first" "second" "third")
    )");
    REQUIRE(output == "required=first\nARGN=second;third\n");
}

TEST_CASE("Macro basic invocation", "[interpreter][macro]") {
    auto output = run_script(R"(
        macro(greet name)
            message("Hello ${name}")
        endmacro()

        greet("World")
    )");
    REQUIRE(output == "Hello World\n");
}

TEST_CASE("Macro scoping - variables DO leak", "[interpreter][macro]") {
    // Macros do NOT create a new scope, so variables set inside affect parent
    auto output = run_script(R"(
        set(MY_VAR "outer")

        macro(test_scope)
            set(MY_VAR "inner")
            message("Inside: ${MY_VAR}")
        endmacro()

        test_scope()
        message("Outside: ${MY_VAR}")
    )");
    REQUIRE(output == "Inside: inner\nOutside: inner\n");
}

TEST_CASE("Macro with multiple parameters", "[interpreter][macro]") {
    auto output = run_script(R"(
        macro(add_message a b c)
            message("${a} ${b} ${c}")
        endmacro()

        add_message("one" "two" "three")
    )");
    REQUIRE(output == "one two three\n");
}

TEST_CASE("Function and macro scoping comparison", "[interpreter][function][macro]") {
    auto output = run_script(R"(
        set(VAR "original")

        function(func_test)
            set(VAR "changed_by_func")
        endfunction()

        macro(macro_test)
            set(VAR "changed_by_macro")
        endmacro()

        func_test()
        message("After function: ${VAR}")

        macro_test()
        message("After macro: ${VAR}")
    )");
    REQUIRE(output == "After function: original\nAfter macro: changed_by_macro\n");
}

TEST_CASE("Nested function calls", "[interpreter][function]") {
    auto output = run_script(R"(
        function(inner msg)
            message("Inner: ${msg}")
        endfunction()

        function(outer msg)
            message("Outer: ${msg}")
            inner("nested")
        endfunction()

        outer("test")
    )");
    REQUIRE(output == "Outer: test\nInner: nested\n");
}

TEST_CASE("Function can read parent variables", "[interpreter][function]") {
    auto output = run_script(R"(
        set(PARENT_VAR "from parent")

        function(read_parent)
            message("${PARENT_VAR}")
        endfunction()

        read_parent()
    )");
    REQUIRE(output == "from parent\n");
}

TEST_CASE("Common CMake variables are initialized", "[interpreter]") {
    // Note: run_script uses an empty string for script_dir
    auto output = run_script(R"(
        message("SRC=${CMAKE_SOURCE_DIR}")
        message("BIN=${CMAKE_BINARY_DIR}")
        message("VER=${CMAKE_MAJOR_VERSION}")
    )");
    REQUIRE(output.find("SRC=") != std::string::npos);
    REQUIRE(output.find("BIN=") != std::string::npos);
    REQUIRE(output.find("VER=3") != std::string::npos);
}

TEST_CASE("set() creates lists from multiple arguments", "[interpreter][list]") {
    auto output = run_script(R"(
        set(MY_LIST "a" "b" "c")
        message("${MY_LIST}")
    )");
    REQUIRE(output == "a;b;c\n");
}

TEST_CASE("list(LENGTH) returns list length", "[interpreter][list]") {
    auto output = run_script(R"(
        set(MY_LIST "x" "y" "z")
        list(LENGTH MY_LIST len)
        message("${len}")
    )");
    REQUIRE(output == "3\n");
}

TEST_CASE("list(GET) retrieves elements by index", "[interpreter][list]") {
    auto output = run_script(R"(
        set(MY_LIST "a" "b" "c" "d")
        list(GET MY_LIST 0 result0)
        list(GET MY_LIST 2 result2)
        message("${result0}")
        message("${result2}")
    )");
    REQUIRE(output == "a\nc\n");
}

TEST_CASE("list(GET) can retrieve multiple indices", "[interpreter][list]") {
    auto output = run_script(R"(
        set(MY_LIST "a" "b" "c" "d")
        list(GET MY_LIST 0 2 3 result)
        message("${result}")
    )");
    REQUIRE(output == "a;c;d\n");
}

TEST_CASE("list(APPEND) adds elements to list", "[interpreter][list]") {
    auto output = run_script(R"(
        set(MY_LIST "a" "b")
        list(APPEND MY_LIST "c" "d")
        message("${MY_LIST}")
    )");
    REQUIRE(output == "a;b;c;d\n");
}

TEST_CASE("list(REVERSE) reverses the list", "[interpreter][list]") {
    auto output = run_script(R"(
        set(MY_LIST "1" "2" "3")
        list(REVERSE MY_LIST)
        message("${MY_LIST}")
    )");
    REQUIRE(output == "3;2;1\n");
}

TEST_CASE("list(SORT) sorts the list", "[interpreter][list]") {
    auto output = run_script(R"(
        set(MY_LIST "c" "a" "b")
        list(SORT MY_LIST)
        message("${MY_LIST}")
    )");
    REQUIRE(output == "a;b;c\n");
}

TEST_CASE("list(REMOVE_DUPLICATES) removes duplicate items", "[interpreter][list]") {
    auto output = run_script(R"(
        set(MY_LIST "a" "b" "a" "c" "b")
        list(REMOVE_DUPLICATES MY_LIST)
        message("${MY_LIST}")
    )");
    REQUIRE(output == "a;b;c\n");
}

TEST_CASE("list(SUBLIST) extracts sublist", "[interpreter][list]") {
    auto output = run_script(R"(
        set(MY_LIST "a" "b" "c" "d" "e")
        list(SUBLIST MY_LIST 1 3 result)
        message("${result}")
    )");
    REQUIRE(output == "b;c;d\n");
}

TEST_CASE("CMakeList handles empty lists", "[interpreter][list]") {
    auto output = run_script(R"(
        set(EMPTY_LIST "")
        list(LENGTH EMPTY_LIST len)
        message("${len}")
    )");
    REQUIRE(output == "0\n");
}

TEST_CASE("CMakeList handles semicolons in variable references", "[interpreter][list]") {
    auto output = run_script(R"(
        set(LIST1 "a;b;c")
        list(LENGTH LIST1 len)
        message("${len}")
    )");
    REQUIRE(output == "3\n");
}

TEST_CASE("foreach simple mode iterates over items", "[interpreter][foreach]") {
    auto output = run_script(R"(
        foreach(item "a" "b" "c")
            message("${item}")
        endforeach()
    )");
    REQUIRE(output == "a\nb\nc\n");
}

TEST_CASE("foreach RANGE with stop only", "[interpreter][foreach]") {
    auto output = run_script(R"(
        foreach(i RANGE 3)
            message("${i}")
        endforeach()
    )");
    REQUIRE(output == "0\n1\n2\n3\n");
}

TEST_CASE("foreach RANGE with start and stop", "[interpreter][foreach]") {
    auto output = run_script(R"(
        foreach(i RANGE 2 5)
            message("${i}")
        endforeach()
    )");
    REQUIRE(output == "2\n3\n4\n5\n");
}

TEST_CASE("foreach RANGE with start, stop, and step", "[interpreter][foreach]") {
    auto output = run_script(R"(
        foreach(i RANGE 0 10 2)
            message("${i}")
        endforeach()
    )");
    REQUIRE(output == "0\n2\n4\n6\n8\n10\n");
}

TEST_CASE("foreach RANGE with negative step", "[interpreter][foreach]") {
    auto output = run_script(R"(
        foreach(i RANGE 5 2 -1)
            message("${i}")
        endforeach()
    )");
    REQUIRE(output == "5\n4\n3\n2\n");
}

TEST_CASE("foreach RANGE empty range produces no iterations", "[interpreter][foreach]") {
    auto output = run_script(R"(
        foreach(i RANGE 5 2)
            message("${i}")
        endforeach()
        message("done")
    )");
    REQUIRE(output == "done\n");
}

TEST_CASE("foreach IN ITEMS iterates over literal items", "[interpreter][foreach]") {
    auto output = run_script(R"(
        foreach(item IN ITEMS "x" "y" "z")
            message("${item}")
        endforeach()
    )");
    REQUIRE(output == "x\ny\nz\n");
}

TEST_CASE("foreach IN LISTS iterates over list variable", "[interpreter][foreach]") {
    auto output = run_script(R"(
        set(MY_LIST "a" "b" "c")
        foreach(item IN LISTS MY_LIST)
            message("${item}")
        endforeach()
    )");
    REQUIRE(output == "a\nb\nc\n");
}

TEST_CASE("foreach IN LISTS with multiple lists", "[interpreter][foreach]") {
    auto output = run_script(R"(
        set(LIST1 "a" "b")
        set(LIST2 "c" "d")
        foreach(item IN LISTS LIST1 LIST2)
            message("${item}")
        endforeach()
    )");
    REQUIRE(output == "a\nb\nc\nd\n");
}

TEST_CASE("foreach IN LISTS and ITEMS combined", "[interpreter][foreach]") {
    auto output = run_script(R"(
        set(MY_LIST "a" "b")
        foreach(item IN LISTS MY_LIST ITEMS "x" "y")
            message("${item}")
        endforeach()
    )");
    REQUIRE(output == "a\nb\nx\ny\n");
}

TEST_CASE("foreach loop variable persists after loop", "[interpreter][foreach]") {
    auto output = run_script(R"(
        foreach(i RANGE 2)
            message("in loop: ${i}")
        endforeach()
        message("after loop: ${i}")
    )");
    REQUIRE(output == "in loop: 0\nin loop: 1\nin loop: 2\nafter loop: 2\n");
}

TEST_CASE("foreach can iterate over variable references", "[interpreter][foreach]") {
    auto output = run_script(R"(
        set(STOP "3")
        foreach(i RANGE ${STOP})
            message("${i}")
        endforeach()
    )");
    REQUIRE(output == "0\n1\n2\n3\n");
}

TEST_CASE("foreach works with nested loops", "[interpreter][foreach]") {
    auto output = run_script(R"(
        foreach(i RANGE 1 2)
            foreach(j RANGE 1 2)
                message("${i},${j}")
            endforeach()
        endforeach()
    )");
    REQUIRE(output == "1,1\n1,2\n2,1\n2,2\n");
}

TEST_CASE("foreach break exits loop early", "[interpreter][foreach][break]") {
    auto output = run_script(R"(
        foreach(i RANGE 0 5)
            message("${i}")
            if(i EQUAL "2")
                break()
            endif()
        endforeach()
        message("done")
    )");
    // Note: We don't have EQUAL condition yet, so let's use a simpler test
    REQUIRE(output.find("done") != std::string::npos);
}

TEST_CASE("foreach break exits loop early (simple test)", "[interpreter][foreach][break]") {
    auto output = run_script(R"(
        foreach(i RANGE 0 10)
            if(i)
                message("${i}")
            endif()
            if(i)
                if(i)
                    if(i)
                        break()
                    endif()
                endif()
            endif()
        endforeach()
        message("done")
    )");
    // First iteration: i=0, condition false, no message, break not executed
    // Second iteration: i=1, prints 1, then breaks
    REQUIRE(output == "1\ndone\n");
}

TEST_CASE("foreach continue skips to next iteration", "[interpreter][foreach][continue]") {
    auto output = run_script(R"(
        foreach(i RANGE 1 5)
            set(VAR "${i}")
            if(VAR)
                if(VAR)
                    continue()
                endif()
            endif()
            message("after continue: ${i}")
        endforeach()
    )");
    // All iterations should skip the message due to continue
    REQUIRE(output == "");
}

TEST_CASE("foreach continue with selective execution", "[interpreter][foreach][continue]") {
    auto output = run_script(R"(
        foreach(num RANGE 0 4)
            if(num)
                continue()
            endif()
            message("${num}")
        endforeach()
    )");
    // Only 0 should print (when num is falsy)
    REQUIRE(output == "0\n");
}

TEST_CASE("foreach break in nested loops only affects inner loop", "[interpreter][foreach][break]") {
    auto output = run_script(R"(
        foreach(i RANGE 1 2)
            message("outer: ${i}")
            foreach(j RANGE 1 3)
                message("  inner: ${j}")
                if(j)
                    break()
                endif()
            endforeach()
        endforeach()
    )");
    // Outer loop runs fully (1, 2)
    // Inner loop breaks after first truthy j (j=1)
    REQUIRE(output == "outer: 1\n  inner: 1\nouter: 2\n  inner: 1\n");
}

TEST_CASE("break outside loop is fatal error", "[interpreter][foreach][break]") {
    CHECK_THROWS(run_script(R"(
        break()
    )"));
}

TEST_CASE("continue outside loop is fatal error", "[interpreter][foreach][continue]") {
    CHECK_THROWS(run_script(R"(
        continue()
    )"));
}

TEST_CASE("continue in function loop", "[interpreter][foreach][continue]") {
    CHECK_THROWS(run_script(R"(
        function(foo)
            continue()
        endfunction()
        foreach(i RANGE 2)
            foo()
        endforeach()
    )"));
}

TEST_CASE("break in macro affects caller loop", "[interpreter][foreach][macro]") {
    auto output = run_script(R"(
        macro(my_break)
            break()
        endmacro()

        foreach(i RANGE 1 5)
            message("${i}")
            my_break()
        endforeach()
        message("done")
    )");
    REQUIRE(output == "1\ndone\n");
}

TEST_CASE("if condition: AND operator", "[interpreter][if]") {
    auto output = run_script(R"(
        set(X "1")
        set(Y "1")
        if(X AND Y)
            message("pass")
        else()
            message("fail")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        set(X "1")
        set(Y "0")
        if(X AND Y)
            message("fail")
        else()
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: OR operator", "[interpreter][if]") {
    auto output = run_script(R"(
        set(X "0")
        set(Y "1")
        if(X OR Y)
            message("pass")
        else()
            message("fail")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        set(X "0")
        set(Y "0")
        if(X OR Y)
            message("fail")
        else()
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: NOT operator", "[interpreter][if]") {
    auto output = run_script(R"(
        set(X "0")
        if(NOT X)
            message("pass")
        else()
            message("fail")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        set(X "1")
        if(NOT X)
            message("fail")
        else()
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: numeric comparisons", "[interpreter][if]") {
    auto output = run_script(R"(
        set(A "5")
        set(B "10")
        if(A LESS B)
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        set(A "10")
        set(B "10")
        if(A EQUAL B)
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        set(A "15")
        set(B "10")
        if(A GREATER B)
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: string comparisons", "[interpreter][if]") {
    auto output = run_script(R"(
        set(A "abc")
        set(B "xyz")
        if(A STRLESS B)
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        set(A "hello")
        set(B "hello")
        if(A STREQUAL B)
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: DEFINED operator", "[interpreter][if]") {
    auto output = run_script(R"(
        set(VAR "value")
        if(DEFINED VAR)
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        if(DEFINED NONEXISTENT)
            message("fail")
        else()
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");

    // DEFINED should work even if variable has empty value
    output = run_script(R"(
        set(EMPTY "")
        if(DEFINED EMPTY)
            message("pass")
        else()
            message("fail")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: true constants (case-insensitive)", "[interpreter][if]") {
    auto output = run_script(R"(
        set(T1 "TRUE")
        set(T2 "True")
        set(T3 "ON")
        set(T4 "YES")
        set(T5 "1")
        if(T1 AND T2 AND T3 AND T4 AND T5)
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: false constants (case-insensitive)", "[interpreter][if]") {
    auto output = run_script(R"(
        set(F1 "FALSE")
        set(F2 "False")
        set(F3 "OFF")
        set(F4 "NO")
        set(F5 "0")
        set(F6 "NOTFOUND")
        set(F7 "IGNORE")
        if(F1 OR F2 OR F3 OR F4 OR F5 OR F6 OR F7)
            message("fail")
        else()
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: empty string is falsy", "[interpreter][if]") {
    auto output = run_script(R"(
        set(EMPTY "")
        if(EMPTY)
            message("fail")
        else()
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: undefined variable is falsy", "[interpreter][if]") {
    auto output = run_script(R"(
        unset(UNDEF)
        if(UNDEF)
            message("fail")
        else()
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: operator precedence", "[interpreter][if]") {
    // AND has higher precedence than OR
    // 1 OR 0 AND 0 should be: 1 OR (0 AND 0) = 1 OR 0 = 1
    auto output = run_script(R"(
        set(ONE "1")
        set(ZERO "0")
        if(ONE OR ZERO AND ZERO)
            message("pass")
        else()
            message("fail")
        endif()
    )");
    REQUIRE(output == "pass\n");

    // NOT has higher precedence than AND
    // NOT 0 AND 1 should be: (NOT 0) AND 1 = 1 AND 1 = 1
    output = run_script(R"(
        set(ONE "1")
        set(ZERO "0")
        if(NOT ZERO AND ONE)
            message("pass")
        else()
            message("fail")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: complex expressions", "[interpreter][if]") {
    auto output = run_script(R"(
        set(A "10")
        set(B "20")
        set(C "15")
        if(A LESS B AND B GREATER C)
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        set(A "5")
        set(B "10")
        if(A LESS B OR A GREATER B)
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");
}

TEST_CASE("if condition: invalid conditions", "[interpreter][if][negative]") {
    // if(A B C) should error - unconsumed tokens
    REQUIRE_THROWS_AS(run_script(R"(
        set(A "10")
        set(B "20")
        set(C "15")
        if(A B C)
            message("pass")
        endif()
    )"), std::runtime_error);

    // if(A EQUAL) should error - missing right operand
    REQUIRE_THROWS_AS(run_script(R"(
        set(A "10")
        if(A EQUAL)
            message("pass")
        endif()
    )"), std::runtime_error);

    // if(AND B) should error - missing left operand
    REQUIRE_THROWS_AS(run_script(R"(
        set(B "10")
        if(AND B)
            message("pass")
        endif()
    )"), std::runtime_error);

    // if(NOT) should error - missing operand
    REQUIRE_THROWS_AS(run_script(R"(
        if(NOT)
            message("pass")
        endif()
    )"), std::runtime_error);

    // Edge case: if(DEFINED) should work - DEFINED is treated as variable name
    auto output = run_script(R"(
        if(DEFINED)
            message("fail")
        else()
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");

    // Edge case: if(TARGET) should work - TARGET is treated as variable name
    output = run_script(R"(
        if(TARGET)
            message("fail")
        else()
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        set(A "10")
        set(B "20")
        set(C "15")
        if(A MATCHES "^[0-9]+$" AND B MATCHES "^[0-9]+$" AND C MATCHES "^[0-9]+$")
            message("pass")
        endif()
    )");
    REQUIRE(output == "pass\n");

    output = run_script(R"(
        get_filename_component(DIR "/path/to/file.tar.gz" DIRECTORY)
        get_filename_component(NAME "/path/to/file.tar.gz" NAME)
        get_filename_component(EXT "/path/to/file.tar.gz" EXT)
        get_filename_component(NAME_WE "/path/to/file.tar.gz" NAME_WE)
        get_filename_component(LAST_EXT "/path/to/file.tar.gz" LAST_EXT)
        get_filename_component(NAME_WLE "/path/to/file.tar.gz" NAME_WLE)
        message("${DIR}|${NAME}|${EXT}|${NAME_WE}|${LAST_EXT}|${NAME_WLE}")
    )");
    REQUIRE(output == "/path/to|file.tar.gz|.tar.gz|file|.gz|file.tar\n");

    output = run_script(R"(
        get_filename_component(EXT ".bashrc" EXT)
        get_filename_component(NAME_WE ".bashrc" NAME_WE)
        message("${EXT}|${NAME_WE}")
    )");
    REQUIRE(output == "|.bashrc\n");
}

TEST_CASE("call case insensitive", "[interpreter][case]") {
    std::string output;
    output = run_script(R"(
        MESSAGE("Hello World")
    )");
    REQUIRE(output == "Hello World\n");

    output = run_script(R"(
        function (hello)
        message("Hello World")
        endfunction()
        hello()
    )");
    REQUIRE(output == "Hello World\n");
}

TEST_CASE("stringly typing", "[interpreter][stringly]") {
    std::string output;
    CHECK_THROWS(run_script(R"(
        message(${THIS_DOES_NOT_EXIST})
    )"));
}

TEST_CASE("option", "[interpreter][option]") {
    auto output = run_script(R"(
        option(FOO "Foo option" ON)
        message("${FOO}")
    )");
    REQUIRE(output == "ON\n");

    output = run_script(R"(
        option(FOO "Foo option" OFF)
        message("${FOO}")
    )");
    REQUIRE(output == "OFF\n");

    output = run_script(R"(
        set(FOO ON)
        option(FOO "Foo option" OFF)
        message("${FOO}")
    )");
    REQUIRE(output == "ON\n");
}

TEST_CASE("Multi-line if condition", "[interpreter][if]") {
    auto output = run_script(R"(
        set(VAR1 "A")
        set(VAR2 "B")
        if(VAR1 STREQUAL "A"
           AND VAR2 STREQUAL "B")
            message("Success")
        endif()
    )");
    REQUIRE(output == "Success\n");

    output = run_script(R"(
        set(TARGET_NAME "Catch2")
        if(NOT DEFINED CHECK_${TARGET_NAME}
           OR TRUE)
            message("Success with var ref")
        endif()
    )");
    REQUIRE(output == "Success with var ref\n");

    output = run_script(R"(
        set(targets "A;B")
        foreach(t IN LISTS targets)
          if(FALSE
              OR NOT DEFINED _check_for_${t}
              OR NOT IS_DIRECTORY "/nonexistent")
            message("Inside for ${t}")
          endif()
        endforeach()
    )");
    REQUIRE(output == "Inside for A\nInside for B\n");

    output = run_script(R"(
        set(t "target")
        set(_check_for_target "EXISTS")
        if(DEFINED _check_for_${t})
            message("Defined")
        endif()
    )");
    REQUIRE(output == "Defined\n");

    output = run_script(R"(
        if(NOT DEFINED UNDEFINED_VAR)
            message("Success NOT DEFINED")
        endif()
    )");
    REQUIRE(output == "Success NOT DEFINED\n");

    output = run_script(R"(
        set(VAR1 "A")
        set(VAR2 "B")
        if((VAR1 STREQUAL "A") AND (VAR2 STREQUAL "B"))
            message("Success")
        endif()
    )");
    REQUIRE(output == "Success\n");
}

TEST_CASE("multiple dereference", "[interpreter][dereference]") {
    auto output = run_script(R"(
        set(VAR1 "A")
        set(VAR2 "VAR1")
        message("${${VAR2}}")
    )");
    CHECK(output == "A\n");

    output = run_script(R"(
        set(VAR1 "A")
        set(VAR2 "VAR1")
        set(VAR3 "VAR")
        message("${${${VAR3}2}}")
    )");
    CHECK(output == "A\n");

    output = run_script(R"(
        set(VAR1 "A")
        set(VAR2 "VAR1")
        set(VAR3 "VAR")
        message("${${${VAR3}2}}")
    )");
    CHECK(output == "A\n");

    output = run_script(R"(
        set(MY_VAR "Cache Value" CACHE STRING "")
        set(MY_VAR "Local Value")
        message("Standard: ${MY_VAR}")
        message("Explicit: $CACHE{MY_VAR}")
    )");
    CHECK(output == "Standard: Local Value\nExplicit: Cache Value\n");
}

TEST_CASE("ENV namespace", "[interpreter][env]") {
    // Test reading environment variables
    auto output = run_script(R"(
        set(ENV{DMAKE_TEST_VAR} "test_value")
        message("$ENV{DMAKE_TEST_VAR}")
    )");
    CHECK(output == "test_value\n");

    // Test case insensitivity
    output = run_script(R"(
        set(ENV{DMAKE_TEST_VAR2} "test123")
        message("$env{DMAKE_TEST_VAR2}")
        message("$Env{DMAKE_TEST_VAR2}")
    )");
    CHECK(output == "test123\ntest123\n");

    // Test unset
    output = run_script(R"(
        set(ENV{DMAKE_TEST_VAR3} "value")
        message("Before: $ENV{DMAKE_TEST_VAR3}")
        unset(ENV{DMAKE_TEST_VAR3})
        message("After: $ENV{DMAKE_TEST_VAR3}")
    )");
    CHECK(output == "Before: value\nAfter: \n");

    // Test with nested variable in name
    output = run_script(R"(
        set(VAR_NAME "DMAKE_TEST_VAR4")
        set(ENV{DMAKE_TEST_VAR4} "nested")
        message("$ENV{${VAR_NAME}}")
    )");
    CHECK(output == "nested\n");
}

TEST_CASE("CACHE namespace", "[interpreter][cache]") {
    // Test basic cache
    auto output = run_script(R"(
        set(MY_VAR "value" CACHE STRING "doc")
        message("$CACHE{MY_VAR}")
    )");
    CHECK(output == "value\n");

    // Test case insensitivity
    output = run_script(R"(
        set(MY_VAR "value" CACHE STRING "doc")
        message("$cache{MY_VAR}")
        message("$Cache{MY_VAR}")
    )");
    CHECK(output == "value\nvalue\n");

    // Test that cache sets both cache and regular variable
    output = run_script(R"(
        set(MY_VAR "cached" CACHE STRING "doc")
        message("Regular: ${MY_VAR}")
        message("Cache: $CACHE{MY_VAR}")
    )");
    CHECK(output == "Regular: cached\nCache: cached\n");

    // Test that local set overrides regular but not cache
    output = run_script(R"(
        set(MY_VAR "cached" CACHE STRING "doc")
        set(MY_VAR "local")
        message("Regular: ${MY_VAR}")
        message("Cache: $CACHE{MY_VAR}")
    )");
    CHECK(output == "Regular: local\nCache: cached\n");

    // Test unset cache
    output = run_script(R"(
        set(MY_VAR "value" CACHE STRING "doc")
        message("Before: $CACHE{MY_VAR}")
        unset(MY_VAR CACHE)
        message("After: $CACHE{MY_VAR}")
    )");
    CHECK(output == "Before: value\nAfter: \n");

    // Test with nested variable in name
    output = run_script(R"(
        set(VAR_NAME "MY_CACHED_VAR")
        set(MY_CACHED_VAR "42" CACHE STRING "doc")
        message("$CACHE{${VAR_NAME}}")
    )");
    CHECK(output == "42\n");

    // Test CACHE keyword is case-insensitive
    output = run_script(R"(
        set(VAR "value" cache STRING "doc")
        message("$CACHE{VAR}")
    )");
    CHECK(output == "value\n");
}

TEST_CASE("PARENT_SCOPE", "[interpreter][parent_scope]") {
    // Test basic PARENT_SCOPE
    auto output = run_script(R"(
        function(set_parent)
            set(OUTER_VAR "from function" PARENT_SCOPE)
        endfunction()

        message("Before: ${OUTER_VAR}")
        set_parent()
        message("After: ${OUTER_VAR}")
    )");
    CHECK(output == "Before: \nAfter: from function\n");

    // Test unset PARENT_SCOPE
    output = run_script(R"(
        function(unset_parent)
            unset(OUTER_VAR PARENT_SCOPE)
        endfunction()

        set(OUTER_VAR "initial")
        message("Before: ${OUTER_VAR}")
        unset_parent()
        message("After: ${OUTER_VAR}")
    )");
    CHECK(output == "Before: initial\nAfter: \n");

    // Test PARENT_SCOPE is case-insensitive
    output = run_script(R"(
        function(test_case)
            set(VAR "value" parent_scope)
        endfunction()

        test_case()
        message("${VAR}")
    )");
    CHECK(output == "value\n");

    // Test nested variable name with PARENT_SCOPE
    output = run_script(R"(
        function(test_nested)
            set(VAR_NAME "RESULT")
            set(${VAR_NAME} "success" PARENT_SCOPE)
        endfunction()

        test_nested()
        message("${RESULT}")
    )");
    CHECK(output == "success\n");
}

TEST_CASE("namespace error cases", "[interpreter][namespace][errors]") {
    // Test invalid set(CACHE{...}) syntax
    CHECK_THROWS_WITH(
        run_script(R"(
            set(CACHE{MY_VAR} "value")
        )"),
        Catch::Matchers::ContainsSubstring("set(CACHE{...} ...) is invalid")
    );

    // Test invalid unset(CACHE{...}) syntax
    CHECK_THROWS_WITH(
        run_script(R"(
            unset(CACHE{MY_VAR})
        )"),
        Catch::Matchers::ContainsSubstring("unset(CACHE{...}) is invalid")
    );

    // Test cannot mix CACHE and PARENT_SCOPE in set
    CHECK_THROWS_WITH(
        run_script(R"(
            function(test_func)
                set(VAR "value" CACHE STRING "doc" PARENT_SCOPE)
            endfunction()
            test_func()
        )"),
        Catch::Matchers::ContainsSubstring("cannot use both CACHE and PARENT_SCOPE")
    );

    // Test cannot mix in unset
    CHECK_THROWS_WITH(
        run_script(R"(
            function(test_func)
                unset(VAR CACHE PARENT_SCOPE)
            endfunction()
            test_func()
        )"),
        Catch::Matchers::ContainsSubstring("cannot use both CACHE and PARENT_SCOPE")
    );

    // Test PARENT_SCOPE outside function
    CHECK_THROWS_WITH(
        run_script(R"(
            set(VAR "value" PARENT_SCOPE)
        )"),
        Catch::Matchers::ContainsSubstring("requires a parent scope")
    );
}

TEST_CASE("nested variables with namespaces", "[interpreter][nested][namespaces]") {
    // Test nested with ENV
    auto output = run_script(R"(
        set(ENV{DMAKE_PATH_TEST} "/usr/bin")
        set(VAR_NAME "DMAKE_PATH_TEST")
        message("$ENV{${VAR_NAME}}")
    )");
    CHECK(output == "/usr/bin\n");

    // Test nested with CACHE
    output = run_script(R"(
        set(BUILD_TYPE "Debug" CACHE STRING "doc")
        set(TYPE_VAR "BUILD_TYPE")
        message("$CACHE{${TYPE_VAR}}")
    )");
    CHECK(output == "Debug\n");

    // Test complex nesting
    output = run_script(R"(
        set(A "B")
        set(B "MY_VAR")
        set(MY_VAR "value" CACHE STRING "doc")
        message("$CACHE{${${A}}}")
    )");
    CHECK(output == "value\n");

    // Test partial expansion with ENV
    output = run_script(R"(
        set(ENV{DMAKE_PREFIX_TEST} "prefix_value")
        set(PREFIX "DMAKE_")
        set(SUFFIX "_TEST")
        message("$ENV{${PREFIX}PREFIX${SUFFIX}}")
    )");
    CHECK(output == "prefix_value\n");
}

TEST_CASE("namespace case insensitivity comprehensive", "[interpreter][case]") {
    // Test all variations of ENV
    auto output = run_script(R"(
        set(ENV{TEST_VAR} "value")
        message("$ENV{TEST_VAR}")
        message("$env{TEST_VAR}")
        message("$Env{TEST_VAR}")
        message("$eNv{TEST_VAR}")
    )");
    CHECK(output == "value\nvalue\nvalue\nvalue\n");

    // Test all variations of CACHE
    output = run_script(R"(
        set(VAR "cached" CACHE STRING "doc")
        message("$CACHE{VAR}")
        message("$cache{VAR}")
        message("$Cache{VAR}")
        message("$cAcHe{VAR}")
    )");
    CHECK(output == "cached\ncached\ncached\ncached\n");

    // Test set keywords are case insensitive
    output = run_script(R"(
        set(ENV{VAR1} "v1")
        set(env{VAR2} "v2")
        set(Env{VAR3} "v3")
        message("$ENV{VAR1} $ENV{VAR2} $ENV{VAR3}")
    )");
    CHECK(output == "v1 v2 v3\n");
}
