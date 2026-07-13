FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 31c1ad37456438565541f4919958214b6e762fb4
)

FetchContent_MakeAvailable(nlohmann_json stb)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

file(READ "${CMAKE_SOURCE_DIR}/docs/PROMPT_AUTHORING_GUIDE.md"
    CHUNKMAP_PROMPT_AUTHORING_GUIDE)
configure_file(
    project/prompt_authoring_guide.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/prompt_authoring_guide.h
    @ONLY
)

add_library(chunkmap_stb INTERFACE)
target_include_directories(chunkmap_stb INTERFACE ${stb_SOURCE_DIR})

add_library(chunkmap_core STATIC
    command/command_dispatcher.cpp
    command/command_codec.cpp
    command/command_request.cpp
    command/command_result.cpp
    command/document_command_queue.cpp
    core/version.cpp
    image/image_buffer.cpp
    image/full_map_exporter.cpp
    image/image_pipeline.cpp
    image/png_stream_writer.cpp
    image/seam_analyzer.cpp
    ipc/desktop_ipc.cpp
    io/atomic_file.cpp
    model/project_document.cpp
    project/project_paths.cpp
    project/project_repository.cpp
    project/project_session.cpp
    project/project_service.cpp
    ui/map_geometry.cpp
)

target_include_directories(chunkmap_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(chunkmap_core PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(chunkmap_core PUBLIC
    nlohmann_json::nlohmann_json
    chunkmap_stb
    Threads::Threads
    ZLIB::ZLIB
)
chunkmap_enable_warnings(chunkmap_core)
if(CHUNKMAP_ENABLE_SANITIZERS AND CHUNKMAP_BUILD_TESTS AND
   NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(chunkmap_core PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_library(chunkmap::core ALIAS chunkmap_core)
