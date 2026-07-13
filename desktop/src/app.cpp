#include "app.h"

#include "image/image_buffer.h"
#include "ui/map_geometry.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <tinyfiledialogs.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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
    draw_main_menu_bar();
    draw_dockspace();
    if (show_map_controls_) draw_map_controls();
    draw_map();
    if (show_inspector_) draw_inspector();
    if (show_log_) draw_log_panel();
    draw_new_project_modal();
    draw_project_settings_modal();
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
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            fit_map = ImGui::MenuItem("Fit Map", "Home", false, project_.has_value());
            focus_selection = ImGui::MenuItem(
                "Focus Selected", "F", false, project_.has_value() && selected_.has_value());
            ImGui::Separator();
            ImGui::BeginDisabled(!project_);
            ImGui::MenuItem("Grid", nullptr, &show_grid_);
            ImGui::MenuItem("Coordinates", nullptr, &show_coordinates_);
            ImGui::MenuItem("Seams", nullptr, &show_seams_);
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
    if (fit_map) fit_requested_ = true;
    if (focus_selection) focus_selected(last_canvas_size_);
    if (reset_layout) {
        show_map_controls_ = true;
        show_inspector_ = true;
        show_log_ = true;
        layout_initialized_ = false;
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
    ImGui::DockSpace(dockspace_id, ImVec2(0.0F, 0.0F));
    if (!layout_initialized_) {
        layout_initialized_ = true;
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
    ImGui::End();
}

void App::draw_map_controls() {
    ImGui::Begin("Map Controls", &show_map_controls_,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginDisabled(!project_);
    if (ImGui::Button("Fit Map")) fit_requested_ = true;
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &show_grid_);
    ImGui::SameLine();
    ImGui::Checkbox("Coordinates", &show_coordinates_);
    ImGui::SameLine();
    ImGui::Checkbox("Seams", &show_seams_);
    ImGui::EndDisabled();

    if (project_) {
        int ready = 0;
        for (int y = 0; y < project_->config.rows; ++y) {
            for (int x = 0; x < project_->config.columns; ++x) {
                ready += chunk_ready({x, y}) ? 1 : 0;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s  |  %dx%d  |  %d/%d Ready", project_->config.name.c_str(),
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
        ImGui::SetCursorPos(ImVec2(
            std::max(18.0F, (available.x - 280.0F) * 0.5F),
            std::max(18.0F, (available.y - 90.0F) * 0.42F)));
        ImGui::TextUnformatted("Open a map project to begin review.");
        if (ImGui::Button("Open Project", ImVec2(132.0F, 34.0F))) open_project_dialog();
        ImGui::SameLine();
        if (ImGui::Button("New Project", ImVec2(132.0F, 34.0F))) new_project_dialog();
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
        return ImVec2(origin.x + (x - pan_.x) * zoom_, origin.y + (y - pan_.y) * zoom_);
    };

    bool needs_concept_texture = !detailed;
    if (detailed) {
        for (int y = 0; y < project_->config.rows && !needs_concept_texture; ++y) {
            for (int x = 0; x < project_->config.columns; ++x) {
                const chunkmap::ChunkCoord coord{x, y};
                if (!chunk_ready(coord) || !chunk_image_visible(coord)) {
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

    for (int y = 0; y < project_->config.rows; ++y) {
        for (int x = 0; x < project_->config.columns; ++x) {
            const chunkmap::ChunkCoord coord{x, y};
            if (chunk_ready(coord)) continue;
            draw_concept_region(coord, IM_COL32(160, 178, 167, 92));
        }
    }
    if (detailed) {
        for (int y = 0; y < project_->config.rows; ++y) {
            for (int x = 0; x < project_->config.columns; ++x) {
                const chunkmap::ChunkCoord coord{x, y};
                if (!chunk_image_visible(coord)) continue;
                if (GlTexture* chunk = textures_.get(project_->paths.chunk_image(coord))) {
                    draw->AddImage(texture_id(*chunk),
                        to_screen(static_cast<float>(x * step_x), static_cast<float>(y * step_y)),
                        to_screen(static_cast<float>(x * step_x + chunk_width),
                                  static_cast<float>(y * step_y + chunk_height)));
                }
            }
        }
        // Hidden chunks are the final image layer so their full footprint, including overlap,
        // cannot be painted back by a neighboring Ready chunk.
        for (int y = 0; y < project_->config.rows; ++y) {
            for (int x = 0; x < project_->config.columns; ++x) {
                const chunkmap::ChunkCoord coord{x, y};
                if (chunk_ready(coord) && !chunk_image_visible(coord)) {
                    draw_concept_region(coord, IM_COL32_WHITE);
                }
            }
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
            if (show_grid_) draw->AddRect(top_left, bottom_right, kGrid, 0.0F, 0, 1.0F);
            if (show_coordinates_ && zoom_ > 0.18F) {
                const std::string label = coord_label(coord);
                draw->AddRectFilled(top_left, ImVec2(top_left.x + 55.0F, top_left.y + 22.0F),
                                    IM_COL32(10, 12, 14, 190));
                draw->AddText(ImVec2(top_left.x + 7.0F, top_left.y + 4.0F),
                              IM_COL32(225, 230, 230, 255), label.c_str());
            }
        }
    }
    if (show_seams_ && detailed) {
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

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0F)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        pan_.x -= delta.x / zoom_;
        pan_.y -= delta.y / zoom_;
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0F) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 world_before(
            pan_.x + (mouse.x - origin.x) / zoom_,
            pan_.y + (mouse.y - origin.y) / zoom_);
        zoom_ = std::clamp(
            zoom_ * std::pow(1.18F, ImGui::GetIO().MouseWheel), 0.04F, 16.0F);
        pan_.x = world_before.x - (mouse.x - origin.x) / zoom_;
        pan_.y = world_before.y - (mouse.y - origin.y) / zoom_;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const double world_x = pan_.x + (mouse.x - origin.x) / zoom_;
        const double world_y = pan_.y + (mouse.y - origin.y) / zoom_;
        std::optional<chunkmap::ChunkCoord> hit;
        if (detailed) {
            hit = chunkmap::topmost_chunk_at(project_->config, world_x, world_y);
        } else if (world_x >= 0 && world_y >= 0 && world_x < world_width && world_y < world_height) {
            hit = chunkmap::ChunkCoord{
                std::min(project_->config.columns - 1, static_cast<int>(world_x / step_x)),
                std::min(project_->config.rows - 1, static_cast<int>(world_y / step_y))};
        }
        if (hit) {
            select_chunk(*hit);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focus_selected(available);
        }
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_F)) focus_selected(available);
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Home)) fit_requested_ = true;
    ImGui::End();
}

void App::draw_inspector() {
    ImGui::Begin("Inspector", &show_inspector_);
    if (!project_) {
        ImGui::TextDisabled("No project open");
        ImGui::End();
        return;
    }
    if (!selected_) {
        ImGui::TextDisabled("Select a chunk on the map");
        ImGui::End();
        return;
    }
    ImGui::Text("Chunk %s", coord_label(*selected_).c_str());
    ImGui::Separator();
    if (ImGui::BeginTabBar("##inspector_tabs")) {
        if (ImGui::BeginTabItem("Chunk")) {
            draw_chunk_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Prompt")) {
            draw_prompt_tab();
            ImGui::EndTabItem();
        }
        const bool seam_available = chunk_ready(*selected_) && ready_neighbor_count(*selected_) > 0;
        ImGui::BeginDisabled(!seam_available);
        if (ImGui::BeginTabItem("Seam")) {
            draw_seam_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndDisabled();
        ImGui::EndTabBar();
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
        if (log_auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 12.0F) {
            ImGui::SetScrollHereY(1.0F);
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
    bool visible = chunk_image_visible(*selected_);
    ImGui::BeginDisabled(!ready);
    if (ImGui::Checkbox("Visible on Map", &visible)) {
        set_chunk_image_visible(*selected_, visible);
    }
    ImGui::EndDisabled();
    if (ready && !visible) {
        ImGui::TextDisabled("Hidden for review; the concept region is shown on the map.");
    }
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
    if (GlTexture* preview = textures_.get(preview_path)) {
        const float width = std::max(1.0F, ImGui::GetContentRegionAvail().x);
        const float aspect = ready
            ? static_cast<float>(preview->height()) / preview->width()
            : static_cast<float>(project_->config.columns) / project_->config.rows;
        const float height = std::min(260.0F, width * aspect);
        if (ready) {
            ImGui::Image(texture_id(*preview), ImVec2(width, height));
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
    if (ImGui::InputTextMultiline(
            "##local_chunk_prompt_editor", &prompt_buffer_, ImVec2(-1.0F, editor_height),
            ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_WordWrap)) {
        prompt_dirty_ = true;
        prompt_last_edit_ = ImGui::GetTime();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) flush_prompt();

    ImGui::Spacing();
    if (global_prompt_dirty_ || prompt_dirty_) {
        ImGui::TextColored(ImVec4(0.92F, 0.72F, 0.30F, 1.0F),
                           "Unsaved changes - autosaving after 1 minute of inactivity...");
    } else {
        ImGui::TextDisabled("Autosaves after 1 min idle or when a field loses focus.");
    }
}

void App::draw_seam_tab() {
    const char* directions[] = {"Top", "Right", "Bottom", "Left"};
    bool available[4] = {
        selected_->y > 0 && chunk_ready({selected_->x, selected_->y - 1}),
        selected_->x + 1 < project_->config.columns && chunk_ready({selected_->x + 1, selected_->y}),
        selected_->y + 1 < project_->config.rows && chunk_ready({selected_->x, selected_->y + 1}),
        selected_->x > 0 && chunk_ready({selected_->x - 1, selected_->y})};
    if (!available[seam_direction_]) {
        for (int index = 0; index < 4; ++index) {
            if (available[index]) {
                seam_direction_ = index;
                seam_analysis_.reset();
                break;
            }
        }
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo("##seam_direction", directions[seam_direction_])) {
        for (int index = 0; index < 4; ++index) {
            if (!available[index]) continue;
            if (ImGui::Selectable(directions[index], seam_direction_ == index)) {
                seam_direction_ = index;
                seam_analysis_.reset();
            }
        }
        ImGui::EndCombo();
    }
    const char* modes[] = {"Raw", "Difference", "Overlap"};
    seam_mode_ = std::clamp(seam_mode_, 0, 2);
    for (int index = 0; index < 3; ++index) {
        if (index > 0) ImGui::SameLine();
        ImGui::RadioButton(modes[index], &seam_mode_, index);
    }
    if (!seam_analysis_) refresh_seam();
    if (!seam_analysis_) {
        ImGui::TextColored(ImVec4(0.95F, 0.42F, 0.38F, 1.0F), "%s", error_message_.c_str());
        return;
    }
    ImGui::Separator();
    ImGui::Text("Overlap  %d px", seam_analysis_->overlap_pixels);
    ImGui::Text("Mean RGB difference  %.2f", seam_analysis_->mean_absolute_rgb_difference);
    ImGui::Spacing();

    const float width = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    if (seam_mode_ == 0) {
        GlTexture* first = textures_.get(project_->paths.chunk_image(seam_first_));
        GlTexture* second = textures_.get(project_->paths.chunk_image(seam_second_));
        if (first && second) {
            const float half = (width - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
            ImGui::Image(texture_id(*first), ImVec2(half, half * first->height() / first->width()));
            ImGui::SameLine();
            ImGui::Image(texture_id(*second), ImVec2(half, half * second->height() / second->width()));
        }
    } else {
        GlTexture& texture = seam_mode_ == 1 ? seam_difference_texture_ : seam_overlap_texture_;
        if (texture.id() != 0) {
            ImGui::Image(texture_id(texture), ImVec2(width, width * texture.height() / texture.width()));
        }
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
    chunk_image_visibility_.assign(
        static_cast<std::size_t>(project_->config.columns) *
            static_cast<std::size_t>(project_->config.rows),
        true);
    selected_.reset();
    prompt_buffer_.clear();
    prompt_dirty_ = false;
    global_prompt_buffer_.clear();
    global_prompt_dirty_ = false;
    seam_analysis_.reset();
    last_export_path_.reset();
    textures_.clear();
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
    if (!project_ || !project_->config.contains(coord) || selected_ == coord) return;
    flush_prompt();
    selected_ = coord;
    auto request = make_request(chunkmap::CommandType::PromptShow);
    request.payload = chunkmap::CoordPayload{coord};
    auto prompt = command_host_.submit_and_wait(std::move(request));
    prompt_buffer_ = prompt ? prompt.value().data.value("prompt", std::string{}) : std::string{};
    prompt_dirty_ = false;
    seam_analysis_.reset();
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
    constexpr float margin = 28.0F;
    zoom_ = std::clamp(std::min(
        (available.x - margin * 2.0F) / std::max(1, width),
        (available.y - margin * 2.0F) / std::max(1, height)), 0.04F, 16.0F);
    pan_.x = -std::max(0.0F, (available.x / zoom_ - width) * 0.5F);
    pan_.y = -std::max(0.0F, (available.y / zoom_ - height) * 0.5F);
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
    zoom_ = std::clamp(std::min(available.x / (chunk_width * 1.25F),
                                available.y / (chunk_height * 1.25F)), 0.04F, 16.0F);
    pan_.x = selected_->x * step_x + chunk_width * 0.5F - available.x / (2.0F * zoom_);
    pan_.y = selected_->y * step_y + chunk_height * 0.5F - available.y / (2.0F * zoom_);
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

void App::refresh_seam() {
    if (!project_ || !selected_) return;
    seam_first_ = *selected_;
    seam_second_ = *selected_;
    if (seam_direction_ == 0) {
        seam_first_.y -= 1;
        seam_core_direction_ = chunkmap::SeamDirection::Bottom;
    } else if (seam_direction_ == 1) {
        seam_second_.x += 1;
        seam_core_direction_ = chunkmap::SeamDirection::Right;
    } else if (seam_direction_ == 2) {
        seam_second_.y += 1;
        seam_core_direction_ = chunkmap::SeamDirection::Bottom;
    } else {
        seam_first_.x -= 1;
        seam_core_direction_ = chunkmap::SeamDirection::Right;
    }
    if (seam_direction_ == 0 || seam_direction_ == 3) seam_second_ = *selected_;
    auto request = make_request(chunkmap::CommandType::SeamInspect);
    request.payload = chunkmap::SeamInspectPayload{
        seam_first_, seam_core_direction_ == chunkmap::SeamDirection::Right
            ? chunkmap::CommandSeamDirection::Right
            : chunkmap::CommandSeamDirection::Bottom};
    auto result = command_host_.submit_and_wait(std::move(request));
    if (result && result.value().seam_analysis) {
        seam_analysis_ = *result.value().seam_analysis;
        seam_overlap_texture_.load(seam_analysis_->overlap_preview);
        seam_difference_texture_.load(seam_analysis_->difference_preview);
        error_message_.clear();
    } else {
        seam_analysis_.reset();
        error_message_ = result ? "Seam command returned no analysis." : result.error().message;
    }
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
        }
        append_log(from_desktop ? "Desktop" : "CLI",
                   chunkmap::command_name(completion.request.type),
                   !completion.result,
                   completion.queue_wait_ms + completion.execution_ms,
                   log_detail.str());
        const bool pending_import = pending_import_request_id_ &&
            completion.request.request_id == *pending_import_request_id_;
        const bool pending_export = pending_export_request_id_ &&
            completion.request.request_id == *pending_export_request_id_;
        if (from_desktop && !pending_import && !pending_export) continue;
        if (pending_import) pending_import_request_id_.reset();
        if (pending_export) pending_export_request_id_.reset();
        if (!completion.result) {
            error_message_ = completion.result.error().message;
            continue;
        }
        auto& result = completion.result.value();
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
        if (!result.changes.changed_chunks.empty()) {
            seam_analysis_.reset();
            seam_overlap_texture_.reset();
            seam_difference_texture_.reset();
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

bool App::chunk_image_visible(chunkmap::ChunkCoord coord) const {
    if (!project_ || !project_->config.contains(coord)) return false;
    const std::size_t index = static_cast<std::size_t>(coord.y) *
        static_cast<std::size_t>(project_->config.columns) +
        static_cast<std::size_t>(coord.x);
    return index >= chunk_image_visibility_.size() || chunk_image_visibility_[index];
}

void App::set_chunk_image_visible(chunkmap::ChunkCoord coord, bool visible) {
    if (!project_ || !project_->config.contains(coord)) return;
    const std::size_t index = static_cast<std::size_t>(coord.y) *
        static_cast<std::size_t>(project_->config.columns) +
        static_cast<std::size_t>(coord.x);
    if (index < chunk_image_visibility_.size()) chunk_image_visibility_[index] = visible;
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
