#include "module_scanner.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <glaze/glaze.hpp>

namespace dmake {

// JSON-serializable version of ModuleDependencyInfo (without file_time_type)
struct DDIJson {
    std::string source;
    std::string provides;
    std::vector<std::string> imports;
    bool is_module_partition = false;
    std::string partition_name;
    int64_t timestamp = 0;  // Epoch time in nanoseconds
};

std::expected<ModuleDependencyInfo, std::string> parse_ddi_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return std::unexpected("Failed to open DDI file: " + path);
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    DDIJson ddi_json;
    auto ec = glz::read_json(ddi_json, content);
    if (ec) {
        return std::unexpected("Failed to parse DDI JSON from " + path + ": " + glz::format_error(ec, content));
    }

    ModuleDependencyInfo info;
    info.source = ddi_json.source;
    info.provides = ddi_json.provides;
    info.imports = ddi_json.imports;
    info.is_module_partition = ddi_json.is_module_partition;
    info.partition_name = ddi_json.partition_name;

    // Convert epoch nanoseconds back to file_time_type
    auto duration = std::chrono::nanoseconds(ddi_json.timestamp);
    info.timestamp = std::filesystem::file_time_type(std::chrono::duration_cast<std::filesystem::file_time_type::duration>(duration));

    return info;
}

std::expected<void, std::string> write_ddi_file(const std::string& path, const ModuleDependencyInfo& info) {
    // Ensure parent directory exists
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) {
        return std::unexpected("Failed to create directory for DDI file: " + ec.message());
    }

    DDIJson ddi_json;
    ddi_json.source = info.source;
    ddi_json.provides = info.provides;
    ddi_json.imports = info.imports;
    ddi_json.is_module_partition = info.is_module_partition;
    ddi_json.partition_name = info.partition_name;
    ddi_json.timestamp = info.timestamp.time_since_epoch().count();

    std::string json;
    auto write_ec = glz::write_json(ddi_json, json);
    if (write_ec) {
        return std::unexpected("Failed to serialize DDI to JSON: " + glz::format_error(write_ec));
    }

    std::ofstream file(path);
    if (!file) {
        return std::unexpected("Failed to open DDI file for writing: " + path);
    }
    file << json;

    if (!file) {
        return std::unexpected("Failed to write DDI file: " + path);
    }

    return {};
}

ModuleDependencyInfo parse_module_scan_output(const std::string& output, const std::string& source_path) {
    ModuleDependencyInfo info;
    info.source = source_path;

    // Parse the preprocessor output line by line
    // Looking for:
    //   export module ModuleName;
    //   export module ModuleName:PartitionName;
    //   module ModuleName;
    //   import ModuleName;
    //   import :PartitionName;

    // Regex patterns for module declarations
    // Note: preprocessor output preserves these directives
    static const std::regex export_module_regex(R"(^\s*export\s+module\s+([a-zA-Z_][a-zA-Z0-9_.]*)\s*(?::([a-zA-Z_][a-zA-Z0-9_]*))?\s*;)");
    static const std::regex module_impl_regex(R"(^\s*module\s+([a-zA-Z_][a-zA-Z0-9_.]*)\s*;)");
    static const std::regex import_regex(R"(^\s*import\s+([a-zA-Z_][a-zA-Z0-9_.]*)\s*;)");
    static const std::regex import_partition_regex(R"(^\s*import\s+:([a-zA-Z_][a-zA-Z0-9_]*)\s*;)");

    std::istringstream stream(output);
    std::string line;
    std::smatch match;

    while (std::getline(stream, line)) {
        // Check for export module declaration
        if (std::regex_search(line, match, export_module_regex)) {
            info.provides = match[1].str();
            if (match[2].matched) {
                info.is_module_partition = true;
                info.partition_name = match[2].str();
                // Full partition name is ModuleName:PartitionName
                info.provides = info.provides + ":" + info.partition_name;
            }
            continue;
        }

        // Check for module implementation unit (module ModuleName;)
        if (std::regex_search(line, match, module_impl_regex)) {
            // Implementation unit doesn't provide the module, it just implements it
            // But it implicitly imports the module interface
            std::string module_name = match[1].str();
            // Add to imports if not already there and not the same as provides
            if (info.provides != module_name &&
                std::find(info.imports.begin(), info.imports.end(), module_name) == info.imports.end()) {
                info.imports.push_back(module_name);
            }
            continue;
        }

        // Check for import declaration
        if (std::regex_search(line, match, import_regex)) {
            std::string module_name = match[1].str();
            // Skip standard library module (we'll handle these separately)
            if (module_name == "std" || module_name.starts_with("std.")) {
                // Still add to imports so we know about the dependency
            }
            if (std::find(info.imports.begin(), info.imports.end(), module_name) == info.imports.end()) {
                info.imports.push_back(module_name);
            }
            continue;
        }

        // Check for partition import (import :PartitionName;)
        if (std::regex_search(line, match, import_partition_regex)) {
            // Partition imports are relative to current module
            // We store them with : prefix to indicate they're partitions
            std::string partition = ":" + match[1].str();
            if (std::find(info.imports.begin(), info.imports.end(), partition) == info.imports.end()) {
                info.imports.push_back(partition);
            }
            continue;
        }
    }

    return info;
}

std::string generate_module_mapper_content(const std::vector<ModuleMapEntry>& entries) {
    // GCC module mapper format (one entry per line):
    // ModuleName BMIPath
    // The mapper is used with -fmodule-mapper=<file>
    std::ostringstream oss;
    for (const auto& entry : entries) {
        oss << entry.module_name << " " << entry.bmi_path << "\n";
    }
    return oss.str();
}

std::expected<std::vector<ModuleMapEntry>, std::string> parse_module_mapper_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return std::unexpected("Failed to open module mapper file: " + path);
    }

    std::vector<ModuleMapEntry> entries;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        ModuleMapEntry entry;
        if (iss >> entry.module_name >> entry.bmi_path) {
            entries.push_back(entry);
        }
    }

    return entries;
}

std::string get_bmi_path(const std::string& binary_dir, const std::string& module_name) {
    // Replace : with - for partition names (ModuleName:Partition -> ModuleName-Partition.gcm)
    std::string safe_name = module_name;
    std::replace(safe_name.begin(), safe_name.end(), ':', '-');

    std::filesystem::path path = std::filesystem::path(binary_dir) / "bmis" / (safe_name + ".gcm");
    return path.lexically_normal().string();
}

std::string get_ddi_path(const std::string& binary_dir, const std::string& source_path) {
    std::filesystem::path src(source_path);
    std::filesystem::path ddi_suffix;

    if (src.is_absolute()) {
        ddi_suffix = src.filename();
    } else {
        ddi_suffix = src;
    }

    std::filesystem::path ddi = std::filesystem::path(binary_dir) / "ddi" / ddi_suffix;
    ddi += ".ddi";
    return ddi.lexically_normal().string();
}

} // namespace dmake
