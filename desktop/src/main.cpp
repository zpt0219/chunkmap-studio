#include "app.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace {

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

bool has_argument(int argc, char** argv, std::string_view expected) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == expected) return true;
    }
    return false;
}

std::optional<std::string> option_value(int argc, char** argv, std::string_view option) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == option) return std::string(argv[i + 1]);
    }
    return std::nullopt;
}

struct WindowState {
    int x = 0;
    int y = 0;
    int width = 1400;
    int height = 900;
    bool has_position = false;
    bool maximized = false;
};

std::filesystem::path desktop_config_directory() {
#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support" /
               "ChunkMap Studio";
    }
#elif defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA")) {
        return std::filesystem::path(appdata) / "ChunkMap Studio";
    }
#else
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg_config) / "chunkmap-studio";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".config" / "chunkmap-studio";
    }
#endif
    return std::filesystem::current_path() / ".chunkmap" / "desktop";
}

WindowState load_window_state(const std::filesystem::path& path) {
    WindowState state;
    std::ifstream input(path);
    int version = 0;
    int has_position = 0;
    int maximized = 0;
    if (!(input >> version >> state.x >> state.y >> state.width >> state.height >>
          has_position >> maximized) ||
        version != 1 || state.width < 640 || state.height < 480) {
        return WindowState{};
    }
    state.has_position = has_position != 0;
    state.maximized = maximized != 0;
    return state;
}

void save_window_state(const std::filesystem::path& path, GLFWwindow* window) {
    WindowState state;
    glfwGetWindowPos(window, &state.x, &state.y);
    glfwGetWindowSize(window, &state.width, &state.height);
    state.has_position = true;
    state.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return;
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return;
        output << 1 << ' ' << state.x << ' ' << state.y << ' '
               << state.width << ' ' << state.height << ' '
               << (state.has_position ? 1 : 0) << ' '
               << (state.maximized ? 1 : 0) << '\n';
        if (!output) return;
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
}

void apply_editor_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0F;
    style.ChildRounding = 3.0F;
    style.FrameRounding = 3.0F;
    style.PopupRounding = 3.0F;
    style.ScrollbarRounding = 3.0F;
    style.GrabRounding = 2.0F;
    style.TabRounding = 3.0F;
    style.WindowPadding = ImVec2(12.0F, 10.0F);
    style.FramePadding = ImVec2(9.0F, 6.0F);
    style.ItemSpacing = ImVec2(8.0F, 7.0F);
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;

    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.105F, 0.118F, 0.125F, 1.0F);
    colors[ImGuiCol_ChildBg] = ImVec4(0.090F, 0.100F, 0.106F, 1.0F);
    colors[ImGuiCol_PopupBg] = ImVec4(0.125F, 0.137F, 0.143F, 1.0F);
    colors[ImGuiCol_Border] = ImVec4(0.265F, 0.292F, 0.300F, 1.0F);
    colors[ImGuiCol_FrameBg] = ImVec4(0.155F, 0.174F, 0.180F, 1.0F);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.205F, 0.245F, 0.245F, 1.0F);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.220F, 0.310F, 0.285F, 1.0F);
    colors[ImGuiCol_TitleBg] = ImVec4(0.080F, 0.090F, 0.095F, 1.0F);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.120F, 0.145F, 0.140F, 1.0F);
    colors[ImGuiCol_Button] = ImVec4(0.180F, 0.215F, 0.210F, 1.0F);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.235F, 0.355F, 0.310F, 1.0F);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.260F, 0.455F, 0.365F, 1.0F);
    colors[ImGuiCol_Header] = ImVec4(0.190F, 0.300F, 0.265F, 1.0F);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.235F, 0.410F, 0.330F, 1.0F);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.265F, 0.480F, 0.380F, 1.0F);
    colors[ImGuiCol_CheckMark] = ImVec4(0.360F, 0.760F, 0.530F, 1.0F);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.360F, 0.680F, 0.510F, 1.0F);
    colors[ImGuiCol_Tab] = ImVec4(0.135F, 0.155F, 0.158F, 1.0F);
    colors[ImGuiCol_TabHovered] = ImVec4(0.220F, 0.360F, 0.300F, 1.0F);
    colors[ImGuiCol_TabSelected] = ImVec4(0.180F, 0.300F, 0.255F, 1.0F);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.300F, 0.630F, 0.480F, 0.65F);
}

}  // namespace

int main(int argc, char** argv) {
    const bool smoke_test = has_argument(argc, argv, "--smoke-test");
    const auto workspace = option_value(argc, argv, "--workspace")
        .value_or(std::filesystem::current_path().string());
    const auto initial_project = option_value(argc, argv, "--project");
    const auto config_directory = desktop_config_directory();
    const auto imgui_ini_path = config_directory / "imgui.ini";
    const auto window_state_path = config_directory / "window-state.ini";
    const WindowState window_state = smoke_test
        ? WindowState{}
        : load_window_state(window_state_path);
    const std::string imgui_ini_filename = imgui_ini_path.string();

    if (!smoke_test) {
        std::error_code error;
        std::filesystem::create_directories(config_directory, error);
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return EXIT_FAILURE;

    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    if (smoke_test) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(
        window_state.width, window_state.height, "AI Chunk Map Studio", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }
    if (window_state.has_position) {
        glfwSetWindowPos(window, window_state.x, window_state.y);
    }
    if (window_state.maximized) glfwMaximizeWindow(window);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(smoke_test ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = smoke_test ? nullptr : imgui_ini_filename.c_str();
    ImGui::StyleColorsDark();
    apply_editor_style();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    int rendered_frames = 0;
    int exit_code = EXIT_SUCCESS;
    try {
        chunkmap_desktop::App app(workspace, initial_project);
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            app.draw();
            if (app.exit_requested()) glfwSetWindowShouldClose(window, GLFW_TRUE);

            ImGui::Render();
            int framebuffer_width = 0;
            int framebuffer_height = 0;
            glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
            glViewport(0, 0, framebuffer_width, framebuffer_height);
            glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);

            ++rendered_frames;
            if (smoke_test && rendered_frames >= 2) break;
        }
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "Desktop error: %s\n", exception.what());
        exit_code = EXIT_FAILURE;
    } catch (...) {
        std::fprintf(stderr, "Desktop error: unknown exception\n");
        exit_code = EXIT_FAILURE;
    }

    if (!smoke_test) {
        ImGui::SaveIniSettingsToDisk(imgui_ini_filename.c_str());
        save_window_state(window_state_path, window);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return exit_code;
}
