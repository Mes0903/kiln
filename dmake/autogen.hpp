#pragma once

#include <string>
#include <map>
#include <memory>

namespace dmake {

class Target;
class GraphTransaction;
class Interpreter;

// Called from Target::generate_tasks() after resolve() and pre-build task creation,
// but before module scanner and object task generation.
// Scans sources/headers for Qt macros, creates moc/uic/rcc tasks,
// injects generated sources into SOURCES, adds autogen include dir.
void generate_autogen_tasks(
    Target& target,
    GraphTransaction& txn,
    Interpreter& interp,
    const std::map<std::string, std::shared_ptr<Target>>& all_targets,
    const std::string& pre_build_task_id);

} // namespace dmake
