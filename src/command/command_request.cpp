#include "command/command_request.h"

namespace chunkmap {

std::string command_name(CommandType type) {
    switch (type) {
    case CommandType::ProjectCreate: return "project init";
    case CommandType::ProjectOpen: return "project open";
    case CommandType::ProjectCurrent: return "project current";
    case CommandType::ProjectGridSet: return "project grid";
    case CommandType::ProjectStatus: return "project status";
    case CommandType::ProjectValidate: return "project validate";
    case CommandType::ConceptContext: return "concept context";
    case CommandType::ConceptSliceExport: return "concept slice export";
    case CommandType::PromptsImport: return "prompts import";
    case CommandType::PromptShow: return "prompt show";
    case CommandType::PromptSet: return "prompt set";
    case CommandType::GlobalPromptShow: return "global-prompt show";
    case CommandType::GlobalPromptSet: return "global-prompt set";
    case CommandType::ChunkImport: return "chunk import";
    case CommandType::ChunkContext: return "chunk context";
    case CommandType::ChunkWrite: return "chunk write";
    case CommandType::ChunkAlignmentPreview: return "chunk alignment preview";
    case CommandType::ChunkShiftApply: return "chunk shift apply";
    case CommandType::ChunkShow: return "chunk show";
    case CommandType::ChunkRemove: return "chunk remove";
    case CommandType::SeamInspect: return "seam inspect";
    case CommandType::SeamSet: return "seam set";
    case CommandType::SeamReset: return "seam reset";
    case CommandType::MapExport: return "map export";
    }
    return "unknown";
}

}  // namespace chunkmap
