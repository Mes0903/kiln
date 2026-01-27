#pragma once

#include <string>
#include <vector>
#include <expected>
#include <optional>
#include <map>
#include <filesystem>

namespace dmake {

// Dynamic Dependency Info - information extracted from module scanning
struct ModuleDependencyInfo {
    std::string source;                      // Source file path
    std::string provides;                    // Module name exported (empty if none)
    std::vector<std::string> imports;        // Module names imported
    std::vector<std::string> header_imports_system;  // System headers imported (e.g., <vector>)
    std::vector<std::string> header_imports_user;    // User headers imported (e.g., "myheader.h")
    bool is_module_partition = false;        // True if this is a module partition
    std::string partition_name;              // Partition name (if is_module_partition)
    std::filesystem::file_time_type timestamp;  // Source file timestamp at scan time
};

// Module map entry - maps module names to their BMI paths
struct ModuleMapEntry {
    std::string module_name;
    std::string bmi_path;
    std::string source_path;
    std::string object_task_id;  // Task ID that produces this module's BMI
};

// Parse DDI file from disk (JSON format)
std::expected<ModuleDependencyInfo, std::string> parse_ddi_file(const std::string& path);

// Write DDI file to disk (JSON format)
std::expected<void, std::string> write_ddi_file(const std::string& path, const ModuleDependencyInfo& info);

// Parse module info from g++ preprocessor output
// Input is the combined stdout/stderr from: g++ -E -fdirectives-only -fmodules-ts <source>
ModuleDependencyInfo parse_module_scan_output(const std::string& output, const std::string& source_path);

// Generate module mapper file content for g++ -fmodule-mapper=<file>
// Maps module names to BMI file paths
std::string generate_module_mapper_content(const std::vector<ModuleMapEntry>& entries);

// Parse module mapper file
std::expected<std::vector<ModuleMapEntry>, std::string> parse_module_mapper_file(const std::string& path);

// Get the BMI (Binary Module Interface) path for a given module name
std::string get_bmi_path(const std::string& binary_dir, const std::string& module_name);

// Get the DDI file path for a given source file
std::string get_ddi_path(const std::string& binary_dir, const std::string& source_path);

// Get the BMI path for a system header unit (e.g., <vector>)
std::string get_header_unit_bmi_path(const std::string& binary_dir, const std::string& header, bool is_system);

} // namespace dmake
