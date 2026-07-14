# Demo Video Production Plan

## Purpose

Create a short product demonstration video for ChunkMap Studio without requiring
the operator to learn a traditional video editor. The operator records the real
Desktop workflow once; AI-assisted tooling handles review, editing, narration,
captions, presentation graphics, and final rendering.

The target is a repeatable local pipeline rather than a one-off manually edited
timeline. A revised voice-over, shorter pause, different callout, or alternate
duration should be expressible as a text or data change followed by a new render.

## Recommended Direction

Use **Remotion + FFmpeg** as the primary production pipeline:

- FFmpeg inspects, decodes, trims, accelerates, mixes, and encodes media.
- Remotion defines the edit as TypeScript/React and renders titles, callouts,
  cursor emphasis, zooms, captions, transitions, and audio placement.
- Codex analyzes extracted frames, authors the edit plan and narration, changes
  the Remotion composition, and runs repeatable preview and final renders.
- A text-to-speech provider supplies narration after the visual edit is stable.

MCP is not required for this route. Direct CLI execution is simpler to inspect,
test, reproduce, and debug than an MCP wrapper around the same FFmpeg commands.

## Alternatives

| Option | Automation | Visual timeline | Best use | Main limitation |
| --- | --- | --- | --- | --- |
| FFmpeg CLI | High | No | Deterministic trimming, speed changes, audio, captions, and encoding | Complex presentation graphics become difficult to maintain |
| Remotion + FFmpeg | High | Code preview | Repeatable software demos with polished motion graphics | Requires a small video project and reusable composition components |
| DaVinci Resolve Studio + MCP | High after setup | Yes | Professional timelines that a person may also fine-tune | Paid Studio edition and a more stateful integration |
| CapCut or Descript | Partial | Yes | Fast manual AI voice and caption workflows | No dependable first-party CLI/MCP automation path |
| Premiere Pro + community MCP | Experimental | Yes | Existing Adobe-centered workflows | Community bridge, paid application, and greater integration fragility |

### DaVinci Resolve Option

A community `davinci-resolve-mcp` server can control DaVinci Resolve Studio
through Resolve's scripting API. It can organize media, construct and inspect
timelines, manipulate markers and subtitles, queue renders, and analyze source
media. This is the preferred alternative if a visible professional timeline is
more important than a repository-owned, fully reproducible render.

This integration is not an official Blackmagic Design MCP product. It requires
DaVinci Resolve Studio because the free edition does not expose the required
external scripting support. The MCP package and its permissions should be
reviewed before installation.

### FFmpeg MCP Wrappers

Community MCP servers expose operations such as trim, concatenate, resize,
subtitle burn-in, overlays, silence removal, and audio extraction. They can be
convenient, but they do not add editing capabilities beyond FFmpeg itself. For
this repository, direct FFmpeg commands and checked-in scripts are preferred.

## Production Workflow

### 1. Prepare the Demonstration

Write a short shot list before recording. The initial video should focus on one
clear product story:

1. Open a ChunkMap Studio project.
2. Show the Concept Map and generated map comparison.
3. Select a chunk and inspect its Global and Local Prompts.
4. Demonstrate one meaningful chunk operation.
5. Export the completed full map.

The first version should aim for 60 to 120 seconds. Longer explanations can be
published separately instead of slowing the primary demo.

### 2. Record Clean Source Material

- Record the real application at a stable resolution and UI scale.
- Record silently; narration is written after the visual edit.
- Pause briefly before and after each important action.
- Multiple takes are acceptable. Mistakes do not require restarting the entire
  recording.
- Avoid moving unrelated windows over the application.
- Keep private notifications and unrelated project names out of the capture.

### 3. Analyze the Recordings

Codex uses FFprobe and FFmpeg to collect media metadata and extract frames at
scene changes or a fixed interval. The analysis identifies:

- useful actions and their source time ranges;
- dead time, loading waits, mistakes, and repeated actions;
- UI regions that deserve a zoom or callout;
- points where narration or captions need additional screen time;
- possible cuts that would make cursor movement appear discontinuous.

The result is an explicit edit plan. Raw recordings remain immutable.

### 4. Build the Visual Edit

The Remotion composition consumes the edit plan and source recordings. Reusable
components should cover:

- clip trimming and speed changes;
- eased zoom and pan around a UI target;
- cursor highlight or click pulse when useful;
- title, section card, lower-third, and feature callout;
- caption display with safe margins;
- loading-time compression rather than misleading removal of application state;
- optional background music and end card.

Effects should clarify the product rather than imitate a promotional template.
The actual application UI remains the visual focus.

### 5. Add Narration and Captions

Narration is written against the locked visual edit, not before it. Each line has
a target time range and should explain intent or benefit instead of merely
describing the cursor.

Voice options, in increasing quality and setup cost:

1. macOS system speech for a free timing draft.
2. CapCut or another desktop TTS service for a lightly assisted workflow.
3. OpenAI Audio API, ElevenLabs, or another scripted TTS provider for a fully
   automated natural voice.
4. A human recording, followed by transcription, cleanup, and timing alignment.

Subscription access to Codex does not imply included third-party or OpenAI API
TTS usage. API credentials and billing must be configured separately when an API
voice is selected. Credentials must remain outside the repository.

The same timed narration data should generate captions. Keep an editable subtitle
file and optionally burn captions into distribution copies.

### 6. Review and Render

Render a low-resolution preview first. Review it for:

- whether every action is understandable without prior product knowledge;
- whether zooms preserve enough spatial context;
- whether narration finishes before the related visual disappears;
- whether captions avoid important UI;
- whether compressed waits still communicate what the application is doing;
- whether the final export and completed map remain on screen long enough.

After approval, render a 1080p H.264 MP4 with normalized narration volume. Keep
the render reproducible from the same edit plan and source assets.

## Proposed Repository Layout

The implementation can use the following structure:

```text
tools/demo-video/
  package.json
  src/
    Root.tsx
    compositions/
    components/
    edit-plan.ts
  scripts/
    inspect-media.sh
    render-preview.sh
    render-final.sh

demo/
  edit-plan.json
  narration.md
  captions.srt
  assets/
  raw/                 # ignored by Git
  generated-audio/     # ignored by Git
  renders/             # ignored by Git
```

Source recordings, generated speech, extracted frames, and rendered videos are
large derived assets and should not be committed. The composition, edit plan,
narration, captions, and small intentional graphics can be versioned.

## Edit Plan Contract

The first implementation should keep the edit model deliberately small. A clip
needs a source file, source range, output speed, and optional presentation cues.
For example:

```json
{
  "clips": [
    {
      "source": "raw/project-open.mov",
      "from_seconds": 1.2,
      "to_seconds": 8.5,
      "speed": 1.0,
      "callout": "Open an existing map project",
      "zoom_target": {"x": 0.18, "y": 0.12, "width": 0.32, "height": 0.28}
    }
  ]
}
```

Do not introduce a general-purpose non-linear editor schema in the first version.
Add fields only when a real demo shot requires them.

## Implementation Stages

### Stage 1: Deterministic Rough Cut

- Install FFmpeg.
- Create the Remotion project and one 1080p composition.
- Inspect one raw screen recording.
- Implement trimming, concatenation, speed changes, and H.264 rendering.
- Produce a silent rough cut.

### Stage 2: Product Presentation

- Add reusable zoom, callout, title, and end-card components.
- Add an edit-plan file instead of hard-coded timing scattered across components.
- Generate and review a low-resolution preview.

### Stage 3: Narration and Captions

- Write timed narration.
- Generate draft speech and captions.
- Mix and normalize narration.
- Select and configure the final voice provider only after timing is accepted.

### Stage 4: Repeatable Delivery

- Add preview and final render commands.
- Validate missing-source and invalid-time-range errors.
- Document the one-command workflow.
- Render both the primary demo and, if useful, a shorter social-media cut from
  the same source plan.

## Definition of Done

The pipeline is ready when:

- a new recording can be substituted without manually rebuilding a timeline;
- Codex can inspect the recording and propose exact source ranges;
- the edit is represented by reviewable files in the repository;
- a preview render can be generated with one command;
- narration and captions remain synchronized after ordinary timing changes;
- the final MP4 can be reproduced without opening a GUI editor;
- raw media, credentials, caches, generated audio, and renders stay out of Git.

## Current Local Prerequisites

At the time this plan was written, Node.js was available on the development Mac,
while FFmpeg, DaVinci Resolve, Premiere Pro, and Blender were not detected. The
recommended route therefore requires installing FFmpeg and adding the local
Remotion project; it does not require purchasing or installing a traditional
video editor.
