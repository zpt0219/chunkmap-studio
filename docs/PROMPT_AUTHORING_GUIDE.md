# ChunkMap Prompt Authoring Guide

Specification version: 2

This file is the semantic source of truth for authoring ChunkMap Global and Local
Prompts and for turning them into an image-generation request. Read it completely
before writing either Prompt type or generating any Chunk image.
`prompts.schema.json` validates the JSON transport shape; this guide defines what
the Prompt text means and how it must be used.

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
8. When generating a Chunk, add only the operational instructions required for the
   template, mask, and expected size. Do not reinterpret or expand its composition.
9. Validate both technical continuity and gameplay-scale visual readability before
   considering the Chunk complete.

If no Ready Chunk exists, do not invent a definitive visual style unless the user
explicitly supplies one. Local Prompts may still be written from the Concept Map.

## 2. Global Prompt: visual style and gameplay-space contract

The Global Prompt contains rules that should remain stable for every Chunk. It must
define both how the map is drawn and why the result reads as a playable tilemap:

- a fixed top-down gameplay camera, projection, orientation, and framing;
- rendering medium, pixel density, edge treatment, and texture language;
- global palette, lighting, contrast, and atmosphere;
- consistent scale relationships among terrain, vegetation, buildings, and people;
- architectural orientation and other repeated construction rules;
- connected walkable ground, readable entrances, routes, stairs, bridges, and passes;
- mountains, cliffs, dense forest, water, and hazards as readable collision boundaries
  around traversable space instead of oversized scenery that consumes the image;
- readability rules for roads, plazas, shorelines, cliffs, and waterways;
- seamless edge continuation and global exclusions.

Use direct gameplay language such as `gameplay-ready overworld tilemap`, `fixed
top-down RPG gameplay camera`, `practical player-sprite scale`, `connected
walkable ground`, and `readable collision boundaries` when it matches the project.

Explicitly exclude panoramic landscape composition, world-map overview,
concept-art framing, horizon, dramatic perspective depth, oversized terrain
landmarks, and large decorative areas without gameplay-scale traversal. Merely
saying `top-down pixel art` is not enough to prevent an illustration-like result.

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
  that must continue through Chunk edges;
- the major traversable route or connected walkable area when the region is
  wilderness, mountain, coast, desert, swamp, or another naturally obstructed biome.

Recommended length: 25-60 English words in one or two sentences.

Preserve model freedom. Do not prescribe:

- exact counts of houses, trees, rocks, props, people, or decorations;
- exact placement of minor objects;
- roof colors, flower types, fence layouts, signs, crates, lamps, or similar detail;
- an exhaustive inventory of everything visible in the Concept region;
- Global rendering or gameplay rules already supplied by the Global Prompt.

Use qualitative language such as `compact`, `substantial`, `sparse`, `dense`,
`open`, or `winding` instead of exact counts and measurements. Prefer
`traversable forest route`, `connected coastal shelf`, or `readable mountain
pass` over scenic composition cues such as `large-scale transition`, `vast
panorama`, `large mountain masses`, or `broad terrain bands`.

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
2. Global Prompt visual and gameplay-space rules;
3. Local Prompt regional intent;
4. model-selected detail and variation.

When Global and Local text conflict, revise the Local Prompt instead of adding an
exception to it.

## 6. Generation-time Prompt discipline

Before generating a Chunk, read this guide, then read the fresh `manifest.json`,
`prompt.txt`, `template.png`, and `mask.png` returned by `chunk context`.

- Use `template.png` as the edit target and `mask.png` as the generation boundary.
- Preserve black-mask/protected pixels exactly and generate only white-mask pixels.
- Use `prompt.txt` as the semantic source. It already combines the Global and Local
  Prompts.
- A generation-time wrapper may identify input roles, require the manifest
  `expected_size`, repeat protected-pixel rules, and request seamless continuation.
- Do not add new subjects, landmarks, visual styles, camera language, or composition
  scale beyond the stored Global and Local Prompts.
- In particular, do not introduce `large-scale`, `panoramic`, `large mountain
  masses`, `broad terrain bands`, or similar scenic language unless it already
  exists intentionally in the stored Prompt.
- Never use the Concept Map or Concept region crop as a detailed image reference.

The execution wrapper must preserve the authored intent, not become a third Prompt
layer with independent creative direction.

## 7. Visual acceptance: continuity and playability

Evaluate each generated Chunk at two levels:

1. Technical: expected dimensions, successful formal `chunk write`, protected
   pixels, Ready state, project validation, and relevant Seam results.
2. Visual: fixed gameplay camera, consistent player/environment scale, connected
   walkable ground, readable routes and entrances, clear collision boundaries, and
   no panoramic or concept-art composition.

A Seam difference of `0.0` proves only that overlap pixels match. It does not prove
that scale, style, traversal, or composition is correct.

Cities are weak tests of the gameplay-space contract because streets and plazas
naturally create walkable structure. Forest, mountain, coast, desert, and other
wilderness Chunks are the best canaries. Reject or regenerate a wilderness Chunk
that reads as a scenic overview even when its Seam is perfect.

## 8. Regenerating adjacent drifted Chunks

When the user asks to replace multiple adjacent Chunks that share the same visual
drift, do not let their old overlap pixels preserve that drift:

1. Remove the affected formal images through `chunk remove <x,y> --yes`; never
   delete files directly from `output/`.
2. Keep at least one good Ready neighbor as the visual and scale anchor.
3. Regenerate in dependency order, starting beside the good anchor.
4. Export a fresh `chunk context` after every successful write so the next Chunk
   receives the new overlap pixels.
5. Re-run technical and visual acceptance for the complete regenerated group.

Do this only when replacement is requested. Do not add candidate history, revision
state, or provenance to the project.

## 9. Examples

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

Bad wilderness Local Prompt:

> A large-scale transition with vast forest bands and massive snowy mountain forms.

Good wilderness Local Prompt:

> A traversable forest route rising into a snowy mountain pass, with connected
> clearings carrying the regional path between the two biomes.

The bad wilderness example specifies a scenic composition. The good example defines
regional terrain and connectivity while leaving the detailed layout to the model.

## 10. Local Prompt JSON transport

Bulk Local Prompt import uses:

```json
{
  "prompts": [
    {"x": 0, "y": 0, "prompt": "A traversable northern ice coast..."}
  ]
}
```

Every project coordinate should appear exactly once when generating a complete
Prompt set. Coordinate `(x, y)` uses zero-based column and row indices.

## 11. Final checklist

Before writing Prompts or generating a Chunk, confirm:

- the guide was read before the Seed, Concept, or generation context was interpreted;
- the Global Prompt contains stable style and gameplay rules but no local geography;
- the Global Prompt defines gameplay camera, practical scale, walkable space,
  collision-boundary readability, and anti-panorama exclusions;
- each Local Prompt contains regional intent but no repeated Global paragraph;
- obstructed wilderness regions state their major traversable connection without
  prescribing minor layout;
- Concept icons were interpreted as semantic regions rather than literal counts;
- minor objects and decorative details remain unspecified;
- only important cross-edge terrain and routes are constrained;
- the generation-time wrapper adds operational constraints but no new creative
  composition language;
- text, labels, logos, UI, and diagonal architecture are excluded globally when
  appropriate to the selected visual style;
- technical validation and gameplay-scale visual validation both passed;
- all coordinates are valid, unique, and structurally accepted by the schema.
