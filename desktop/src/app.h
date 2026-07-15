#pragma once

#include "concept_comparison_state.h"
#include "desktop_command_host.h"
#include "gl_texture.h"
#include "map_zoom_state.h"
#include "model/project.h"
#include "image/layout_renderer.h"

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace chunkmap_desktop {

class App {
public:
    App(std::filesystem::path workspace,
        std::optional<std::string> initial_project = std::nullopt);
    ~App();

    void draw();
    bool exit_requested() const { return exit_requested_; }

private:
    void draw_main_menu_bar();
    void draw_dockspace();
    void draw_map_controls();
    void draw_map();
    void draw_inspector();
    void draw_log_panel();
    void draw_chunk_tab();
    void draw_prompt_tab();
    void draw_seam_tab();
    void draw_seam_editor();
    void draw_new_project_modal();
    void draw_project_settings_modal();
    void draw_change_grid_modal();
    void draw_export_progress_modal();

    void new_project_dialog();
    void open_project_dialog();
    void open_project(const std::filesystem::path& workspace, const std::string& name);
    void reload_project();
    void select_chunk(chunkmap::ChunkCoord coord);
    void flush_prompt();
    void flush_global_prompt();
    void load_global_prompt();
    void fit_map(const ImVec2& available);
    void focus_selected(const ImVec2& available);
    void import_image();
    void remove_selected_chunk();
    void request_alignment_preview(bool automatic);
    void reset_alignment_preview();
    void apply_alignment_shift();
    void preview_alignment_candidate();
    void export_full_map();
    void export_concept_slice();
    void export_generation_context();
    void refresh_seam();
    void open_seam_editor();
    void refresh_seam_editor_preview();
    void rebuild_seam_textures(
        std::optional<chunkmap::ChunkCoord> placement_preview = std::nullopt,
        chunkmap::ChunkPlacement preview_value = {});
    void poll_commands();
    void append_log(std::string source,
                    std::string operation,
                    bool error,
                    double duration_ms,
                    std::string detail);
    void apply_project_snapshot(chunkmap::Project project, bool reset_selection);
    chunkmap::CommandRequest make_request(chunkmap::CommandType type) const;

    bool chunk_ready(chunkmap::ChunkCoord coord) const;
    int ready_neighbor_count(chunkmap::ChunkCoord coord) const;
    std::string ready_neighbor_text(chunkmap::ChunkCoord coord) const;

    std::filesystem::path workspace_;
    DesktopCommandHost command_host_;
    std::optional<chunkmap::Project> project_;
    std::optional<chunkmap::ChunkCoord> selected_;
    TextureCache textures_;

    std::string prompt_buffer_;
    bool prompt_dirty_ = false;
    double prompt_last_edit_ = 0.0;

    std::string global_prompt_buffer_;
    bool global_prompt_dirty_ = false;
    double global_prompt_last_edit_ = 0.0;

    MapZoomState map_view_;
    ImVec2 last_canvas_size_ = ImVec2(1.0F, 1.0F);
    bool fit_requested_ = true;
    bool show_overlays_ = true;
    ConceptComparisonState concept_comparison_;
    bool show_map_controls_ = true;
    bool show_inspector_ = true;
    bool show_log_ = true;
    bool layout_initialized_ = false;
    bool reset_layout_requested_ = false;
    bool exit_requested_ = false;

    bool show_new_project_ = false;
    bool show_project_settings_ = false;
    bool show_change_grid_ = false;
    int change_grid_columns_ = 1;
    int change_grid_rows_ = 1;
    std::string change_grid_error_;
    std::string new_project_name_;
    std::string new_concept_path_;
    int new_columns_ = 3;
    int new_rows_ = 3;
    float new_overlap_x_ = 0.15F;
    float new_overlap_y_ = 0.15F;

    int seam_direction_ = 0;
    int seam_mode_ = 0;
    std::optional<chunkmap::SeamAnalysis> seam_analysis_;
    GlTexture seam_overlap_texture_;
    GlTexture seam_difference_texture_;
    chunkmap::ChunkCoord seam_first_{};
    chunkmap::ChunkCoord seam_second_{};
    chunkmap::SeamDirection seam_core_direction_ = chunkmap::SeamDirection::Right;
    std::unordered_map<chunkmap::SeamKey, std::unique_ptr<GlTexture>,
                       chunkmap::SeamKeyHash> seam_textures_;
    std::unordered_map<chunkmap::ChunkCoord, chunkmap::ImageBuffer,
                       chunkmap::ChunkCoordHash> seam_preview_sources_;
    bool show_seam_editor_ = false;
    chunkmap::SeamDefinition seam_editor_value_;
    std::optional<chunkmap::ImageBuffer> seam_editor_first_image_;
    std::optional<chunkmap::ImageBuffer> seam_editor_second_image_;
    GlTexture seam_editor_texture_;
    int seam_editor_active_point_ = -1;
    bool seam_editor_dirty_ = false;

    int alignment_offset_x_ = 0;
    int alignment_offset_y_ = 0;
    std::optional<chunkmap::ChunkCoord> alignment_preview_coord_;
    GlTexture alignment_preview_texture_;
    struct AlignmentCandidateSummary {
        int offset_x = 0;
        int offset_y = 0;
        double score = 0.0;
        double relative_improvement = 0.0;
        bool accepted = false;
    };
    struct AlignmentComparisonSummary {
        AlignmentCandidateSummary low_resolution;
        AlignmentCandidateSummary projection;
        std::string selected_method;
        int selected_offset_x = 0;
        int selected_offset_y = 0;
    };
    std::optional<AlignmentComparisonSummary> alignment_comparison_;
    int alignment_result_choice_ = 0;

    std::string status_message_;
    std::string error_message_;
    std::optional<std::string> pending_import_request_id_;
    std::optional<std::string> pending_remove_request_id_;
    std::optional<std::string> pending_alignment_preview_request_id_;
    std::optional<std::string> pending_alignment_apply_request_id_;
    std::optional<std::string> pending_export_request_id_;
    std::optional<std::string> pending_concept_export_request_id_;
    std::optional<std::filesystem::path> last_export_path_;
    std::size_t export_progress_completed_ = 0;
    std::size_t export_progress_total_ = 0;
    std::string export_progress_message_;

    struct LogEntry {
        std::string time;
        std::string source;
        std::string operation;
        bool error = false;
        double duration_ms = 0.0;
        std::string detail;
    };
    std::vector<LogEntry> log_entries_;
    bool log_auto_scroll_ = true;
    std::optional<float> log_scroll_y_;
};

}  // namespace chunkmap_desktop
