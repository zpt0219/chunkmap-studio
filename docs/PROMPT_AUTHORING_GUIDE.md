# ChunkMap Prompt Authoring Guide

Specification version: 1

This file is the semantic source of truth for generating or revising ChunkMap
Global and Local Prompts. Read it completely before writing either Prompt type.
`prompts.schema.json` validates the JSON transport shape; this guide defines what
the Prompt text means.

## 1. Required workflow

1. Read this guide completely.
2. Read the current project status and existing Global/Local Prompts.
3. Use the first formal Ready Chunk as the visual-style reference for the Global
   Prompt. Do not use the Concept Map as a detailed image-generation reference.
4. Use the full Concept Map and its regions only to understand world structure and
   write Local Prompts.
5. Generate or revise the Global Prompt and Local Prompts according to the separate
   responsibilities below.
6. Validate coordinates and JSON structure with `prompts.schema.json` before import.
7. Re-read a sample of the combined Prompts and remove duplicated or conflicting
   instructions.

If no Ready Chunk exists, do not invent a definitive visual style unless the user
explicitly supplies one. Local Prompts may still be written from the Concept Map.

## 2. Global Prompt: how the whole map is drawn

The Global Prompt contains only visual rules that should remain stable for every
Chunk:

- camera angle, projection, orientation, and framing;
- rendering medium, pixel density, edge treatment, and texture language;
- global palette, lighting, contrast, and atmosphere;
- consistent scale relationships among terrain, vegetation, buildings, and people;
- architectural orientation and other repeated construction rules;
- readability rules for roads, plazas, shorelines, cliffs, and waterways;
- seamless edge continuation and global exclusions.

Do not include a specific town, landmark, biome location, road route, shoreline
shape, or other Chunk-local layout. Do not describe the Seed Chunk's contents.

Recommended length: 100-180 English words. Prefer one compact paragraph. The
Prompt should be reusable without knowing which coordinate is being generated.

## 3. Local Prompt: what this region is

The Local Prompt is a short regional brief. It should describe only:

- the dominant terrain or biome;
- the region's broad purpose, such as village, city, wilderness, academy, port, or
  travel corridor;
- approximate settlement or wilderness density;
- one major landmark only when it defines the region;
- major roads, rivers, coastlines, cliffs, elevation bands, or other connections
  that must continue through Chunk edges.

Recommended length: 25-60 English words in one or two sentences.

Preserve model freedom. Do not prescribe:

- exact counts of houses, trees, rocks, props, people, or decorations;
- exact placement of minor objects;
- roof colors, flower types, fence layouts, signs, crates, lamps, or similar detail;
- an exhaustive inventory of everything visible in the Concept region;
- Global rendering rules already supplied by the Global Prompt.

Use qualitative language such as `compact`, `substantial`, `sparse`, `dense`,
`broad`, or `winding` instead of exact counts and measurements.

## 4. Interpret Concept symbols semantically

The Concept Map is a planning diagram, not a literal object-placement blueprint.
A small icon may represent an entire region:

- one house labeled as a village means a village distributed through the region;
- one civic building may mean a full town or city district;
- one academy building may mean a campus and surrounding settlement;
- one tree may identify a forest town organized around an ancient tree;
- one tower may represent a tower complex on an island or plateau.

Expand symbols to the scale implied by their label and world role. Do not reproduce
the icon count literally. Conversely, do not turn a minor unlabeled prop into a
major landmark.

Text labels are semantic input only. Generated map images must not render labels,
logos, coordinate text, or UI.

## 5. Continuity and priority

Only describe edge continuity when it affects large map structure. A Local Prompt
may state that a coast continues west, a mountain wall spans north-south, or a main
route exits east. Leave minor paths and decorative transitions to the model and the
neighbor template.

Instruction priority is:

1. protected neighbor pixels and generation mask;
2. Global Prompt visual rules;
3. Local Prompt regional intent;
4. model-selected detail and variation.

When Global and Local text conflict, revise the Local Prompt instead of adding an
exception to it.

## 6. Examples

Bad Local Prompt:

> Draw exactly one red-roof house at the center, six pine trees on the left, three
> flower beds, two benches, a sign, four lamps, and a straight eight-tile road.

Good Local Prompt:

> A quiet forest village spread through open meadow, surrounded by dense woodland,
> with the main regional route continuing east and west.

Bad Local Prompt:

> Bright handheld pixel art with crisp outlines, top-down camera, saturated colors,
> and no text. Add a desert city.

Good Local Prompt:

> A substantial desert city occupying a sheltered basin between layered canyon
> walls, with the main trade route continuing toward the eastern coast.

The first bad example over-specifies model-selected detail. The second repeats
Global rules instead of describing the region.

## 7. Local Prompt JSON transport

Bulk Local Prompt import uses:

```json
{
  "prompts": [
    {"x": 0, "y": 0, "prompt": "A broad northern ice coast..."}
  ]
}
```

Every project coordinate should appear exactly once when generating a complete
Prompt set. Coordinate `(x, y)` uses zero-based column and row indices.

## 8. Final checklist

Before writing Prompts, confirm:

- the guide was read before the Seed and Concept were interpreted;
- the Global Prompt contains style but no local geography;
- each Local Prompt contains regional intent but no repeated style paragraph;
- Concept icons were interpreted as semantic regions rather than literal counts;
- minor objects and decorative details remain unspecified;
- only important cross-edge terrain and routes are constrained;
- text, labels, logos, UI, and diagonal architecture are excluded globally when
  appropriate to the selected visual style;
- all coordinates are valid, unique, and structurally accepted by the schema.
