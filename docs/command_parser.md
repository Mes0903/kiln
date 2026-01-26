# Command Argument Parsing Utility

## Problem
Currently, `dmake` builtins manually parse `std::vector<std::string> args`. This leads to:
1. **Inconsistency**: Different commands handle keywords and flags differently.
2. **Poor Error Reporting**: Commands often fail silently or with vague "wrong number of arguments" messages when encountering unknown keywords.
3. **Complexity**: Commands like `add_custom_command` or `execute_process` require complex state machines to handle repeating keywords (e.g., `COMMAND`).

## Solution: `CommandParser`
A builder-style utility that declares the expected structure of a command and handles the iteration and validation automatically.

### API Overview

```cpp
class CommandParser {
public:
    CommandParser(std::string cmd_name, const std::vector<std::string>& args);

    // Bindings
    void add_positional(std::string& var, std::string label, bool required = true);
    void add_flag(std::string keyword, bool& var);
    void add_value(std::string keyword, std::string& var);
    void add_list(std::string keyword, std::vector<std::string>& var);
    void add_multi_list(std::string keyword, std::vector<std::vector<std::string>>& var);
    void add_default_list(std::vector<std::string>& var);

    // Execution
    std::expected<void, std::string> parse();
};

// Helper macro for builtins
#define PARSE_OR_RETURN(parser, interp)
```

### Parsing Algorithm
The parser uses a single-pass state machine over the expanded arguments.

1. **Phase 1: Positional Arguments**
   - The first $N$ arguments that are NOT recognized as keywords are assigned to the registered positionals.
   - If a recognized keyword is encountered during this phase, it terminates positional parsing (unless the keyword is a flag).

2. **Phase 2: Keyword-State Parsing**
   - The parser maintains an "Active Keyword" state.
   - If an argument matches a registered `VALUE`, `LIST`, or `MULTI_LIST` keyword, that keyword becomes **Active**.
   - If an argument matches a `FLAG` keyword, the boolean is set to `true`, and the **Active** keyword remains unchanged.
   - If an argument is NOT a keyword:
     - If a keyword is **Active**, the argument is appended to its target (string, vector, or nested vector).
     - If no keyword is active, the argument is appended to the **Default List** (if provided).
     - Otherwise, it is treated as an **Unknown Argument** error.

### Special Handling: Multi-Lists
For commands like `execute_process(COMMAND a b COMMAND c d)`, each time the `COMMAND` keyword is encountered, the parser starts a new `std::vector<std::string>` in the target `vector<vector<string>>`.

### Error Handling
The `parse()` method returns `std::expected<void, std::string>`. If parsing fails, it returns an error message.
The `PARSE_OR_RETURN(parser, interp)` macro is recommended for use in builtins to automatically report the error to the interpreter and return.

## Examples

### `target_include_directories`
```cpp
CommandParser parser("target_include_directories", args);
std::string target;
std::vector<std::string> pub, priv, inter;

parser.add_positional(target, "target");
parser.add_list("PUBLIC", pub);
parser.add_list("PRIVATE", priv);
parser.add_list("INTERFACE", inter);

PARSE_OR_RETURN(parser, interp);
// Logic using pub, priv, inter...
```

### `execute_process`
```cpp
CommandParser parser("execute_process", args);
std::vector<std::vector<std::string>> commands;
std::string working_dir;

parser.add_multi_list("COMMAND", commands);
parser.add_value("WORKING_DIRECTORY", working_dir);

PARSE_OR_RETURN(parser, interp);
// commands[0] = {"ls", "-l"}
// commands[1] = {"grep", "foo"}
```

## Assumptions & Limitations
- **Keyword Recognition**: Any argument exactly matching a registered keyword is treated as that keyword. Users must quote arguments that happen to be keywords if they want them treated as literal values (dmake currently requires further work to distinguish quoted vs unquoted after expansion).
- **Flag Persistence**: Flags do not reset the active list. `target_sources(mylib PRIVATE s1.cpp FLAG s2.cpp)` will result in both `s1.cpp` and `s2.cpp` in the `PRIVATE` list.