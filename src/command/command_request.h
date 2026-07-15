#pragma once

#include "model/layout_state.h"

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
    ChunkAlignmentPreview,
    ChunkShiftApply,
    ChunkShow,
    ChunkRemove,
    SeamInspect,
    SeamSet,
    SeamReset,
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

struct ChunkAlignmentPayload {
    ChunkCoord coord;
    int offset_x = 0;
    int offset_y = 0;
    bool automatic = false;
};

enum class CommandSeamDirection {
    Right,
    Bottom,
};

struct SeamInspectPayload {
    ChunkCoord coord;
    CommandSeamDirection direction = CommandSeamDirection::Right;
};

struct SeamSetPayload {
    SeamDefinition seam;
};

struct SeamResetPayload {
    SeamKey key;
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
    ChunkAlignmentPayload,
    SeamInspectPayload,
    SeamSetPayload,
    SeamResetPayload,
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
