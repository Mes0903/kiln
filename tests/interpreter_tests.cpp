#include <catch2/catch_test_macros.hpp>
#include "dmake/interperter.hpp"
#include "dmake/cmake-language.hpp"
#include <sstream>

std::string run_script(std::string src) {
    std::stringstream output;
    dmake::Interpreter interpreter("", &output);

    interpreter.add_builtin("message", [&](const std::vector<dmake::Argument>& args) {
        // A simple implementation for testing purposes
        for (const auto& arg : args) {
            output << interpreter.evaluate_argument(arg);
        }
        output << std::endl;
    });



    dmake::Parser parser(src);
    auto ast_or_error = parser.parse();
    if (!ast_or_error.has_value()) {
        throw std::runtime_error("Failed to parse script");
    }

    interpreter.interpret(ast_or_error.value());

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
