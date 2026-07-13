FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

add_executable(chunkmap_tests
    test_command_system.cpp
    test_full_map_export.cpp
    test_golden_images.cpp
    test_image_pipeline.cpp
    test_phase6_architecture.cpp
    test_project_service.cpp
    test_version.cpp
)
target_compile_definitions(chunkmap_tests PRIVATE
    CHUNKMAP_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    CHUNKMAP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(chunkmap_tests PRIVATE
    chunkmap::core
    doctest::doctest_with_main
)
chunkmap_enable_warnings(chunkmap_tests)
chunkmap_enable_sanitizers(chunkmap_tests)

include(CTest)

add_executable(chunkmap_ipc_test_host ipc_test_host.cpp)
target_link_libraries(chunkmap_ipc_test_host PRIVATE chunkmap::core)
chunkmap_enable_warnings(chunkmap_ipc_test_host)
chunkmap_enable_sanitizers(chunkmap_ipc_test_host)

add_test(NAME chunkmap_core_tests COMMAND chunkmap_tests)
add_test(NAME chunkmap_cli_smoke COMMAND $<TARGET_FILE:chunkmap> --version)
add_test(
    NAME chunkmap_cli_json_contract
    COMMAND $<TARGET_FILE:chunkmap_ipc_test_host> ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:chunkmap>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_json_contract_test.cmake
)
add_test(
    NAME chunkmap_phase2_cli
    COMMAND $<TARGET_FILE:chunkmap_ipc_test_host> ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:chunkmap>
        "-DWORKSPACE=${CMAKE_CURRENT_BINARY_DIR}/phase2 cli workspace"
        -DCONCEPT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/chunk.png
        -P ${CMAKE_CURRENT_SOURCE_DIR}/phase2_cli_test.cmake
)
add_test(
    NAME chunkmap_phase3_cli
    COMMAND $<TARGET_FILE:chunkmap_ipc_test_host> ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:chunkmap>
        "-DWORKSPACE=${CMAKE_CURRENT_BINARY_DIR}/phase3 cli workspace"
        -DIMAGE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/chunk.png
        -P ${CMAKE_CURRENT_SOURCE_DIR}/phase3_cli_test.cmake
)
add_test(
    NAME chunkmap_cli_without_desktop
    COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:chunkmap>
        "-DWORKSPACE=${CMAKE_CURRENT_BINARY_DIR}/offline cli workspace"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_without_desktop_test.cmake
)

set_tests_properties(
    chunkmap_core_tests
    chunkmap_cli_json_contract
    chunkmap_phase2_cli
    chunkmap_phase3_cli
    chunkmap_cli_without_desktop
    PROPERTIES RUN_SERIAL TRUE
)

if(TARGET chunkmap_desktop)
    add_test(NAME chunkmap_desktop_smoke COMMAND $<TARGET_FILE:chunkmap_desktop> --smoke-test)
    set_tests_properties(chunkmap_desktop_smoke PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
    add_test(
        NAME chunkmap_desktop_project_smoke
        COMMAND $<TARGET_FILE:chunkmap_desktop>
            --smoke-test
            --workspace "${CMAKE_CURRENT_BINARY_DIR}/phase3 cli workspace"
            --project phase3-world
    )
    set_tests_properties(chunkmap_desktop_project_smoke PROPERTIES
        DEPENDS chunkmap_phase3_cli
        RUN_SERIAL TRUE
        TIMEOUT 20
    )
endif()
