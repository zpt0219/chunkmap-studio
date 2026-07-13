#pragma once

#include "desktop_command_host.h"
#include "gl_texture.h"
#include "model/project.h"

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chunkmap_desktop {

class App {
public:
    App(std::filesystem::path workspace,
        std::optional<std::string> initial_project = std::nullopt);
    ~App();

    void draw();

private:
    void draw_dockspace();
    void draw_toolbar();
    void draw_map();
    void draw_inspector();
    void draw_log_panel();
    void draw_chunk_tab();
    void draw_prompt_tab();
    void draw_seam_tab();
    void draw_new_project_modal();
    void draw_project_settings_modal();

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
    void export_generation_context();
    void refresh_seam();
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

    float zoom_ = 1.0F;
    ImVec2 pan_ = ImVec2(0.0F, 0.0F);
    ImVec2 last_canvas_size_ = ImVec2(1.0F, 1.0F);
    bool fit_requested_ = true;
    bool show_grid_ = true;
    bool show_coordinates_ = true;
    bool show_seams_ = true;
    bool layout_initialized_ = false;

    bool show_new_project_ = false;
    bool show_project_settings_ = false;
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

    std::string status_message_;
    std::string error_message_;
    std::optional<std::string> pending_import_request_id_;

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
};

}  // namespace chunkmap_desktop
