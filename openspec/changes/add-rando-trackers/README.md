# add-rando-trackers

Phase B Slice 1. Implements the Phase A-specced item + location tracker overlays (`randomizer-ui/spec.md:142-164`) with full backend-compatibility matrix and the checked-location bitmap write path.

## Status

Authored: 2026-05-26. **Pending Phase A archive** before this change can validate.

## Read these in order

| File | Purpose |
|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | Spec delta — MODIFIED tracker requirements + ADDED keybinding + backend-matrix contract |
| [specs/randomizer-save/spec.md](specs/randomizer-save/spec.md) | Spec delta — ADDED checked-location bitmap write path |
| [tasks.md](tasks.md) | Implementation checklist (10 sections, ~50 tasks) |

No `design.md` — the spec carries the design decisions (anchor configuration, OAM-cache invalidation, region grouping mirrors text spoiler).

## Key files touched

- **Rendering**: `src/hud.c` + `src/hud.h` (new `Hud_RandoDrawItemTracker` + `Hud_RandoDrawLocationTracker`)
- **Bindings**: `src/config.c` (new `kKeys_RandoToggleItemTracker` + `kKeys_RandoToggleLocationTracker`)
- **Backend glue**: `src/main.c`, `src/opengl.c` (verify overlay composites correctly on all backends)
- **Bitmap**: `src/rando/rando_save.c` (write path), `src/rando/rando.c` (set-on-dispatch + at audit-exempt event-flag sites)
- **Docs**: `README.md`, `docs/randomizer.md`

## Verification

- **5 backends**: SDL software, SDL hardware, OpenGL, OpenGL ES, Switch — all render the overlay correctly.
- **OAM-cache invariant**: per-frame cost is bounded to OAM-copy of fixed size when `reachability_state_counter` is unchanged.
- **Bitmap persistence**: round-trip across save/load on at least 3 seeds.
- **`placement_digest_hex` byte-identical** before/after — no `kGeneratorVersion` bump.
- **Switch build verification** is a release-cut gate.

## Dependencies

- **Phase A archived first.** Deltas `randomizer-ui` and `randomizer-save` post-archive.
- **Helpful before #4a Inverted**: trackers surface reachability bugs early; #4a authors Inverted YAML and benefits from immediate visibility into placement-vs-reachability mismatches.

## Why second

Highest player-impact UX in Phase B (per `docs/randomizer_phase_b.md` Slice 1 rationale). Also a test substrate for the Inverted world-state work coming later.
