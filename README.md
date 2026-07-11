# AI Chunk Map Studio

The project includes a C++17 Core, a Dear ImGui review application, and a CLI
that submits typed commands to the running Desktop process.

## Build

```bash
cmake -S . -B build
cmake --build build -j 8
```

## Run

```bash
./build/cli/chunkmap --version
./build/desktop/chunkmap_desktop.app/Contents/MacOS/chunkmap_desktop
```

## CLI Workflow

Start the Desktop app first. It is the single document host; project commands
return `desktop_not_running` without modifying files when Desktop is absent.

Create a project under `output/my-world/`:

```bash
./build/cli/chunkmap project init my-world \
  --concept /path/to/world-concept.png \
  --columns 3 \
  --rows 3
```

Import a user image into any chunk. The first imported image determines the
project chunk size; later imports use the same validation and deterministic
1px edge normalization as generated images:

```bash
./build/cli/chunkmap --project my-world \
  chunk import 2,2 --image /path/to/detail.png
```

Inspect and validate the project:

```bash
./build/cli/chunkmap --project my-world project status
./build/cli/chunkmap --project my-world project validate
```

Read or replace one Prompt:

```bash
./build/cli/chunkmap --project my-world prompt show 1,2
./build/cli/chunkmap --project my-world \
  prompt set 1,2 --file /path/to/prompt.md
```

Bulk Prompt import uses this shape and overwrites only the listed coordinates:

```json
{
  "prompts": [
    {"x": 0, "y": 0, "prompt": "Northwestern ice frontier."},
    {"x": 1, "y": 0, "prompt": "Central ice citadel."}
  ]
}
```

```bash
./build/cli/chunkmap --project my-world \
  prompts import --input /path/to/prompts.json
```

All commands accept `--workspace <path>` and `--json`.

JSON mode has a stable versioned envelope:

```json
{
  "schema_version": 1,
  "ok": true,
  "command": "project status",
  "project": "my-world",
  "data": {}
}
```

Failures use the same envelope with `ok: false` and an `error` object containing
`code` and `message`.

## Desktop Review App

On macOS, open a project directly from the command line:

```bash
./build/desktop/chunkmap_desktop.app/Contents/MacOS/chunkmap_desktop \
  --workspace /path/to/workspace \
  --project my-world
```

The Desktop app supports project creation/opening, a zoomable and pannable map,
chunk selection, Empty/Ready review, automatic Prompt writes, and seam
inspection. UI and CLI writes share one FIFO `DocumentCommandQueue`; completed
CLI commands refresh the app through `ChangeSet`, without file polling. Image
generation remains outside the app and can use either Codex with the CLI or a
manually operated external image generator.

The same workflow can be used without Codex or the CLI. Select a chunk with at
least one Ready orthogonal neighbor and use `Export Context` in the Chunk tab.
The app writes `template.png`, `mask.png`, `prompt.txt`, and `manifest.json`,
then reveals the context folder. `mask.png` uses white for the area to generate
and black for protected neighbor pixels. Generate in Stable Diffusion, ComfyUI,
or another external tool, then use the same `Import Image` or `Replace Image`
action used for map anchors. The result goes through dimension checks,
registration, seam rebuild, and official image replacement.

Export the Concept Map and region crops for prompt planning:

```bash
./build/cli/chunkmap --project my-world concept context --json
```

Export a self-contained generation context from all available orthogonal
neighbors, then write the generated image as the official chunk:

```bash
./build/cli/chunkmap --project my-world chunk context 1,2 --json
./build/cli/chunkmap --project my-world \
  chunk write 1,2 --image /path/to/generated.png --json
```

`chunk write` validates dimensions, allows deterministic 1px edge padding,
automatically registers small translations against all Ready neighbors, then
overwrites the official image and rebuilds `cache/composite.png`. Its JSON and
chunk metadata report the applied offset and before/after registration scores.
Generated writes require at least one Ready orthogonal neighbor. User imports
do not, so several disconnected reference images can anchor the map style.

Render, inspect a seam, or export the current map:

```bash
./build/cli/chunkmap --project my-world render
./build/cli/chunkmap --project my-world \
  seam inspect 1,2 --direction right --json
./build/cli/chunkmap --project my-world map export /path/to/map.png
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

Debug test builds enable AddressSanitizer and UndefinedBehaviorSanitizer by
default on supported non-MSVC compilers. Disable them when needed with:

```bash
cmake -S . -B build -DCHUNKMAP_ENABLE_SANITIZERS=OFF
```

Golden fixtures under `tests/golden/` lock the deterministic four-side template
and 3x3 composite pixel layouts.

## Core And CLI Build

```bash
cmake -S . -B build-cli -DCHUNKMAP_BUILD_DESKTOP=OFF
cmake --build build-cli -j 8
```

Generated map projects will live under `output/<project-name>/`.

This build omits the Desktop host, so only local CLI commands such as
`--help` and `--version` run; project commands require a Desktop-enabled build
with `chunkmap_desktop` running.
