#include <catch2/catch_test_macros.hpp>
#include "kiln/target.hpp"
#include "kiln/genex_evaluator.hpp"
#include "kiln/interperter.hpp"
#include "kiln/build_system.hpp"
#include "kiln/cmake-language.hpp"
#include "kiln/msvc_compiler.hpp"
#include "kiln/builtins/registry.hpp"
#include <filesystem>
#include <sstream>
#include <variant>

using namespace kiln;

// Helper: interpret a script, resolve all targets, return them + stderr output
static auto run_and_resolve(const std::string& script) {
    std::string temp_dir = "build_test_resolve";
    std::filesystem::create_directories(temp_dir);

    std::ostringstream out, err;
    Interpreter interp(".", &out, &err, temp_dir);
    register_target_builtins(interp);

    // Set variables that resolve/genex evaluation needs
    interp.set_variable("CMAKE_BUILD_TYPE", "Debug");
    interp.set_variable("CMAKE_SYSTEM_NAME", "Linux");
    interp.set_variable("CMAKE_CXX_COMPILER_ID", "GNU");
    interp.set_variable("CMAKE_C_COMPILER_ID", "GNU");
    interp.set_variable("CMAKE_OBJECT_FILE_SUFFIX", ".o");
    interp.set_variable("CMAKE_EXECUTABLE_SUFFIX", "");
    interp.set_variable("CMAKE_SHARED_LIBRARY_PREFIX", "lib");
    interp.set_variable("CMAKE_SHARED_LIBRARY_SUFFIX", ".so");
    interp.set_variable("CMAKE_IMPORT_LIBRARY_PREFIX", "");
    interp.set_variable("CMAKE_IMPORT_LIBRARY_SUFFIX", "");
    interp.set_variable("CMAKE_STATIC_LIBRARY_PREFIX", "lib");
    interp.set_variable("CMAKE_STATIC_LIBRARY_SUFFIX", ".a");

    interp.set_source_view(script);
    Parser parser(script);
    auto ast = parser.parse();
    REQUIRE(ast.has_value());
    auto result = interp.interpret(*ast);
    REQUIRE(result.has_value());

    // Resolve all targets
    auto& targets = interp.get_targets();
    for (auto& [name, target] : targets) { target->resolve(targets, interp); }

    // Deferred circular dep pass
    for (auto& [name, target] : targets) { target->resolve_deferred_circular_deps(targets); }

    std::filesystem::remove_all(temp_dir);
    return std::make_pair(std::move(targets), err.str());
}

static bool contains(const std::vector<std::string>& vec, const std::string& val) {
    return std::find(vec.begin(), vec.end(), val) != vec.end();
}

static void configure_windows_msvc(Interpreter& interp) {
    interp.set_variable("CMAKE_BUILD_TYPE", "Debug");
    interp.set_variable("CMAKE_SYSTEM_NAME", "Windows");
    interp.set_variable("CMAKE_CXX_COMPILER_ID", "MSVC");
    interp.set_variable("CMAKE_C_COMPILER_ID", "MSVC");
    interp.set_variable("CMAKE_OBJECT_FILE_SUFFIX", ".obj");
    interp.set_variable("CMAKE_EXECUTABLE_SUFFIX", ".exe");
    interp.set_variable("CMAKE_SHARED_LIBRARY_PREFIX", "");
    interp.set_variable("CMAKE_SHARED_LIBRARY_SUFFIX", ".dll");
    interp.set_variable("CMAKE_IMPORT_LIBRARY_PREFIX", "");
    interp.set_variable("CMAKE_IMPORT_LIBRARY_SUFFIX", ".lib");
    interp.set_variable("CMAKE_STATIC_LIBRARY_PREFIX", "");
    interp.set_variable("CMAKE_STATIC_LIBRARY_SUFFIX", ".lib");
    interp.get_toolchain().set_compiler(Language::CXX, make_compiler("MSVC", "cl.exe", Language::CXX));
}

TEST_CASE("Target artifacts expose Windows MSVC shared runtime and import library", "[target][artifacts][windows]") {
    Target target("core", TargetType::SHARED_LIBRARY, "/src", "/build");
    Target static_target("core_static", TargetType::STATIC_LIBRARY, "/src", "/build");

    GenexEvaluationContext ctx;
    ctx.cxx_compiler_id = "MSVC";
    ctx.shared_library_prefix = "";
    ctx.shared_library_suffix = ".dll";
    ctx.import_library_prefix = "";
    ctx.import_library_suffix = ".lib";
    ctx.static_library_prefix = "";
    ctx.static_library_suffix = ".lib";
    GenexEvaluator eval(ctx);

    auto artifacts = target.get_artifacts(&eval);
    REQUIRE(artifacts.runtime == "/build/core.dll");
    REQUIRE(artifacts.import_library == "/build/core.lib");
    REQUIRE(artifacts.linker == "/build/core.lib");
    REQUIRE(target.get_runtime_artifact_path(&eval) == "/build/core.dll");
    REQUIRE(target.get_linker_artifact_path(&eval) == "/build/core.lib");
    REQUIRE(target.get_import_library_path(&eval) == "/build/core.lib");
    REQUIRE(target.get_output_path(&eval) == "/build/core.dll");

    auto static_artifacts = static_target.get_artifacts(&eval);
    REQUIRE(static_artifacts.archive == "/build/core_static.lib");
    REQUIRE(static_artifacts.linker == "/build/core_static.lib");
    REQUIRE(static_target.get_output_path(&eval) == "/build/core_static.lib");
}

TEST_CASE("Windows MSVC shared library consumers link the import library artifact", "[target][artifacts][windows]") {
    std::string temp_dir = "build_test_windows_import_artifact";
    std::filesystem::create_directories(temp_dir);

    std::ostringstream out, err;
    Interpreter interp(".", &out, &err, temp_dir);
    register_target_builtins(interp);
    configure_windows_msvc(interp);

    const std::string script = R"(
        add_library(core SHARED core.cpp)
        add_executable(app main.cpp)
        target_link_libraries(app PRIVATE core)
    )";
    interp.set_source_view(script);
    Parser parser(script);
    auto ast = parser.parse();
    REQUIRE(ast.has_value());
    REQUIRE(interp.interpret(*ast).has_value());

    auto graph_result = interp.generate_build_graph({"app"});
    REQUIRE(graph_result.has_value());

    auto& targets = interp.get_targets();
    auto genex_ctx = GenexEvaluationContext::from_interpreter(interp, targets);
    GenexEvaluator eval(genex_ctx);
    const std::string runtime_dll = targets["core"]->get_runtime_artifact_path(&eval);
    const std::string import_lib = targets["core"]->get_linker_artifact_path(&eval);
    REQUIRE(runtime_dll == (std::filesystem::path(temp_dir) / "core.dll").generic_string());
    REQUIRE(import_lib == (std::filesystem::path(temp_dir) / "core.lib").generic_string());

    const BuildTask* core_link = nullptr;
    const BuildTask* app_link = nullptr;
    for (const auto& task : graph_result->get_tasks()) {
        if (!std::holds_alternative<LinkTask>(task->kind) || !task->parent_target) continue;
        if (task->parent_target->get_name() == "core") core_link = task.get();
        if (task->parent_target->get_name() == "app") app_link = task.get();
    }

    REQUIRE(core_link != nullptr);
    REQUIRE(app_link != nullptr);
    REQUIRE(contains(core_link->outputs, runtime_dll));
    REQUIRE(contains(core_link->outputs, import_lib));
    REQUIRE_FALSE(contains(app_link->commands.front(), runtime_dll));
    REQUIRE(contains(app_link->commands.front(), import_lib));
    REQUIRE(contains(app_link->inputs, import_lib));
    REQUIRE(contains(app_link->explicit_deps, import_lib));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("Windows MSVC versioned shared libraries do not add POSIX soname symlink artifacts", "[target][artifacts][windows]") {
    std::string temp_dir = "build_test_windows_versioned_shared";
    std::filesystem::create_directories(temp_dir);

    std::ostringstream out, err;
    Interpreter interp(".", &out, &err, temp_dir);
    register_target_builtins(interp);
    configure_windows_msvc(interp);

    const std::string script = R"(
        add_library(core SHARED core.cpp)
        set_target_properties(core PROPERTIES VERSION 1.2.3 SOVERSION 1)
    )";
    interp.set_source_view(script);
    Parser parser(script);
    auto ast = parser.parse();
    REQUIRE(ast.has_value());
    REQUIRE(interp.interpret(*ast).has_value());

    auto graph_result = interp.generate_build_graph({"core"});
    REQUIRE(graph_result.has_value());

    const std::string runtime_dll = (std::filesystem::path(temp_dir) / "core.dll").generic_string();
    const std::string import_lib = (std::filesystem::path(temp_dir) / "core.lib").generic_string();

    const BuildTask* core_link = nullptr;
    for (const auto& task : graph_result->get_tasks()) {
        if (std::holds_alternative<LinkTask>(task->kind) && task->parent_target && task->parent_target->get_name() == "core") {
            core_link = task.get();
            break;
        }
    }

    REQUIRE(core_link != nullptr);
    REQUIRE(contains(core_link->outputs, runtime_dll));
    REQUIRE(contains(core_link->outputs, import_lib));
    for (const auto& output : core_link->outputs) { REQUIRE(output.find(".so") == std::string::npos); }
    for (const auto& command : core_link->commands) {
        const bool is_ln_command = !command.empty() && command.front() == "ln";
        REQUIRE_FALSE(is_ln_command);
    }

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("Windows MSVC custom command COMMAND target resolves to executable artifact", "[target][artifacts][windows]") {
    std::string temp_dir = "build_test_windows_custom_command_tool";
    std::filesystem::create_directories(temp_dir);

    std::ostringstream out, err;
    Interpreter interp(".", &out, &err, temp_dir);
    register_target_builtins(interp);
    configure_windows_msvc(interp);

    const std::string script = R"(
        add_executable(tool tool.cpp)
        add_custom_command(
            OUTPUT generated.cpp
            COMMAND tool --emit generated.cpp
            DEPENDS tool)
        add_executable(app generated.cpp)
    )";
    interp.set_source_view(script);
    Parser parser(script);
    auto ast = parser.parse();
    REQUIRE(ast.has_value());
    REQUIRE(interp.interpret(*ast).has_value());

    auto graph_result = interp.generate_build_graph({"app"});
    REQUIRE(graph_result.has_value());

    const std::string tool_exe = (std::filesystem::path(temp_dir) / "tool.exe").generic_string();
    const std::string generated_cpp = (std::filesystem::path(temp_dir) / "generated.cpp").generic_string();

    const BuildTask* custom_command = nullptr;
    for (const auto& task : graph_result->get_tasks()) {
        if (std::holds_alternative<CustomCommandTask>(task->kind) && contains(task->outputs, generated_cpp)) {
            custom_command = task.get();
            break;
        }
    }

    REQUIRE(custom_command != nullptr);
    REQUIRE_FALSE(custom_command->commands.empty());
    REQUIRE_FALSE(custom_command->commands.front().empty());
    REQUIRE(custom_command->commands.front().front() == tool_exe);
    REQUIRE(contains(custom_command->inputs, tool_exe));
    REQUIRE(graph_result->has_task(tool_exe));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("Windows MSVC add_dependencies uses static library artifact graph dependency", "[target][artifacts][windows]") {
    std::string temp_dir = "build_test_windows_manual_static_dep";
    std::filesystem::create_directories(temp_dir);

    std::ostringstream out, err;
    Interpreter interp(".", &out, &err, temp_dir);
    register_target_builtins(interp);
    configure_windows_msvc(interp);

    const std::string script = R"(
        add_library(core_static STATIC core.cpp)
        add_executable(app main.cpp)
        add_dependencies(app core_static)
    )";
    interp.set_source_view(script);
    Parser parser(script);
    auto ast = parser.parse();
    REQUIRE(ast.has_value());
    REQUIRE(interp.interpret(*ast).has_value());

    auto graph_result = interp.generate_build_graph({"app"});
    REQUIRE(graph_result.has_value());

    const std::string core_lib = (std::filesystem::path(temp_dir) / "core_static.lib").generic_string();
    const std::string posix_archive = (std::filesystem::path(temp_dir) / "libcore_static.a").generic_string();

    REQUIRE(graph_result->has_task(core_lib));

    bool app_task_depends_on_core_lib = false;
    for (const auto& task : graph_result->get_tasks()) {
        if (task->parent_target && task->parent_target->get_name() == "app") {
            REQUIRE_FALSE(contains(task->inputs, posix_archive));
            REQUIRE_FALSE(contains(task->explicit_deps, posix_archive));
            for (const auto* dep : task->dependencies) {
                if (dep->id == core_lib) app_task_depends_on_core_lib = true;
            }
        }
    }
    REQUIRE(app_task_depends_on_core_lib);

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("Windows MSVC circular static dependencies use static library artifacts", "[target][artifacts][windows]") {
    std::string temp_dir = "build_test_windows_circular_static_dep";
    std::filesystem::create_directories(temp_dir);

    std::ostringstream out, err;
    Interpreter interp(".", &out, &err, temp_dir);
    register_target_builtins(interp);
    configure_windows_msvc(interp);

    const std::string script = R"(
        add_library(alpha STATIC alpha.cpp)
        add_library(beta STATIC beta.cpp)
        target_link_libraries(alpha PRIVATE beta)
        target_link_libraries(beta PRIVATE alpha)
    )";
    interp.set_source_view(script);
    Parser parser(script);
    auto ast = parser.parse();
    REQUIRE(ast.has_value());
    REQUIRE(interp.interpret(*ast).has_value());

    auto& targets = interp.get_targets();
    targets["alpha"]->resolve(targets, interp);

    const std::string alpha_lib = (std::filesystem::path(temp_dir) / "alpha.lib").generic_string();
    const std::string posix_archive = (std::filesystem::path(temp_dir) / "libalpha.a").generic_string();
    const auto& beta_link_libs = targets["beta"]->get_resolved_property("LINK_LIBRARIES");
    REQUIRE(contains(beta_link_libs, alpha_lib));
    REQUIRE_FALSE(contains(beta_link_libs, posix_archive));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("PUBLIC include propagation", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(mylib INTERFACE)
        target_include_directories(mylib INTERFACE /usr/include/mylib)
        add_executable(myapp main.cpp)
        target_link_libraries(myapp PUBLIC mylib)
    )");

    auto& app = targets["myapp"];
    const auto& includes = app->get_resolved_property("INCLUDE_DIRECTORIES");
    REQUIRE(contains(includes, "/usr/include/mylib"));
}

TEST_CASE("PRIVATE include does NOT propagate", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(mylib STATIC lib.cpp)
        target_include_directories(mylib PRIVATE /internal/include)
        add_executable(myapp main.cpp)
        target_link_libraries(myapp PUBLIC mylib)
    )");

    auto& app = targets["myapp"];
    const auto& includes = app->get_resolved_property("INCLUDE_DIRECTORIES");
    REQUIRE_FALSE(contains(includes, "/internal/include"));
}

TEST_CASE("Transitive propagation A->B->C", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(C INTERFACE)
        target_include_directories(C INTERFACE /c/include)
        add_library(B INTERFACE)
        target_link_libraries(B INTERFACE C)
        add_executable(A main.cpp)
        target_link_libraries(A PUBLIC B)
    )");

    auto& a = targets["A"];
    const auto& includes = a->get_resolved_property("INCLUDE_DIRECTORIES");
    REQUIRE(contains(includes, "/c/include"));
}

TEST_CASE("PRIVATE breaks transitive chain", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(C INTERFACE)
        target_include_directories(C INTERFACE /c/include)
        add_library(B STATIC b.cpp)
        target_link_libraries(B PRIVATE C)
        add_executable(A main.cpp)
        target_link_libraries(A PUBLIC B)
    )");

    auto& a = targets["A"];
    const auto& includes = a->get_resolved_property("INCLUDE_DIRECTORIES");
    REQUIRE_FALSE(contains(includes, "/c/include"));
}

TEST_CASE("INTERFACE library propagation", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(iface INTERFACE)
        target_include_directories(iface INTERFACE /iface/include)
        target_compile_definitions(iface INTERFACE USE_IFACE)
        add_executable(myapp main.cpp)
        target_link_libraries(myapp PUBLIC iface)
    )");

    auto& app = targets["myapp"];
    REQUIRE(contains(app->get_resolved_property("INCLUDE_DIRECTORIES"), "/iface/include"));
    REQUIRE(contains(app->get_resolved_property("COMPILE_DEFINITIONS"), "USE_IFACE"));
}

TEST_CASE("Static lib propagates PRIVATE link deps", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(inner STATIC inner.cpp)
        add_library(outer STATIC outer.cpp)
        target_link_libraries(outer PRIVATE inner)
        add_executable(myapp main.cpp)
        target_link_libraries(myapp PUBLIC outer)
    )");

    auto& app = targets["myapp"];
    const auto& libs = app->get_resolved_property("LINK_LIBRARIES");
    // Static libs propagate ALL link deps (inner must appear for symbol resolution)
    bool has_inner = std::any_of(libs.begin(), libs.end(), [](const std::string& s) { return s.find("libinner.a") != std::string::npos; });
    REQUIRE(has_inner);
}

TEST_CASE("Shared lib does NOT propagate PRIVATE link deps", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(inner STATIC inner.cpp)
        add_library(outer SHARED outer.cpp)
        target_link_libraries(outer PRIVATE inner)
        add_executable(myapp main.cpp)
        target_link_libraries(myapp PUBLIC outer)
    )");

    auto& app = targets["myapp"];
    const auto& libs = app->get_resolved_property("LINK_LIBRARIES");
    bool has_inner = std::any_of(libs.begin(), libs.end(), [](const std::string& s) { return s.find("libinner.a") != std::string::npos; });
    REQUIRE_FALSE(has_inner);
}

TEST_CASE("Order-preserving dedup", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(B INTERFACE)
        target_include_directories(B INTERFACE /common/include)
        add_library(C INTERFACE)
        target_include_directories(C INTERFACE /common/include)
        add_executable(A main.cpp)
        target_link_libraries(A PUBLIC B C)
    )");

    auto& a = targets["A"];
    const auto& includes = a->get_resolved_property("INCLUDE_DIRECTORIES");
    // /common/include should appear exactly once
    int count = std::count(includes.begin(), includes.end(), "/common/include");
    REQUIRE(count == 1);
}

TEST_CASE("TargetPropertyScope::BUILD returns PRIVATE + PUBLIC", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(mylib STATIC priv.cpp)
        target_include_directories(mylib PRIVATE /priv/inc)
        target_include_directories(mylib PUBLIC /pub/inc)
        target_include_directories(mylib INTERFACE /iface/inc)
    )");

    auto& lib = targets["mylib"];
    auto build = lib->get_property_list("INCLUDE_DIRECTORIES", TargetPropertyScope::BUILD);
    REQUIRE(contains(build, "/priv/inc"));
    REQUIRE(contains(build, "/pub/inc"));
    REQUIRE_FALSE(contains(build, "/iface/inc"));

    auto iface = lib->get_property_list("INCLUDE_DIRECTORIES", TargetPropertyScope::INTERFACE);
    REQUIRE_FALSE(contains(iface, "/priv/inc"));
    REQUIRE(contains(iface, "/pub/inc"));
    REQUIRE(contains(iface, "/iface/inc"));
}

TEST_CASE("Multiple visibility resolved correctly", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(mylib STATIC lib.cpp)
        target_include_directories(mylib PRIVATE /priv)
        target_include_directories(mylib PUBLIC /pub)
        target_include_directories(mylib INTERFACE /iface)
    )");

    auto& lib = targets["mylib"];
    // resolved_properties_ should have PRIVATE + PUBLIC
    const auto& resolved = lib->get_resolved_property("INCLUDE_DIRECTORIES");
    REQUIRE(contains(resolved, "/priv"));
    REQUIRE(contains(resolved, "/pub"));
    REQUIRE_FALSE(contains(resolved, "/iface"));

    // resolved_interface_properties_ should have PUBLIC + INTERFACE
    const auto& resolved_iface = lib->get_resolved_interface_property("INCLUDE_DIRECTORIES");
    REQUIRE_FALSE(contains(resolved_iface, "/priv"));
    REQUIRE(contains(resolved_iface, "/pub"));
    REQUIRE(contains(resolved_iface, "/iface"));
}

TEST_CASE("Imported target includes become system includes", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(ext STATIC IMPORTED)
        target_include_directories(ext INTERFACE /ext/include)
        add_executable(myapp main.cpp)
        target_link_libraries(myapp PUBLIC ext)
    )");

    auto& app = targets["myapp"];
    const auto& sys_includes = app->get_resolved_property("SYSTEM_INCLUDE_DIRECTORIES");
    REQUIRE(contains(sys_includes, "/ext/include"));
    // Should NOT be in regular includes
    const auto& includes = app->get_resolved_property("INCLUDE_DIRECTORIES");
    REQUIRE_FALSE(contains(includes, "/ext/include"));
}

TEST_CASE("COMPILE_DEFINITIONS propagation", "[resolve]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(mylib INTERFACE)
        target_compile_definitions(mylib INTERFACE FOO=1 BAR)
        add_executable(myapp main.cpp)
        target_link_libraries(myapp PUBLIC mylib)
    )");

    auto& app = targets["myapp"];
    const auto& defs = app->get_resolved_property("COMPILE_DEFINITIONS");
    REQUIRE(contains(defs, "FOO=1"));
    REQUIRE(contains(defs, "BAR"));
}

TEST_CASE("Per-artifact OUTPUT_NAME overrides OUTPUT_NAME", "[target][output]") {
    auto [targets, err] = run_and_resolve(R"(
        add_executable(myapp main.cpp)
        set_target_properties(myapp PROPERTIES
            OUTPUT_NAME generic_name
            RUNTIME_OUTPUT_NAME runtime_name)
        add_library(mystatic STATIC src.cpp)
        set_target_properties(mystatic PROPERTIES
            OUTPUT_NAME generic_static
            ARCHIVE_OUTPUT_NAME archive_name)
        add_library(myshared SHARED src.cpp)
        set_target_properties(myshared PROPERTIES
            OUTPUT_NAME generic_shared
            LIBRARY_OUTPUT_NAME library_name)
    )");

    REQUIRE(targets["myapp"]->get_output_path().find("runtime_name") != std::string::npos);
    REQUIRE(targets["mystatic"]->get_output_path().find("libarchive_name.a") != std::string::npos);
    REQUIRE(targets["myshared"]->get_output_path().find("liblibrary_name.so") != std::string::npos);
}

TEST_CASE("DEBUG_POSTFIX appended for libraries only", "[target][output]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(mylib STATIC src.cpp)
        set_target_properties(mylib PROPERTIES DEBUG_POSTFIX _d)
        add_library(myshared SHARED src.cpp)
        set_target_properties(myshared PROPERTIES DEBUG_POSTFIX _d)
        add_executable(myapp main.cpp)
        set_target_properties(myapp PROPERTIES DEBUG_POSTFIX _d)
    )");

    // Build type is Debug in the harness — postfix applies to libs.
    REQUIRE(targets["mylib"]->get_output_path().find("libmylib_d.a") != std::string::npos);
    REQUIRE(targets["myshared"]->get_output_path().find("libmyshared_d.so") != std::string::npos);
    // Executables are not postfixed.
    REQUIRE(targets["myapp"]->get_output_path().find("myapp_d") == std::string::npos);
}

TEST_CASE("Standard-required property is storable", "[target][std]") {
    auto [targets, err] = run_and_resolve(R"(
        add_executable(myapp main.cpp)
        set_target_properties(myapp PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            C_STANDARD_REQUIRED OFF)
    )");
    auto& t = targets["myapp"];
    REQUIRE(t->get_property("CXX_STANDARD_REQUIRED") == "ON");
    REQUIRE(t->get_property("C_STANDARD_REQUIRED") == "OFF");
    REQUIRE(t->get_language_standard(Language::CXX) == "20");
}

TEST_CASE("VERSION/SOVERSION/RPATH properties stored", "[target][rpath]") {
    auto [targets, err] = run_and_resolve(R"(
        add_library(mylib SHARED src.cpp)
        set_target_properties(mylib PROPERTIES
            VERSION 1.2.3
            SOVERSION 1
            BUILD_RPATH "/opt/foo/lib;$ORIGIN/../lib"
            INSTALL_RPATH "/opt/install/lib"
            BUILD_WITH_INSTALL_RPATH TRUE)
    )");
    auto& t = targets["mylib"];
    REQUIRE(t->get_property("VERSION") == "1.2.3");
    REQUIRE(t->get_property("SOVERSION") == "1");
    REQUIRE(t->get_property("BUILD_RPATH") == "/opt/foo/lib;$ORIGIN/../lib");
    REQUIRE(t->get_property("INSTALL_RPATH") == "/opt/install/lib");
    REQUIRE(t->get_property("BUILD_WITH_INSTALL_RPATH") == "TRUE");
}
