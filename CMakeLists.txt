cmake_minimum_required(VERSION 3.20)

project(chunkmap_studio LANGUAGES C CXX)

# CMake 4 removes compatibility modes older than 3.5. Some pinned third-party
# projects still declare an older minimum even though they build correctly.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

function(chunkmap_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(DEFINED FETCHCONTENT_BASE_DIR AND
   FETCHCONTENT_BASE_DIR STREQUAL "${CMAKE_SOURCE_DIR}/third_party/_deps")
    unset(FETCHCONTENT_BASE_DIR CACHE)
    unset(FETCHCONTENT_BASE_DIR)
endif()
include(FetchContent)

option(CHUNKMAP_BUILD_DESKTOP "Build the Dear ImGui desktop application" ON)
option(CHUNKMAP_BUILD_TESTS "Build tests" ON)
option(CHUNKMAP_ENABLE_SANITIZERS "Enable ASan and UBSan in Debug builds" ON)

function(chunkmap_enable_sanitizers target_name)
    if(CHUNKMAP_ENABLE_SANITIZERS AND CHUNKMAP_BUILD_TESTS AND
       NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${target_name} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target_name} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
    endif()
endfunction()

add_subdirectory(src)
add_subdirectory(cli)

if(CHUNKMAP_BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()

if(CHUNKMAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
