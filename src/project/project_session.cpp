#include "project/project_session.h"

namespace chunkmap {

namespace {

std::filesystem::path normalized(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal();
}

}  // namespace

ProjectService& ProjectSession::service(const std::filesystem::path& workspace) {
    const auto requested = normalized(workspace);
    if (!service_ || workspace_ != requested) {
        workspace_ = requested;
        service_ = std::make_unique<ProjectService>(workspace_);
        document_.reset();
    }
    return *service_;
}

bool ProjectSession::matches(const std::filesystem::path& workspace,
                             const std::string& project_name) const {
    return document_ && workspace_ == normalized(workspace) &&
           document_->config().name == project_name;
}

Result<ProjectDocument*> ProjectSession::open(const std::filesystem::path& workspace,
                                              const std::string& project_name,
                                              bool reload) {
    auto& project_service = service(workspace);
    if (!reload && matches(workspace, project_name)) {
        return Result<ProjectDocument*>::success(&*document_);
    }
    auto project = project_service.open_project(project_name);
    if (!project) return Result<ProjectDocument*>::failure(project.error().code, project.error().message);
    auto loaded = ProjectDocument::load(project.take_value());
    if (!loaded) return Result<ProjectDocument*>::failure(loaded.error().code, loaded.error().message);
    document_ = loaded.take_value();
    return Result<ProjectDocument*>::success(&*document_);
}

Result<ProjectDocument*> ProjectSession::create(const std::filesystem::path& workspace,
                                                const CreateProjectRequest& request) {
    auto& project_service = service(workspace);
    auto project = project_service.create_project(request);
    if (!project) return Result<ProjectDocument*>::failure(project.error().code, project.error().message);
    auto loaded = ProjectDocument::load(project.take_value());
    if (!loaded) return Result<ProjectDocument*>::failure(loaded.error().code, loaded.error().message);
    document_ = loaded.take_value();
    return Result<ProjectDocument*>::success(&*document_);
}

}  // namespace chunkmap
