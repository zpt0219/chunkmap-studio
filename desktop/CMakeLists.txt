set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.8-docking
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    tinyfd
    GIT_REPOSITORY https://github.com/native-toolkit/libtinyfiledialogs.git
    GIT_TAG cc6b593c029110af8045826ce691f540c85e850c
)

FetchContent_MakeAvailable(glfw imgui tinyfd)

add_library(chunkmap_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(chunkmap_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(chunkmap_imgui PUBLIC glfw)

add_library(chunkmap_tinyfd STATIC
    ${tinyfd_SOURCE_DIR}/tinyfiledialogs.c
)
target_include_directories(chunkmap_tinyfd PUBLIC ${tinyfd_SOURCE_DIR})

if(MSVC)
    target_compile_options(chunkmap_imgui PRIVATE /w)
    target_compile_options(chunkmap_tinyfd PRIVATE /w)
else()
    target_compile_options(chunkmap_imgui PRIVATE -w)
    target_compile_options(chunkmap_tinyfd PRIVATE -w)
endif()

find_package(OpenGL REQUIRED)

add_executable(chunkmap_desktop
    src/main.cpp
    src/app.cpp
    src/desktop_command_host.cpp
    src/gl_texture.cpp
)

target_include_directories(chunkmap_desktop PRIVATE src)
target_link_libraries(chunkmap_desktop PRIVATE
    chunkmap::core
    chunkmap_imgui
    chunkmap_tinyfd
    OpenGL::GL
)
if(WIN32)
    target_link_libraries(chunkmap_desktop PRIVATE shell32)
endif()
chunkmap_enable_warnings(chunkmap_desktop)
chunkmap_enable_sanitizers(chunkmap_desktop)

if(APPLE)
    target_compile_definitions(chunkmap_desktop PRIVATE GL_SILENCE_DEPRECATION)
    set_target_properties(chunkmap_desktop PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "AI Chunk Map Studio"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.chunkmap.studio"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.6.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
    )
endif()
