#include "interperter.hpp"
#include "command_parser.hpp"
#include "target.hpp"
#include "build_system.hpp"
#include "gnu_compiler.hpp"
#include "genex_evaluator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <array>
#include <regex>
#include <sys/stat.h>
#include "builtins/registry.hpp"

namespace dmake {

namespace {

// HACK: This function fakes CMake's compiler detection and platform initialization.
// CMake runs a complex startup sequence (CMakeDetermineSystem, CMakeDetermineCXXCompiler,
// CMakeDetermineCompilerABI, etc.) that sets hundreds of variables. We bypass all that
// and extract the essential information directly from the compiler.
//
// This is NOT proper CMake compatibility - it's a pragmatic shortcut that:
// - Only works with GCC (will break on Clang, MSVC, etc.)
// - Doesn't support cross-compilation properly
// - Doesn't run CMake's try_compile() tests for ABI detection
// - May miss variables that some Find modules expect
//
// TODO: Eventually either:
// 1. Implement proper platform detection (slow but correct)
// 2. Cache the results of CMake's detection (run once, reuse)
// 3. Incrementally add variables as projects need them
static std::unordered_map<std::string, std::string> backup_vars; // We setup interperter multiple times during tests, so back up vars to avoid re-detecting compilers
void fake_cmake_compiler_checks_and_init(
    Interpreter& interp,
    Toolchain& toolchain)
{
    if(!backup_vars.empty()) {
        interp.get_variables().merge(backup_vars);
        auto cxx_compiler = std::make_unique<GnuCompiler>(interp.get_variable("CMAKE_CXX_COMPILER"), Language::CXX);
        auto c_compiler = std::make_unique<GnuCompiler>(interp.get_variable("CMAKE_C_COMPILER"), Language::C);
        toolchain.set_compiler(Language::CXX, std::move(cxx_compiler));
        toolchain.set_compiler(Language::C, std::move(c_compiler));
        return;
    }

    // Initialize compilers
    interp.set_variable("CMAKE_CXX_COMPILER", "g++");
    interp.set_variable("CMAKE_C_COMPILER", "gcc");
    auto cxx_compiler = std::make_unique<GnuCompiler>(interp.get_variable("CMAKE_CXX_COMPILER"), Language::CXX);
    auto c_compiler = std::make_unique<GnuCompiler>(interp.get_variable("CMAKE_C_COMPILER"), Language::C);

    // C++ compiler platform info
    auto cxx_info = cxx_compiler->detect_platform();
    interp.set_variable("CMAKE_CXX_COMPILER_ID", cxx_info.compiler_id);
    interp.set_variable("CMAKE_CXX_COMPILER_VERSION", cxx_info.compiler_version);
    // Legacy compatibility variable for GCC
    if (cxx_info.compiler_id == "GNU") {
        interp.set_variable("CMAKE_COMPILER_IS_GNUCXX", "1");
    }
    if (!cxx_info.implicit_includes.empty()) {
        interp.set_variable("CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES", CMakeList(cxx_info.implicit_includes).to_string());
    }
    if (!cxx_info.implicit_link_dirs.empty()) {
        interp.set_variable("CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES", CMakeList(cxx_info.implicit_link_dirs).to_string());
    }
    if (!cxx_info.implicit_link_libs.empty()) {
        interp.set_variable("CMAKE_CXX_IMPLICIT_LINK_LIBRARIES", CMakeList(cxx_info.implicit_link_libs).to_string());
    }

    // C compiler platform info
    auto c_info = c_compiler->detect_platform();
    interp.set_variable("CMAKE_C_COMPILER_ID", c_info.compiler_id);
    interp.set_variable("CMAKE_C_COMPILER_VERSION", c_info.compiler_version);
    // Legacy compatibility variable for GCC
    if (c_info.compiler_id == "GNU") {
        interp.set_variable("CMAKE_COMPILER_IS_GNUCC", "1");
    }
    if (!c_info.implicit_includes.empty()) {
        interp.set_variable("CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES", CMakeList(c_info.implicit_includes).to_string());
    }
    if (!c_info.implicit_link_dirs.empty()) {
        interp.set_variable("CMAKE_C_IMPLICIT_LINK_DIRECTORIES", CMakeList(c_info.implicit_link_dirs).to_string());
    }
    if (!c_info.implicit_link_libs.empty()) {
        interp.set_variable("CMAKE_C_IMPLICIT_LINK_LIBRARIES", CMakeList(c_info.implicit_link_libs).to_string());
    }

    // System-level info (same for both compilers)
    interp.set_variable("CMAKE_SYSTEM_NAME", cxx_info.system_name);
    interp.set_variable("CMAKE_SYSTEM_PROCESSOR", cxx_info.system_processor);
    interp.set_variable("CMAKE_SIZEOF_VOID_P", cxx_info.sizeof_void_p);
    interp.set_variable("CMAKE_HOST_SYSTEM_NAME", cxx_info.system_name);
    interp.set_variable("CMAKE_HOST_SYSTEM_PROCESSOR", cxx_info.system_processor);
    interp.set_variable("CMAKE_C_COMPILER_LOADED", "1");
    interp.set_variable("CMAKE_CXX_COMPILER_LOADED", "1");

    toolchain.set_compiler(Language::CXX, std::move(cxx_compiler));
    toolchain.set_compiler(Language::C, std::move(c_compiler));

    // Cache only the compiler-related variables (not directory-specific ones)
    static constexpr std::array compiler_vars = {
        "CMAKE_CXX_COMPILER", "CMAKE_C_COMPILER",
        "CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION",
        "CMAKE_C_COMPILER_ID", "CMAKE_C_COMPILER_VERSION",
        "CMAKE_SYSTEM_NAME", "CMAKE_SYSTEM_PROCESSOR",
        "CMAKE_SIZEOF_VOID_P", "CMAKE_HOST_SYSTEM_NAME", "CMAKE_HOST_SYSTEM_PROCESSOR",
        "CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES", "CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES", "CMAKE_CXX_IMPLICIT_LINK_LIBRARIES",
        "CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES", "CMAKE_C_IMPLICIT_LINK_DIRECTORIES", "CMAKE_C_IMPLICIT_LINK_LIBRARIES",
        "CMAKE_C_COMPILER_LOADED", "CMAKE_CXX_COMPILER_LOADED"
    };
    for (const auto& name : compiler_vars) {
        backup_vars[name] = interp.get_variable(name);
    }
}

// Compare version strings component-wise (CMake behavior)
// Returns: -1 if a < b, 0 if a == b, 1 if a > b
// CMake behavior: split by '.', parse each component as integer (non-numeric suffix stripped),
// missing components treated as 0
int compare_versions(const std::string& a, const std::string& b) {
    std::vector<int> parts_a, parts_b;

    // Parse version a - split by '.' and parse each component
    std::istringstream iss_a(a);
    std::string component;
    while (std::getline(iss_a, component, '.')) {
        // CMake strips non-numeric suffixes: "1a" -> 1, "1-suffix" -> 1
        try {
            parts_a.push_back(std::stoi(component));
        } catch (...) {
            parts_a.push_back(0);
        }
    }

    // Parse version b
    std::istringstream iss_b(b);
    while (std::getline(iss_b, component, '.')) {
        try {
            parts_b.push_back(std::stoi(component));
        } catch (...) {
            parts_b.push_back(0);
        }
    }

    // Pad shorter vector with zeros (missing components = 0)
    size_t max_len = std::max(parts_a.size(), parts_b.size());
    parts_a.resize(max_len, 0);
    parts_b.resize(max_len, 0);

    // Compare component by component
    for (size_t i = 0; i < max_len; ++i) {
        if (parts_a[i] < parts_b[i]) return -1;
        if (parts_a[i] > parts_b[i]) return 1;
    }
    return 0;
}

} // anonymous namespace

Interpreter* Interpreter::get_root() {
    return root_;
}

const Interpreter* Interpreter::get_root() const {
    return root_;
}

Interpreter* Interpreter::get_interpreter_for_directory(const std::string& dir) {
    std::filesystem::path abs_dir;
    if (std::filesystem::path(dir).is_absolute()) {
        abs_dir = std::filesystem::path(dir).lexically_normal();
    } else {
        abs_dir = (std::filesystem::path(get_variable("CMAKE_CURRENT_SOURCE_DIR")) / dir).lexically_normal();
    }

    auto& registry = get_directory_interpreters();
    auto it = registry.find(abs_dir.string());
    if (it != registry.end()) {
        return it->second;
    }
    return nullptr;
}


std::expected<dmake::Interpreter*, dmake::BuildError> dmake::Interpreter::run_build(int jobs, const std::vector<std::string>& requested_targets) {
    // Sanity check CMAKE_BUILD_TYPE
    std::array<std::string, 4> stanard_build_types_lower = {"debug", "release", "minsize", "relwithdebinfo"};
    auto build_type = get_variable("CMAKE_BUILD_TYPE");
    std::transform(build_type.begin(), build_type.end(), build_type.begin(), ::tolower);
    if (std::find(stanard_build_types_lower.begin(), stanard_build_types_lower.end(), build_type) == stanard_build_types_lower.end()) {
        print_message("WARN", "Build type '" + build_type + "' is not a standard build type. Things MIGHT go wrong.");
    }

    if (parent_ != nullptr) return get_root()->run_build(jobs, requested_targets);

    // Determine which targets to build
    std::set<std::string> targets_to_build;
    if (requested_targets.empty()) {
        for (const auto& [name, target] : targets_) {
            auto custom = std::dynamic_pointer_cast<CustomTarget>(target);
            if (custom) {
                // Custom targets only build by default if they have ALL flag
                if (custom->is_build_by_default()) {
                    targets_to_build.insert(name);
                }
            } else {
                // Executables and libraries are "ALL" by default unless EXCLUDE_FROM_ALL is set
                targets_to_build.insert(name);
            }
        }
    } else {
        std::function<void(const std::string&)> collect = [&](const std::string& name) {
            if (targets_to_build.count(name)) return;
            if (!targets_.count(name)) {
                // Not a dmake target, might be a system lib or imported target
                return;
            }
            targets_to_build.insert(name);
            auto target = targets_[name];
            for (const auto& lib : target->get_property_list("LINK_LIBRARIES", PropertyVisibility::PRIVATE)) collect(lib);
            for (const auto& lib : target->get_property_list("LINK_LIBRARIES", PropertyVisibility::PUBLIC)) collect(lib);
            for (const auto& lib : target->get_property_list("LINK_LIBRARIES", PropertyVisibility::INTERFACE)) collect(lib);

            auto custom = std::dynamic_pointer_cast<CustomTarget>(target);
            if (custom) {
                for (const auto& dep : custom->get_custom_dependencies()) {
                    collect(dep);
                }
            }
        };
        for (const auto& t : requested_targets) {
            // Resolve aliases to real target names
            std::string resolved = resolve_target_alias(t);
            if (!targets_.count(resolved)) {
                return std::unexpected(BuildError{current_file_, "Unknown target: " + t});
            }
            collect(resolved);
        }
    }

    std::string root_binary_dir = get_variable("CMAKE_BINARY_DIR");
    std::filesystem::create_directories(root_binary_dir);

    print_message("STATUS", "Generating build graph...");
    BuildGraph graph;

    // 1. (Redundant property propagation removed - handled by Target::resolve)

    // 2. Generate tasks for selected targets
    // Build linker flags from CMAKE variables
    std::vector<std::string> exe_linker_flags;
    std::vector<std::string> shared_linker_flags;

    // Handle CMAKE_LINKER_TYPE (convert to -fuse-ld=<type>)
    std::string linker_type = get_variable("CMAKE_LINKER_TYPE");
    if (!linker_type.empty()) {
        std::string linker_type_upper = linker_type;
        std::transform(linker_type_upper.begin(), linker_type_upper.end(), linker_type_upper.begin(), ::toupper);

        if (linker_type_upper == "BFD" || linker_type_upper == "GOLD" ||
            linker_type_upper == "MOLD" || linker_type_upper == "LLD") {
            std::string linker_type_lower = linker_type;
            std::transform(linker_type_lower.begin(), linker_type_lower.end(), linker_type_lower.begin(), ::tolower);
            std::string flag = "-fuse-ld=" + linker_type_lower;
            exe_linker_flags.push_back(flag);
            shared_linker_flags.push_back(flag);
        } else {
            return std::unexpected(BuildError{current_file_, "Invalid CMAKE_LINKER_TYPE: " + linker_type + ". Must be one of: BFD, GOLD, MOLD, LLD"});
        }
    }

    // Handle CMAKE_EXE_LINKER_FLAGS
    CMakeList exe_flags_list(get_variable("CMAKE_EXE_LINKER_FLAGS"));
    for (const auto& flag : exe_flags_list) {
        if (!flag.empty()) exe_linker_flags.push_back(flag);
    }

    // Handle CMAKE_SHARED_LINKER_FLAGS
    CMakeList shared_flags_list(get_variable("CMAKE_SHARED_LINKER_FLAGS"));
    for (const auto& flag : shared_flags_list) {
        if (!flag.empty()) shared_linker_flags.push_back(flag);
    }

    for (const auto& name : targets_to_build) {
        targets_[name]->generate_tasks(graph, get_root()->toolchain_, targets_, *this, exe_linker_flags, shared_linker_flags);
    }

    // Link dependency resolution (adding inputs to link tasks)
    for (const auto& name : targets_to_build) {
        auto target = targets_[name];
        std::string out_path = target->get_output_path();

        // For custom targets, out_path might be empty or same as name
        std::string task_id = out_path.empty() ? target->get_name() : out_path;

        if (graph.has_task(task_id)) {
            auto& task = graph.get_task(task_id);

            auto add_lib_inputs = [&](PropertyVisibility vis) {
                for (const auto& lib_name : target->get_property_list("LINK_LIBRARIES", vis)) {
                    if (targets_.count(lib_name)) {
                        auto dep = targets_[lib_name];
                        std::string lib_out = dep->get_output_path();
                        if (!lib_out.empty()) {
                            task.inputs.push_back(lib_out);
                        } else {
                            // Dependency on a utility target
                            task.dependencies.insert(lib_name);
                        }
                    }
                }
            };
            add_lib_inputs(PropertyVisibility::PRIVATE);
            add_lib_inputs(PropertyVisibility::PUBLIC);

            // If it's a standard target (linking), we need to add libraries to the command line
            if (target->get_type() != TargetType::CUSTOM && !task.commands.empty()) {
                 // The link command already has these from Target::generate_tasks
                 // but we need them as inputs for the graph.
            }
        }
    }

    // 2b. Finalize - evaluate all generator expressions in all tasks
    {
        GenexEvaluationContext genex_ctx;
        genex_ctx.build_type = get_variable("CMAKE_BUILD_TYPE");
        genex_ctx.system_name = get_variable("CMAKE_SYSTEM_NAME");
        genex_ctx.cxx_compiler_id = get_variable("CMAKE_CXX_COMPILER_ID");
        genex_ctx.c_compiler_id = get_variable("CMAKE_C_COMPILER_ID");
        genex_ctx.all_targets = &targets_;
        genex_ctx.phase = GenexEvaluationContext::Phase::BUILD;

        auto finalize_result = graph.finalize(genex_ctx);
        if (!finalize_result) {
            return std::unexpected(BuildError{current_file_, finalize_result.error()});
        }
    }

    // 3. Generate compile_commands.json (on by default)
    std::string export_cmds = get_variable("CMAKE_EXPORT_COMPILE_COMMANDS");
    if (!is_falsy(export_cmds)) {
        auto result = graph.generate_compile_commands(root_binary_dir);
        if (!result) {
            return std::unexpected(BuildError{current_file_, result.error()});
        }
    }

    // 4. Execute the build graph
    print_message("STATUS", "Starting " + build_type + " build...");
    auto result = graph.execute(root_binary_dir, jobs);


    if (!result) {
        return std::unexpected(BuildError{current_file_, result.error()});
    }

    print_message("STATUS", "Build finished.");
    return this;
}

Interpreter::Interpreter(std::string script_dir, std::ostream* out, std::ostream* err, Interpreter* parent, std::optional<std::string> build_dir)
    : out_(out), err_(err), parent_(parent) {

    // Cache the root interpreter to avoid O(N) parent chain traversal on every global lookup.
    root_ = parent_ ? parent_->root_ : this;

    std::filesystem::path abs_script_dir = script_dir.empty() ?
        std::filesystem::current_path() :
        std::filesystem::absolute(script_dir).lexically_normal();
    frame_stack_.push_front({abs_script_dir.string(), nullptr});

    // Initialize variables via ShadowMap (depth starts at 0)
    variables_.set("CMAKE_SIZEOF_VOID_P", std::to_string(sizeof(void*)));
    variables_.set("CMAKE_CURRENT_SOURCE_DIR", abs_script_dir.string());
    variables_.set("CMAKE_CURRENT_LIST_DIR", abs_script_dir.string());
    variables_.set("CMAKE_CURRENT_LIST_FILE", (abs_script_dir / "CMakeLists.txt").string()); // Default assumption

    if (parent_ == nullptr) {
        std::filesystem::path abs_binary_dir;
        if (build_dir.has_value()) {
            build_dir_ = *build_dir;
            abs_binary_dir = std::filesystem::absolute(build_dir_).lexically_normal();
        } else {
            build_dir_ = "build";
            abs_binary_dir = abs_script_dir / build_dir_;
        }

        variables_.set("DMAKE_VERSION", "0.1.0");

        variables_.set("CMAKE_SOURCE_DIR", variables_.get("CMAKE_CURRENT_SOURCE_DIR"));
        variables_.set("CMAKE_BINARY_DIR", abs_binary_dir.string());
        variables_.set("CMAKE_CURRENT_BINARY_DIR", abs_binary_dir.string());
        variables_.set("CMAKE_EXPORT_COMPILE_COMMANDS", "ON");

        // Set default install prefix if not already set
        if (get_variable("CMAKE_INSTALL_PREFIX").empty()) {
            variables_.set("CMAKE_INSTALL_PREFIX", "/usr/local");
        }

        if (abs_binary_dir == abs_script_dir) {
            set_fatal_error("Build directory cannot be the same as the source directory: " + abs_script_dir.string());
        }

        variables_.set("CMAKE_VERSION", "3.20.0");
        variables_.set("CMAKE_MAJOR_VERSION", "3");
        variables_.set("CMAKE_MINOR_VERSION", "20");
        variables_.set("CMAKE_PATCH_VERSION", "0");

        variables_.set("CMAKE_FILES_DIRECTORY", "/CMakeFiles");

        // Platform flags
#ifdef __unix__
        variables_.set("UNIX", "1");
#endif
#ifdef __APPLE__
        variables_.set("APPLE", "1");
#endif
#ifdef _WIN32
        variables_.set("WIN32", "1");
#endif
#ifdef __linux__
        variables_.set("LINUX", "1");
#endif

        variables_.set("CMAKE_EXECUTABLE_SUFFIX", "");
        variables_.set("CMAKE_SHARED_LIBRARY_SUFFIX", ".so");
        variables_.set("CMAKE_STATIC_LIBRARY_SUFFIX", ".a");

        variables_.set("CMAKE_COMMAND", get_executable_path());
        variables_.set("CMAKE_ROOT", "/usr/share/cmake");

        // Initialize toolchain with compiler detection
        // See fake_cmake_compiler_checks_and_init() for what this does and its limitations
        // NOTE: This function directly modifies variables via set_variable(), which now uses ShadowMap
        fake_cmake_compiler_checks_and_init(*this, toolchain_);

        // Initialize built-in global properties
        // CMake modules expect these to be set (e.g., CheckLanguage.cmake checks ENABLED_LANGUAGES)
        global_properties_["ENABLED_LANGUAGES"] = "C;CXX";
        global_properties_["GENERATOR_IS_MULTI_CONFIG"] = "FALSE";
        global_properties_["TARGET_SUPPORTS_SHARED_LIBS"] = "TRUE";
        global_properties_["CMAKE_ROLE"] = "PROJECT";
        global_properties_["FIND_LIBRARY_USE_LIB64_PATHS"] = "TRUE";
        global_properties_["FIND_LIBRARY_USE_LIB32_PATHS"] = "FALSE";


        // Some CMake defaults
        variables_.set("CMAKE_INSTALL_BINDIR", "bin");
        variables_.set("CMAKE_INSTALL_LIBDIR", "lib");
        variables_.set("CMAKE_INSTALL_INCLUDEDIR", "include");

        // Register this interpreter for its directory
        directory_interpreters_[abs_script_dir.string()] = this;

        // Initialize cache store
        std::filesystem::path cache_path = std::filesystem::path(abs_binary_dir) / ".dmake_subsystem_cache.json";
        cache_store_ = std::make_unique<CacheStore>(cache_path);
        auto res = cache_store_->load();  // Graceful - starts with empty cache if file doesn't exist
        (void)res;

        // Register non-internal builtins
        register_message_builtins(*this);
        register_variable_builtins(*this);
        register_list_builtins(*this);
        register_target_builtins(*this);
        register_project_builtins(*this);
        register_file_builtins(*this);
        register_find_commands_builtins(*this);
        register_math_builtins(*this);
        register_string_builtins(*this);
        register_process_builtins(*this);
        register_property_builtins(*this);
        register_try_compile_builtins(*this);
        register_path_builtins(*this);
        register_install_builtins(*this);
        register_source_properties_builtins(*this);

        add_builtin("enable_testing", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (!args.empty()) {
                interp.print_message("WARN", "enable_testing() expects no arguments");
            }
            interp.enable_testing_globally();
        });

        add_builtin("add_test", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (!interp.is_testing_enabled()) {
                return;
            }

            // Warn if BUILD_TESTING is OFF but add_test is being called
            auto build_testing = interp.get_variable("BUILD_TESTING");
            if (Interpreter::is_falsy(build_testing)) {
                interp.print_message("WARNING", "add_test() called but BUILD_TESTING is OFF. Tests may not have been built. Use -DBUILD_TESTING=ON");
            }

            CommandParser parser("add_test");
            std::string name;
            std::string command;
            std::vector<std::string> cmd_args;
            std::string working_dir;
            std::vector<std::string> raw_cmd;

            parser.value("NAME", name);
            parser.list("COMMAND", raw_cmd);
            parser.value("WORKING_DIRECTORY", working_dir);

            auto parse_res = parser.parse(args);
            if (!parse_res) {
                interp.set_fatal_error(parse_res.error());
                return;
            }

            if (name.empty() || raw_cmd.empty()) {
                interp.set_fatal_error("add_test requires NAME and COMMAND");
                return;
            }

            TestDefinition test;
            test.name = name;
            test.command = raw_cmd[0];
            for (size_t i = 1; i < raw_cmd.size(); ++i) {
                test.args.push_back(raw_cmd[i]);
            }
            test.working_dir = working_dir.empty() ? interp.get_variable("CMAKE_CURRENT_SOURCE_DIR") : working_dir;

            interp.get_tests().push_back(std::move(test));
        });

        add_builtin("set_tests_properties", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (!interp.is_testing_enabled()) {
                return;
            }

            // Supported test properties (explicitly tracked)
            static const std::unordered_set<std::string> SUPPORTED_PROPERTIES = {
                "TIMEOUT",
                "SKIP_RETURN_CODE",
                // Future properties can be added here:
                // "WILL_FAIL", "PASS_REGULAR_EXPRESSION", "FAIL_REGULAR_EXPRESSION",
                // "WORKING_DIRECTORY", "ENVIRONMENT", "LABELS", "DEPENDS", etc.
            };

            // Parse: set_tests_properties(test1 [test2...] PROPERTIES prop1 val1 [prop2 val2...])
            if (args.size() < 3) {
                interp.set_fatal_error("set_tests_properties requires at least one test name and PROPERTIES keyword");
                return;
            }

            // Find PROPERTIES keyword
            auto props_it = std::find(args.begin(), args.end(), "PROPERTIES");
            if (props_it == args.end()) {
                interp.set_fatal_error("set_tests_properties requires PROPERTIES keyword");
                return;
            }

            // Extract test names (everything before PROPERTIES)
            std::vector<std::string> test_names(args.begin(), props_it);
            if (test_names.empty()) {
                interp.set_fatal_error("set_tests_properties requires at least one test name");
                return;
            }

            // Extract properties (everything after PROPERTIES)
            std::vector<std::string> prop_args(props_it + 1, args.end());
            if (prop_args.size() % 2 != 0) {
                interp.set_fatal_error("set_tests_properties PROPERTIES must have pairs of property names and values");
                return;
            }

            // Parse properties into map
            std::map<std::string, std::string> properties;
            for (size_t i = 0; i < prop_args.size(); i += 2) {
                std::string prop_name = prop_args[i];
                std::string prop_value = prop_args[i + 1];

                // Warn if property is not supported (but still set it)
                if (SUPPORTED_PROPERTIES.find(prop_name) == SUPPORTED_PROPERTIES.end()) {
                    interp.print_message("WARN", "Test property '" + prop_name + "' is not yet supported by dmake");
                }

                properties[prop_name] = prop_value;
            }

            // Apply properties to all named tests
            auto& tests = interp.get_tests();
            for (const auto& test_name : test_names) {
                bool found = false;
                for (auto& test : tests) {
                    if (test.name == test_name) {
                        // Merge properties (allowing multiple set_tests_properties calls)
                        for (const auto& [key, value] : properties) {
                            test.properties[key] = value;
                        }
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    interp.print_message("WARN", "Test '" + test_name + "' not found, cannot set properties");
                }
            }
        });

        register_find_package_builtins(*this);

        add_builtin("include_guard", [](Interpreter& interp, const std::vector<std::string>& args) {
            std::string scope = "DIRECTORY";
            if (!args.empty()) scope = args[0];

            std::string current_file = interp.get_current_file();
            if (current_file.empty()) return;

            if (scope == "GLOBAL") {
                interp.get_root()->global_guarded_files_.insert(current_file);
            } else {
                interp.directory_guarded_files_.insert(current_file);
            }
        });

        // Internal builtins (interact with interpreter state/stack)

        add_builtin("add_subdirectory", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (args.empty()) return;
            std::string subdir = args[0];
            std::filesystem::path path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / subdir;
            std::filesystem::path cmake_file = path / "CMakeLists.txt";

            if (!std::filesystem::exists(cmake_file)) {
                interp.set_fatal_error("CMakeLists.txt not found in " + path.string());
                return;
            }

            std::ifstream file(cmake_file);
            if(!file) { interp.set_fatal_error("Could not read " + cmake_file.string()); return; }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            Interpreter sub_interp(path.string(), interp.out_, interp.err_, &interp);
            sub_interp.set_current_file(cmake_file.string());
            sub_interp.set_variable("CMAKE_CURRENT_LIST_FILE", cmake_file.string());
            sub_interp.set_variable("CMAKE_CURRENT_LIST_DIR", path.string());

            // Inherit parent's accumulated directory properties
            sub_interp.accumulated_directory_properties_ = interp.accumulated_directory_properties_;

            // Register this interpreter for its directory
            std::filesystem::path abs_path = std::filesystem::absolute(path).lexically_normal();
            interp.get_directory_interpreters()[abs_path.string()] = &sub_interp;

            Parser parser(content, cmake_file.string());
            auto ast = parser.parse();
            if (ast) {
                auto res = sub_interp.interpret(ast.value());
                if (!res) interp.set_fatal_error(res.error());
                else sub_interp.finalize_directory_targets();  // Apply retroactive properties
            } else {
                interp.set_fatal_error(InterpreterError{cmake_file.string(), ast.error().row, ast.error().col, ast.error().offset, ast.error().length, ast.error().reason, {}});
            }
        });

        add_builtin("include", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (args.empty()) { interp.set_fatal_error("include() requires an argument"); return; }
            bool optional = false;
            for (size_t i = 1; i < args.size(); ++i) if (args[i] == "OPTIONAL") optional = true;

            auto res = interp.include_file(args[0], optional);
            if (!res) interp.set_fatal_error(res.error());
        });


        add_builtin("break", [](Interpreter& interp, const std::vector<std::string>&) {
            if (interp.get_loop_depth() == 0) {
                interp.set_fatal_error("break() can only be called inside a loop");
                return;
            }
            interp.set_loop_control(Interpreter::LoopControl::BREAK);
        });

        add_builtin("continue", [](Interpreter& interp, const std::vector<std::string>&) {
            if (interp.get_loop_depth() == 0) {
                interp.set_fatal_error("continue() can only be called inside a loop");
                return;
            }
            interp.set_loop_control(Interpreter::LoopControl::CONTINUE);
        });

        add_builtin("return", [](Interpreter& interp, const std::vector<std::string>& args) {
            // CMake 3.25+ supports return(PROPAGATE var1 var2 ...)
            // which propagates variables to the parent scope before returning
            if (!args.empty() && args[0] == "PROPAGATE") {
                for (size_t i = 1; i < args.size(); ++i) {
                    const std::string& var_name = args[i];
                    auto value = interp.get_optional_variable(var_name);
                    if (value.has_value()) {
                        interp.get_variables().set_parent_scope(var_name, *value);
                    } else {
                        // Variable not defined - unset in parent scope
                        interp.get_variables().unset_parent_scope(var_name);
                    }
                }
            }
            interp.request_return();
        });

        add_builtin("cmake_language", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (args.empty()) {
                interp.set_fatal_error("cmake_language requires arguments");
                return;
            }

            if (args[0] == "EVAL") {
                 if (args.size() < 3 || args[1] != "CODE") {
                     interp.set_fatal_error("cmake_language(EVAL) requires CODE keyword and code arguments");
                     return;
                 }
                 std::string code;
                 for (size_t i = 2; i < args.size(); ++i) {
                     code += args[i];
                 }

                 Parser p(code);
                 auto ast = p.parse();
                 if (!ast) {
                     std::vector<CallLocation> backtrace;
                     Interpreter* root = interp.get_root();
                     if (!root->trace_stack_.empty()) {
                         // For parse errors during EVAL, the current command (cmake_language)
                         // should be part of the backtrace.
                         backtrace = root->trace_stack_;
                     }
                     InterpreterError err{"<EVAL>", ast.error().row, ast.error().col, ast.error().offset, ast.error().length, "cmake_language(EVAL) parse error: " + ast.error().reason, backtrace, code};
                     interp.set_fatal_error(err);
                     return;
                 }

                 std::string old_file = interp.get_current_file();
                 interp.set_current_file("<EVAL>");
                 auto res = interp.interpret(ast.value());
                 interp.set_current_file(old_file);

                 if (!res) {
                     InterpreterError err = res.error();
                     if (err.file == "<EVAL>" && !err.source_content) {
                         err.source_content = code;
                     }
                     interp.set_fatal_error(err);
                 }

            } else if (args[0] == "CALL") {
                 if (args.size() < 2) {
                     interp.set_fatal_error("cmake_language(CALL) requires a command name");
                     return;
                 }
                 std::string cmd = args[1];
                 std::vector<std::string> cmd_args;
                 if (args.size() > 2) {
                     cmd_args.assign(args.begin() + 2, args.end());
                 }

                 auto res = interp.execute_command_with_args(cmd, cmd_args);
                 if (!res) interp.set_fatal_error(res.error());
            } else {
                interp.set_fatal_error("Unknown cmake_language mode: " + args[0]);
            }
        });
    } else {
        variables_.set("CMAKE_SOURCE_DIR", parent_->get_variable("CMAKE_SOURCE_DIR"));
        variables_.set("CMAKE_BINARY_DIR", parent_->get_variable("CMAKE_BINARY_DIR"));
        build_dir_ = parent_->build_dir_;

        // Calculate CMAKE_CURRENT_BINARY_DIR based on relative path from source root
        std::filesystem::path rel_path = std::filesystem::relative(abs_script_dir, parent_->get_variable("CMAKE_SOURCE_DIR"));
        variables_.set("CMAKE_CURRENT_BINARY_DIR", (std::filesystem::path(variables_.get("CMAKE_BINARY_DIR")) / rel_path).lexically_normal().string());
    }
}

std::expected<void, InterpreterError> Interpreter::interpret(const std::vector<AstNode>& ast) {
    for (const auto& node : ast) {
        std::expected<void, InterpreterError> res;
        if (std::holds_alternative<CommandInvocation>(node)) res = execute_command(std::get<CommandInvocation>(node));
        else if (std::holds_alternative<IfBlock>(node)) res = execute_if_block(std::get<IfBlock>(node));
        else if (std::holds_alternative<FunctionBlock>(node)) res = execute_function_block(std::get<FunctionBlock>(node));
        else if (std::holds_alternative<MacroBlock>(node)) res = execute_macro_block(std::get<MacroBlock>(node));
        else if (std::holds_alternative<ForeachBlock>(node)) res = execute_foreach_block(std::get<ForeachBlock>(node));
        else if (std::holds_alternative<WhileBlock>(node)) res = execute_while_block(std::get<WhileBlock>(node));
        else if (std::holds_alternative<BlockBlock>(node)) res = execute_block_block(std::get<BlockBlock>(node));

        if (!res) return res;
        if (loop_control_ != LoopControl::NONE) return {};
        if (return_requested_) return {};  // Early return from script/function
    }
    return {};
}

std::expected<void, InterpreterError> Interpreter::include_file(const std::string& file_path, bool optional) {
    std::filesystem::path path = std::filesystem::path(file_path);

    if(file_path.ends_with("CPack") || file_path.ends_with("CPack.cmake")) {
        print_message("WARNING", "dmake does not support cpack (yet). Ignoring..");
        return {};
    }

    // Helper to check if a path exists, trying with and without .cmake extension
    auto try_path = [this](const std::filesystem::path& p) -> std::optional<std::filesystem::path> {
        if (cached_file_exists(p)) {
            // Verify it's not a directory
            std::error_code ec;
            if (!std::filesystem::is_directory(p, ec)) return p;
        }
        if (!p.has_extension()) {
            std::filesystem::path with_ext = p;
            with_ext.replace_extension(".cmake");
            if (cached_file_exists(with_ext)) {
                std::error_code ec;
                if (!std::filesystem::is_directory(with_ext, ec)) return with_ext;
            }
        }
        return std::nullopt;
    };

    auto find_in_dir = [&try_path](const std::filesystem::path& dir, const std::string& file_path) -> std::optional<std::filesystem::path> {
        auto candidate = dir / file_path;
        return try_path(candidate);
    };

    std::optional<std::filesystem::path> found_path;

    if (path.is_absolute()) {
        found_path = try_path(path);
    } else {
        // Try relative to CMAKE_CURRENT_SOURCE_DIR first
        std::filesystem::path candidate = std::filesystem::path(get_variable("CMAKE_CURRENT_SOURCE_DIR")) / file_path;
        found_path = try_path(candidate);

        // If not found, search CMAKE_MODULE_PATH
        if (!found_path) {
            CMakeList module_paths(get_variable("CMAKE_MODULE_PATH"));
            for (const auto& dir : module_paths) {
                if (!dir.empty()) {
                    found_path = find_in_dir(dir, file_path);
                    if (found_path) break;
                }
            }
        }

        // If not found, search system paths
        if (!found_path) {
            std::vector<std::string> system_modules = {
                "/usr/share/cmake/Modules",
                "/usr/local/share/cmake/Modules",
                "/usr/lib/cmake/Modules",
                "/usr/lib/x86_64-linux-gnu/cmake/Modules"
            };
            for (const auto& dir : system_modules) {
                found_path = find_in_dir(dir, file_path);
                if (found_path) break;
            }
        }
    }

    if (!found_path) {
        if (optional) return {};
        return std::unexpected(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_, 0, 0, "include() could not find: " + file_path, {}});
    }

    path = *found_path;

    // Check if it's a directory (reject directories, but accept symlinks to files)
    if (std::filesystem::is_directory(path)) {
        if (optional) return {};
        return std::unexpected(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_, 0, 0, "include() path is a directory: " + path.string(), {}});
    }

    std::string abs_path = std::filesystem::absolute(path).string();

    // Check include guards
    if (get_root()->global_guarded_files_.contains(abs_path)) return {};
    if (directory_guarded_files_.contains(abs_path)) return {};

    std::ifstream file(path);
    if (!file) {
        return std::unexpected(InterpreterError{path.string(), 0, 0, 0, 0, "include() could not read: " + path.string(), {}});
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Parser parser(content, path.string());
    auto ast = parser.parse();
    if (!ast) {
        return std::unexpected(InterpreterError{path.string(), ast.error().row, ast.error().col, ast.error().offset, ast.error().length, ast.error().reason, {}});
    }

    std::string old_file = get_current_file();
    std::string abs_dir = std::filesystem::path(abs_path).parent_path().string();

    std::string old_list_file = get_variable("CMAKE_CURRENT_LIST_FILE");
    std::string old_list_dir = get_variable("CMAKE_CURRENT_LIST_DIR");

    // Save return state - return() in included file shouldn't affect caller
    bool saved_return = return_requested_;
    return_requested_ = false;

    set_variable("CMAKE_CURRENT_LIST_FILE", abs_path);
    set_variable("CMAKE_CURRENT_LIST_DIR", abs_dir);
    set_current_file(abs_path);

    auto res = interpret(ast.value());

    set_variable("CMAKE_CURRENT_LIST_FILE", old_list_file);
    set_variable("CMAKE_CURRENT_LIST_DIR", old_list_dir);
    set_current_file(old_file);

    // Restore return state - included file's return() doesn't propagate
    return_requested_ = saved_return;

    return res;
}

const std::unordered_set<std::string>* Interpreter::get_directory_listing(const std::filesystem::path& dir) {
    Interpreter* root = get_root();

    // Normalize to absolute path
    std::error_code ec;
    std::filesystem::path abs_dir = std::filesystem::absolute(dir, ec);
    if (ec) return nullptr;

    std::string dir_key = abs_dir.string();

    // Check if directory exists
    if (!std::filesystem::exists(abs_dir, ec) || ec) return nullptr;
    if (!std::filesystem::is_directory(abs_dir, ec) || ec) return nullptr;

    // Get current directory mtime
    std::filesystem::file_time_type current_mtime;
    try {
        current_mtime = std::filesystem::last_write_time(abs_dir, ec);
        if (ec) return nullptr;
    } catch (...) {
        return nullptr;
    }

    // Clock skew detection
    auto now = std::filesystem::file_time_type::clock::now();
    if (current_mtime > now) {
        // Print warning once per directory
        static std::unordered_set<std::string> warned_dirs;
        if (!warned_dirs.contains(dir_key)) {
            *err_ << colors::YELLOW << "Warning: Directory has modification time in the future: "
                  << dir_key << ". Possible clock skew detected." << colors::RESET << std::endl;
            warned_dirs.insert(dir_key);
        }
        // Invalidate cache entry
        root->dir_scan_cache_.erase(dir_key);
        // Continue with fresh scan
    }

    // Look up in cache
    auto it = root->dir_scan_cache_.find(dir_key);
    if (it != root->dir_scan_cache_.end()) {
        // Cache hit - validate mtime
        if (it->second.mtime == current_mtime) {
            return &it->second.entries;
        }
        // Mtime changed - invalidate
        root->dir_scan_cache_.erase(it);
    }

    // Cache miss - scan directory
    std::unordered_set<std::string> entries;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(abs_dir, ec)) {
            if (ec) {
                // Error during iteration - don't cache
                return nullptr;
            }
            entries.insert(entry.path().filename().string());
        }
    } catch (...) {
        // Permission denied or other errors - don't cache
        return nullptr;
    }

    // Store in cache
    DirectoryCacheEntry cache_entry{current_mtime, std::move(entries)};
    auto [inserted_it, _] = root->dir_scan_cache_.emplace(dir_key, std::move(cache_entry));
    return &inserted_it->second.entries;
}

bool Interpreter::cached_file_exists(const std::filesystem::path& full_path) {
    // Split path into directory and filename
    auto parent = full_path.parent_path();
    auto filename = full_path.filename().string();

    if (filename.empty()) return false;

    // Try to get cached listing of parent directory
    // This will populate the cache on-demand if not already cached
    auto* entries = get_directory_listing(parent);
    if (!entries) {
        // Cache population failed (permissions, doesn't exist, etc.)
        // Fall back to direct filesystem check
        std::error_code ec;
        return std::filesystem::exists(full_path, ec);
    }

    return entries->contains(filename);
}

bool Interpreter::cached_file_exists(const std::filesystem::path& dir, const std::string& filename) {
    auto* entries = get_directory_listing(dir);
    return entries && entries->contains(filename);
}

void Interpreter::set_fatal_error(const std::string& message) {
    Interpreter* root = get_root();
    std::vector<CallLocation> backtrace;
    if (!root->trace_stack_.empty()) {
        // The last element is the current command, we want everything BEFORE it as backtrace
        for (size_t i = 0; i < root->trace_stack_.size() - 1; ++i) {
            backtrace.push_back(root->trace_stack_[i]);
        }

        const auto& current = root->trace_stack_.back();
        set_fatal_error(InterpreterError{current.file, current.row, current.col, current.offset, current.length, message, backtrace});
    } else {
        set_fatal_error(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_, 0, 0, message, {}});
    }
}

void Interpreter::set_fatal_error(const InterpreterError& error) {
    Interpreter* root = get_root();
    if (!root->fatal_error_) {
        root->fatal_error_ = error;
    } else {
        // Enrich existing error if possible
        if (error.source_content && !root->fatal_error_->source_content && error.file == root->fatal_error_->file) {
            root->fatal_error_->source_content = error.source_content;
        }
    }
}

std::optional<InterpreterError> Interpreter::get_fatal_error() const {
    return const_cast<Interpreter*>(this)->get_root()->fatal_error_;
}

void Interpreter::clear_fatal_error() {
    get_root()->fatal_error_ = std::nullopt;
}

void Interpreter::add_builtin(const std::string& name, BuiltinFunction func) {
    get_root()->builtins_[name] = func;
}

std::vector<std::string> Interpreter::expand_arguments(const std::vector<Argument>& args) {
    std::vector<std::string> result;
    for (const auto& arg : args) {
        std::string val = evaluate_argument(arg);
        if (arg.quoted) {
            result.push_back(val);
        } else {
            if (val.empty()) continue;
            // Split by semicolon for unquoted arguments (list expansion)
            CMakeList lst(val);
            for(const auto& item : lst) {
                result.push_back(item);
            }
        }
    }
    return result;
}

std::expected<void, InterpreterError> Interpreter::execute_command(const CommandInvocation& cmd) {
    current_cmd_row_ = cmd.row;
    current_cmd_col_ = cmd.col;

    // Push to backtrace stack
    Interpreter* root = get_root();
    root->trace_stack_.push_back({current_file_, cmd.row, cmd.col, cmd.offset, cmd.length, cmd.identifier});

    // Expand arguments once
    std::vector<std::string> expanded_args = expand_arguments(cmd.arguments);

    auto res = execute_command_with_args(cmd.identifier, expanded_args);

    if (!res) {
        if (auto err = get_fatal_error()) {
             // The error is already set, just clean up stack and return it
             safe_pop_trace_stack("error in: " + cmd.identifier);
             return std::unexpected(*err);
        }
    }

    safe_pop_trace_stack("command: " + cmd.identifier);
    return res;
}

std::expected<void, InterpreterError> Interpreter::execute_command_with_args(const std::string& identifier, const std::vector<std::string>& args) {
    Interpreter* root = get_root();
    std::string lower_identifier = identifier;
    std::transform(lower_identifier.begin(), lower_identifier.end(), lower_identifier.begin(), ::tolower);

    auto bit = root->builtins_.find(lower_identifier);
    if (bit != root->builtins_.end()) {
        bit->second(*this, args);
        if (auto err = get_fatal_error()) {
            return std::unexpected(*err);
        }
        return {};
    }

    // Look up user functions/macros at root - CMake functions are globally visible
    auto fit = root->user_functions_.find(lower_identifier);
    if (fit != root->user_functions_.end()) {
        // Move the unique_ptr out to keep the FunctionBlock alive if it gets redefined during execution
        auto func_ptr = std::move(fit->second);
        root->user_functions_.erase(fit);
        auto result = invoke_user_function(*func_ptr, args);
        // Put it back if not replaced by a new definition
        if (root->user_functions_.find(lower_identifier) == root->user_functions_.end()) {
            root->user_functions_[lower_identifier] = std::move(func_ptr);
        }
        return result;
    }
    auto mit = root->user_macros_.find(lower_identifier);
    if (mit != root->user_macros_.end()) {
        // Move the unique_ptr out to keep the MacroBlock alive if it gets redefined during execution
        auto macro_ptr = std::move(mit->second);
        root->user_macros_.erase(mit);
        auto result = invoke_user_macro(*macro_ptr, args);
        // Put it back if not replaced by a new definition
        if (root->user_macros_.find(lower_identifier) == root->user_macros_.end()) {
            root->user_macros_[lower_identifier] = std::move(macro_ptr);
        }
        return result;
    }

    set_fatal_error("Unknown command: " + identifier);
    return std::unexpected(*get_fatal_error());
}

std::expected<void, InterpreterError> Interpreter::execute_if_block(const IfBlock& if_block) {
    Interpreter* root = get_root();
    root->trace_stack_.push_back({current_file_, if_block.row, if_block.col, if_block.offset, if_block.length, "if"});

    // CMake argument elision: Remove arguments that evaluate to empty strings
    // when they contain variable references (unquoted arguments with ${...})
    auto filter_empty_args = [&](const std::vector<Argument>& args) -> std::vector<Argument> {
        std::vector<Argument> filtered;
        for (const auto& arg : args) {
            // Check if argument contains variable references
            bool has_var_ref = false;
            for (const auto& part : arg.parts) {
                if (std::holds_alternative<VariableReference>(part)) {
                    has_var_ref = true;
                    break;
                }
            }

            // If it has variable references, evaluate it and check if empty
            if (has_var_ref && !arg.quoted) {
                std::string val = evaluate_argument(arg);
                if (val.empty()) {
                    continue;  // Skip this argument (elision)
                }
            }

            filtered.push_back(arg);
        }
        return filtered;
    };

    auto filtered_condition = filter_empty_args(if_block.condition);
    auto cond_result = evaluate_condition(filtered_condition, if_block.row, if_block.col, if_block.offset, if_block.length);
    if (!cond_result) {
        set_fatal_error(cond_result.error());
        safe_pop_trace_stack("if block condition error");
        return std::unexpected(cond_result.error());
    }

    if (cond_result.value()) {
        auto res = interpret(if_block.then_branch);
        if (!res) set_fatal_error(res.error());
        safe_pop_trace_stack("if block");
        return res;
    }

    for (const auto& elseif : if_block.elseif_branches) {
        auto filtered_elseif_cond = filter_empty_args(elseif.condition);
        auto elseif_cond = evaluate_condition(filtered_elseif_cond, elseif.row, elseif.col, elseif.offset, elseif.length);
        if (!elseif_cond) {
            set_fatal_error(elseif_cond.error());
            safe_pop_trace_stack("elseif block condition error");
            return std::unexpected(elseif_cond.error());
        }
        if (elseif_cond.value()) {
            auto res = interpret(elseif.body);
            if (!res) set_fatal_error(res.error());
            safe_pop_trace_stack("elseif block");
            return res;
        }
    }

    auto res = interpret(if_block.else_branch);
    if (!res) set_fatal_error(res.error());
    safe_pop_trace_stack("if block");
    return res;
}

std::expected<void, InterpreterError> Interpreter::execute_function_block(const FunctionBlock& block) {
    std::string lower_name = block.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    // Store at root - CMake functions/macros are globally visible
    Interpreter* root = get_root();
    root->user_functions_[lower_name] = std::make_unique<FunctionBlock>(block);
    root->user_macros_.erase(lower_name);
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_macro_block(const MacroBlock& block) {
    std::string lower_name = block.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    // Store at root - CMake functions/macros are globally visible
    Interpreter* root = get_root();
    root->user_macros_[lower_name] = std::make_unique<MacroBlock>(block);
    root->user_functions_.erase(lower_name);
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_foreach_block(const ForeachBlock& block) {
    Interpreter* root = get_root();
    root->trace_stack_.push_back({current_file_, block.row, block.col, block.offset, block.length, "foreach"});

    // Handle ZIP_LISTS mode separately due to different variable handling
    if (std::holds_alternative<ForeachZipLists>(block.params)) {
        const auto& zip = std::get<ForeachZipLists>(block.params);

        // Save previous values of all loop variables (for nested loops)
        std::vector<std::pair<std::string, bool>> saved_vars;
        std::vector<std::string> saved_values;

        // For single loop var: save var_0, var_1, ... based on number of lists
        // For multiple loop vars: save each variable directly
        std::vector<std::string> var_names_to_save;
        if (zip.loop_vars.size() == 1) {
            // Single variable mode: create var_0, var_1, etc.
            for (size_t i = 0; i < zip.lists.size(); ++i) {
                var_names_to_save.push_back(zip.loop_vars[0] + "_" + std::to_string(i));
            }
        } else {
            // Multiple variables mode: use variables directly
            var_names_to_save = zip.loop_vars;
        }

        for (const auto& var_name : var_names_to_save) {
            bool was_set = is_variable_set(var_name);
            std::string old_value = was_set ? get_variable(var_name) : "";
            saved_vars.push_back({var_name, was_set});
            saved_values.push_back(old_value);
        }

        // Evaluate all lists and find max length
        std::vector<CMakeList> evaluated_lists;
        size_t max_length = 0;
        for (const auto& list_arg : zip.lists) {
            std::string list_name = evaluate_argument(list_arg);
            CMakeList list(get_variable(list_name));
            max_length = std::max(max_length, list.size());
            evaluated_lists.push_back(std::move(list));
        }

        loop_depth_++;

        // Iterate over all lists in parallel
        for (size_t i = 0; i < max_length; ++i) {
            // Set loop variables for this iteration
            for (size_t list_idx = 0; list_idx < evaluated_lists.size(); ++list_idx) {
                const auto& list = evaluated_lists[list_idx];
                std::string value = (i < list.size()) ? list[i] : "";  // Pad with empty string

                // Determine variable name
                std::string var_name;
                if (zip.loop_vars.size() == 1) {
                    // Single loop var: set var_0, var_1, etc.
                    var_name = zip.loop_vars[0] + "_" + std::to_string(list_idx);
                } else {
                    // Multiple loop vars: set directly (if we have enough variables)
                    if (list_idx < zip.loop_vars.size()) {
                        var_name = zip.loop_vars[list_idx];
                    } else {
                        // More lists than variables - skip extra lists
                        continue;
                    }
                }

                set_variable(var_name, value);
            }

            // Execute loop body
            auto res = interpret(block.body);
            if (!res) {
                set_fatal_error(res.error());
                if (loop_depth_ <= 0) {
                    std::cerr << "FATAL: loop_depth_ is " << loop_depth_ << " when handling foreach body error\n";
                    std::abort();
                }
                loop_depth_--;
                // Restore all loop variables
                for (size_t j = 0; j < saved_vars.size(); ++j) {
                    if (saved_vars[j].second) {
                        set_variable(saved_vars[j].first, saved_values[j]);
                    } else {
                        unset_variable(saved_vars[j].first);
                    }
                }
                safe_pop_trace_stack("foreach zip_lists body error");
                return res;
            }

            if (loop_control_ == LoopControl::BREAK) { clear_loop_control(); break; }
            if (loop_control_ == LoopControl::CONTINUE) clear_loop_control();
            if (return_requested_) break;
        }

        // Restore all loop variables
        for (size_t j = 0; j < saved_vars.size(); ++j) {
            if (saved_vars[j].second) {
                set_variable(saved_vars[j].first, saved_values[j]);
            } else {
                unset_variable(saved_vars[j].first);
            }
        }

        if (loop_depth_ <= 0) {
            std::cerr << "FATAL: loop_depth_ is " << loop_depth_ << " when trying to decrement in foreach\n";
            std::abort();
        }
        loop_depth_--;
        safe_pop_trace_stack("foreach zip_lists");
        return {};
    }

    // Original logic for non-ZIP_LISTS modes
    // Evaluate the loop variable name (may contain variable references like _${PREFIX}_VAR)
    std::string loop_var_name = evaluate_argument(block.loop_var);

    // Save the loop variable's previous value (for nested loops)
    bool loop_var_was_set = is_variable_set(loop_var_name);
    std::string loop_var_old_value;
    if (loop_var_was_set) {
        loop_var_old_value = get_variable(loop_var_name);
    }

    loop_depth_++;
    CMakeList items;
    if (std::holds_alternative<ForeachSimple>(block.params)) {
        items = from_arguments(expand_arguments(std::get<ForeachSimple>(block.params).items));
    } else if (std::holds_alternative<ForeachRange>(block.params)) {
        const auto& r = std::get<ForeachRange>(block.params);
        long start = r.start ? std::stol(evaluate_argument(*r.start)) : 0;
        long stop = std::stol(evaluate_argument(r.stop));
        // If step not provided, infer direction from start/stop
        long step;
        if (r.step) {
            step = std::stol(evaluate_argument(*r.step));
        } else {
            // Default step: 1 if ascending, -1 if descending
            step = (start <= stop) ? 1 : -1;
        }
        if (step == 0) {
            if (loop_depth_ <= 0) {
                std::cerr << "FATAL: loop_depth_ is " << loop_depth_ << " when handling step=0 error\n";
                std::abort();
            }
            loop_depth_--;
            set_fatal_error("Step cannot be zero");
            auto err = get_fatal_error();
            clear_fatal_error();
            safe_pop_trace_stack("foreach step=0 error");
            return std::unexpected(*err);
        }
        for (long i = start; (step > 0) ? (i <= stop) : (i >= stop); i += step) items.append(std::to_string(i));
    } else if (std::holds_alternative<ForeachIn>(block.params)) {
        const auto& in = std::get<ForeachIn>(block.params);
        for (const auto& l : in.lists) items.append(CMakeList(get_variable(evaluate_argument(l))));
        items.append(from_arguments(expand_arguments(in.items)));
    }

    for (const auto& item : items) {
        set_variable(loop_var_name, item);
        auto res = interpret(block.body);
        if (!res) {
            set_fatal_error(res.error());
            if (loop_depth_ <= 0) {
                std::cerr << "FATAL: loop_depth_ is " << loop_depth_ << " when handling foreach body error\n";
                std::abort();
            }
            loop_depth_--;
            // Restore loop variable before returning
            if (loop_var_was_set) {
                set_variable(loop_var_name, loop_var_old_value);
            } else {
                unset_variable(loop_var_name);
            }
            safe_pop_trace_stack("foreach body error");
            return res;
        }
        if (loop_control_ == LoopControl::BREAK) { clear_loop_control(); break; }
        if (loop_control_ == LoopControl::CONTINUE) clear_loop_control();
        if (return_requested_) break;  // return() exits the loop and propagates to caller
    }

    // Restore loop variable after loop completes
    if (loop_var_was_set) {
        set_variable(loop_var_name, loop_var_old_value);
    } else {
        unset_variable(loop_var_name);
    }

    // Sanity check: loop depth should never go negative
    if (loop_depth_ <= 0) {
        std::cerr << "FATAL: loop_depth_ is " << loop_depth_ << " when trying to decrement in foreach\n";
        std::abort();
    }
    loop_depth_--;
    safe_pop_trace_stack("foreach");
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_while_block(const WhileBlock& block) {
    Interpreter* root = get_root();
    root->trace_stack_.push_back({current_file_, block.row, block.col, block.offset, block.length, "while"});

    loop_depth_++;

    // Evaluate condition and loop
    while (true) {
        auto cond_result = evaluate_condition(block.condition, block.row, block.col, block.offset, block.length);
        if (!cond_result) {
            loop_depth_--;
            safe_pop_trace_stack("while condition error");
            return std::unexpected(cond_result.error());
        }

        // Break if condition is false
        if (!cond_result.value()) {
            break;
        }

        // Execute body
        auto res = interpret(block.body);
        if (!res) {
            loop_depth_--;
            safe_pop_trace_stack("while body error");
            return res;
        }

        if (loop_control_ == LoopControl::BREAK) { clear_loop_control(); break; }
        if (loop_control_ == LoopControl::CONTINUE) clear_loop_control();
        if (return_requested_) break;  // return() exits the loop and propagates to caller
    }

    loop_depth_--;
    safe_pop_trace_stack("while");
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_block_block(const BlockBlock& block) {
    Interpreter* root = get_root();
    root->trace_stack_.push_back({current_file_, block.row, block.col, block.offset, block.length, "block"});

    // Create a new variable scope if SCOPE_FOR VARIABLES is set
    if (block.scope_for_variables) {
        // Push a new variable scope
        variables_.push_scope();

        // Execute the body
        auto res = interpret(block.body);

        // Propagate variables back to parent scope before popping
        if (!block.propagate_vars.empty()) {
            // Save propagated variable values before popping
            std::unordered_map<std::string, std::string> propagated_values;
            for (const auto& var_name : block.propagate_vars) {
                if (variables_.is_defined(var_name)) {
                    propagated_values[var_name] = variables_.get(var_name);
                }
            }

            // Pop the block scope
            variables_.pop_scope();

            // Set propagated variables in parent scope
            for (const auto& [var_name, value] : propagated_values) {
                variables_.set(var_name, value);
            }
        } else {
            // Pop the block scope without propagation
            variables_.pop_scope();
        }

        if (!res) {
            safe_pop_trace_stack("block error");
            return res;
        }
    } else {
        // No variable scope, just execute the body
        auto res = interpret(block.body);
        if (!res) {
            safe_pop_trace_stack("block error");
            return res;
        }
    }

    safe_pop_trace_stack("block");
    return {};
}

std::expected<void, InterpreterError> Interpreter::invoke_user_function(const FunctionBlock& func, const std::vector<std::string>& args) {
    // Push metadata frame (for script_dir and function_block tracking)
    frame_stack_.push_front({func.definition_dir, &func});

    // Push variable scope
    variables_.push_scope();

    // Set function parameters
    CMakeList all(args);
    variables_.set("ARGC", std::to_string(all.size()));
    variables_.set("ARGV", all.to_string());
    variables_.set("ARGN", all.sublist(func.parameters.size(), all.size()).to_string());
    for (size_t i = 0; i < all.size(); ++i) {
        variables_.set("ARGV" + std::to_string(i), all[i]);
    }
    for (size_t i = 0; i < func.parameters.size() && i < all.size(); ++i) {
        variables_.set(func.parameters[i], all[i]);
    }

    // Fix CMAKE_CURRENT_LIST_DIR bug: Set to function's definition location
    variables_.set("CMAKE_CURRENT_LIST_DIR", func.definition_dir);
    variables_.set("CMAKE_CURRENT_LIST_FILE", func.definition_file);

    int saved_depth = loop_depth_;
    LoopControl saved_control = loop_control_;
    bool saved_return = return_requested_;
    std::string saved_file = current_file_;
    // Save and clear macro substitutions - functions create a new scope so outer
    // macro parameters should not bleed into the function's variable lookups
    std::map<std::string, std::string> saved_macro_substitutions = macro_substitutions_;
    macro_substitutions_.clear();

    loop_depth_ = 0;
    loop_control_ = LoopControl::NONE;
    return_requested_ = false;
    current_file_ = func.definition_file;

    auto res = interpret(func.body);

    // Pop variable scope (automatic cleanup of all function-local variables)
    variables_.pop_scope();

    // Pop metadata frame
    frame_stack_.pop_front();

    if (!res) set_fatal_error(res.error());

    // Functions create a new scope, so return() inside doesn't affect caller
    return_requested_ = saved_return;
    loop_depth_ = saved_depth;
    loop_control_ = saved_control;
    current_file_ = saved_file;
    macro_substitutions_ = saved_macro_substitutions;
    return res;
}

bool Interpreter::call_user_function(const std::string& name, const std::vector<std::string>& args) {
    // Normalize to lowercase for lookup
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                  [](unsigned char c){ return std::tolower(c); });

    // Look up function in root interpreter
    auto* root = get_root();
    auto it = root->user_functions_.find(lower_name);
    if (it == root->user_functions_.end()) {
        // Function doesn't exist - return false
        return false;
    }

    // Call the function
    auto result = invoke_user_function(*it->second, args);
    return result.has_value();
}

std::expected<void, InterpreterError> Interpreter::invoke_user_macro(const MacroBlock& macro, const std::vector<std::string>& args) {
    // Save and set up macro parameter substitutions (text-replacement, not variables)
    std::map<std::string, std::string> saved_substitutions = macro_substitutions_;

    CMakeList all(args);
    macro_substitutions_["ARGC"] = std::to_string(all.size());
    macro_substitutions_["ARGV"] = all.to_string();
    macro_substitutions_["ARGN"] = all.sublist(macro.parameters.size(), all.size()).to_string();
    for (size_t i = 0; i < all.size(); ++i) {
        macro_substitutions_["ARGV" + std::to_string(i)] = all[i];
    }
    for (size_t i = 0; i < macro.parameters.size() && i < all.size(); ++i) {
        macro_substitutions_[macro.parameters[i]] = all[i];
    }

    std::string saved_file = current_file_;
    current_file_ = macro.definition_file;
    auto res = interpret(macro.body);
    current_file_ = saved_file;
    if (!res) set_fatal_error(res.error());

    // Restore macro substitutions
    macro_substitutions_ = saved_substitutions;

    return res;
}

bool Interpreter::is_falsy(const std::string& val) {
    if (val.empty()) return true;

    // Exact match for "0" only (not "00", "0.0", "-0", etc.)
    if (val == "0") return true;

    std::string upper_val = val;
    std::transform(upper_val.begin(), upper_val.end(), upper_val.begin(), ::toupper);

    // False constants (case-insensitive, exact match)
    if (upper_val == "OFF" || upper_val == "NO" ||
        upper_val == "FALSE" || upper_val == "N" ||
        upper_val == "IGNORE" || upper_val == "NOTFOUND") {
        return true;
    }

    // Ends with -NOTFOUND
    if (upper_val.ends_with("-NOTFOUND")) {
        return true;
    }

    // Everything else is truthy (including "2x", "0.0", "00", "-0", etc.)
    return false;
}

std::expected<bool, InterpreterError> Interpreter::evaluate_condition(const std::vector<Argument>& condition, size_t row, size_t col, size_t offset, size_t length) {
    // Empty condition evaluates to FALSE (CMake behavior)
    if (condition.empty()) {
        return false;
    }

    // Set of keywords that should not be dereferenced as variables
    // All keywords are in uppercase; comparisons are done case-insensitively
    // We use a sorted static array and linear search because it is more branch-prediction
    // friendly for small sets of strings compared to std::set or binary search.
    static constexpr std::array keywords = {
        "(", ")", "AND", "COMMAND", "DEFINED", "EQUAL", "EXISTS", "GREATER",
        "GREATER_EQUAL", "IN_LIST", "IS_ABSOLUTE", "IS_DIRECTORY", "IS_NEWER_THAN",
        "IS_SYMLINK", "LESS", "LESS_EQUAL", "MATCHES", "NOT", "NOT_EQUAL", "OR",
        "POLICY", "STREQUAL", "STRGREATER", "STRGREATER_EQUAL", "STRLESS",
        "STRLESS_EQUAL", "TARGET", "TEST", "VERSION_EQUAL", "VERSION_GREATER",
        "VERSION_GREATER_EQUAL", "VERSION_LESS", "VERSION_LESS_EQUAL"
    };

    // Boolean constants that have fixed truthiness values (case-insensitive)
    static constexpr std::array boolean_constants = {
        "FALSE", "IGNORE", "N", "NO", "NOTFOUND", "OFF", "ON", "TRUE", "Y", "YES"
    };

    // Helper to get the string value of an argument token
    // For unquoted arguments that are not keywords, dereference as variable
    auto get_token_string = [&](const Argument& arg) -> std::string {
        if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0])) {
            return std::get<std::string>(arg.parts[0]);
        }
        return evaluate_argument(arg);
    };

    // Helper to check if a string looks like a numeric constant
    auto is_numeric_constant = [](const std::string& s) -> bool {
        if (s.empty()) return false;
        size_t start = 0;
        if (s[0] == '-' || s[0] == '+') start = 1;
        if (start >= s.length()) return false;

        // Check if rest of string is numeric
        for (size_t i = start; i < s.length(); ++i) {
            if (!std::isdigit(s[i]) && s[i] != '.') return false;
        }
        return true;
    };

    // Helper to check if a variable is defined in any scope
    auto is_variable_defined = [&](const std::string& name) -> bool {
        return variables_.is_defined(name);
    };

    // Helper to evaluate an argument, dereferencing variables unless it's a keyword or constant
    // CMake behavior (pre-CMP0054):
    // - Keywords and quoted strings are returned as-is
    // - Numeric constants are returned as-is
    // - Everything else is dereferenced as a variable:
    //   - If defined, return the variable's value (even if empty)
    //   - If undefined, return empty string
    // Helper to check if an argument contains any variable references
    auto contains_variable_reference = [](const Argument& arg) -> bool {
        for (const auto& part : arg.parts) {
            if (std::holds_alternative<VariableReference>(part)) {
                return true;
            }
        }
        return false;
    };

    auto evaluate_token = [&](const Argument& arg) -> std::string {
        std::string token = get_token_string(arg);

        // Don't dereference keywords (operators) or boolean constants or quoted strings
        // Check case-insensitively for both
        std::string upper_token = token;
        std::transform(upper_token.begin(), upper_token.end(), upper_token.begin(), ::toupper);
        if (arg.quoted ||
            std::find(keywords.begin(), keywords.end(), upper_token) != keywords.end() ||
            std::find(boolean_constants.begin(), boolean_constants.end(), upper_token) != boolean_constants.end()) {
            return token;
        }

        // Don't dereference numeric constants (0, 1, -5, 3.14, etc.)
        if (is_numeric_constant(token)) {
            return token;
        }

        // If the argument is ONLY a single variable reference (e.g., ${VAR}), the expansion
        // already happened in get_token_string() and we have the value. Don't dereference again.
        // But if the argument contains mixed parts (e.g., _${PREFIX}_SUFFIX), the expansion
        // gives us a variable NAME that should be dereferenced to get its value.
        if (arg.parts.size() == 1 && std::holds_alternative<VariableReference>(arg.parts[0])) {
            return token;  // Already have the value from the variable reference
        }

        // Dereference as variable
        // If defined: return value (even if empty)
        // If undefined: return empty string
        // Note: This means undefined variables evaluate to empty (falsy)
        //       but named boolean constants like TRUE, FALSE, ON, OFF, YES, NO, Y, N
        //       are treated as variable names that dereference to empty if not defined
        return get_variable(token);
    };

    // Recursive descent parser with proper CMake precedence
    // Precedence (high to low): unary tests > binary tests > NOT > AND/OR
    size_t pos = 0;
    std::string error_msg;  // Track parsing errors

    std::function<bool()> parse_or;
    std::function<bool()> parse_and;
    std::function<bool()> parse_not;
    std::function<bool()> parse_comparison;
    std::function<bool()> parse_unary_or_primary;

    // AND/OR have lowest precedence and evaluate left-to-right
    // NOTE: CMake does NOT short-circuit - both sides are always evaluated
    parse_or = [&]() -> bool {
        bool left = parse_and();

        while (pos < condition.size() && error_msg.empty()) {
            std::string token = get_token_string(condition[pos]);
            if (token == "OR") {
                pos++;
                if (pos >= condition.size()) {
                    error_msg = "OR operator requires a right operand";
                    return false;
                }
                bool right = parse_and();  // Always evaluate right side (no short-circuit)
                left = left || right;
            } else {
                break;
            }
        }
        return left;
    };

    parse_and = [&]() -> bool {
        bool left = parse_not();

        while (pos < condition.size() && error_msg.empty()) {
            std::string token = get_token_string(condition[pos]);
            if (token == "AND") {
                pos++;
                if (pos >= condition.size()) {
                    error_msg = "AND operator requires a right operand";
                    return false;
                }
                bool right = parse_not();  // Always evaluate right side (no short-circuit)
                left = left && right;
            } else {
                break;
            }
        }
        return left;
    };

    // NOT has higher precedence than AND/OR but lower than comparisons
    parse_not = [&]() -> bool {
        if (pos >= condition.size()) {
            error_msg = "Unexpected end of condition";
            return false;
        }

        std::string token = get_token_string(condition[pos]);
        if (token == "NOT") {
            // Check if there's a valid operand after NOT
            // NOT followed by nothing or by AND/OR should be treated as a primary value
            // (CMake compatibility - NOT without operand evaluates to false)
            if (pos + 1 < condition.size()) {
                std::string next_token = get_token_string(condition[pos + 1]);
                if (next_token != "AND" && next_token != "OR") {
                    // Valid operand exists - NOT is an operator
                    pos++;
                    return !parse_not();  // Right-associative
                }
            }
            // No valid operand - fall through to treat "NOT" as a primary value
        }
        return parse_comparison();
    };

    // Binary comparison operators (EQUAL, LESS, STREQUAL, etc.)
    parse_comparison = [&]() -> bool {
        if (pos >= condition.size()) return false;

        // Save start position in case this isn't a comparison
        size_t start_pos = pos;

        // Try to parse left operand
        bool unary_result = parse_unary_or_primary();

        // Check if next token is a binary operator
        if (pos >= condition.size()) {
            return unary_result;
        }

        std::string op = get_token_string(condition[pos]);

        // Numeric comparisons
        if (op == "EQUAL" || op == "LESS" || op == "GREATER" ||
            op == "LESS_EQUAL" || op == "GREATER_EQUAL" || op == "NOT_EQUAL") {
            pos++;
            if (pos >= condition.size()) {
                error_msg = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition[start_pos]);
            std::string right = evaluate_token(condition[pos++]);

            try {
                double left_num = std::stod(left);
                double right_num = std::stod(right);

                if (op == "EQUAL") return left_num == right_num;
                if (op == "NOT_EQUAL") return left_num != right_num;
                if (op == "LESS") return left_num < right_num;
                if (op == "GREATER") return left_num > right_num;
                if (op == "LESS_EQUAL") return left_num <= right_num;
                if (op == "GREATER_EQUAL") return left_num >= right_num;
            } catch (...) {
                // Fallback to string comparison for EQUAL/NOT_EQUAL
                if (op == "EQUAL") return left == right;
                if (op == "NOT_EQUAL") return left != right;
                return false;
            }
        }
        // String comparisons
        else if (op == "STREQUAL" || op == "STRLESS" || op == "STRGREATER" ||
                 op == "STRLESS_EQUAL" || op == "STRGREATER_EQUAL") {
            pos++;
            if (pos >= condition.size()) {
                error_msg = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition[start_pos]);
            std::string right = evaluate_token(condition[pos++]);

            if (op == "STREQUAL") return left == right;
            if (op == "STRLESS") return left < right;
            if (op == "STRGREATER") return left > right;
            if (op == "STRLESS_EQUAL") return left <= right;
            if (op == "STRGREATER_EQUAL") return left >= right;
        }
        // Version comparisons - component-wise numeric comparison (CMake behavior)
        else if (op.starts_with("VERSION_")) {
            pos++;
            if (pos >= condition.size()) {
                error_msg = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition[start_pos]);
            std::string right = evaluate_token(condition[pos++]);

            int cmp = compare_versions(left, right);

            if (op == "VERSION_EQUAL") return cmp == 0;
            if (op == "VERSION_LESS") return cmp < 0;
            if (op == "VERSION_GREATER") return cmp > 0;
            if (op == "VERSION_LESS_EQUAL") return cmp <= 0;
            if (op == "VERSION_GREATER_EQUAL") return cmp >= 0;
        }
        // regex
        else if(op == "MATCHES") {
            pos++; // Consume operator
            if (pos >= condition.size()) {
                error_msg = "MATCHES operator requires a right operand";
                return false;
            }
            std::string pattern = evaluate_token(condition[pos++]);
            std::regex regex(pattern);
            std::smatch match;
            std::string left = evaluate_token(condition[start_pos]);
            bool result = std::regex_search(left, match, regex);

            if (result) {
                set_variable("CMAKE_MATCH_COUNT", std::to_string(match.size() - 1));
                for (size_t i = 0; i < match.size() && i < 10; ++i) {
                    set_variable("CMAKE_MATCH_" + std::to_string(i), match[i].str());
                }
                // Clear remaining matches if fewer than previous
                 for (size_t i = match.size(); i < 10; ++i) {
                    set_variable("CMAKE_MATCH_" + std::to_string(i), "");
                }
            } else {
                 set_variable("CMAKE_MATCH_COUNT", "0");
                 for (size_t i = 0; i < 10; ++i) {
                    set_variable("CMAKE_MATCH_" + std::to_string(i), "");
                }
            }
            return result;
        }
        // IN_LIST operator: checks if value is in a list variable
        else if (op == "IN_LIST") {
            pos++; // Consume operator
            if (pos >= condition.size()) {
                error_msg = "IN_LIST operator requires a right operand";
                return false;
            }

            std::string value = evaluate_token(condition[start_pos]);
            std::string list_str = evaluate_token(condition[pos++]);

            // Parse the list (semicolon-separated) and check if value is in it
            CMakeList list(list_str);
            return list.contains(value);
        }
        // IS_NEWER_THAN - file timestamp comparison
        else if (op == "IS_NEWER_THAN") {
            pos++;
            if (pos >= condition.size()) {
                error_msg = "IS_NEWER_THAN operator requires a right operand";
                return false;
            }

            std::string left = evaluate_argument(condition[start_pos]);
            std::string right = evaluate_argument(condition[pos++]);

            // CMake behavior: returns true if file1 >= file2 OR either doesn't exist
            std::error_code ec1, ec2;
            auto time1 = std::filesystem::last_write_time(left, ec1);
            auto time2 = std::filesystem::last_write_time(right, ec2);

            if (ec1 || ec2) {
                // Either file doesn't exist - return true
                return true;
            }
            return time1 >= time2;
        }

        // Not a comparison operator - return the unary/primary result
        return unary_result;
    };

    // Unary operators (highest precedence) and primary values
    parse_unary_or_primary = [&]() -> bool {
        if (pos >= condition.size()) return false;

        std::string token = get_token_string(condition[pos]);

        // Parentheses for grouping
        if (token == "(") {
            pos++;
            bool result = parse_or();
            if (pos >= condition.size() || get_token_string(condition[pos]) != ")") {
                error_msg = "Expected ')' to close group";
                return false;
            }
            pos++;
            return result;
        }

        // Unary operators that take one argument
        // If there's no next token, treat the keyword as a primary value instead
        if (token == "DEFINED" && pos + 1 < condition.size()) {
            pos++;
            // DEFINED takes a variable name (don't dereference it)
            std::string var_name = get_token_string(condition[pos++]);

            // Check if variable is defined in any scope
            return variables_.is_defined(var_name);
        } else if (token == "TARGET" && pos + 1 < condition.size()) {
            pos++;
            std::string target_name = get_token_string(condition[pos++]);
            return targets_.contains(target_name);
        } else if (token == "EXISTS" && pos + 1 < condition.size()) {
            pos++;
            // File test operators take paths literally (with variable expansion)
            // but do NOT dereference the entire path as a variable name
            std::string path = evaluate_argument(condition[pos++]);
            return std::filesystem::exists(path);
        } else if (token == "IS_DIRECTORY" && pos + 1 < condition.size()) {
            pos++;
            std::string path = evaluate_argument(condition[pos++]);
            return std::filesystem::is_directory(path);
        } else if (token == "IS_ABSOLUTE" && pos + 1 < condition.size()) {
            pos++;
            std::string path = evaluate_argument(condition[pos++]);
            return std::filesystem::path(path).is_absolute();
        } else if (token == "IS_SYMLINK" && pos + 1 < condition.size()) {
            pos++;
            std::string path = evaluate_argument(condition[pos++]);
            return std::filesystem::is_symlink(path);
        } else if (token == "COMMAND" && pos + 1 < condition.size()) {
            pos++;
            std::string name = evaluate_token(condition[pos++]);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            return get_root()->user_functions_.contains(name);
        } else if (token == "POLICY" && pos + 1 < condition.size()) {
            pos++;
            pos++;  // Consume the policy name/number
            // Always return true - dmake doesn't implement policies but we want
            // scripts to think we support the latest policies
            return true;
        }

        // Primary value - evaluate and check truthiness
        // For keywords that aren't being used as operators (like standalone DEFINED, TARGET, etc.),
        // we need to dereference them as variables, not treat them as keywords
        const Argument& arg = condition[pos++];
        std::string token_str = get_token_string(arg);

        // If it's quoted or a numeric constant, use it as-is
        if (arg.quoted || is_numeric_constant(token_str)) {
            return !is_falsy(token_str);
        }

        // Check if this is a boolean constant (case-insensitive)
        // These have fixed truthiness and should not be dereferenced as variables
        std::string upper_token = token_str;
        std::transform(upper_token.begin(), upper_token.end(), upper_token.begin(), ::toupper);
        if (std::find(boolean_constants.begin(), boolean_constants.end(), upper_token) != boolean_constants.end()) {
            return !is_falsy(token_str);
        }

        // For all other cases (plain literals or concatenations like Prefix_${Suffix}),
        // dereference the result as a variable name
        // Example: if(VAR) should look up the value of variable VAR
        // Example: if(VAR_${suffix}) where suffix="" should expand to "VAR_", then look up VAR_
        std::string val = get_variable(token_str);
        return !is_falsy(val);
    };

    // Start parsing at the lowest precedence level (OR)
    bool result = parse_or();

    // Check if there was an error during parsing
    if (!error_msg.empty()) {
        set_fatal_error(error_msg);
        return std::unexpected(*get_fatal_error());
    }

    // Lenient handling of leftover tokens (CMake compatibility)
    // CMake returns FALSE for malformed conditions instead of erroring
    if (pos < condition.size()) {
        std::string remaining;
        for (size_t i = pos; i < condition.size(); ++i) {
            if (!remaining.empty()) remaining += " ";
            remaining += get_token_string(condition[i]);
        }

        // Trim whitespace (space, tab, CR, LF)
        auto is_whitespace = [](char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        };

        // Trim leading whitespace
        size_t start = 0;
        while (start < remaining.size() && is_whitespace(remaining[start])) {
            ++start;
        }

        // Trim trailing whitespace
        size_t end = remaining.size();
        while (end > start && is_whitespace(remaining[end - 1])) {
            --end;
        }

        remaining = remaining.substr(start, end - start);

        // Only warn if there are actual non-whitespace tokens
        if (!remaining.empty()) {
            print_message("AUTHOR_WARNING",
                          "Malformed if() condition - unexpected tokens: " + remaining +
                          "\n  Condition evaluates to FALSE (CMake compatibility mode)");
        }
        return false;
    }

    return result;
}

std::string Interpreter::evaluate_variable_reference(const VariableReference& ref) {
    // Step 1: Recursively evaluate name_parts to build the final variable name
    std::string name;
    for (const auto& part : ref.name_parts) {
        if (std::holds_alternative<std::string>(part)) {
            name += std::get<std::string>(part);
        } else {
            name += evaluate_variable_reference(std::get<VariableReference>(part));
        }
    }

    // Step 2: Lookup based on namespace
    if (ref.namespace_prefix.empty()) {
        // Regular variable lookup
        return get_variable(name);
    } else if (ref.namespace_prefix == "ENV") {
        const char* env_var = getenv(name.c_str());
        return env_var ? env_var : "";
    } else if (ref.namespace_prefix == "CACHE") {
        auto* root = get_root();
        auto it = root->cache_variables_.find(name);
        return (it != root->cache_variables_.end()) ? it->second : "";
    }
    return "";
}

std::string Interpreter::evaluate_argument(const Argument& arg) {
    std::string res;
    for (const auto& p : arg.parts) {
        if (std::holds_alternative<std::string>(p)) {
            res += std::get<std::string>(p);
        } else {
            res += evaluate_variable_reference(std::get<VariableReference>(p));
        }
    }
    return res;
}

std::string Interpreter::get_variable(const std::string& name) const {
    return get_optional_variable(name).value_or("");
}

void Interpreter::set_variable(const std::string& name, const std::string& val) {
    variables_.set(name, val);
}

void Interpreter::set_cache_variable(const std::string& var_name, const std::string& value) {
    get_root()->cache_variables_[var_name] = value;
}

bool Interpreter::unset_variable(const std::string& name) {
    variables_.unset(name);
    return true;  // ShadowMap::unset is always safe (no-op if variable doesn't exist)
}

bool Interpreter::is_variable_set(const std::string& name) const {
    // Check macro substitutions first
    if (macro_substitutions_.contains(name)) {
        return true;
    }

    // Check local variables (O(1) via ShadowMap)
    if (variables_.is_defined(name)) {
        return true;
    }

    // Check parent interpreter (subdirectory scope)
    if (parent_ && parent_->is_variable_set(name)) {
        return true;
    }

    // Check cache variables
    return get_root()->cache_variables_.contains(name);
}

std::optional<std::string> Interpreter::get_optional_variable(const std::string& name) const {
    if (frame_stack_.empty()) {
        std::cerr << "FATAL: get_optional_variable('" << name << "') called with empty frame_stack_\n";
        std::abort();
    }

    // Check macro substitutions first (macro parameters take precedence)
    auto macro_it = macro_substitutions_.find(name);
    if (macro_it != macro_substitutions_.end()) {
        return macro_it->second;
    }

    // Function-scoped special variables (lazy evaluation - check current frame only)
    if (name == "CMAKE_CURRENT_FUNCTION" ||
        name == "CMAKE_CURRENT_FUNCTION_LIST_FILE" ||
        name == "CMAKE_CURRENT_FUNCTION_LIST_DIR") {

        const auto* fb = frame_stack_.front().function_block;
        if (fb != nullptr) {
            if (name == "CMAKE_CURRENT_FUNCTION") return fb->name;
            if (name == "CMAKE_CURRENT_FUNCTION_LIST_FILE") return fb->definition_file;
            if (name == "CMAKE_CURRENT_FUNCTION_LIST_DIR") return fb->definition_dir;
        }
        return std::nullopt;  // Not in a function
    }

    // Check local variables (use is_defined to distinguish "not set" from "set to empty")
    if (variables_.is_defined(name)) {
        return variables_.get(name);
    }

    // Check parent interpreter (subdirectory scope)
    if (parent_) {
        auto parent_value = parent_->get_optional_variable(name);
        if (parent_value.has_value()) {
            return parent_value;
        }
    }

    // Check cache variables (CACHE variables are globally accessible)
    auto cache_it = get_root()->cache_variables_.find(name);
    if (cache_it != get_root()->cache_variables_.end()) {
        return cache_it->second;
    }

    return std::nullopt;
}

void Interpreter::print_message(const std::string& mode, const std::string& msg, bool is_err) {
    std::ostream& os = is_err ? *err_ : *out_;
    bool color = isatty(is_err ? STDERR_FILENO : STDOUT_FILENO);
    std::string prefix, msg_color;

    // Get indentation
    std::string indent = get_variable("CMAKE_MESSAGE_INDENT");

    // Determine prefix and color based on mode
    if (mode == "FATAL_ERROR") {
        prefix = "[FATAL_ERROR]";
        msg_color = colors::BOLD_RED;
    } else if (mode == "SEND_ERROR") {
        prefix = "[SEND_ERROR]";
        msg_color = colors::RED;
    } else if (mode == "WARNING") {
        prefix = "[WARNING]";
        msg_color = colors::YELLOW;
    } else if (mode == "AUTHOR_WARNING") {
        prefix = "[AUTHOR_WARNING]";
        msg_color = colors::YELLOW;
    } else if (mode == "DEPRECATION") {
        prefix = "[DEPRECATION]";
        msg_color = colors::YELLOW;
    } else if (mode == "DEPRECATION_ERROR") {
        prefix = "[DEPRECATION]";
        msg_color = colors::RED;
    } else if (mode == "NOTICE") {
        prefix = "[NOTICE]";
        msg_color = colors::WHITE;
    } else if (mode == "STATUS") {
        prefix = "[STATUS]";
        msg_color = colors::CYAN;
    } else if (mode == "VERBOSE") {
        prefix = "[VERBOSE]";
        msg_color = colors::DIM;
    } else if (mode == "DEBUG") {
        prefix = "[DEBUG]";
        msg_color = colors::DIM_CYAN;
    } else if (mode == "TRACE") {
        prefix = "[TRACE]";
        msg_color = colors::DIM;
    } else if (mode == "CHECK_START") {
        prefix = "--";
        msg_color = colors::CYAN;
    } else if (mode == "CHECK_PASS") {
        prefix = "--";
        msg_color = colors::GREEN;
    } else if (mode == "CHECK_FAIL") {
        prefix = "--";
        msg_color = colors::RED;
    } else {
        prefix = "[INFO]";
        msg_color = "";
    }

    if (color && !msg_color.empty()) {
        os << msg_color << prefix << " " << indent << msg << colors::RESET << std::endl;
    } else {
        os << prefix << " " << indent << msg << std::endl;
    }
}

CMakeList Interpreter::from_arguments(const std::vector<std::string>& args) {
    return CMakeList(args);
}

void Interpreter::check_start(const std::string& message) {
    // Add indentation for nested checks
    std::string indent(check_stack_.size() * 2, ' ');
    print_message("CHECK_START", indent + "Checking " + message, false);
    check_stack_.push_back(message);
}

void Interpreter::check_pass(const std::string& result_message) {
    if (check_stack_.empty()) {
        print_message("WARNING", "CHECK_PASS without matching CHECK_START", true);
        return;
    }

    std::string original = check_stack_.back();
    check_stack_.pop_back();

    // Add indentation for nested checks
    std::string indent(check_stack_.size() * 2, ' ');

    // Print result with original message
    print_message("CHECK_PASS", indent + "Checking " + original + " - " + result_message, false);
}

void Interpreter::check_fail(const std::string& result_message) {
    if (check_stack_.empty()) {
        print_message("WARNING", "CHECK_FAIL without matching CHECK_START", true);
        return;
    }

    std::string original = check_stack_.back();
    check_stack_.pop_back();

    // Add indentation for nested checks
    std::string indent(check_stack_.size() * 2, ' ');

    // Print result with original message
    print_message("CHECK_FAIL", indent + "Checking " + original + " - " + result_message, false);
}

void Interpreter::accumulate_error(const std::string& error) {
    has_send_errors_ = true;
    // Errors are already printed by message(), just track that we had errors
}

void Interpreter::check_invariants() const {
    // Critical invariant: frame stack should never be empty during execution
    if (frame_stack_.empty()) {
        std::cerr << "FATAL: frame_stack_ is empty (this should never happen)\n";
        std::abort();
    }

    // Loop control should only be set when in a loop
    if (loop_control_ != LoopControl::NONE && loop_depth_ <= 0) {
        std::cerr << "FATAL: loop_control_ is set (" << static_cast<int>(loop_control_)
                  << ") but loop_depth_ is " << loop_depth_ << "\n";
        std::abort();
    }

    // Loop depth should never be negative
    if (loop_depth_ < 0) {
        std::cerr << "FATAL: loop_depth_ is negative (" << loop_depth_ << ")\n";
        std::abort();
    }

    // Root interpreter should have builtins
    if (parent_ == nullptr && builtins_.empty()) {
        std::cerr << "FATAL: root interpreter has no builtins (not properly initialized?)\n";
        std::abort();
    }
}

void Interpreter::safe_pop_trace_stack(const std::string& context) {
    Interpreter* root = get_root();
    if (root->trace_stack_.empty()) {
        std::cerr << "FATAL: Attempting to pop empty trace_stack_ in context: " << context << "\n";
        std::cerr << "       This indicates a push/pop imbalance in the interpreter\n";
        std::abort();
    }
    root->trace_stack_.pop_back();
}

void Interpreter::finalize_directory_targets() {
    // Apply accumulated directory properties to all owned targets
    for (const auto& target : owned_targets_) {
        for (const auto& [prop_name, values] : accumulated_directory_properties_) {
            if (!values.empty()) {
                // append_property handles duplicates gracefully (just appends again)
                target->append_property(prop_name, values, PropertyVisibility::PRIVATE);
            }
        }
    }
}

std::optional<int64_t> Interpreter::get_dir_mtime_cached(const std::string& path) {
    auto root = get_root();

    // Skip caching for project paths (source/binary dirs) - they can change during build
    if (is_project_path(path)) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            return st.st_mtime;
        }
        return std::nullopt;
    }

    // Check session cache
    auto it = root->dir_mtime_cache_.find(path);
    if (it != root->dir_mtime_cache_.end()) {
        return it->second;
    }

    // Not cached - stat and cache it
    struct stat st;
    std::optional<int64_t> mtime;
    if (stat(path.c_str(), &st) == 0) {
        mtime = st.st_mtime;
    }

    root->dir_mtime_cache_[path] = mtime;
    return mtime;
}

bool Interpreter::is_project_path(const std::string& path) const {
    std::filesystem::path abs_path = std::filesystem::absolute(path);
    std::filesystem::path source_dir = std::filesystem::absolute(get_variable("CMAKE_SOURCE_DIR"));
    std::filesystem::path binary_dir = std::filesystem::absolute(get_variable("CMAKE_BINARY_DIR"));

    // Check if path starts with source_dir or binary_dir
    auto mismatch_src = std::mismatch(source_dir.begin(), source_dir.end(), abs_path.begin(), abs_path.end());
    if (mismatch_src.first == source_dir.end()) {
        return true;  // Under source dir
    }

    auto mismatch_bin = std::mismatch(binary_dir.begin(), binary_dir.end(), abs_path.begin(), abs_path.end());
    if (mismatch_bin.first == binary_dir.end()) {
        return true;  // Under binary dir
    }

    return false;
}

}
