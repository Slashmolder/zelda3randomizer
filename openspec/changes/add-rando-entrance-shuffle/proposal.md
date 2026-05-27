## Why

Entrance shuffle is the headline Phase C feature per `add-randomizer-support / tasks.md §14.3` ("Phase C acceptance: entrance shuffle (Simple/Restricted/Crossed/Insanity) with goal-reachability preserved").

ALTTPR's entrance shuffle randomizes which interior screen each overworld door leads to (and vice versa). Linus enters Link's House on the overworld map and ends up in Hyrule Castle's dungeon room; the player can no longer assume "this door leads to vanilla X." Four modes per ALTTPR convention:
- **Simple** — interior-to-interior matched pairs (every door pair stays pair-matched).
- **Restricted** — interiors shuffle within categories (overworld doors → caves; dungeon entrances → other dungeon entrances).
- **Crossed** — interiors shuffle across categories (an overworld door could go to a dungeon's first room).
- **Insanity** — every screen-pair endpoint is independently shuffled.

Phase A scaffolded `RegionRemap_Lookup` in `src/rando/rando_logic.c` precisely so Phase C could swap region accessors at runtime without re-authoring the static `EdgeDef[]` graph. Phase B Slice #4a Inverted activated `RegionRemap` for the first time (Light↔Dark swap); Phase C extends it for arbitrary entrance permutations.

This change is a **Phase C** scaffold — proposal-only stub. Phase C cannot start before Phase B has shipped at least #4a Inverted (RegionRemap must be production-grade before entrance shuffle layers on top of it).

## What Changes (intended scope)

- **Entrance shuffle module** (`src/rando/shuffle_entrance.{c,h}` new) that runs during generation, computes an entrance permutation given the active mode + RNG, and produces a `RegionRemap_*` overlay that the logic graph consumes.
- **4 mode-specific algorithms** per ALTTPR's convention (Simple / Restricted / Crossed / Insanity), each with goal-completability preservation: the permutation algorithm SHALL retry until the goal is reachable under the permutation. Per-mode complexity differs; Insanity is the hardest case.
- **Spoiler integration**: per-door mapping listed in JSON + text spoilers under a new `entrance_mapping` section. Critical for race admins and route planners.
- **Settings axis**: `entrance_shuffle` enum (`none | simple | restricted | crossed | insanity`). Default `none`. Phase A reserved this axis ordinally but didn't pin a bit position — Phase C confirms placement in the canonical-serialization order.
- **Sidecar TLV**: the entrance permutation is per-seed state that must persist across save/load. Phase A's `randomizer-save / Forward-compat reserve (Phase C foresight)` already specs a TLV chain after the bitmap; this change defines the `TAIL_ENTRANCE_MAP` TLV.
- **`kGeneratorVersion` bumps**.
- **In-game tracker integration** (#2 trackers): entrance-shuffle mode displays the per-door mapping in the location tracker (so the player knows "this door I entered actually leads to PoD's first room").

## Capabilities

### New Capabilities

(none — entrance shuffle is a shuffle module, peer to boss/drop/prize/medallion; lives in `randomizer-shuffles`.)

### Modified Capabilities

- `randomizer-shuffles`: MODIFIED Requirement on "Entrance shuffle modes (Phase C)" (Phase A drafted this at `randomizer-shuffles/spec.md:81`). Phase C fleshes out the per-mode algorithm and the goal-preservation contract.
- `randomizer-logic`: MODIFIED Requirement on `RegionRemap` to support the entrance-shuffle overlay shape (Phase A activated Light↔Dark swap; Phase C extends to per-door permutation).
- `randomizer-save`: ADDED Requirement for the `TAIL_ENTRANCE_MAP` TLV chain entry; MODIFIED Requirement on the "Forward-compat reserve" section to mark this as the first realized TLV.
- `randomizer-core`: MODIFIED Requirement on `Settings canonical serialization order (normative)` to confirm the `entrance_shuffle` byte position.
- `randomizer-ui`: ADDED Requirement for the entrance-shuffle mode picker on the settings screen.

## Impact

- **Code**: `src/rando/shuffle_entrance.c` (new), `src/rando/shuffle_entrance.h` (new), `src/rando/rando_logic.c` (extend `RegionRemap` for entrance overlay), `src/rando/rando_save.c` (TLV write/read), `src/rando/rando_spoiler.c` (entrance_mapping section), `src/select_file.c` (settings-screen mode picker).
- **Assets**: `assets/rando/entrance_registry.yaml` (new — enumerates every shuffleable entrance with stable IDs; mirrors ALTTPR's entrance-table convention).
- **Effort**: Large — **4-8 weeks of focused work** depending on how clean the RegionRemap activation in Phase B ended up. Insanity mode is the long pole; the algorithm has to keep retrying permutations until goal-reachable, and the success probability can be low.
- **Regression risk**: high `kGeneratorVersion` impact; corpus regenerates. Existing non-entrance-shuffle seeds (`entrance_shuffle == none`) MUST remain byte-identical.
- **Dependencies**: REQUIRES Phase B #4a Inverted (RegionRemap activation), and benefits from #2 trackers + #6 hints (entrance-shuffle is much more playable with hints + a per-door tracker).
- **ALTTPR provenance**: entrance-shuffle code in ALTTPR is in `app/EntranceRandomizer.php`, but per `CLAUDE.md` claim-grounding note, that file's docstring incorrectly claims "we use mt_rand" — the actual code shells out to a Python script. Phase C translation discipline applies: read the actual code, not just comments.

## Status (stub)

This is a **proposal-only stub** for Phase C. Detail is deferred to `/openspec-explore` AFTER Phase B's #4a Inverted RegionRemap activation ships.

Reasons for deferring detail to apply-time:
- RegionRemap's production shape post-#4a may differ from the Phase A scaffold; Phase C spec text depends on the final shape.
- ALTTPR's entrance-shuffle algorithm needs careful study (the PHP shells out to Python — translation discipline is non-trivial).
- Per-mode complexity bounds (Simple is mechanical; Insanity is a retry-loop with low success rate) need a prototype.
- Goal-preservation algorithm choice (reject + retry vs. constrained construction) needs investigation.

Phase C should not begin until at least #4a Inverted has archived.

## When work starts

1. Wait for Phase A archive + Phase B #4a Inverted archive.
2. `/openspec-explore add-rando-entrance-shuffle` to flesh out the 4 per-mode algorithms + RegionRemap overlay shape + TLV format.
3. `/openspec-propose` to finalize spec deltas + design.md.
4. `/openspec-apply` to walk through tasks.
5. `/openspec-archive` when done.
