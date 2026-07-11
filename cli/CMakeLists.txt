add_executable(chunkmap
    src/cli_app.cpp
    src/command_parser.cpp
    src/main.cpp
)

target_link_libraries(chunkmap PRIVATE chunkmap::core)
chunkmap_enable_warnings(chunkmap)
chunkmap_enable_sanitizers(chunkmap)
