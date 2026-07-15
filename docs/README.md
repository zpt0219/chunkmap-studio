# Documentation Index

Start with the current documents:

- [Current Code Architecture](./CODE_ARCHITECTURE_DESIGN.md) — runtime boundaries,
  persisted project format, image writes, rendering, and verification.
- [Source Reading Guide](./SOURCE_READING_GUIDE.md) — recommended path through the
  model, command system, image pipeline, and Desktop UI.
- [Demo Video Production Plan](./DEMO_VIDEO_PRODUCTION_PLAN.md) — recommended
  Remotion and FFmpeg workflow for turning a real app recording into a narrated,
  captioned, reproducible product demo.
- [Non-destructive Chunk Placement and Editable Seams](./CHUNK_WRITE_REGISTRATION_RESTORATION_DESIGN.md) —
  approved redesign that keeps source PNGs immutable, stores registration as placement
  parameters, and renders editable polyline seams only across overlaps. The first
  implementation is complete.

Completed implementation plans and superseded product baselines live under
[historical/](./historical/README.md). They explain why the current architecture
exists, but they are not the source of truth for current behavior.
