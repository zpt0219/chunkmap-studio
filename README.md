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

Query the project currently open in Desktop without supplying `--project`:

```bash
./build/cli/chunkmap --workspace "$PWD" --json project current
```

The command is read-only and returns `no_project_open` when Desktop has no open
project. It reports the active project's name and workspace and never loads or
switches a project.

Before importing the first chunk image, the Concept grid can be corrected without
recreating the project. Local chunk prompts must still be empty; the Global Prompt
is preserved:

```bash
./build/cli/chunkmap --project my-world \
  project grid --columns 4 --rows 4
```

Import a user image into any chunk. The first imported image determines the
project chunk size; later imports and generated images must match it exactly.
The PNG bytes are kept as the formal source and are never rewritten by alignment
or seam editing:

```bash
./build/cli/chunkmap --project my-world \
  chunk import 2,2 --image /path/to/detail.png
```

Delete a formal chunk image and return its coordinate to `Empty`:

```bash
./build/cli/chunkmap --project my-world chunk remove 2,2 --yes
```

The Chunk Inspector offers the same operation through `Delete Chunk`, with a
confirmation prompt. Both routes submit the same Desktop-hosted command.

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

Prompt generation follows [`docs/PROMPT_AUTHORING_GUIDE.md`](docs/PROMPT_AUTHORING_GUIDE.md).
The guide separates reusable Global visual rules from short Local regional intent,
defines gameplay-scale walkability and collision-boundary readability, interprets
Concept icons semantically rather than literally, and leaves minor objects and
decorative detail to the image model. Agents must also read it before image
generation so the execution wrapper cannot turn authored tilemap Prompts into a
panoramic or concept-art composition.

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
mode with both `authoring_guide` and `seed_image`. Codex must read the guide,
analyze the formal Seed, describe only its reusable visual style, and write the
result with `global-prompt set`. The seed identity is not persisted; users can
edit the Global Prompt at any time.

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

The Chunk Inspector has two momentary Concept comparison controls. Hold
`This Chunk` to reveal the selected coordinate's Concept region across its complete
footprint, including overlap, or hold `Full Map` to reveal the complete Concept Map.
Releasing either button immediately restores generated images. These controls do
not change Ready state, exports, generation context, session state, or project files.
`Export Concept Slice...` writes the selected coordinate's exact Concept grid region
to a user-selected PNG outside the project without exporting the other regions.

The application menu bar owns project lifecycle actions: use File for New,
Open, Reload, Export Full Map, and Quit; Project for Project Settings and the
dedicated Change Grid action; and View for map overlays, panel visibility, and
layout reset. Window size, position, and docked panel layout persist in the user's
platform configuration directory; smoke tests do not read or write these settings.
The compact Map Controls
panel provides Reset Scale, Fit Map, and one Overlays switch for Grid, Coordinates,
and Seams. Reset Scale restores 1:1 at the map's top-left; Fit Map uses a continuous
scale and centers the map. The displayed `Scale: 0.000x` value always reflects the
actual view. Mouse-wheel zoom moves through discrete scale presets while preserving
the world point under the cursor. Drag with either the middle or right mouse button
to pan. Coordinate labels remain visible at every scale while Overlays is enabled.
Project > Change Grid exposes Concept Grid columns and rows in a dedicated dialog
until the first chunk image establishes the project chunk size. Grid changes keep
the Concept and Global Prompt, and require the Local Chunk Prompts to be empty.

The Log panel follows the conventional three-line mouse-wheel step so short log
views remain controllable. Trackpad input stays continuous and proportional to the
platform-provided wheel delta.

The Prompt Inspector edits both source texts with explicit labels: the Global
Prompt shared by every chunk and the Local Chunk Prompt for the selected
coordinate. Edits autosave after 60 seconds without typing or immediately when
the field loses focus. Long lines soft-wrap to the editor width without changing
the stored Prompt. Project Settings also exposes the same Global Prompt.
A chunk generation context exports both sources and a combined `prompt.txt`
alongside its template and mask.

The same workflow can be used without Codex or the CLI. Select a chunk with at
least one Ready orthogonal neighbor and use `Export Context` in the Chunk tab.
The app writes `template.png`, `mask.png`, `prompt.txt`, and `manifest.json`,
under `.chunkmap/handoff/<project>/`, then reveals the context folder. `mask.png` uses
white for generation and black only for the outermost 1px neighbor anchor edge.
Generate in Stable Diffusion, ComfyUI,
or another external tool, then use the same `Import Image` or `Replace Image`
action used for map anchors. User imports remain byte-faithful after dimension
validation. Alignment stores a non-destructive placement; Seam Editor stores an
editable polyline boundary and feather width while previewing only the overlap band.

Export the Concept Map and region crops for prompt planning:

```bash
./build/cli/chunkmap --project my-world concept context --json
```

The response and manifest include an `authoring_guide` path. The Core embeds the
versioned Markdown source and exports it to the project handoff, so this workflow
remains self-contained even when the map workspace is outside the source tree.

Export a self-contained generation context from all available orthogonal
neighbors, then write the generated image as the official chunk:

```bash
./build/cli/chunkmap --project my-world chunk context 1,2 --json
./build/cli/chunkmap --project my-world \
  chunk write 1,2 --image /path/to/generated.png --json
```

`chunk write` validates exact dimensions, copies the generated PNG as the formal
source, and searches for a conservative whole-image translation against Ready
neighbors. The chosen offset is stored as placement data instead of being baked
into the PNG. Its JSON response reports transient registration diagnostics; it does
not build a Composite or derived image cache.
Generated writes require at least one Ready orthogonal neighbor. User imports
do not and are never silently translated, so several disconnected reference
images can anchor the map style. For any Ready Chunk, the Desktop Alignment
controls provide Horizontal/Vertical preview, an `Auto` suggestion, `Reset`,
and `Save Placement`; saving changes only placement parameters. Auto runs both a
low-resolution 2D matcher and a row/column projection matcher, displays their
offsets and confidence results, and lets the reviewer preview either result.

Inspect a seam in memory:

```bash
./build/cli/chunkmap --project my-world \
  seam inspect 1,2 --direction right --json
```

The Desktop lays out original Ready chunk textures using their placements, then
draws small right and bottom seam patches derived from editable seam parameters.
The project deliberately has no Composite file and never exports automatically.
Use File > Export Full Map in Desktop, or explicitly export a full-resolution
RGBA PNG through the running Desktop host:

```bash
./build/cli/chunkmap --workspace "$PWD" --project my-world \
  map export /absolute/path/to/my-world.png
```

Add `--force` to replace an existing output. Empty chunks remain transparent,
the output must be outside `output/my-world/`, and the streaming exporter does
not create project cache or Composite files. Desktop shows a modal progress
window while either UI or CLI export work is running. The completed design is archived in
[`docs/historical/FULL_MAP_EXPORT_DESIGN.md`](docs/historical/FULL_MAP_EXPORT_DESIGN.md).

The persisted project is intentionally sparse:

```text
output/my-world/
  project.json
  concept.png
  global_prompt.md       # only when non-empty
  prompts/<x>_<y>.md     # only when non-empty
  chunks/<x>_<y>.png     # only when Ready
  placements.json        # only when any placement is non-zero
  seams/<key>.json       # only for user-edited seam overrides
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
