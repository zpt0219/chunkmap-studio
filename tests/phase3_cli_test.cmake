if(NOT DEFINED CLI OR NOT DEFINED WORKSPACE OR NOT DEFINED IMAGE)
    message(FATAL_ERROR "CLI, WORKSPACE and IMAGE are required")
endif()

file(REMOVE_RECURSE "${WORKSPACE}")
file(MAKE_DIRECTORY "${WORKSPACE}")

function(run_chunkmap)
    execute_process(
        COMMAND "${CLI}" --workspace "${WORKSPACE}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "chunkmap failed (${result}): ${ARGN}\nstdout: ${output}\nstderr: ${error}")
    endif()
    set(LAST_OUTPUT "${output}" PARENT_SCOPE)
endfunction()

run_chunkmap(
    --json project init phase3-world
    --concept "${IMAGE}" --columns 2 --rows 2
)
run_chunkmap(--project phase3-world --json concept context)
if(NOT LAST_OUTPUT MATCHES "\"ok\":true")
    message(FATAL_ERROR "Concept context did not return JSON success: ${LAST_OUTPUT}")
endif()
if(NOT LAST_OUTPUT MATCHES "authoring_guide")
    message(FATAL_ERROR "Concept context did not return its Prompt guide: ${LAST_OUTPUT}")
endif()
set(handoff_root "${WORKSPACE}/.chunkmap/handoff/phase3-world")
file(READ "${handoff_root}/prompt-authoring-guide.md" authoring_guide)
if(NOT authoring_guide MATCHES "Specification version: 2" OR
   NOT authoring_guide MATCHES "gameplay-ready overworld tilemap" OR
   NOT authoring_guide MATCHES "Generation-time Prompt discipline")
    message(FATAL_ERROR "Exported Prompt guide is not the required Version 2")
endif()

run_chunkmap(--project phase3-world --json global-prompt show)
string(JSON initial_global_prompt GET "${LAST_OUTPUT}" data prompt)
if(NOT initial_global_prompt STREQUAL "")
    message(FATAL_ERROR "Initial Global Prompt must be empty: ${LAST_OUTPUT}")
endif()
file(WRITE "${WORKSPACE}/global-prompt.md" "Top-down GBA pixel art")
run_chunkmap(--project phase3-world --json global-prompt set
    --file "${WORKSPACE}/global-prompt.md")
run_chunkmap(--project phase3-world --json global-prompt show)
if(NOT LAST_OUTPUT MATCHES "Top-down GBA pixel art")
    message(FATAL_ERROR "Global Prompt did not round trip: ${LAST_OUTPUT}")
endif()

run_chunkmap(--project phase3-world --json chunk import 1,1 --image "${IMAGE}")
if(NOT LAST_OUTPUT MATCHES "global_prompt_action" OR
   NOT LAST_OUTPUT MATCHES "first_chunk_imported" OR
   NOT LAST_OUTPUT MATCHES "authoring_guide")
    message(FATAL_ERROR "First import did not request a Global Prompt: ${LAST_OUTPUT}")
endif()
run_chunkmap(--project phase3-world --json chunk import 1,1 --image "${IMAGE}")
if(LAST_OUTPUT MATCHES "global_prompt_action")
    message(FATAL_ERROR "Later import repeated the Global Prompt action: ${LAST_OUTPUT}")
endif()
execute_process(
    COMMAND "${CLI}" --workspace "${WORKSPACE}" --project phase3-world --json chunk context 0,0
    RESULT_VARIABLE no_neighbor_result
    OUTPUT_VARIABLE no_neighbor_output
)
if(no_neighbor_result EQUAL 0 OR NOT no_neighbor_output MATCHES "no_ready_neighbor")
    message(FATAL_ERROR "No-neighbor context should return structured failure: ${no_neighbor_output}")
endif()

execute_process(
    COMMAND "${CLI}" --workspace "${WORKSPACE}" --project phase3-world --json
        chunk write 0,0 --image "${IMAGE}"
    RESULT_VARIABLE no_neighbor_write_result
    OUTPUT_VARIABLE no_neighbor_write_output
)
if(no_neighbor_write_result EQUAL 0 OR NOT no_neighbor_write_output MATCHES "no_ready_neighbor")
    message(FATAL_ERROR "No-neighbor generated write should fail: ${no_neighbor_write_output}")
endif()

file(WRITE "${WORKSPACE}/chunk-prompt.md" "A forest path beside a river")
run_chunkmap(--project phase3-world prompt set 0,1 --file "${WORKSPACE}/chunk-prompt.md")
run_chunkmap(--project phase3-world --json chunk context 0,1)
if(NOT LAST_OUTPUT MATCHES "\"mask\"")
    message(FATAL_ERROR "Chunk context did not report an inpaint mask: ${LAST_OUTPUT}")
endif()
file(READ "${handoff_root}/chunk_0_1/prompt.txt" combined_prompt)
if(NOT combined_prompt MATCHES "GLOBAL VISUAL STYLE" OR
   NOT combined_prompt MATCHES "Top-down GBA pixel art" OR
   NOT combined_prompt MATCHES "CHUNK CONTENT" OR
   NOT combined_prompt MATCHES "A forest path beside a river")
    message(FATAL_ERROR "Chunk context did not include Global Prompt: ${combined_prompt}")
endif()
run_chunkmap(--project phase3-world --json chunk write 0,1 --image "${IMAGE}")
if(LAST_OUTPUT MATCHES "registration" OR LAST_OUTPUT MATCHES "composite")
    message(FATAL_ERROR "Chunk write leaked removed derived state: ${LAST_OUTPUT}")
endif()
run_chunkmap(--project phase3-world --json seam inspect 0,1 --direction right)
run_chunkmap(--project phase3-world project validate)

set(full_map_export "${WORKSPACE}/phase3-full-map.png")
run_chunkmap(--project phase3-world --json map export "${full_map_export}")
if(NOT EXISTS "${full_map_export}" OR
   NOT LAST_OUTPUT MATCHES "\"size\":\\[")
    message(FATAL_ERROR "Full map export did not create a PNG: ${LAST_OUTPUT}")
endif()
execute_process(
    COMMAND "${CLI}" --workspace "${WORKSPACE}" --project phase3-world --json
        map export "${full_map_export}"
    RESULT_VARIABLE existing_export_result
    OUTPUT_VARIABLE existing_export_output
)
if(existing_export_result EQUAL 0 OR NOT existing_export_output MATCHES "export_exists")
    message(FATAL_ERROR "Existing export should require --force: ${existing_export_output}")
endif()
run_chunkmap(--project phase3-world --json map export "${full_map_export}" --force)

set(project_root "${WORKSPACE}/output/phase3-world")
foreach(required_file
        "project.json"
        "concept.png"
        "global_prompt.md"
        "prompts/0_1.md"
        "chunks/0_1.png"
        "chunks/1_1.png")
    if(NOT EXISTS "${project_root}/${required_file}")
        message(FATAL_ERROR "Expected Phase 3 file missing: ${project_root}/${required_file}")
    endif()
endforeach()

foreach(required_handoff
        "prompt-authoring-guide.md"
        "concept/regions/0_0.png"
        "concept/regions/1_1.png"
        "concept/manifest.json"
        "concept/prompts.schema.json"
        "chunk_0_1/template.png"
        "chunk_0_1/mask.png"
        "chunk_0_1/global_prompt.txt"
        "chunk_0_1/chunk_prompt.txt"
        "chunk_0_1/prompt.txt"
        "chunk_0_1/manifest.json")
    if(NOT EXISTS "${handoff_root}/${required_handoff}")
        message(FATAL_ERROR "Expected handoff file missing: ${handoff_root}/${required_handoff}")
    endif()
endforeach()

foreach(forbidden_path "cache" "context" "concept" "chunks/0_1" "chunks/1_1")
    if(EXISTS "${project_root}/${forbidden_path}")
        message(FATAL_ERROR "Derived project path must not exist: ${project_root}/${forbidden_path}")
    endif()
endforeach()

file(READ "${handoff_root}/chunk_0_1/manifest.json" chunk_manifest)
if(chunk_manifest MATCHES "concept_image" OR chunk_manifest MATCHES "concept/regions")
    message(FATAL_ERROR "Chunk context leaked a concept image reference")
endif()
