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

run_chunkmap(--project phase3-world chunk import 1,1 --image "${IMAGE}")
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

run_chunkmap(--project phase3-world --json chunk context 0,1)
if(NOT LAST_OUTPUT MATCHES "\"mask\"")
    message(FATAL_ERROR "Chunk context did not report an inpaint mask: ${LAST_OUTPUT}")
endif()
run_chunkmap(--project phase3-world --json chunk write 0,1 --image "${IMAGE}")
if(NOT LAST_OUTPUT MATCHES "\"registration\"")
    message(FATAL_ERROR "Chunk write did not report registration: ${LAST_OUTPUT}")
endif()
run_chunkmap(--project phase3-world --json seam inspect 0,1 --direction right)
run_chunkmap(--project phase3-world --json render)
run_chunkmap(--project phase3-world --json map export "${WORKSPACE}/exported-map.png")
run_chunkmap(--project phase3-world project validate)

set(project_root "${WORKSPACE}/output/phase3-world")
foreach(required_file
        "concept/regions/0_0.png"
        "concept/regions/1_1.png"
        "context/concept/manifest.json"
        "context/concept/prompts.schema.json"
        "context/chunk_0_1/template.png"
        "context/chunk_0_1/mask.png"
        "context/chunk_0_1/prompt.txt"
        "context/chunk_0_1/manifest.json"
        "chunks/0_1/image.png"
        "chunks/0_1/metadata.json"
        "cache/composite.png"
        "cache/seams/0_1_right/metrics.json")
    if(NOT EXISTS "${project_root}/${required_file}")
        message(FATAL_ERROR "Expected Phase 3 file missing: ${project_root}/${required_file}")
    endif()
endforeach()

if(NOT EXISTS "${WORKSPACE}/exported-map.png")
    message(FATAL_ERROR "Map export is missing")
endif()

file(READ "${project_root}/context/chunk_0_1/manifest.json" chunk_manifest)
if(chunk_manifest MATCHES "concept_image" OR chunk_manifest MATCHES "concept/regions")
    message(FATAL_ERROR "Chunk context leaked a concept image reference")
endif()
