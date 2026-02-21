#pragma once
#include <string>
#include <vector>
#include <expected>

namespace kiln {

class Interpreter;

// Download a file from a URL, optionally verifying a hash.
// Returns error string on failure.
std::expected<void, std::string> download_url(
    const std::string& url,
    const std::string& dest_file,
    const std::string& hash_algo = "",   // "SHA256", "MD5", or empty
    const std::string& hash_value = ""
);

// Extract an archive to a destination directory using libarchive.
std::expected<void, std::string> extract_archive(
    const std::string& archive_path,
    const std::string& dest_dir
);

// Clone a git repository.
std::expected<void, std::string> git_clone(
    const std::string& repo_url,
    const std::string& dest_dir,
    const std::string& tag = "",
    bool shallow = false
);

// Replace <SOURCE_DIR>, <BINARY_DIR>, <INSTALL_DIR> tokens in command strings.
void replace_tokens(std::vector<std::string>& command,
                    const std::vector<std::pair<std::string, std::string>>& replacements);

// Run a list of commands sequentially, aborting on failure.
std::expected<void, std::string> run_steps(
    const std::vector<std::vector<std::string>>& commands,
    const std::string& working_dir = ""
);

} // namespace kiln
