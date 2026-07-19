#include "app.h"

#include "image/image_buffer.h"
#include "image/image_registration.h"
#include "ui/map_geometry.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <tinyfiledialogs.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace chunkmap_desktop {

namespace {

constexpr const char* kDockspaceName = "ChunkMapDockspace";
constexpr ImU32 kCanvas = IM_COL32(24, 27, 30, 255);
constexpr ImU32 kGrid = IM_COL32(111, 122, 126, 150);
constexpr ImU32 kSelection = IM_COL32(76, 159, 255, 255);
constexpr ImU32 kEmpty = IM_COL32(17, 20, 22, 150);
constexpr double kPromptAutosaveDelaySeconds = 60.0;
constexpr float kLogWheelLines = 3.0F;

std::string next_desktop_request_id() {
    static std::atomic<unsigned long long> sequence{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return "desktop-" + std::to_string(ticks) + "-" +
           std::to_string(sequence.fetch_add(1));
}

ImTextureID texture_id(const GlTexture& texture) {
    return static_cast<ImTextureID>(static_cast<std::intptr_t>(texture.id()));
}

std::string coord_label(chunkmap::ChunkCoord coord) {
    return "(" + std::to_string(coord.x) + ", " + std::to_string(coord.y) + ")";
}

bool path_is_regular_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

bool same_seam(const chunkmap::SeamDefinition& first,
               const chunkmap::SeamDefinition& second) {
    if (!(first.key == second.key) || first.feather_width != second.feather_width ||
        first.points.size() != second.points.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.points.size(); ++index) {
        if (first.points[index].along != second.points[index].along ||
            first.points[index].across != second.points[index].across) {
            return false;
        }
    }
    return true;
}

void reveal_file(const std::filesystem::path& path) {
#if defined(__APPLE__)
    const pid_t process = fork();
    if (process == 0) {
        execlp("open", "open", "-R", path.string().c_str(), nullptr);
        _exit(127);
    }
    if (process > 0) waitpid(process, nullptr, 0);
#elif defined(_WIN32)
    const std::wstring parameters = L"/select,\"" + path.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
#else
    const pid_t process = fork();
    if (process == 0) {
        execlp("xdg-open", "xdg-open", path.parent_path().string().c_str(), nullptr);
        _exit(127);
    }
    if (process > 0) waitpid(process, nullptr, 0);
#endif
}

}  // namespace

App::App(std::filesystem::path workspace, std::optional<std::string> initial_project)
    : workspace_(std::filesystem::absolute(std::move(workspace)).lexically_normal()) {
    append_log("Desktop", "application start", false, 0.0, workspace_.string());
    append_log("Renderer", "decoder pool", false, 0.0,
               std::to_string(textures_.decoder_worker_count()) + " workers");
    if (initial_project) open_project(workspace_, *initial_project);
}

App::~App() {
    flush_prompt();
    flush_global_prompt();
    textures_.clear();
}

void App::draw() {
    poll_commands();
    if (!show_inspector_ || ImGui::GetIO().AppFocusLost) {
        concept_comparison_.cancel();
        reset_alignment_preview();
    }
    draw_main_menu_bar();
    draw_dockspace();
    if (show_map_controls_) draw_map_controls();
    draw_map();
    if (show_inspector_) draw_inspector();
    if (show_log_) draw_log_panel();
    draw_new_project_modal();
    draw_project_settings_modal();
    draw_change_grid_modal();
    draw_export_progress_modal();
    if (prompt_dirty_ &&
        ImGui::GetTime() - prompt_last_edit_ >= kPromptAutosaveDelaySeconds) {
        flush_prompt();
    }
    if (global_prompt_dirty_ &&
        ImGui::GetTime() - global_prompt_last_edit_ >= kPromptAutosaveDelaySeconds) {
        flush_global_prompt();
    }
}

void App::draw_main_menu_bar() {
#if defined(__APPLE__)
    constexpr const char* kNewShortcut = "Cmd+N";
    constexpr const char* kOpenShortcut = "Cmd+O";
    constexpr const char* kReloadShortcut = "Cmd+R";
    constexpr const char* kExportShortcut = "Cmd+Shift+E";
    constexpr const char* kQuitShortcut = "Cmd+Q";
#else
    constexpr const char* kNewShortcut = "Ctrl+N";
    constexpr const char* kOpenShortcut = "Ctrl+O";
    constexpr const char* kReloadShortcut = "Ctrl+R";
    constexpr const char* kExportShortcut = "Ctrl+Shift+E";
    constexpr const char* kQuitShortcut = "Ctrl+Q";
#endif

    bool new_project = false;
    bool open_project = false;
    bool reload = false;
    bool export_map = false;
    bool project_settings = false;
    bool change_grid = false;
    bool fit_map = false;
    bool focus_selection = false;
    bool reset_layout = false;
    bool quit = false;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            new_project = ImGui::MenuItem("New Project...", kNewShortcut);
            open_project = ImGui::MenuItem("Open Project...", kOpenShortcut);
            ImGui::Separator();
            reload = ImGui::MenuItem("Reload Project", kReloadShortcut, false, project_.has_value());
            ImGui::Separator();
            export_map = ImGui::MenuItem(
                "Export Full Map...", kExportShortcut, false,
                project_.has_value() && !pending_export_request_id_);
            ImGui::Separator();
            quit = ImGui::MenuItem("Quit", kQuitShortcut);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project", project_.has_value())) {
            project_settings = ImGui::MenuItem("Project Settings...");
            change_grid = ImGui::MenuItem("Change Grid...");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            fit_map = ImGui::MenuItem("Fit Map", "Home", false, project_.has_value());
            focus_selection = ImGui::MenuItem(
                "Focus Selected", "F", false, project_.has_value() && selected_.has_value());
            ImGui::Separator();
            ImGui::BeginDisabled(!project_);
            ImGui::MenuItem("Overlays", nullptr, &show_overlays_);
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::BeginMenu("Panels")) {
                ImGui::MenuItem("Map Controls", nullptr, &show_map_controls_);
                ImGui::MenuItem("Inspector", nullptr, &show_inspector_);
                ImGui::MenuItem("Log", nullptr, &show_log_);
                ImGui::EndMenu();
            }
            reset_layout = ImGui::MenuItem("Reset Layout");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    const bool popup_open = ImGui::IsPopupOpen(
        nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!popup_open) {
        new_project |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, global);
        open_project |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, global);
        if (project_) reload |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, global);
        if (project_ && !pending_export_request_id_) {
            export_map |= ImGui::Shortcut(
                ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E, global);
        }
    }
    quit |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, global);

    if (new_project) new_project_dialog();
    if (open_project) open_project_dialog();
    if (reload) reload_project();
    if (export_map) export_full_map();
    if (project_settings) show_project_settings_ = true;
    if (change_grid && project_) {
        change_grid_columns_ = project_->config.columns;
        change_grid_rows_ = project_->config.rows;
        change_grid_error_.clear();
        show_change_grid_ = true;
    }
    if (fit_map) fit_requested_ = true;
    if (focus_selection) focus_selected(last_canvas_size_);
    if (reset_layout) {
        show_map_controls_ = true;
        show_inspector_ = true;
        show_log_ = true;
        reset_layout_requested_ = true;
    }
    if (quit) exit_requested_ = true;
}

void App::draw_dockspace() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::Begin("##ChunkMapDockspaceHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);
    const ImGuiID dockspace_id = ImGui::GetID(kDockspaceName);
    const bool needs_default_layout = reset_layout_requested_ ||
        (!layout_initialized_ && ImGui::DockBuilderGetNode(dockspace_id) == nullptr);
    ImGui::DockSpace(dockspace_id, ImVec2(0.0F, 0.0F));
    if (needs_default_layout) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
        ImGuiID body = dockspace_id;
        ImGuiID map_controls = ImGui::DockBuilderSplitNode(
            body, ImGuiDir_Up, 0.075F, nullptr, &body);
        if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(map_controls)) {
            node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        }
        ImGuiID log = ImGui::DockBuilderSplitNode(
            body, ImGuiDir_Down, 0.26F, nullptr, &body);
        ImGuiID inspector = ImGui::DockBuilderSplitNode(
            body, ImGuiDir_Right, 0.29F, nullptr, &body);
        ImGui::DockBuilderDockWindow("Map Controls", map_controls);
        ImGui::DockBuilderDockWindow("Map", body);
        ImGui::DockBuilderDockWindow("Inspector", inspector);
        ImGui::DockBuilderDockWindow("Log", log);
        ImGui::DockBuilderFinish(dockspace_id);
    }
    layout_initialized_ = true;
    reset_layout_requested_ = false;
    ImGui::End();
}

void App::draw_map_controls() {
    ImGui::Begin("Map Controls", &show_map_controls_,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginDisabled(!project_);
    if (ImGui::Button("Reset Scale")) {
        map_view_.reset();
        fit_requested_ = false;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reset scale to 1:1 and pan to the map's top-left corner.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit Map")) fit_requested_ = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Scale and center the whole map to fit the canvas.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Scale: %.3fx", map_view_.scale());
    ImGui::SameLine();
    ImGui::Checkbox("Overlays", &show_overlays_);
    ImGui::EndDisabled();

    if (project_) {
        int ready = 0;
        for (int y = 0; y < project_->config.rows; ++y) {
            for (int x = 0; x < project_->config.columns; ++x) {
                ready += chunk_ready({x, y}) ? 1 : 0;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s  |  %dx%d  |  %d/%d Ready",
                            project_->config.name.c_str(),
                            project_->config.columns, project_->config.rows, ready,
                            project_->config.columns * project_->config.rows);
    }
    if (!error_message_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95F, 0.42F, 0.38F, 1.0F), "%s", error_message_.c_str());
    } else if (!status_message_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status_message_.c_str());
    }
    if (last_export_path_) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reveal Export")) reveal_file(*last_export_path_);
    }
    ImGui::End();
}

void App::draw_map() {
    ImGui::Begin("Map", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!project_) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 text_size = ImGui::CalcTextSize("Open a map project to begin review.");
        const float button_width = 140.0F;
        const float button_height = 34.0F;
        const float spacing = 12.0F;

        // Gather recent projects dynamically by scanning output/ folder
        struct RecentProject {
            std::string name;
            std::filesystem::file_time_type last_modified;
        };
        std::vector<RecentProject> recent_projects;
        std::error_code ec;
        auto output_dir = workspace_ / "output";
        if (std::filesystem::exists(output_dir, ec) && std::filesystem::is_directory(output_dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(output_dir, ec)) {
                if (entry.is_directory()) {
                    auto project_json = entry.path() / "project.json";
                    if (std::filesystem::exists(project_json)) {
                        auto write_time = std::filesystem::last_write_time(project_json, ec);
                        if (!ec) {
                            recent_projects.push_back({entry.path().filename().string(), write_time});
                        }
                    }
                }
            }
            std::sort(recent_projects.begin(), recent_projects.end(), [](const RecentProject& a, const RecentProject& b) {
                return a.last_modified > b.last_modified;
            });
        }

        const float welcome_height = text_size.y + spacing + button_height + spacing + button_height;
        float list_height = 0.0F;
        float recent_section_height = 0.0F;
        if (!recent_projects.empty()) {
            list_height = std::min(180.0F, static_cast<float>(recent_projects.size()) * 54.0F);
            recent_section_height = 24.0F + 20.0F + spacing + list_height;
        }

        const float total_height = welcome_height + recent_section_height;
        const float start_y = std::max(18.0F, (available.y - total_height) * 0.42F);

        // Center the text label
        ImGui::SetCursorPos(ImVec2(
            std::max(18.0F, (available.x - text_size.x) * 0.5F),
            start_y));
        ImGui::TextUnformatted("Open a map project to begin review.");

        // Center the Open Project button below the text label
        ImGui::SetCursorPos(ImVec2(
            std::max(18.0F, (available.x - button_width) * 0.5F),
            start_y + text_size.y + spacing));
        if (ImGui::Button("Open Project", ImVec2(button_width, button_height))) {
            open_project_dialog();
        }

        // Center the New Project button below the Open Project button
        ImGui::SetCursorPos(ImVec2(
            std::max(18.0F, (available.x - button_width) * 0.5F),
            start_y + text_size.y + spacing + button_height + spacing));
        if (ImGui::Button("New Project", ImVec2(button_width, button_height))) {
            new_project_dialog();
        }

        // Render Recent Projects list if found
        if (!recent_projects.empty()) {
            const float recent_y = start_y + welcome_height + 24.0F;
            const float section_width = 320.0F;
            const float section_height = 20.0F + spacing + list_height;

            ImGui::SetCursorPos(ImVec2(
                std::max(18.0F, (available.x - section_width) * 0.5F),
                recent_y));

            if (ImGui::BeginChild("##recent_projects_section", ImVec2(section_width, section_height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar)) {
                ImGui::SeparatorText("Recent Projects");

                ImGui::SetCursorPos(ImVec2(0.0F, 20.0F + spacing));

                if (ImGui::BeginChild("##recent_projects_list", ImVec2(section_width, list_height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
                    for (const auto& proj : recent_projects) {
                        ImGui::PushID(proj.name.c_str());

                        const ImVec2 card_size = ImVec2(ImGui::GetContentRegionAvail().x, 48.0F);
                        const ImVec2 start_pos = ImGui::GetCursorPos();
                        const ImVec2 screen_pos = ImGui::GetCursorScreenPos();

                        bool clicked = ImGui::Selectable("##card", false, ImGuiSelectableFlags_AllowOverlap, card_size);

                        ImDrawList* draw_list = ImGui::GetWindowDrawList();
                        ImU32 border_color = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_HeaderActive : ImGuiCol_Border);
                        ImU32 bg_color = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg);
                        draw_list->AddRectFilled(screen_pos, ImVec2(screen_pos.x + card_size.x, screen_pos.y + card_size.y), bg_color, 4.0F);
                        draw_list->AddRect(screen_pos, ImVec2(screen_pos.x + card_size.x, screen_pos.y + card_size.y), border_color, 4.0F);

                        // Render Project Name
                        ImGui::SetCursorPos(ImVec2(start_pos.x + 8.0F, start_pos.y + 4.0F));
                        ImGui::Text("%s", proj.name.c_str());

                        // Render Project Path
                        ImGui::SetCursorPos(ImVec2(start_pos.x + 8.0F, start_pos.y + 24.0F));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        std::string path_str = (workspace_ / "output" / proj.name).string();
                        ImGui::TextUnformatted(path_str.c_str());
                        ImGui::PopStyleColor();

                        ImGui::SetCursorPos(ImVec2(start_pos.x, start_pos.y + card_size.y + 6.0F));
                        ImGui::Dummy(ImVec2(0.0F, 0.0F));
                        ImGui::PopID();

                        if (clicked) {
                            open_project(workspace_, proj.name);
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndChild();
            }
        }

        ImGui::End();
        return;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 1.0F);
    available.y = std::max(available.y, 1.0F);
    last_canvas_size_ = available;
    ImGui::InvisibleButton("##map_canvas", available,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 origin = ImGui::GetItemRectMin();
    const ImVec2 end = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, end, true);
    draw->AddRectFilled(origin, end, kCanvas);

    int world_width = 0;
    int world_height = 0;
    int chunk_width = 0;
    int chunk_height = 0;
    int step_x = 0;
    int step_y = 0;
    bool detailed = project_->config.has_chunk_size();
    if (detailed) {
        auto geometry = chunkmap::map_geometry(project_->config);
        if (!geometry) {
            error_message_ = geometry.error().message;
            draw->PopClipRect();
            ImGui::End();
            return;
        }
        world_width = geometry.value().world_width;
        world_height = geometry.value().world_height;
        chunk_width = geometry.value().chunk_width;
        chunk_height = geometry.value().chunk_height;
        step_x = geometry.value().step_x;
        step_y = geometry.value().step_y;
    } else {
        GlTexture* concept = textures_.get(project_->paths.concept_source());
        world_width = concept ? concept->width() : project_->config.columns * 256;
        world_height = concept ? concept->height() : project_->config.rows * 256;
        chunk_width = std::max(1, world_width / project_->config.columns);
        chunk_height = std::max(1, world_height / project_->config.rows);
        step_x = chunk_width;
        step_y = chunk_height;
    }

    if (fit_requested_) {
        fit_map(available);
        fit_requested_ = false;
    }
    auto to_screen = [&](float x, float y) {
        return ImVec2(origin.x + (x - map_view_.pan_x()) * map_view_.scale(),
                      origin.y + (y - map_view_.pan_y()) * map_view_.scale());
    };
    std::optional<chunkmap::SeamKey> hovered_seam;
    if (hovered && detailed && show_overlays_ &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const auto world = map_view_.screen_to_world(mouse.x - origin.x, mouse.y - origin.y);
        auto hit = chunkmap::seam_at(project_->config, world.x, world.y);
        if (hit && chunk_ready(hit->first) && chunk_ready(chunkmap::seam_second(*hit))) {
            hovered_seam = *hit;
        }
    }

    const auto comparison_mode = concept_comparison_.visible_mode(
        ImGui::IsMouseDown(ImGuiMouseButton_Left));
    const bool compare_selected =
        comparison_mode == ConceptComparisonMode::SelectedChunk && selected_;
    const bool compare_full = comparison_mode == ConceptComparisonMode::FullMap;
    bool needs_concept_texture = !detailed || compare_selected || compare_full;
    if (detailed) {
        for (int y = 0; y < project_->config.rows && !needs_concept_texture; ++y) {
            for (int x = 0; x < project_->config.columns; ++x) {
                const chunkmap::ChunkCoord coord{x, y};
                if (!chunk_ready(coord)) {
                    needs_concept_texture = true;
                    break;
                }
            }
        }
    }
    GlTexture* concept_texture = needs_concept_texture
        ? textures_.get(project_->paths.concept_source()) : nullptr;
    auto draw_concept_region = [&](chunkmap::ChunkCoord coord, ImU32 tint) {
        const ImVec2 top_left = to_screen(
            static_cast<float>(coord.x * step_x), static_cast<float>(coord.y * step_y));
        const ImVec2 bottom_right = to_screen(
            static_cast<float>(coord.x * step_x + chunk_width),
            static_cast<float>(coord.y * step_y + chunk_height));
        if (concept_texture) {
            const ImVec2 uv0(
                static_cast<float>(coord.x) / project_->config.columns,
                static_cast<float>(coord.y) / project_->config.rows);
            const ImVec2 uv1(
                static_cast<float>(coord.x + 1) / project_->config.columns,
                static_cast<float>(coord.y + 1) / project_->config.rows);
            draw->AddImage(texture_id(*concept_texture), top_left, bottom_right, uv0, uv1, tint);
        } else {
            draw->AddRectFilled(top_left, bottom_right, kEmpty);
        }
    };

    if (compare_full && concept_texture) {
        draw->AddImage(texture_id(*concept_texture), to_screen(0, 0),
                       to_screen(static_cast<float>(world_width), static_cast<float>(world_height)));
    } else if (detailed) {
        for (int y = 0; y < project_->config.rows; ++y) {
            for (int x = 0; x < project_->config.columns; ++x) {
                const chunkmap::ChunkCoord coord{x, y};
                if (chunk_ready(coord)) continue;
                draw_concept_region(coord, IM_COL32(160, 178, 167, 92));
            }
        }
        for (int y = 0; y < project_->config.rows; ++y) {
            for (int x = 0; x < project_->config.columns; ++x) {
                const chunkmap::ChunkCoord coord{x, y};
                GlTexture* chunk = alignment_preview_coord_ == coord &&
                        alignment_preview_texture_.id() != 0
                    ? &alignment_preview_texture_
                    : textures_.get(project_->paths.chunk_image(coord));
                if (chunk) {
                    const bool transient = chunk == &alignment_preview_texture_;
                    const auto placement = project_->layout.placement(coord);
                    const ImVec2 uv0 = transient ? ImVec2(0.0F, 0.0F) : ImVec2(
                        -static_cast<float>(placement.offset_x) / chunk_width,
                        -static_cast<float>(placement.offset_y) / chunk_height);
                    const ImVec2 uv1 = transient ? ImVec2(1.0F, 1.0F) : ImVec2(
                        static_cast<float>(chunk_width - placement.offset_x) / chunk_width,
                        static_cast<float>(chunk_height - placement.offset_y) / chunk_height);
                    draw->AddImage(texture_id(*chunk),
                        to_screen(static_cast<float>(x * step_x), static_cast<float>(y * step_y)),
                        to_screen(static_cast<float>(x * step_x + chunk_width),
                                  static_cast<float>(y * step_y + chunk_height)), uv0, uv1);
                }
            }
        }
        for (const auto direction : {chunkmap::SeamDirection::Right,
                                     chunkmap::SeamDirection::Bottom}) {
            for (int y = 0; y < project_->config.rows; ++y) {
                for (int x = 0; x < project_->config.columns; ++x) {
                    const chunkmap::SeamKey key{{x, y}, direction};
                    const auto found = seam_textures_.find(key);
                    if (found == seam_textures_.end() || !found->second ||
                        found->second->id() == 0) continue;
                    const auto second = chunkmap::seam_second(key);
                    const float left = static_cast<float>(
                        direction == chunkmap::SeamDirection::Right
                            ? second.x * step_x : x * step_x);
                    const float top = static_cast<float>(
                        direction == chunkmap::SeamDirection::Bottom
                            ? second.y * step_y : y * step_y);
                    draw->AddImage(texture_id(*found->second), to_screen(left, top),
                        to_screen(left + found->second->width(),
                                  top + found->second->height()));
                }
            }
        }
        if (compare_selected && chunk_ready(*selected_) && concept_texture) {
            draw_concept_region(*selected_, IM_COL32_WHITE);
        }
    } else if (GlTexture* concept = textures_.get(project_->paths.concept_source())) {
        draw->AddImage(texture_id(*concept), to_screen(0, 0),
                       to_screen(static_cast<float>(world_width), static_cast<float>(world_height)),
                       ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 135));
    }

    for (int y = 0; y < project_->config.rows; ++y) {
        for (int x = 0; x < project_->config.columns; ++x) {
            const chunkmap::ChunkCoord coord{x, y};
            const ImVec2 top_left = to_screen(static_cast<float>(x * step_x), static_cast<float>(y * step_y));
            const ImVec2 bottom_right = to_screen(
                static_cast<float>(x * step_x + chunk_width),
                static_cast<float>(y * step_y + chunk_height));
            if (show_overlays_) draw->AddRect(top_left, bottom_right, kGrid, 0.0F, 0, 1.0F);
            if (show_overlays_) {
                const std::string label = coord_label(coord);
                draw->AddRectFilled(top_left, ImVec2(top_left.x + 55.0F, top_left.y + 22.0F),
                                    IM_COL32(10, 12, 14, 190));
                draw->AddText(ImVec2(top_left.x + 7.0F, top_left.y + 4.0F),
                              IM_COL32(225, 230, 230, 255), label.c_str());
            }
        }
    }
    if (show_overlays_ && detailed) {
        std::optional<chunkmap::SeamKey> emphasized_seam = hovered_seam;
        if (!emphasized_seam && seam_editor_value_) {
            emphasized_seam = seam_editor_value_->key;
        }
        for (int x = 1; x < project_->config.columns; ++x) {
            const float screen_x = to_screen(static_cast<float>(x * step_x), 0).x;
            draw->AddLine(ImVec2(screen_x, to_screen(0, 0).y),
                          ImVec2(screen_x, to_screen(0, static_cast<float>(world_height)).y),
                          IM_COL32(239, 180, 70, 130), 1.0F);
        }
        for (int y = 1; y < project_->config.rows; ++y) {
            const float screen_y = to_screen(0, static_cast<float>(y * step_y)).y;
            draw->AddLine(ImVec2(to_screen(0, 0).x, screen_y),
                          ImVec2(to_screen(static_cast<float>(world_width), 0).x, screen_y),
                          IM_COL32(239, 180, 70, 130), 1.0F);
        }
        if (emphasized_seam) {
            const auto key = *emphasized_seam;
            if (key.direction == chunkmap::SeamDirection::Right) {
                const float left = static_cast<float>((key.first.x + 1) * step_x);
                const float top = static_cast<float>(key.first.y * step_y);
                const ImVec2 band_min = to_screen(left, top);
                const ImVec2 band_max = to_screen(
                    left + chunk_width - step_x, top + chunk_height);
                draw->AddRectFilled(band_min, band_max, IM_COL32(255, 198, 75, 38));
                draw->AddRect(band_min, band_max, IM_COL32(255, 198, 75, 255), 0.0F, 0, 2.0F);
            } else {
                const float left = static_cast<float>(key.first.x * step_x);
                const float top = static_cast<float>((key.first.y + 1) * step_y);
                const ImVec2 band_min = to_screen(left, top);
                const ImVec2 band_max = to_screen(
                    left + chunk_width, top + chunk_height - step_y);
                draw->AddRectFilled(band_min, band_max, IM_COL32(255, 198, 75, 38));
                draw->AddRect(band_min, band_max, IM_COL32(255, 198, 75, 255), 0.0F, 0, 2.0F);
            }
            if (hovered_seam) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }
    if (selected_) {
        const ImVec2 top_left = to_screen(
            static_cast<float>(selected_->x * step_x), static_cast<float>(selected_->y * step_y));
        const ImVec2 bottom_right = to_screen(
            static_cast<float>(selected_->x * step_x + chunk_width),
            static_cast<float>(selected_->y * step_y + chunk_height));
        draw->AddRect(top_left, bottom_right, kSelection, 0.0F, 0, 3.0F);
    }
    draw->PopClipRect();

    const bool panning = ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0F) ||
                         ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0F);
    if (hovered && panning) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        map_view_.pan_by_screen(delta.x, delta.y);
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0F) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        map_view_.step_at(
            ImGui::GetIO().MouseWheel, mouse.x - origin.x, mouse.y - origin.y);
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hovered_seam) {
            select_seam(*hovered_seam);
        } else {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const auto world = map_view_.screen_to_world(mouse.x - origin.x, mouse.y - origin.y);
            const double world_x = world.x;
            const double world_y = world.y;
            std::optional<chunkmap::ChunkCoord> hit;
            if (detailed) {
                hit = chunkmap::topmost_chunk_at(project_->config, world_x, world_y);
            } else if (world_x >= 0 && world_y >= 0 &&
                       world_x < world_width && world_y < world_height) {
                hit = chunkmap::ChunkCoord{
                    std::min(project_->config.columns - 1, static_cast<int>(world_x / step_x)),
                    std::min(project_->config.rows - 1, static_cast<int>(world_y / step_y))};
            }
            if (hit) {
                select_chunk(*hit);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focus_selected(available);
            }
        }
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_F)) focus_selected(available);
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Home)) fit_requested_ = true;
    ImGui::End();
}

void App::draw_inspector() {
    ImGui::Begin("Inspector", &show_inspector_);
    if (!project_) {
        concept_comparison_.cancel();
        ImGui::TextDisabled("No project open");
        ImGui::End();
        return;
    }
    if (seam_editor_value_) {
        concept_comparison_.cancel();
        draw_seam_inspector();
        ImGui::End();
        return;
    }
    if (!selected_) {
        concept_comparison_.cancel();
        ImGui::TextDisabled("Select a chunk on the map");
        ImGui::End();
        return;
    }
    ImGui::Text("Chunk %s", coord_label(*selected_).c_str());
    ImGui::Separator();
    bool chunk_tab_visible = false;
    if (ImGui::BeginTabBar("##inspector_tabs")) {
        if (ImGui::BeginTabItem("Chunk")) {
            chunk_tab_visible = true;
            draw_chunk_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Prompt")) {
            draw_prompt_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (!chunk_tab_visible) {
        concept_comparison_.cancel();
    }
    ImGui::End();
}

void App::draw_log_panel() {
    for (auto& event : textures_.take_load_events()) {
        std::ostringstream detail;
        detail << event.path.string() << " | decode " << std::fixed << std::setprecision(1)
               << event.decode_ms << " ms | upload " << event.upload_ms << " ms";
        if (!event.error.empty()) detail << " | " << event.error;
        append_log("Renderer", "texture load", !event.success,
                   event.decode_ms + event.upload_ms, detail.str());
    }

    ImGui::Begin("Log", &show_log_);
    if (ImGui::Button("Clear")) log_entries_.clear();
    ImGui::SameLine();
    if (ImGui::Button("Copy All")) {
        std::ostringstream output;
        for (const auto& entry : log_entries_) {
            output << entry.time << '\t' << entry.source << '\t' << entry.operation << '\t'
                   << (entry.error ? "ERROR" : "OK") << '\t' << std::fixed
                   << std::setprecision(1) << entry.duration_ms << " ms\t" << entry.detail << '\n';
        }
        ImGui::SetClipboardText(output.str().c_str());
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &log_auto_scroll_);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu operations", log_entries_.size());

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##operation_log", 6, flags, ImVec2(0.0F, 0.0F))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 76.0F);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 68.0F);
        ImGui::TableSetupColumn("Operation", ImGuiTableColumnFlags_WidthFixed, 135.0F);
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 54.0F);
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 76.0F);
        ImGui::TableSetupColumn("Details");
        ImGui::TableHeadersRow();
        for (const auto& entry : log_entries_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(entry.time.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.source.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(entry.operation.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(entry.error ? ImVec4(0.95F, 0.42F, 0.38F, 1.0F)
                                           : ImVec4(0.40F, 0.78F, 0.52F, 1.0F),
                               "%s", entry.error ? "ERROR" : "OK");
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.1f ms", entry.duration_ms);
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(entry.detail.c_str());
        }
        const float current_scroll = ImGui::GetScrollY();
        const float max_scroll = ImGui::GetScrollMaxY();
        const float wheel = ImGui::GetIO().MouseWheel;
        if (log_scroll_y_ && ImGui::IsWindowHovered() && wheel != 0.0F) {
            const float target = std::clamp(
                *log_scroll_y_ - wheel * ImGui::GetTextLineHeightWithSpacing() * kLogWheelLines,
                0.0F, max_scroll);
            ImGui::SetScrollY(target);
            log_scroll_y_ = target;
        } else {
            log_scroll_y_ = std::clamp(current_scroll, 0.0F, max_scroll);
        }
        if (log_auto_scroll_ && current_scroll >= max_scroll - 12.0F) {
            ImGui::SetScrollHereY(1.0F);
            log_scroll_y_ = max_scroll;
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void App::draw_chunk_tab() {
    const bool ready = chunk_ready(*selected_);
    ImGui::TextDisabled("STATUS");
    ImGui::SameLine();
    ImGui::TextColored(ready ? ImVec4(0.40F, 0.78F, 0.52F, 1.0F)
                             : ImVec4(0.68F, 0.71F, 0.72F, 1.0F),
                       "%s", ready ? "Ready" : "Empty");
    if (project_->config.has_chunk_size()) {
        ImGui::Text("Image size  %d x %d", *project_->config.chunk_width, *project_->config.chunk_height);
    } else {
        ImGui::TextDisabled("Image size  Waiting for imported image");
    }
    ImGui::TextDisabled("CONCEPT COMPARISON");
    ImGui::BeginDisabled(!ready);
    ImGui::Button("Hold: This Chunk");
    const bool selected_chunk_active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("Hold to temporarily compare this chunk with its Concept region.");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Button("Hold: Full Map");
    const bool full_map_active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("Hold to temporarily compare the full Concept Map.");
    }
    concept_comparison_.update_controls(selected_chunk_active, full_map_active);
    ImGui::BeginDisabled(pending_concept_export_request_id_.has_value());
    if (ImGui::Button("Export Concept Slice...")) export_concept_slice();
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextDisabled("READY NEIGHBORS");
    ImGui::TextWrapped("%s", ready_neighbor_text(*selected_).c_str());
    if (!ready && ready_neighbor_count(*selected_) > 0) {
        ImGui::TextDisabled("Generation context has %d Ready neighbor%s.",
            ready_neighbor_count(*selected_), ready_neighbor_count(*selected_) == 1 ? "" : "s");
    }
    ImGui::Spacing();
    const auto preview_path = ready ? project_->paths.chunk_image(*selected_)
                                    : project_->paths.concept_source();
    GlTexture* preview = ready && alignment_preview_coord_ == *selected_ &&
            alignment_preview_texture_.id() != 0
        ? &alignment_preview_texture_
        : textures_.get(preview_path);
    if (preview) {
        const float width = std::max(1.0F, ImGui::GetContentRegionAvail().x);
        const float aspect = ready
            ? static_cast<float>(preview->height()) / preview->width()
            : static_cast<float>(project_->config.columns) / project_->config.rows;
        const float height = std::min(260.0F, width * aspect);
        if (ready) {
            const bool transient = preview == &alignment_preview_texture_;
            const auto placement = project_->layout.placement(*selected_);
            const ImVec2 uv0 = transient ? ImVec2(0.0F, 0.0F) : ImVec2(
                -static_cast<float>(placement.offset_x) / preview->width(),
                -static_cast<float>(placement.offset_y) / preview->height());
            const ImVec2 uv1 = transient ? ImVec2(1.0F, 1.0F) : ImVec2(
                static_cast<float>(preview->width() - placement.offset_x) / preview->width(),
                static_cast<float>(preview->height() - placement.offset_y) / preview->height());
            ImGui::Image(texture_id(*preview), ImVec2(width, height), uv0, uv1);
        } else {
            const ImVec2 uv0(
                static_cast<float>(selected_->x) / project_->config.columns,
                static_cast<float>(selected_->y) / project_->config.rows);
            const ImVec2 uv1(
                static_cast<float>(selected_->x + 1) / project_->config.columns,
                static_cast<float>(selected_->y + 1) / project_->config.rows);
            ImGui::Image(texture_id(*preview), ImVec2(width, height), uv0, uv1);
        }
    }
    if (ready && project_->config.has_chunk_size()) {
        auto limits = chunkmap::ImageRegistration::limits(project_->config);
        const int maximum_x = limits ? limits.value().maximum_x : 0;
        const int maximum_y = limits ? limits.value().maximum_y : 0;
        const bool alignment_pending = pending_alignment_preview_request_id_.has_value() ||
                                       pending_alignment_apply_request_id_.has_value();
        ImGui::Spacing();
        ImGui::SeparatorText("ALIGNMENT");
        ImGui::TextDisabled(
            "Preview a translation. Positive X moves right; positive Y moves down.");
        bool offset_changed = false;
        ImGui::SetNextItemWidth(130.0F);
        offset_changed |= ImGui::InputInt("Horizontal (px)", &alignment_offset_x_);
        ImGui::SetNextItemWidth(130.0F);
        offset_changed |= ImGui::InputInt("Vertical (px)", &alignment_offset_y_);
        alignment_offset_x_ = std::clamp(alignment_offset_x_, -maximum_x, maximum_x);
        alignment_offset_y_ = std::clamp(alignment_offset_y_, -maximum_y, maximum_y);
        if (offset_changed) {
            alignment_comparison_.reset();
            alignment_result_choice_ = 0;
            const auto saved = project_->layout.placement(*selected_);
            if (alignment_offset_x_ == saved.offset_x &&
                alignment_offset_y_ == saved.offset_y) {
                alignment_preview_coord_.reset();
                alignment_preview_texture_.reset();
                rebuild_seam_textures(*selected_, saved);
            } else {
                request_alignment_preview(false);
            }
        }
        ImGui::TextDisabled("Safe range: X [%d, %d], Y [%d, %d]",
                            -maximum_x, maximum_x, -maximum_y, maximum_y);
        ImGui::BeginDisabled(ready_neighbor_count(*selected_) == 0 || alignment_pending);
        if (ImGui::Button("Auto")) request_alignment_preview(true);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Find a conservative translation from Ready neighbors.");
        }
        ImGui::SameLine();
        const bool has_alignment = alignment_preview_coord_ == *selected_ &&
                                   alignment_preview_texture_.id() != 0;
        const auto saved_placement = project_->layout.placement(*selected_);
        ImGui::BeginDisabled((!has_alignment && alignment_offset_x_ == saved_placement.offset_x &&
                              alignment_offset_y_ == saved_placement.offset_y) || alignment_pending);
        if (ImGui::Button("Reset")) reset_alignment_preview();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_alignment || alignment_pending ||
                             (alignment_offset_x_ == saved_placement.offset_x &&
                              alignment_offset_y_ == saved_placement.offset_y));
        if (ImGui::Button("Save Placement")) apply_alignment_shift();
        ImGui::EndDisabled();
        if (pending_alignment_preview_request_id_) {
            ImGui::SameLine();
            ImGui::TextDisabled("Previewing...");
        } else if (pending_alignment_apply_request_id_) {
            ImGui::SameLine();
            ImGui::TextDisabled("Applying...");
        }
        if (alignment_comparison_) {
            ImGui::SetNextItemWidth(180.0F);
            if (ImGui::Combo("Preview result", &alignment_result_choice_,
                             "Best of both\0Low-resolution 2D\0Projection\0")) {
                preview_alignment_candidate();
            }
            constexpr ImGuiTableFlags comparison_flags =
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##alignment_comparison", 4, comparison_flags)) {
                ImGui::TableSetupColumn("Method");
                ImGui::TableSetupColumn("Offset");
                ImGui::TableSetupColumn("Improvement");
                ImGui::TableSetupColumn("Auto");
                ImGui::TableHeadersRow();
                const auto row = [&](const char* name,
                                     const char* method,
                                     const AlignmentCandidateSummary& candidate) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const bool selected = alignment_comparison_->selected_method == method;
                    ImGui::TextUnformatted((std::string(selected ? "* " : "") + name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%+d, %+d", candidate.offset_x, candidate.offset_y);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.1f%%", candidate.relative_improvement * 100.0);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(candidate.accepted ? "Accepted" : "Rejected");
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        ImGui::SetTooltip("Common validation score: %.3f", candidate.score);
                    }
                };
                row("Low-res 2D", "low_resolution_2d",
                    alignment_comparison_->low_resolution);
                row("Projection", "projection",
                    alignment_comparison_->projection);
                ImGui::EndTable();
            }
            ImGui::TextDisabled("* Auto-selected result after the confidence gate");
        }
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(pending_import_request_id_.has_value());
    if (ImGui::Button(ready ? "Replace Image" : "Import Image")) import_image();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ready) {
        if (ImGui::Button("Reveal Image File")) {
            reveal_file(project_->paths.chunk_image(*selected_));
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!ready || pending_remove_request_id_.has_value());
    if (ImGui::Button("Delete Chunk")) remove_selected_chunk();
    ImGui::EndDisabled();
    const bool can_generate = project_->config.has_chunk_size() &&
                              ready_neighbor_count(*selected_) > 0;
    ImGui::Separator();
    ImGui::BeginDisabled(!can_generate);
    if (ImGui::Button("Export Context")) export_generation_context();
    ImGui::EndDisabled();
    if (ImGui::Button("Copy Coordinate")) {
        ImGui::SetClipboardText((std::to_string(selected_->x) + "," +
                                 std::to_string(selected_->y)).c_str());
    }
    ImGui::TextWrapped("%s", preview_path.string().c_str());
}

void App::draw_prompt_tab() {
    ImGui::TextDisabled("GENERATION PROMPT");
    ImGui::TextWrapped(
        "Generation combines the shared map style with this chunk's local content.");
    ImGui::Spacing();

    const float available_height = ImGui::GetContentRegionAvail().y;
    const float editor_height = std::max(110.0F, (available_height - 150.0F) * 0.5F);

    ImGui::SeparatorText("Global Prompt");
    ImGui::TextDisabled("Visual style shared by every chunk in this project.");
    if (ImGui::InputTextMultiline(
            "##global_prompt_inspector", &global_prompt_buffer_, ImVec2(-1.0F, editor_height),
            ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_WordWrap)) {
        global_prompt_dirty_ = true;
        global_prompt_last_edit_ = ImGui::GetTime();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) flush_global_prompt();

    ImGui::Spacing();
    ImGui::SeparatorText("Local Chunk Prompt");
    ImGui::TextDisabled("Content and layout for chunk %s only.", coord_label(*selected_).c_str());
    ImGui::PushID(selected_->x);
    ImGui::PushID(selected_->y);
    if (ImGui::InputTextMultiline(
            "##local_chunk_prompt_editor", &prompt_buffer_, ImVec2(-1.0F, editor_height),
            ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_WordWrap)) {
        prompt_dirty_ = true;
        prompt_last_edit_ = ImGui::GetTime();
    }
    ImGui::PopID();
    ImGui::PopID();
    if (ImGui::IsItemDeactivatedAfterEdit()) flush_prompt();

    ImGui::Spacing();
    if (global_prompt_dirty_ || prompt_dirty_) {
        ImGui::TextColored(ImVec4(0.92F, 0.72F, 0.30F, 1.0F),
                           "Unsaved changes - autosaving after 1 minute of inactivity...");
    } else {
        ImGui::TextDisabled("Autosaves after 1 min idle or when a field loses focus.");
    }
}

void App::draw_new_project_modal() {
    if (show_new_project_) ImGui::OpenPopup("New Project");
    bool open = show_new_project_;
    if (!ImGui::BeginPopupModal("New Project", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        show_new_project_ = open;
        return;
    }
    show_new_project_ = true;
    ImGui::SetNextItemWidth(360.0F);
    ImGui::InputText("Name", &new_project_name_);
    ImGui::SetNextItemWidth(300.0F);
    ImGui::InputText("Concept Map", &new_concept_path_);
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        const char* path = tinyfd_openFileDialog(
            "Choose Concept Map", "", 0, nullptr, nullptr, 0);
        if (path) new_concept_path_ = path;
    }
    ImGui::InputInt("Columns", &new_columns_);
    ImGui::InputInt("Rows", &new_rows_);
    ImGui::SliderFloat("Horizontal overlap", &new_overlap_x_, 0.01F, 0.49F, "%.2f");
    ImGui::SliderFloat("Vertical overlap", &new_overlap_y_, 0.01F, 0.49F, "%.2f");
    ImGui::Separator();
    const bool valid = !new_project_name_.empty() && !new_concept_path_.empty() &&
                       new_columns_ > 0 && new_rows_ > 0;
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Create Project", ImVec2(150.0F, 0.0F))) {
        auto request = make_request(chunkmap::CommandType::ProjectCreate);
        request.project_name = new_project_name_;
        request.payload = chunkmap::ProjectCreatePayload{
            new_project_name_, std::filesystem::absolute(new_concept_path_).lexically_normal(),
            new_columns_, new_rows_, new_overlap_x_, new_overlap_y_};
        auto created = command_host_.submit_and_wait(std::move(request));
        if (created && created.value().project_snapshot) {
            apply_project_snapshot(*created.value().project_snapshot, true);
            show_new_project_ = false;
            ImGui::CloseCurrentPopup();
        } else {
            error_message_ = created ? "Project command returned no project."
                                     : created.error().message;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        show_new_project_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    show_new_project_ = open && show_new_project_;
}

void App::draw_project_settings_modal() {
    if (show_project_settings_) ImGui::OpenPopup("Project Settings");
    bool open = show_project_settings_;
    if (!ImGui::BeginPopupModal("Project Settings", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        show_project_settings_ = open;
        return;
    }
    if (project_) {
        ImGui::Text("Project  %s", project_->config.name.c_str());
        ImGui::Text("Workspace  %s", workspace_.string().c_str());
        ImGui::Text("Grid  %d x %d", project_->config.columns, project_->config.rows);
        ImGui::Text("Overlap  %.2f x %.2f", project_->config.horizontal_overlap_ratio,
                    project_->config.vertical_overlap_ratio);
        if (project_->config.has_chunk_size()) {
            auto geometry = chunkmap::map_geometry(project_->config);
            ImGui::Text("Chunk  %d x %d", *project_->config.chunk_width, *project_->config.chunk_height);
            if (geometry) ImGui::Text("Map extent  %d x %d", geometry.value().world_width,
                                      geometry.value().world_height);
        } else {
            ImGui::TextDisabled("Chunk size  Waiting for imported image");
        }
        ImGui::SeparatorText("Global Prompt");
        ImGui::TextDisabled("Project-wide visual style applied to every chunk context.");
        if (ImGui::InputTextMultiline(
                "##global_prompt_editor", &global_prompt_buffer_, ImVec2(520.0F, 220.0F),
                ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_WordWrap)) {
            global_prompt_dirty_ = true;
            global_prompt_last_edit_ = ImGui::GetTime();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) flush_global_prompt();
        if (global_prompt_buffer_.empty()) {
            ImGui::TextDisabled(
                "Ask Codex to derive a Global Prompt from the first formal chunk image.");
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Close")) {
        show_project_settings_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    show_project_settings_ = open && show_project_settings_;
}

void App::draw_change_grid_modal() {
    if (show_change_grid_) ImGui::OpenPopup("Change Grid");
    bool open = show_change_grid_;
    if (!ImGui::BeginPopupModal("Change Grid", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        show_change_grid_ = open;
        return;
    }
    if (project_) {
        ImGui::Text("Current grid  %d x %d", project_->config.columns, project_->config.rows);
        ImGui::TextDisabled(
            "Change how the Concept Map is divided before importing the first chunk image.");
        ImGui::Spacing();
        const bool grid_editable = !project_->config.has_chunk_size();
        ImGui::BeginDisabled(!grid_editable);
        ImGui::SetNextItemWidth(120.0F);
        ImGui::InputInt("Columns", &change_grid_columns_);
        ImGui::SetNextItemWidth(120.0F);
        ImGui::InputInt("Rows", &change_grid_rows_);
        ImGui::EndDisabled();
        if (grid_editable) {
            ImGui::TextDisabled("Local chunk prompts must be empty before changing the grid.");
        } else {
            ImGui::TextDisabled("Grid is locked because a chunk image has already been imported.");
        }
        if (!change_grid_error_.empty()) {
            ImGui::TextColored(
                ImVec4(0.95F, 0.42F, 0.38F, 1.0F), "%s", change_grid_error_.c_str());
        }
        ImGui::Separator();
        const bool grid_valid = change_grid_columns_ > 0 && change_grid_rows_ > 0;
        const bool grid_changed = change_grid_columns_ != project_->config.columns ||
                                  change_grid_rows_ != project_->config.rows;
        ImGui::BeginDisabled(!grid_editable || !grid_valid || !grid_changed);
        if (ImGui::Button("Apply Grid")) {
            flush_global_prompt();
            if (!global_prompt_dirty_) {
                auto request = make_request(chunkmap::CommandType::ProjectGridSet);
                request.payload = chunkmap::ProjectGridSetPayload{
                    change_grid_columns_, change_grid_rows_};
                auto updated = command_host_.submit_and_wait(std::move(request));
                if (updated && updated.value().project_snapshot) {
                    apply_project_snapshot(*updated.value().project_snapshot, false);
                    status_message_ = "Concept grid updated";
                    error_message_.clear();
                    change_grid_error_.clear();
                    show_change_grid_ = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    change_grid_error_ = updated ? "Grid command returned no project."
                                                 : updated.error().message;
                    error_message_ = change_grid_error_;
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel")) {
        show_change_grid_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    show_change_grid_ = open && show_change_grid_;
}

void App::draw_export_progress_modal() {
    constexpr const char* title = "Exporting Full Map";
    if (pending_export_request_id_) ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(
            title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) {
        return;
    }
    if (!pending_export_request_id_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted("Writing a full-resolution PNG...");
    ImGui::Spacing();
    const float fraction = export_progress_total_ == 0U
        ? 0.0F
        : std::clamp(static_cast<float>(export_progress_completed_) /
                         static_cast<float>(export_progress_total_),
                     0.0F, 1.0F);
    const std::string overlay = std::to_string(
        static_cast<int>(std::round(fraction * 100.0F))) + "%";
    ImGui::ProgressBar(fraction, ImVec2(420.0F, 0.0F), overlay.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("%s", export_progress_message_.empty()
        ? "Waiting for the export worker..."
        : export_progress_message_.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("The editor remains responsive. Project files are unchanged.");
    ImGui::EndPopup();
}

void App::new_project_dialog() {
    flush_prompt();
    flush_global_prompt();
    show_new_project_ = true;
}

void App::open_project_dialog() {
    const char* path = tinyfd_openFileDialog(
        "Open Chunk Map Project", workspace_.string().c_str(), 0, nullptr, nullptr, 0);
    if (!path) return;
    const std::filesystem::path project_json(path);
    if (project_json.filename() != "project.json" || project_json.parent_path().parent_path().filename() != "output") {
        error_message_ = "Choose output/<project-name>/project.json.";
        return;
    }
    open_project(project_json.parent_path().parent_path().parent_path(),
                 project_json.parent_path().filename().string());
}

void App::open_project(const std::filesystem::path& workspace, const std::string& name) {
    flush_prompt();
    flush_global_prompt();
    auto request = make_request(chunkmap::CommandType::ProjectOpen);
    request.workspace = std::filesystem::absolute(workspace).lexically_normal();
    request.project_name = name;
    auto loaded = command_host_.submit_and_wait(std::move(request));
    if (!loaded || !loaded.value().project_snapshot) {
        error_message_ = loaded ? "Open command returned no project." : loaded.error().message;
        return;
    }
    workspace_ = std::filesystem::absolute(workspace).lexically_normal();
    apply_project_snapshot(*loaded.value().project_snapshot, true);
}

void App::apply_project_snapshot(chunkmap::Project project, bool reset_selection) {
    const auto previous_selection = reset_selection ? std::optional<chunkmap::ChunkCoord>{} : selected_;
    workspace_ = project.paths.root().parent_path().parent_path();
    project_ = std::move(project);
    change_grid_columns_ = project_->config.columns;
    change_grid_rows_ = project_->config.rows;
    concept_comparison_.cancel();
    reset_alignment_preview();
    selected_.reset();
    prompt_buffer_.clear();
    prompt_dirty_ = false;
    global_prompt_buffer_.clear();
    global_prompt_dirty_ = false;
    seam_textures_.clear();
    seam_preview_sources_.clear();
    seam_editor_value_.reset();
    seam_editor_first_image_ = {};
    seam_editor_second_image_ = {};
    seam_editor_dirty_ = false;
    last_export_path_.reset();
    textures_.clear();
    rebuild_seam_textures();
    fit_requested_ = true;
    status_message_ = "Project opened";
    error_message_.clear();
    load_global_prompt();
    if (previous_selection && project_->config.contains(*previous_selection)) {
        select_chunk(*previous_selection);
    }
}

void App::reload_project() {
    if (!project_) return;
    const std::string name = project_->config.name;
    flush_prompt();
    flush_global_prompt();
    auto request = make_request(chunkmap::CommandType::ProjectOpen);
    request.project_name = name;
    auto loaded = command_host_.submit_and_wait(std::move(request));
    if (!loaded || !loaded.value().project_snapshot) {
        error_message_ = loaded ? "Reload command returned no project." : loaded.error().message;
        return;
    }
    apply_project_snapshot(*loaded.value().project_snapshot, false);
    status_message_ = "Reloaded";
    error_message_.clear();
}

void App::select_chunk(chunkmap::ChunkCoord coord) {
    if (!project_ || !project_->config.contains(coord)) return;
    if (seam_editor_value_ && seam_editor_dirty_) {
        status_message_ = "Save or cancel the current Seam before selecting a Chunk";
        return;
    }
    if (seam_editor_value_) close_seam_inspector();
    if (selected_ == coord) return;
    flush_prompt();
    concept_comparison_.cancel();
    reset_alignment_preview();
    selected_ = coord;
    seam_preview_sources_.clear();
    const auto placement = project_->layout.placement(coord);
    alignment_offset_x_ = placement.offset_x;
    alignment_offset_y_ = placement.offset_y;
    auto request = make_request(chunkmap::CommandType::PromptShow);
    request.payload = chunkmap::CoordPayload{coord};
    auto prompt = command_host_.submit_and_wait(std::move(request));
    prompt_buffer_ = prompt ? prompt.value().data.value("prompt", std::string{}) : std::string{};
    prompt_dirty_ = false;
    if (!prompt) error_message_ = prompt.error().message;
}

void App::flush_prompt() {
    if (!project_ || !selected_ || !prompt_dirty_) return;
    auto request = make_request(chunkmap::CommandType::PromptSet);
    request.payload = chunkmap::PromptSetPayload{*selected_, prompt_buffer_};
    auto written = command_host_.submit_and_wait(std::move(request));
    if (written) {
        prompt_dirty_ = false;
        status_message_ = "Prompt updated";
        error_message_.clear();
    } else {
        error_message_ = written.error().message;
    }
}

void App::load_global_prompt() {
    if (!project_) return;
    auto request = make_request(chunkmap::CommandType::GlobalPromptShow);
    auto prompt = command_host_.submit_and_wait(std::move(request));
    if (prompt) {
        global_prompt_buffer_ = prompt.value().data.value("prompt", std::string{});
        global_prompt_dirty_ = false;
    } else {
        global_prompt_buffer_.clear();
        global_prompt_dirty_ = false;
        error_message_ = prompt.error().message;
    }
}

void App::flush_global_prompt() {
    if (!project_ || !global_prompt_dirty_) return;
    auto request = make_request(chunkmap::CommandType::GlobalPromptSet);
    request.payload = chunkmap::GlobalPromptSetPayload{global_prompt_buffer_};
    auto written = command_host_.submit_and_wait(std::move(request));
    if (written) {
        global_prompt_dirty_ = false;
        status_message_ = "Global Prompt updated";
        error_message_.clear();
    } else {
        error_message_ = written.error().message;
    }
}

void App::fit_map(const ImVec2& available) {
    if (!project_) return;
    int width = project_->config.columns * 256;
    int height = project_->config.rows * 256;
    if (project_->config.has_chunk_size()) {
        auto geometry = chunkmap::map_geometry(project_->config);
        if (geometry) {
            width = geometry.value().world_width;
            height = geometry.value().world_height;
        }
    } else if (GlTexture* concept = textures_.get(project_->paths.concept_source())) {
        width = concept->width();
        height = concept->height();
    }
    map_view_.fit(available.x, available.y, static_cast<float>(width),
                  static_cast<float>(height), 28.0F);
}

void App::focus_selected(const ImVec2& available) {
    if (!project_ || !selected_) return;
    int chunk_width = 256;
    int chunk_height = 256;
    int step_x = 256;
    int step_y = 256;
    if (project_->config.has_chunk_size()) {
        auto geometry = chunkmap::map_geometry(project_->config);
        if (geometry) {
            chunk_width = geometry.value().chunk_width;
            chunk_height = geometry.value().chunk_height;
            step_x = geometry.value().step_x;
            step_y = geometry.value().step_y;
        }
    } else if (GlTexture* concept = textures_.get(project_->paths.concept_source())) {
        chunk_width = concept->width() / project_->config.columns;
        chunk_height = concept->height() / project_->config.rows;
        step_x = chunk_width;
        step_y = chunk_height;
    }
    map_view_.focus(
        available.x, available.y,
        selected_->x * step_x + chunk_width * 0.5F,
        selected_->y * step_y + chunk_height * 0.5F,
        static_cast<float>(chunk_width), static_cast<float>(chunk_height), 1.25F);
}

void App::import_image() {
    if (!project_ || !selected_) return;
    const char* path = tinyfd_openFileDialog("Import Chunk Image", "", 0, nullptr, nullptr, 0);
    if (!path) return;
    auto image = chunkmap::ImageBuffer::load(path);
    if (!image) {
        error_message_ = image.error().message;
        return;
    }
    if (!project_->config.has_chunk_size()) {
        const std::string message = "Use " + std::to_string(image.value().width()) + " x " +
            std::to_string(image.value().height()) + " as the chunk size for this project?";
        if (tinyfd_messageBox("Import Chunk Image", message.c_str(),
                              "yesno", "question", 1) != 1) return;
    }
    auto request = make_request(chunkmap::CommandType::ChunkImport);
    request.payload = chunkmap::ChunkImagePayload{
        *selected_, std::filesystem::absolute(path).lexically_normal()};
    pending_import_request_id_ = request.request_id;
    command_host_.submit(std::move(request));
    status_message_ = "Importing chunk image...";
    error_message_.clear();
}

void App::remove_selected_chunk() {
    if (!project_ || !selected_ || !chunk_ready(*selected_)) return;
    const std::string message = "Delete chunk " + coord_label(*selected_) +
        "? Its formal image will be removed and the chunk will become Empty.";
    if (tinyfd_messageBox("Delete Chunk", message.c_str(), "yesno", "warning", 0) != 1) return;
    auto request = make_request(chunkmap::CommandType::ChunkRemove);
    request.payload = chunkmap::CoordPayload{*selected_};
    pending_remove_request_id_ = request.request_id;
    command_host_.submit(std::move(request));
    status_message_ = "Deleting chunk image...";
    error_message_.clear();
}

void App::request_alignment_preview(bool automatic) {
    if (!project_ || !selected_ || !chunk_ready(*selected_) ||
        pending_alignment_apply_request_id_) {
        return;
    }
    if (automatic && ready_neighbor_count(*selected_) == 0) return;
    auto request = make_request(chunkmap::CommandType::ChunkAlignmentPreview);
    request.payload = chunkmap::ChunkAlignmentPayload{
        *selected_, alignment_offset_x_, alignment_offset_y_, automatic};
    pending_alignment_preview_request_id_ = request.request_id;
    command_host_.submit(std::move(request));
    status_message_ = automatic ? "Finding automatic alignment..." :
                                  "Updating alignment preview...";
    error_message_.clear();
}

void App::reset_alignment_preview() {
    const bool had_preview = alignment_preview_coord_.has_value();
    const auto preview_coord = alignment_preview_coord_;
    const auto placement = project_ && selected_
        ? project_->layout.placement(*selected_) : chunkmap::ChunkPlacement{};
    alignment_offset_x_ = placement.offset_x;
    alignment_offset_y_ = placement.offset_y;
    alignment_preview_coord_.reset();
    alignment_preview_texture_.reset();
    pending_alignment_preview_request_id_.reset();
    alignment_comparison_.reset();
    alignment_result_choice_ = 0;
    if (had_preview && preview_coord) {
        rebuild_seam_textures(*preview_coord, placement);
    }
}

void App::preview_alignment_candidate() {
    if (!alignment_comparison_) return;
    const AlignmentCandidateSummary* candidate = nullptr;
    if (alignment_result_choice_ == 1) {
        candidate = &alignment_comparison_->low_resolution;
    } else if (alignment_result_choice_ == 2) {
        candidate = &alignment_comparison_->projection;
    } else {
        alignment_offset_x_ = alignment_comparison_->selected_offset_x;
        alignment_offset_y_ = alignment_comparison_->selected_offset_y;
        const auto saved = project_ && selected_
            ? project_->layout.placement(*selected_) : chunkmap::ChunkPlacement{};
        if (alignment_offset_x_ == saved.offset_x && alignment_offset_y_ == saved.offset_y) {
            alignment_preview_coord_.reset();
            alignment_preview_texture_.reset();
            pending_alignment_preview_request_id_.reset();
            if (selected_) rebuild_seam_textures(*selected_, saved);
            return;
        }
        request_alignment_preview(false);
        return;
    }
    alignment_offset_x_ = candidate->offset_x;
    alignment_offset_y_ = candidate->offset_y;
    const auto saved = project_ && selected_
        ? project_->layout.placement(*selected_) : chunkmap::ChunkPlacement{};
    if (alignment_offset_x_ == saved.offset_x && alignment_offset_y_ == saved.offset_y) {
        alignment_preview_coord_.reset();
        alignment_preview_texture_.reset();
        pending_alignment_preview_request_id_.reset();
        if (selected_) rebuild_seam_textures(*selected_, saved);
        return;
    }
    request_alignment_preview(false);
}

void App::apply_alignment_shift() {
    if (!project_ || !selected_ || !chunk_ready(*selected_) ||
        pending_alignment_preview_request_id_ || pending_alignment_apply_request_id_ ||
        alignment_preview_coord_ != *selected_ || alignment_preview_texture_.id() == 0 ||
        (alignment_offset_x_ == project_->layout.placement(*selected_).offset_x &&
         alignment_offset_y_ == project_->layout.placement(*selected_).offset_y)) {
        return;
    }
    auto request = make_request(chunkmap::CommandType::ChunkShiftApply);
    request.payload = chunkmap::ChunkAlignmentPayload{
        *selected_, alignment_offset_x_, alignment_offset_y_, false};
    pending_alignment_apply_request_id_ = request.request_id;
    command_host_.submit(std::move(request));
    status_message_ = "Saving chunk placement...";
    error_message_.clear();
}

void App::export_full_map() {
    if (!project_ || pending_export_request_id_) return;
    flush_prompt();
    flush_global_prompt();
    const auto suggested = workspace_ / (project_->config.name + ".png");
    const char* path = tinyfd_saveFileDialog(
        "Export Full Map", suggested.string().c_str(), 0, nullptr, nullptr);
    if (!path) return;
    std::filesystem::path output(path);
    if (output.extension().empty()) output += ".png";
    auto request = make_request(chunkmap::CommandType::MapExport);
    request.payload = chunkmap::MapExportPayload{
        std::filesystem::absolute(output).lexically_normal(), true};
    pending_export_request_id_ = request.request_id;
    export_progress_completed_ = 0U;
    export_progress_total_ = 0U;
    export_progress_message_ = "Waiting for the export worker...";
    command_host_.submit(std::move(request));
    status_message_ = "Exporting full map...";
    error_message_.clear();
}

void App::export_concept_slice() {
    if (!project_ || !selected_ || pending_concept_export_request_id_) return;
    const auto suggested = workspace_ /
        (project_->config.name + "_concept_" + std::to_string(selected_->x) + "_" +
         std::to_string(selected_->y) + ".png");
    const char* path = tinyfd_saveFileDialog(
        "Export Concept Slice", suggested.string().c_str(), 0, nullptr, nullptr);
    if (!path) return;
    std::filesystem::path output(path);
    if (output.extension().empty()) output += ".png";
    auto request = make_request(chunkmap::CommandType::ConceptSliceExport);
    request.payload = chunkmap::ConceptSliceExportPayload{
        *selected_, std::filesystem::absolute(output).lexically_normal(), true};
    pending_concept_export_request_id_ = request.request_id;
    command_host_.submit(std::move(request));
    status_message_ = "Exporting concept slice...";
    error_message_.clear();
}

void App::export_generation_context() {
    if (!project_ || !selected_ || ready_neighbor_count(*selected_) == 0) return;
    flush_prompt();
    flush_global_prompt();
    if (prompt_dirty_ || global_prompt_dirty_) return;
    auto request = make_request(chunkmap::CommandType::ChunkContext);
    request.payload = chunkmap::CoordPayload{*selected_};
    auto exported = command_host_.submit_and_wait(std::move(request));
    if (!exported) {
        error_message_ = exported.error().message;
        return;
    }
    const auto manifest = exported.value().data.value("manifest", std::string{});
    if (manifest.empty()) {
        error_message_ = "Context command returned no manifest.";
        return;
    }
    reveal_file(manifest);
    status_message_ = "Generation context exported";
    error_message_.clear();
}

void App::rebuild_seam_textures(
    std::optional<chunkmap::ChunkCoord> placement_preview,
    chunkmap::ChunkPlacement preview_value) {
    if (!placement_preview) {
        seam_textures_.clear();
        seam_preview_sources_.clear();
    }
    if (!project_ || !project_->config.has_chunk_size()) return;
    std::unordered_map<chunkmap::ChunkCoord, chunkmap::ImageBuffer,
                       chunkmap::ChunkCoordHash> full_rebuild_sources;
    auto load_source = [&](chunkmap::ChunkCoord coord) -> const chunkmap::ImageBuffer* {
        auto& cache = placement_preview ? seam_preview_sources_ : full_rebuild_sources;
        const auto found = cache.find(coord);
        if (found != cache.end()) return &found->second;
        auto loaded = chunkmap::ImageBuffer::load(project_->paths.chunk_image(coord));
        if (!loaded) return nullptr;
        const auto inserted = cache.emplace(coord, loaded.take_value());
        return &inserted.first->second;
    };
    for (const auto direction : {chunkmap::SeamDirection::Right,
                                 chunkmap::SeamDirection::Bottom}) {
        for (int y = 0; y < project_->config.rows; ++y) {
            for (int x = 0; x < project_->config.columns; ++x) {
                const chunkmap::SeamKey key{{x, y}, direction};
                const auto second = chunkmap::seam_second(key);
                if (placement_preview && key.first != *placement_preview &&
                    second != *placement_preview) continue;
                if (!project_->config.contains(second) || !chunk_ready(key.first) ||
                    !chunk_ready(second)) continue;
                const auto* first_image = load_source(key.first);
                const auto* second_image = load_source(second);
                if (!first_image || !second_image) continue;
                const auto found = project_->layout.seams.find(key);
                const auto seam = found == project_->layout.seams.end()
                    ? chunkmap::LayoutRenderer::default_seam(project_->config, key)
                    : found->second;
                const auto placement = [&](chunkmap::ChunkCoord coord) {
                    return placement_preview && *placement_preview == coord
                        ? preview_value : project_->layout.placement(coord);
                };
                auto patch = chunkmap::LayoutRenderer::render_seam_patch(
                    *first_image, *second_image, project_->config,
                    placement(key.first), placement(second), seam);
                if (!patch) continue;
                auto texture = std::make_unique<GlTexture>();
                if (texture->load(patch.value())) {
                    seam_textures_[key] = std::move(texture);
                }
            }
        }
    }
}

void App::select_seam(chunkmap::SeamKey key) {
    if (!project_ || !project_->config.contains(key.first) ||
        !project_->config.contains(chunkmap::seam_second(key)) ||
        !chunk_ready(key.first) || !chunk_ready(chunkmap::seam_second(key))) {
        return;
    }
    if (seam_editor_value_ && seam_editor_value_->key == key) return;
    if (seam_editor_value_ && seam_editor_dirty_) {
        status_message_ = "Save or cancel the current Seam before opening another";
        return;
    }
    flush_prompt();
    flush_global_prompt();
    concept_comparison_.cancel();
    reset_alignment_preview();
    if (seam_editor_value_) {
        close_seam_inspector();
    } else {
        seam_preview_sources_.clear();
    }
    const auto found = project_->layout.seams.find(key);
    auto editor_value = found == project_->layout.seams.end()
        ? chunkmap::LayoutRenderer::default_seam(project_->config, key)
        : found->second;
    auto first_image = chunkmap::ImageBuffer::load(project_->paths.chunk_image(key.first));
    auto second_image = chunkmap::ImageBuffer::load(
        project_->paths.chunk_image(chunkmap::seam_second(key)));
    if (!first_image || !second_image) {
        error_message_ = !first_image ? first_image.error().message : second_image.error().message;
        return;
    }
    seam_editor_first_image_ = first_image.take_value();
    seam_editor_second_image_ = second_image.take_value();
    seam_editor_value_ = std::move(editor_value);
    seam_editor_active_point_ = -1;
    seam_editor_dirty_ = false;
    show_inspector_ = true;
    refresh_seam_editor_preview();
    status_message_ = "Editing Seam " + coord_label(key.first) + " -> " +
                      coord_label(chunkmap::seam_second(key));
    error_message_.clear();
}

void App::close_seam_inspector() {
    if (!seam_editor_value_) return;
    const auto key = seam_editor_value_->key;
    const bool restore_saved_seam = seam_editor_dirty_;
    seam_editor_value_.reset();
    seam_editor_dirty_ = false;
    seam_editor_active_point_ = -1;
    seam_editor_first_image_ = {};
    seam_editor_second_image_ = {};
    seam_preview_sources_.clear();
    if (project_ && restore_saved_seam) {
        rebuild_seam_textures(key.first, project_->layout.placement(key.first));
    }
}

void App::refresh_seam_editor_preview() {
    if (!project_ || !seam_editor_value_ || seam_editor_first_image_.empty() ||
        seam_editor_second_image_.empty()) {
        return;
    }
    const auto key = seam_editor_value_->key;
    const auto second = chunkmap::seam_second(key);
    auto patch = chunkmap::LayoutRenderer::render_seam_patch(
        seam_editor_first_image_, seam_editor_second_image_, project_->config,
        project_->layout.placement(key.first), project_->layout.placement(second),
        *seam_editor_value_);
    if (!patch) {
        error_message_ = patch.error().message;
        return;
    }
    auto texture = std::make_unique<GlTexture>();
    if (texture->load(patch.value())) {
        seam_textures_[key] = std::move(texture);
    }
}

void App::draw_seam_inspector() {
    if (!project_ || !seam_editor_value_) return;
    auto& seam = *seam_editor_value_;
    const auto key = seam.key;
    const auto second = chunkmap::seam_second(key);
    const auto update_dirty = [&]() {
        const auto found = project_->layout.seams.find(key);
        const auto saved = found == project_->layout.seams.end()
            ? chunkmap::LayoutRenderer::default_seam(project_->config, key)
            : found->second;
        seam_editor_dirty_ = !same_seam(seam, saved);
    };
    ImGui::TextUnformatted("Seam");
    ImGui::SameLine();
    ImGui::Text("%s  ->  %s", coord_label(key.first).c_str(), coord_label(second).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s overlap only", chunkmap::seam_direction_name(key.direction));
    ImGui::TextDisabled(
        "Drag points. Click the band to add a point; right-click an inner point to remove it.");

    auto geometry = chunkmap::image_geometry(project_->config);
    const int overlap = geometry
        ? (key.direction == chunkmap::SeamDirection::Right
               ? geometry.value().overlap_x : geometry.value().overlap_y)
        : 0;
    int feather = seam.feather_width;
    ImGui::SetNextItemWidth(240.0F);
    if (ImGui::SliderInt("Feather width", &feather, 0, std::max(0, overlap), "%d px")) {
        seam.feather_width = feather;
        update_dirty();
        refresh_seam_editor_preview();
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto Boundary")) {
        seam = chunkmap::LayoutRenderer::default_seam(project_->config, key);
        update_dirty();
        refresh_seam_editor_preview();
    }

    const auto texture_found = seam_textures_.find(key);
    const GlTexture* seam_texture = texture_found != seam_textures_.end()
        ? texture_found->second.get() : nullptr;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float source_width = static_cast<float>(std::max(
        1, seam_texture ? seam_texture->width() : 0));
    const float source_height = static_cast<float>(std::max(
        1, seam_texture ? seam_texture->height() : 0));
    const float scale = std::min(
        std::max(1.0F, available.x) / source_width,
        std::max(180.0F, available.y - 64.0F) / source_height);
    const ImVec2 canvas_size(
        std::max(120.0F, source_width * scale),
        std::max(120.0F, source_height * scale));
    ImGui::InvisibleButton("##seam_edit_canvas", canvas_size,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 canvas_min = ImGui::GetItemRectMin();
    const ImVec2 canvas_max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(18, 20, 23, 255));
    if (seam_texture && seam_texture->id() != 0) {
        draw->AddImage(texture_id(*seam_texture), canvas_min, canvas_max);
    }
    const bool right = key.direction == chunkmap::SeamDirection::Right;
    const auto point_position = [&](const chunkmap::SeamPoint& point) {
        return right
            ? ImVec2(canvas_min.x + static_cast<float>(point.across) * canvas_size.x,
                     canvas_min.y + static_cast<float>(point.along) * canvas_size.y)
            : ImVec2(canvas_min.x + static_cast<float>(point.along) * canvas_size.x,
                     canvas_min.y + static_cast<float>(point.across) * canvas_size.y);
    };
    const ImU32 boundary_color = IM_COL32(247, 184, 69, 255);
    for (std::size_t index = 1; index < seam.points.size(); ++index) {
        draw->AddLine(point_position(seam.points[index - 1]),
                      point_position(seam.points[index]),
                      boundary_color, 2.5F);
    }
    for (const auto& point : seam.points) {
        draw->AddCircleFilled(point_position(point), 5.5F, boundary_color);
        draw->AddCircle(point_position(point), 7.5F, IM_COL32(20, 22, 25, 230), 0, 2.0F);
    }

    const auto normalized_mouse = [&]() {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const double horizontal = std::clamp(
            static_cast<double>((mouse.x - canvas_min.x) / canvas_size.x), 0.0, 1.0);
        const double vertical = std::clamp(
            static_cast<double>((mouse.y - canvas_min.y) / canvas_size.y), 0.0, 1.0);
        return right ? chunkmap::SeamPoint{vertical, horizontal}
                     : chunkmap::SeamPoint{horizontal, vertical};
    };
    const auto nearest_point = [&]() {
        int result = -1;
        float distance_squared = 13.0F * 13.0F;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (std::size_t index = 0; index < seam.points.size(); ++index) {
            const ImVec2 position = point_position(seam.points[index]);
            const float dx = mouse.x - position.x;
            const float dy = mouse.y - position.y;
            const float candidate = dx * dx + dy * dy;
            if (candidate <= distance_squared) {
                result = static_cast<int>(index);
                distance_squared = candidate;
            }
        }
        return result;
    };
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const int nearest = nearest_point();
        if (nearest > 0 && nearest + 1 < static_cast<int>(seam.points.size())) {
            seam.points.erase(seam.points.begin() + nearest);
            update_dirty();
            refresh_seam_editor_preview();
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        seam_editor_active_point_ = nearest_point();
        if (seam_editor_active_point_ < 0) {
            auto point = normalized_mouse();
            point.along = std::clamp(point.along, 0.001, 0.999);
            const auto insertion = std::lower_bound(
                seam.points.begin(), seam.points.end(), point.along,
                [](const auto& candidate, double along) { return candidate.along < along; });
            const auto too_close = [&](const auto& candidate) {
                return std::abs(candidate.along - point.along) < 0.0005;
            };
            if ((insertion != seam.points.end() && too_close(*insertion)) ||
                (insertion != seam.points.begin() && too_close(*(insertion - 1)))) {
                seam_editor_active_point_ = static_cast<int>(
                    insertion != seam.points.end() && too_close(*insertion)
                        ? std::distance(seam.points.begin(), insertion)
                        : std::distance(seam.points.begin(), insertion - 1));
            } else {
                seam_editor_active_point_ = static_cast<int>(
                    std::distance(seam.points.begin(), insertion));
                seam.points.insert(insertion, point);
                update_dirty();
                refresh_seam_editor_preview();
            }
        }
    }
    if (seam_editor_active_point_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        auto point = normalized_mouse();
        const int index = seam_editor_active_point_;
        if (index == 0) point.along = 0.0;
        else if (index + 1 == static_cast<int>(seam.points.size())) point.along = 1.0;
        else {
            const double lower =
                seam.points[static_cast<std::size_t>(index - 1)].along + 0.0005;
            const double upper =
                seam.points[static_cast<std::size_t>(index + 1)].along - 0.0005;
            point.along = lower <= upper
                ? std::clamp(point.along, lower, upper)
                : seam.points[static_cast<std::size_t>(index)].along;
        }
        const auto previous = seam.points[static_cast<std::size_t>(index)];
        if (previous.along != point.along || previous.across != point.across) {
            seam.points[static_cast<std::size_t>(index)] = point;
            update_dirty();
            refresh_seam_editor_preview();
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) seam_editor_active_point_ = -1;

    ImGui::Spacing();
    ImGui::BeginDisabled(!seam_editor_dirty_);
    if (ImGui::Button("Save Seam")) {
        auto request = make_request(chunkmap::CommandType::SeamSet);
        request.payload = chunkmap::SeamSetPayload{seam};
        auto saved = command_host_.submit_and_wait(std::move(request));
        if (saved) {
            project_->layout.seams[key] = seam;
            seam_editor_dirty_ = false;
            status_message_ = "Seam parameters saved";
            error_message_.clear();
        } else {
            error_message_ = saved.error().message;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool has_saved_override = project_->layout.seams.find(key) !=
                                    project_->layout.seams.end();
    ImGui::BeginDisabled(!seam_editor_dirty_ && !has_saved_override);
    if (ImGui::Button("Use Auto Default")) {
        auto request = make_request(chunkmap::CommandType::SeamReset);
        request.payload = chunkmap::SeamResetPayload{key};
        auto reset = command_host_.submit_and_wait(std::move(request));
        if (reset) {
            project_->layout.seams.erase(key);
            seam = chunkmap::LayoutRenderer::default_seam(project_->config, key);
            seam_editor_dirty_ = false;
            refresh_seam_editor_preview();
            status_message_ = "Using automatic seam default";
            error_message_.clear();
        } else {
            error_message_ = reset.error().message;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        close_seam_inspector();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Chunk PNG files are never modified.");
}

chunkmap::CommandRequest App::make_request(chunkmap::CommandType type) const {
    chunkmap::CommandRequest request;
    request.request_id = next_desktop_request_id();
    request.type = type;
    request.workspace = workspace_;
    if (project_) request.project_name = project_->config.name;
    return request;
}

void App::append_log(std::string source,
                     std::string operation,
                     bool error,
                     double duration_ms,
                     std::string detail) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char time[16]{};
    std::strftime(time, sizeof(time), "%H:%M:%S", &local);
    std::clog << '[' << time << "] " << source << ' ' << operation << ' '
              << (error ? "ERROR" : "OK") << ' ' << std::fixed << std::setprecision(1)
              << duration_ms << " ms | " << detail << '\n';
    log_entries_.push_back(
        {time, std::move(source), std::move(operation), error,
         duration_ms, std::move(detail)});
}

void App::poll_commands() {
    auto updates = command_host_.take_updates();
    for (const auto& progress : updates.progress) {
        if (progress.type != chunkmap::CommandType::MapExport) continue;
        if (!pending_export_request_id_) pending_export_request_id_ = progress.request_id;
        if (*pending_export_request_id_ != progress.request_id) continue;
        export_progress_completed_ = progress.completed;
        export_progress_total_ = progress.total;
        export_progress_message_ = progress.message;
    }
    for (auto& completion : updates.completions) {
        const bool from_desktop = completion.request.request_id.rfind("desktop-", 0) == 0;
        std::ostringstream log_detail;
        log_detail << "queue " << std::fixed << std::setprecision(1)
                   << completion.queue_wait_ms << " ms | execute "
                   << completion.execution_ms << " ms";
        if (completion.request.project_name) {
            log_detail << " | project " << *completion.request.project_name;
        }
        if (!completion.result) {
            log_detail << " | " << completion.result.error().code << ": "
                       << completion.result.error().message;
        } else if (completion.result.value().data.contains("registration")) {
            const auto& registration =
                completion.result.value().data.at("registration");
            const auto offset = registration.value("offset", nlohmann::json::array());
            if (offset.is_array() && offset.size() == 2U) {
                log_detail << " | registration "
                           << offset.at(0).get<int>() << ','
                           << offset.at(1).get<int>() << " ("
                           << std::setprecision(1)
                           << registration.value("relative_improvement", 0.0) * 100.0
                           << "% better, "
                           << (registration.value("applied", false) ? "applied" : "unchanged")
                           << ')';
            }
        }
        append_log(from_desktop ? "Desktop" : "CLI",
                   chunkmap::command_name(completion.request.type),
                   !completion.result,
                   completion.queue_wait_ms + completion.execution_ms,
                   log_detail.str());
        const bool pending_import = pending_import_request_id_ &&
            completion.request.request_id == *pending_import_request_id_;
        const bool pending_remove = pending_remove_request_id_ &&
            completion.request.request_id == *pending_remove_request_id_;
        const bool pending_alignment_preview = pending_alignment_preview_request_id_ &&
            completion.request.request_id == *pending_alignment_preview_request_id_;
        const bool pending_alignment_apply = pending_alignment_apply_request_id_ &&
            completion.request.request_id == *pending_alignment_apply_request_id_;
        const bool pending_export = pending_export_request_id_ &&
            completion.request.request_id == *pending_export_request_id_;
        const bool pending_concept_export = pending_concept_export_request_id_ &&
            completion.request.request_id == *pending_concept_export_request_id_;
        if (from_desktop && !pending_import && !pending_remove &&
            !pending_alignment_preview && !pending_alignment_apply &&
            !pending_export && !pending_concept_export) continue;
        if (pending_import) pending_import_request_id_.reset();
        if (pending_remove) pending_remove_request_id_.reset();
        if (pending_alignment_preview) pending_alignment_preview_request_id_.reset();
        if (pending_alignment_apply) pending_alignment_apply_request_id_.reset();
        if (pending_export) pending_export_request_id_.reset();
        if (pending_concept_export) pending_concept_export_request_id_.reset();
        if (!completion.result) {
            error_message_ = completion.result.error().message;
            continue;
        }
        auto& result = completion.result.value();
        if (pending_alignment_preview) {
            const auto chunk = result.data.value("chunk", nlohmann::json::array());
            if (!selected_ || !chunk.is_array() || chunk.size() != 2U ||
                selected_->x != chunk.at(0).get<int>() ||
                selected_->y != chunk.at(1).get<int>() ||
                !result.alignment_preview_image) {
                continue;
            }
            auto uploaded = alignment_preview_texture_.load(
                *result.alignment_preview_image);
            if (!uploaded) {
                error_message_ = uploaded.error().message;
                continue;
            }
            alignment_preview_coord_ = *selected_;
            const auto registration = result.data.value(
                "registration", nlohmann::json::object());
            const auto offset = registration.value("offset", nlohmann::json::array());
            if (offset.is_array() && offset.size() == 2U) {
                alignment_offset_x_ = offset.at(0).get<int>();
                alignment_offset_y_ = offset.at(1).get<int>();
                rebuild_seam_textures(
                    *selected_, {alignment_offset_x_, alignment_offset_y_});
            }
            const auto* payload = std::get_if<chunkmap::ChunkAlignmentPayload>(
                &completion.request.payload);
            if (payload && payload->automatic) {
                const auto comparison = registration.value(
                    "comparison", nlohmann::json::object());
                const auto candidates = comparison.value(
                    "candidates", nlohmann::json::object());
                if (candidates.contains("low_resolution_2d") &&
                    candidates.contains("projection")) {
                    const auto parse_candidate = [](const nlohmann::json& value) {
                        AlignmentCandidateSummary candidate;
                        const auto candidate_offset = value.value(
                            "offset", nlohmann::json::array());
                        if (candidate_offset.is_array() && candidate_offset.size() == 2U) {
                            candidate.offset_x = candidate_offset.at(0).get<int>();
                            candidate.offset_y = candidate_offset.at(1).get<int>();
                        }
                        candidate.score = value.value("score", 0.0);
                        candidate.relative_improvement = value.value(
                            "relative_improvement", 0.0);
                        candidate.accepted = value.value("accepted", false);
                        return candidate;
                    };
                    AlignmentComparisonSummary summary;
                    summary.low_resolution = parse_candidate(
                        candidates.at("low_resolution_2d"));
                    summary.projection = parse_candidate(candidates.at("projection"));
                    summary.selected_method = comparison.value(
                        "selected_method", std::string("low_resolution_2d"));
                    summary.selected_offset_x = alignment_offset_x_;
                    summary.selected_offset_y = alignment_offset_y_;
                    alignment_comparison_ = std::move(summary);
                    alignment_result_choice_ = 0;
                }
            }
            if (payload && payload->automatic && alignment_offset_x_ == 0 &&
                alignment_offset_y_ == 0) {
                status_message_ = "Auto found no confident alignment shift";
            } else {
                status_message_ = "Alignment preview: X " +
                    std::to_string(alignment_offset_x_) + ", Y " +
                    std::to_string(alignment_offset_y_);
            }
            error_message_.clear();
            continue;
        }
        if (pending_export) {
            const auto path = result.data.value("output", std::string{});
            if (!path.empty()) last_export_path_ = std::filesystem::path(path);
            const auto size = result.data.value("size", nlohmann::json::array());
            status_message_ = size.is_array() && size.size() == 2U
                ? "Full map exported (" + std::to_string(size.at(0).get<int>()) + " x " +
                      std::to_string(size.at(1).get<int>()) + ")"
                : "Full map exported";
            error_message_.clear();
            continue;
        }
        if (pending_concept_export) {
            const auto size = result.data.value("size", nlohmann::json::array());
            status_message_ = size.is_array() && size.size() == 2U
                ? "Concept slice exported (" + std::to_string(size.at(0).get<int>()) + " x " +
                      std::to_string(size.at(1).get<int>()) + ")"
                : "Concept slice exported";
            error_message_.clear();
            continue;
        }
        if (completion.request.type == chunkmap::CommandType::ProjectCreate &&
            result.project_snapshot) {
            apply_project_snapshot(*result.project_snapshot, true);
            status_message_ = "CLI project created";
            continue;
        }
        if (completion.request.type == chunkmap::CommandType::ProjectOpen &&
            result.project_snapshot) {
            apply_project_snapshot(*result.project_snapshot, true);
            status_message_ = "CLI project opened";
            continue;
        }
        if (!project_ || !result.changes.project ||
            !(result.changes.project.value() ==
              chunkmap::make_project_key(workspace_, project_->config.name))) {
            continue;
        }
        if (result.project_snapshot && result.changes.project_changed) {
            apply_project_snapshot(*result.project_snapshot, false);
        }
        for (const auto coord : result.changes.changed_chunks) {
            const auto path = project_->paths.chunk_image(coord);
            if (result.changed_chunk_image && result.changes.changed_chunks.size() == 1U) {
                auto uploaded = textures_.put(path, *result.changed_chunk_image);
                if (!uploaded) error_message_ = uploaded.error().message;
            } else {
                textures_.invalidate(path);
            }
        }
        if (alignment_preview_coord_ &&
            std::find(result.changes.changed_chunks.begin(),
                      result.changes.changed_chunks.end(), *alignment_preview_coord_) !=
                result.changes.changed_chunks.end()) {
            reset_alignment_preview();
        }
        if (!result.changes.changed_chunks.empty()) {
            rebuild_seam_textures();
        }
        if (selected_ && std::find(result.changes.changed_prompts.begin(),
                                   result.changes.changed_prompts.end(), *selected_) !=
                             result.changes.changed_prompts.end()) {
            auto request = make_request(chunkmap::CommandType::PromptShow);
            request.payload = chunkmap::CoordPayload{*selected_};
            auto prompt = command_host_.submit_and_wait(std::move(request));
            if (prompt) {
                prompt_buffer_ = prompt.value().data.value("prompt", std::string{});
                prompt_dirty_ = false;
            }
        }
        if (result.changes.global_prompt_changed) load_global_prompt();
        if (pending_import) {
            status_message_ = result.data.contains("global_prompt_action")
                ? "First image imported. Ask Codex to create the project Global Prompt from it."
                : "Chunk image imported";
        } else if (pending_remove) {
            status_message_ = "Chunk image deleted";
        } else if (pending_alignment_apply) {
            const auto registration = result.data.value(
                "registration", nlohmann::json::object());
            const auto offset = registration.value("offset", nlohmann::json::array());
            status_message_ = offset.is_array() && offset.size() == 2U
                ? "Saved chunk placement: X " +
                      std::to_string(offset.at(0).get<int>()) + ", Y " +
                      std::to_string(offset.at(1).get<int>())
                : "Saved chunk placement";
        } else {
            status_message_ = "CLI command applied";
        }
        error_message_.clear();
    }
}

bool App::chunk_ready(chunkmap::ChunkCoord coord) const {
    return project_ && project_->config.contains(coord) &&
           path_is_regular_file(project_->paths.chunk_image(coord));
}

int App::ready_neighbor_count(chunkmap::ChunkCoord coord) const {
    int result = 0;
    for (const auto neighbor : {chunkmap::ChunkCoord{coord.x, coord.y - 1},
                                chunkmap::ChunkCoord{coord.x + 1, coord.y},
                                chunkmap::ChunkCoord{coord.x, coord.y + 1},
                                chunkmap::ChunkCoord{coord.x - 1, coord.y}}) {
        if (chunk_ready(neighbor)) ++result;
    }
    return result;
}

std::string App::ready_neighbor_text(chunkmap::ChunkCoord coord) const {
    std::vector<std::string> names;
    if (chunk_ready({coord.x, coord.y - 1})) names.emplace_back("top");
    if (chunk_ready({coord.x + 1, coord.y})) names.emplace_back("right");
    if (chunk_ready({coord.x, coord.y + 1})) names.emplace_back("bottom");
    if (chunk_ready({coord.x - 1, coord.y})) names.emplace_back("left");
    if (names.empty()) return "None";
    std::ostringstream result;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0) result << ", ";
        result << names[index];
    }
    return result.str();
}

}  // namespace chunkmap_desktop
