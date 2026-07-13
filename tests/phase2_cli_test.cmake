if(NOT DEFINED CLI OR NOT DEFINED WORKSPACE OR NOT DEFINED CONCEPT)
    message(FATAL_ERROR "CLI, WORKSPACE and CONCEPT are required")
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
        message(FATAL_ERROR "chunkmap command failed (${result}): ${ARGN}\nstdout: ${output}\nstderr: ${error}")
    endif()
endfunction()

run_chunkmap(
    --json project init cli-world
    --concept "${CONCEPT}"
    --columns 2
    --rows 2
)

run_chunkmap(--json project open cli-world)
execute_process(
    COMMAND "${CLI}" --json project current
    RESULT_VARIABLE current_result
    OUTPUT_VARIABLE current_output
    ERROR_VARIABLE current_error
)
if(NOT current_result EQUAL 0 OR NOT current_error STREQUAL "")
    message(FATAL_ERROR "project current failed: ${current_error}")
endif()
string(JSON current_name GET "${current_output}" data name)
string(JSON current_envelope_project GET "${current_output}" project)
if(NOT current_name STREQUAL "cli-world" OR
   NOT current_envelope_project STREQUAL "cli-world")
    message(FATAL_ERROR "project current returned the wrong project: ${current_output}")
endif()
run_chunkmap(--project cli-world --json project grid --columns 3 --rows 2)

run_chunkmap(--project cli-world --json chunk import 1,1 --image "${CONCEPT}")
run_chunkmap(--project cli-world --json chunk import 0,0 --image "${CONCEPT}")

file(WRITE "${WORKSPACE}/prompt.md" "manual prompt")
run_chunkmap(
    --project cli-world prompt set 0,0 --file "${WORKSPACE}/prompt.md"
)

file(WRITE "${WORKSPACE}/prompts.json"
    "{\"prompts\":[{\"x\":0,\"y\":0,\"prompt\":\"AI overwrite\"},{\"x\":1,\"y\":0,\"prompt\":\"east\"}]}"
)
run_chunkmap(
    --project cli-world prompts import --input "${WORKSPACE}/prompts.json"
)

run_chunkmap(--project cli-world --json project status)
run_chunkmap(--project cli-world project validate)

set(project_root "${WORKSPACE}/output/cli-world")
file(READ "${project_root}/project.json" project_json)
string(JSON project_columns GET "${project_json}" columns)
string(JSON project_rows GET "${project_json}" rows)
if(NOT project_columns EQUAL 3 OR NOT project_rows EQUAL 2)
    message(FATAL_ERROR "Project grid update was not persisted: ${project_json}")
endif()
foreach(required_file
        "project.json"
        "concept.png"
        "prompts/0_0.md"
        "chunks/0_0.png"
        "prompts/1_0.md"
        "chunks/1_1.png")
    if(NOT EXISTS "${project_root}/${required_file}")
        message(FATAL_ERROR "Expected file missing: ${project_root}/${required_file}")
    endif()
endforeach()

file(READ "${project_root}/prompts/0_0.md" prompt_0_0)
if(NOT prompt_0_0 STREQUAL "AI overwrite")
    message(FATAL_ERROR "Prompt overwrite failed: ${prompt_0_0}")
endif()

foreach(forbidden_path "cache" "context" "concept" "chunks/0_0")
    if(EXISTS "${project_root}/${forbidden_path}")
        message(FATAL_ERROR "Derived project path must not exist: ${project_root}/${forbidden_path}")
    endif()
endforeach()
