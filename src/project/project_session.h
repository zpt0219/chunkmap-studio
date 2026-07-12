#pragma once

#include "model/project_document.h"
#include "project/project_service.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace chunkmap {

class ProjectSession {
public:
    Result<ProjectDocument*> open(const std::filesystem::path& workspace,
                                  const std::string& project_name,
                                  bool reload = false);
    Result<ProjectDocument*> create(const std::filesystem::path& workspace,
                                    const CreateProjectRequest& request);
    ProjectService& service(const std::filesystem::path& workspace);
    const ProjectDocument* current() const { return document_ ? &*document_ : nullptr; }

private:
    bool matches(const std::filesystem::path& workspace,
                 const std::string& project_name) const;

    std::filesystem::path workspace_;
    std::unique_ptr<ProjectService> service_;
    std::optional<ProjectDocument> document_;
};

}  // namespace chunkmap
