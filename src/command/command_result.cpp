#include "command/command_result.h"

#include <system_error>

namespace chunkmap {

ProjectKey make_project_key(const std::filesystem::path& workspace,
                            const std::string& project_name) {
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(workspace, error);
    if (error) {
        error.clear();
        normalized = std::filesystem::absolute(workspace, error);
        if (error) normalized = workspace;
    }
    return {normalized.lexically_normal(), project_name};
}

}  // namespace chunkmap
