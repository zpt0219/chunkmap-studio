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

Switch a running Desktop instance to an existing project:

```bash
./build/cli/chunkmap --workspace "$PWD" project open my-world
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

Read or replace the project-wide visual style Prompt:

```bash
./build/cli/chunkmap --project my-world global-prompt show
./build/cli/chunkmap --project my-world \
  global-prompt set --file /path/to/global-prompt.md
```

The first successful `chunk import` reports a `global_prompt_action` in JSON
mode. Codex should analyze the returned formal `seed_image`, describe only its
reusable visual style, and write the result with `global-prompt set`. The seed
identity is not persisted; users can edit the Global Prompt at any time.

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

The Project Settings modal edits the Global Prompt shared by every chunk. A
chunk generation context exports the Global Prompt, the coordinate's Chunk
Prompt, and a combined `prompt.txt` alongside its template and mask.

The same workflow can be used without Codex or the CLI. Select a chunk with at
least one Ready orthogonal neighbor and use `Export Context` in the Chunk tab.
The app writes `template.png`, `mask.png`, `prompt.txt`, and `manifest.json`,
under `.chunkmap/handoff/<project>/`, then reveals the context folder. `mask.png` uses white for the area to generate
and black for protected neighbor pixels. Generate in Stable Diffusion, ComfyUI,
or another external tool, then use the same `Import Image` or `Replace Image`
action used for map anchors. The result goes through dimension checks,
deterministic 1px normalization, protected-neighbor pixel restoration, and
official image replacement.

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
restores every protected overlap pixel from Ready neighbors, then overwrites
the one formal chunk image. It does not build a Composite, metadata, or Seam
cache.
Generated writes require at least one Ready orthogonal neighbor. User imports
do not, so several disconnected reference images can anchor the map style.

Inspect a seam in memory:

```bash
./build/cli/chunkmap --project my-world \
  seam inspect 1,2 --direction right --json
```

The Desktop draws Ready chunk textures directly with their configured overlap.
The project deliberately has no Composite file and no automatic whole-map
export. A future explicit export feature will write outside the project.

The persisted project is intentionally sparse:

```text
output/my-world/
  project.json
  concept.png
  global_prompt.md       # only when non-empty
  prompts/<x>_<y>.md     # only when non-empty
  chunks/<x>_<y>.png     # only when Ready
```

Desktop owns a long-lived in-memory `ProjectDocument`. CLI commands execute
against that same document through IPC. Use Desktop Reload to intentionally
re-read external file changes.

## Test

```bash
ctest --test-dir build --output-on-failure
```

Debug test builds enable AddressSanitizer and UndefinedBehaviorSanitizer by
default on supported non-MSVC compilers. Disable them when needed with:

```bash
cmake -S . -B build -DCHUNKMAP_ENABLE_SANITIZERS=OFF
```

Golden fixtures under `tests/golden/` lock the deterministic four-side template.

## Core And CLI Build

```bash
cmake -S . -B build-cli -DCHUNKMAP_BUILD_DESKTOP=OFF
cmake --build build-cli -j 8
```

Generated map projects will live under `output/<project-name>/`.

This build omits the Desktop host, so only local CLI commands such as
`--help` and `--version` run; project commands require a Desktop-enabled build
with `chunkmap_desktop` running.
