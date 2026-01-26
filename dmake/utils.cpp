#include "utils.hpp"
#include "inner/blake2b.h"
#include <type_traits>

dmake::Hash256 dmake::blake2b(const void *data, size_t len, const void* key, size_t keylen)
{
    Hash256 hash;
    static_assert(sizeof(hash) == 32, "Hash256 must be 32 bytes");
    static_assert(std::is_standard_layout<Hash256>::value, "Hash256 must be a standard layout type");
    dmake_blake2b(&hash, sizeof(hash), data, len, key, keylen);
    return hash;
}

std::string dmake::Hash256::to_string() const
{
    std::string result;
    result.reserve(2 * sizeof(bytes));
    for (unsigned char byte : bytes)
    {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 0xf];
    }
    return result;
}

dmake::CommandResult dmake::run_command(const std::string& command, const std::string& working_dir)
{
    std::string prev_dir;
    if (!working_dir.empty()) {
        char* cwd = getcwd(nullptr, 0);
        if (cwd) {
            prev_dir = cwd;
            free(cwd);
        }
        if (chdir(working_dir.c_str()) != 0) {
            return {-1, "Failed to change directory to " + working_dir};
        }
    }

    std::string full_command = command + " 2>&1";
    FILE* pipe = popen(full_command.c_str(), "r");
    if (!pipe) {
        if (!prev_dir.empty()) chdir(prev_dir.c_str());
        return {-1, "Failed to execute command"};
    }

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }

    int status = pclose(pipe);
    if (!prev_dir.empty()) chdir(prev_dir.c_str());
    
    // WEXITSTATUS is only valid if WIFEXITED is true. 
    // On many systems pclose returns the raw status.
    int exit_code = (status == -1) ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : status);

    return {exit_code, output};
}
