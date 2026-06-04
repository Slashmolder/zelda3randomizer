## Why

> **Dependency note (add-rando-fairy-chest-model, 2026-06-03):** the swordless-mode
> `can_place` work in this change references `LOC_Pyramid_Fairy_Sword` (and `_Bow`).
> Those two Pyramid Fairy Trade slots have since been **retired** from the placement
> pool (the pond is now a two-chest model granting `Pyramid_Fairy_Left/Right`). Before
> implementing swordless here, re-point that logic to a still-existing sword slot — the
> retired LOC ids (210/211) no longer resolve.

Phase A pinned three logic-relaxation axes to their safest values and reserved op-codes / settings bits for Phase B activation:

- `tricks = none` (per `audit.md §0.5`); `OP_TRICK` reserved at op-code 15 per `assets/rando/op_registry.yaml`.
- `logic = NoGlitches` (per `audit.md §0.5`); `OP_DIFFICULTY_AT_LEAST` + `OP_GLITCH_LEVEL_AT_LEAST` reserved at op-codes 16 + 17.
- `mode.weapons = randomized | assured`; `swordless` reserved for Phase B per `Randomizer.php:230-240`.
- `accessibility = items | locations`; `none` (no accessibility guarantee) reserved for Phase B per Phase A proposal.
- `pyramid_bow_upgrade = silvers`; `arrows` (Pyramid Fairy bow trade yields silver arrows directly) reserved for Phase B per `Randomizer.php:150-152`.

Phase A1 audit Bug #7 (per-item bounded rewind) is also still open per `audit_phase_a1.md:79-80`. The Phase A spec at `randomizer-core/spec.md:344` says "the placer rewinds the last N placements and retries" — Phase A implementation uses whole-attempt retry via `kAssumedFillMaxAttempts=8` (per `audit_phase_a1.md:20`). **This is implementation-matches-spec work; no spec change needed.** Folded here because tricks make placement harder via predicate density — the relaxation axes and the placement-quality fix go together.

This change un-pins all 5 axes + implements Bug #7's per-item rewind in one bundle. Chunking rationale: all four axes are "logic relaxations" sharing one mental model; Bug #7 lands with them because trick predicates raise the placement difficulty floor.

## What Changes

### Op-code handlers (predicate VM)

- **`OP_TRICK`** (op-code 15): evaluates true when the active trick is included in `settings.tricks` bitmask. Each named trick (boots-clip, fake-flippers, bunny-revival, dark-room nav, bomb-jump, sword-less Aga, etc.) is a bit; `settings.tricks` un-pinned in `rando_settings.h:86` (currently pinned to 0).
- **`OP_DIFFICULTY_AT_LEAST`** (op-code 16): evaluates true when `settings.item_pool_difficulty` is at or above the specified threshold. Phase A already supports `easy/normal/hard/expert` in pool composition; the op exposes the same axis in predicates (e.g., "this trick predicate requires hard or higher difficulty").
- **`OP_GLITCH_LEVEL_AT_LEAST`** (op-code 17): evaluates true when `settings.logic` is at or above the specified threshold. Phase B exposes `NoGlitches` (Phase A default) + `OverworldGlitches` + `MajorGlitches`. `HybridMG` and `NoLogic` remain Phase C+.

### Settings-axis un-pinning (NOT enum-space expansion)

Phase A's canonical-serialization spec at `randomizer-core/spec.md:29-66` (Requirement "Settings canonical serialization order (normative)") **already enumerates all the Phase B values** — they were defined at Phase A spec time but with user input pinned to a subset. Phase B's work is un-pinning the user-facing input layer; the byte layout, enum value assignments, and field widths are unchanged.

- **Un-pin `tricks`** (currently pinned to 0 in `rando_settings.h:86`). Width is uint8 LE (Phase A item #5); Phase B exposes up to 8 trick bits. CSV parser accepts `tricks=boots-clip,fake-flippers,...` syntax; bit positions enumerated in `assets/rando/op_registry.yaml` `tricks:` table. Future widening to uint16/uint64 (more than 8 tricks) is a separate `generator_version` bump.
- **Un-pin `logic`** (currently pinned to 0 in `rando_settings.h:88`). Phase A already defines `NoGlitches=0`, `OverworldGlitches=1`, `MajorGlitches=2`, `HybridMajorGlitches=3`, `NoLogic=4` (item #7). Phase B un-pins user input to allow values 1 and 2. Values 3 and 4 remain reserved for Phase C+.
- **Un-pin `mode_weapons` to allow `swordless=3`**. Phase A already defines `randomized=0/assured=1/vanilla=2/swordless=3` (item #8). Phase B exposes `swordless`. `vanilla` remains reserved.
- **Un-pin `accessibility` to allow `none=2`**. Phase A already defines `items=0/locations=1/none=2` (item #9). Phase B exposes `none`.
- **Un-pin `region_pyramid_bow_upgrade=false`**. Phase A pinned to `1=true` (BowAndSilverArrows). The boolean field already allows `0=false` (BowAndArrows variant). Phase B un-pins user input — this is the "arrows variant" the source doc described.

### Trick predicate authoring

- New `assets/rando/logic_parts/*.yaml` predicates for each Phase B trick. Each trick is a named macro in `macros.yaml` that locations gate on via `OP_TRICK`. Initial trick set per ALTTPR's region-file PHP closures (translation discipline per `audit.md §0.10`).
- Glitch-level predicates added similarly — e.g., a Bumper Cave Ledge "skip" location goes from unreachable in `NoGlitches` to reachable in `OverworldGlitches`.

### Placement algorithm

- **Implement per-item bounded rewind in `Place_AssumedFill`** per `randomizer-core/spec.md:344`'s pre-existing SHALL. Replace `kAssumedFillMaxAttempts=8` whole-attempt-retry loop with: on no-valid-location, rewind the last N placements (e.g., N=10) and retry; only after exhausting per-item rewind budget does the whole-attempt retry kick in. Better placement quality on hard/trick-dense seeds; lower CPU cost.

### Corpus + version

- `kGeneratorVersion` bumps; regression corpus regenerates. Tricks-off + glitch-NoGlitches + non-swordless + accessibility=items + pyramid_bow_upgrade=silvers seeds SHOULD remain byte-identical in `placement_digest_hex` — i.e., the default-settings digest is preserved. CI verifies.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-logic`: ADDED Requirements for `OP_TRICK` / `OP_DIFFICULTY_AT_LEAST` / `OP_GLITCH_LEVEL_AT_LEAST` semantics, trick predicate authoring discipline, and the macro-source-line citation convention for trick predicates.
- `randomizer-core`: MODIFIED Requirements on the settings axes for `tricks`, `logic`, `mode.weapons` (adds `swordless`), `accessibility` (adds `none`), `pyramid_bow_upgrade` (adds `arrows`). MODIFIED Requirement on the assumed-fill placement algorithm — clarify that "rewind the last N placements" is per-item, not whole-attempt (implementation must match spec; no SHALL text change needed at the requirement-prose level, but a new Scenario should be added that distinguishes the two cases).

## Impact

- **Code**: `src/rando/rando_logic.c` (3 op handlers), `src/rando/rando_settings.h` + `src/rando/rando_settings.c` (un-pin + CSV parsing), `src/rando/rando_placement.c` (per-item rewind in `Place_AssumedFill`), `assets/rando/macros.yaml` (trick macros), `assets/rando/logic_parts/*.yaml` (trick predicates per dungeon/region).
- **Effort**: **2-3 weeks of focused work.** Trick-predicate authoring is the work; the op handlers are small (~50 lines each); per-item rewind is a focused refactor.
- **Regression risk**: Default-settings digest should remain byte-identical. Non-default seeds (with tricks on, or with `accessibility=none`, etc.) will produce new digests. Corpus regenerates.
- **No dependency on other Phase B slices.** Could ship alongside #4a Inverted (trick predicates in Inverted dungeons may need to be authored as part of #4a; mark them as a follow-on of #5).
- **`mode.weapons=swordless` interaction with Phase A's "Pyramid Fairy Sword" location**: swordless mode rejects sword placement at `LOC_Pyramid_Fairy_Sword`; spec must update the `can_place` predicate at that slot.

## Status

**Fully authored** as of 2026-05-26. Promoted from initial stub after the user requested completing every Phase B plan. design.md captures the per-item rewind algorithm + trick bit-position assignments + CSV syntax + swordless/accessibility=none semantics. See [README.md](README.md) for the file index.
