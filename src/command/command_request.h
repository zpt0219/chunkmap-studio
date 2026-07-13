#pragma once

#include "model/chunk_coord.h"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

namespace chunkmap {

enum class CommandType {
    ProjectCreate,
    ProjectOpen,
    ProjectCurrent,
    ProjectGridSet,
    ProjectStatus,
    ProjectValidate,
    ConceptContext,
    ConceptSliceExport,
    PromptsImport,
    PromptShow,
    PromptSet,
    GlobalPromptShow,
    GlobalPromptSet,
    ChunkImport,
    ChunkContext,
    ChunkWrite,
    ChunkShow,
    ChunkRemove,
    SeamInspect,
    MapExport,
};

struct ProjectCreatePayload {
    std::string name;
    std::filesystem::path concept_image;
    int columns = 0;
    int rows = 0;
    double horizontal_overlap_ratio = 0.15;
    double vertical_overlap_ratio = 0.15;
};

struct ProjectGridSetPayload {
    int columns = 0;
    int rows = 0;
};

struct CoordPayload {
    ChunkCoord coord;
};

struct PromptSetPayload {
    ChunkCoord coord;
    std::string text;
};

struct GlobalPromptSetPayload {
    std::string text;
};

struct PathPayload {
    std::filesystem::path path;
};

struct ChunkImagePayload {
    ChunkCoord coord;
    std::filesystem::path image;
};

enum class CommandSeamDirection {
    Right,
    Bottom,
};

struct SeamInspectPayload {
    ChunkCoord coord;
    CommandSeamDirection direction = CommandSeamDirection::Right;
};

struct MapExportPayload {
    std::filesystem::path output;
    bool overwrite = false;
};

struct ConceptSliceExportPayload {
    ChunkCoord coord;
    std::filesystem::path output;
    bool overwrite = false;
};

using CommandPayload = std::variant<
    std::monostate,
    ProjectCreatePayload,
    ProjectGridSetPayload,
    CoordPayload,
    PromptSetPayload,
    GlobalPromptSetPayload,
    PathPayload,
    ChunkImagePayload,
    SeamInspectPayload,
    MapExportPayload,
    ConceptSliceExportPayload>;

struct CommandRequest {
    std::string request_id;
    CommandType type = CommandType::ProjectStatus;
    std::filesystem::path workspace;
    std::optional<std::string> project_name;
    CommandPayload payload;
};

std::string command_name(CommandType type);

}  // namespace chunkmap
