## Why

Phase A's settings-canonical-serialization (`randomizer-core/spec.md:41`) already enumerates the full glitch-logic enum: `NoGlitches=0`, `OverworldGlitches=1`, `MajorGlitches=2`, `HybridMajorGlitches=3`, `NoLogic=4`. Phase B #5 (`add-rando-trick-logic-and-axes`) un-pins user input for the first three (`NoGlitches`, `OverworldGlitches`, `MajorGlitches`). Phase D extends the supported range to include `HybridMajorGlitches` and `NoLogic`.

The difference:
- **MajorGlitches** (Phase B) — glitches that don't bypass the entire intended progression (e.g., bomb-jump skip in Misery Mire).
- **HybridMajorGlitches** (Phase D) — combinations of glitch techniques + community-specific routing that requires very specific tech knowledge.
- **NoLogic** (Phase D) — disable logic entirely. Every location is "reachable" by definition. Items can be placed anywhere. Player must use their own judgment to complete.

These two are Phase D because they introduce significantly more complex predicate paths and the player-base for them is much smaller. Most race-relevant glitch logic is at OverworldGlitches or MajorGlitches.

## What Changes (intended scope)

- **Un-pin `logic` enum** to allow values 3 (HybridMajorGlitches) and 4 (NoLogic) in CSV input + settings-screen picker.
- **`OP_GLITCH_LEVEL_AT_LEAST 3`** predicates: author the HybridMG-specific predicate set for locations that ONLY become reachable under hybrid major-glitch logic.
- **NoLogic mode**: when `logic == NoLogic`, the predicate VM short-circuits — every predicate evaluates true. Placement becomes trivial; the player gets a fully-random distribution with no reachability guarantees.
- **Spoiler warning**: NoLogic seeds emit a `fallback_warnings` entry: `{"code": "no_logic_seed", "detail": "Seed generated with logic=NoLogic; reachability is not enforced."}`. Players see this in the spoiler so they know the seed may be un-completable.
- **`accessibility=none` interaction**: NoLogic + accessibility=none is the maximally-permissive combination; both flags allow un-completable seeds but for different reasons.
- **`kGeneratorVersion` bumps**; corpus regenerates.

## Capabilities

### Modified Capabilities

- `randomizer-core`: MODIFIED Requirement on `Settings canonical serialization order (normative)` to confirm the `logic` enum un-pin extends to values 3 and 4.
- `randomizer-logic`: MODIFIED Requirement on `OP_GLITCH_LEVEL_AT_LEAST` (Phase B added the predicate handler; this change extends the supported threshold space to include `HybridMajorGlitches` and `NoLogic` short-circuit semantics).

## Impact

- **Code**: `src/rando/rando_logic.c` (extend `OP_GLITCH_LEVEL_AT_LEAST` handler; NoLogic short-circuit), `src/rando/rando_placement.c` (NoLogic skips goal-completability check), `src/rando/rando_settings.c` (CSV un-pin), `src/rando/rando_spoiler.c` (NoLogic warning emission).
- **YAML**: hybrid-major-glitch predicates in `assets/rando/logic_parts/*.yaml` for locations newly reachable under HybridMG. Hand-translated from ALTTPR's per-Region PHP closures.
- **Effort**: **2-3 weeks of focused work.** HybridMG predicate authoring is the bulk.
- **Regression risk**: `kGeneratorVersion` bumps. Default-settings seeds (`logic=NoGlitches`) remain byte-identical.
- **Dependencies**: REQUIRES Phase B #5 (`add-rando-trick-logic-and-axes`) shipped first — Phase D extends Phase B's `OP_GLITCH_LEVEL_AT_LEAST` handler.
- **ALTTPR provenance**: hybrid-major-glitch predicates per ALTTPR's region files; translation discipline same as Phase B.

## Status (stub)

Proposal-only Phase D stub. Detail deferred to Phase D apply-time. Phase D cannot start before Phase B #5 archives.
