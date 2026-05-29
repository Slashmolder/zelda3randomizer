## Why

Entrance shuffle is the headline Phase C feature per `add-randomizer-support / tasks.md §14.3` ("Phase C acceptance: entrance shuffle (Simple/Restricted/Crossed/Insanity) with goal-reachability preserved").

ALTTPR's entrance shuffle randomizes which interior screen each overworld door leads to (and vice versa). Linus enters Link's House on the overworld map and ends up in Hyrule Castle's dungeon room; the player can no longer assume "this door leads to vanilla X." Four modes per ALTTPR convention:
- **Simple** — interior-to-interior matched pairs (every door pair stays pair-matched).
- **Restricted** — interiors shuffle within categories (overworld doors → caves; dungeon entrances → other dungeon entrances).
- **Crossed** — interiors shuffle across categories (an overworld door could go to a dungeon's first room).
- **Insanity** — every screen-pair endpoint is independently shuffled.

Phase A scaffolded `RegionRemap_Lookup` in `src/rando/rando_logic.c` intending it as the Phase C entrance hook. **Investigation (2026-05-29, branch `pc-entrance-spike`) found that scaffold is both dead code and the wrong abstraction** — see `design.md §1`. It has zero callers; it is identity in every shipped seed; and Phase B #4a Inverted did NOT activate it (Inverted shipped via a *static alternate edge table* `kRandoEdges_Inverted[]` + a visual tile overlay, never the runtime overlay). `RegionRemap` also models a region *lookup*, whereas entrance shuffle must rewire which interior-region a door-*edge* terminates at. This change therefore **retires `RegionRemap`** and feeds the logic graph via a per-seed alternate edge table (Inverted's proven pattern, regenerated from the permutation each seed).

A worktree spike rotated 57 cave entrances at runtime via a shadow copy of `kOverworld_Entrance_Id` and the user playtested clean, walkable redirected interiors — so the runtime door-rewiring story is validated. This change is now **fully scoped** (design + staged tasks) rather than a stub. The hard runtime risk is retired; the work is staged functional-first (see `design.md §6`).

## What Changes (intended scope)

- **Entrance shuffle module** (`src/rando/shuffle_entrance.{c,h}` new) that runs during generation, computes an entrance permutation given the active mode + RNG, and produces a `RegionRemap_*` overlay that the logic graph consumes.
- **4 mode-specific algorithms** per ALTTPR's convention (Simple / Restricted / Crossed / Insanity), each with goal-completability preservation: the permutation algorithm SHALL retry until the goal is reachable under the permutation. Per-mode complexity differs; Insanity is the hardest case.
- **Spoiler integration**: per-door mapping listed in JSON + text spoilers under a new `entrance_mapping` section. Critical for race admins and route planners.
- **Settings model**: **composable boolean axes** (`shuffle_cave_entrances`, `shuffle_dungeon_entrances`, `coupled` [default on], `cross_category`, `decoupled`) rather than a single monolithic enum — the user chooses what to swap. The 4 famous ALTTPR modes (Simple/Restricted/Crossed/Insanity) become **presets** over these axes, plus a "Custom" mode for free. NOTE: contrary to the original stub, there is **no** existing `entrance_shuffle` enum and **no** Phase A reservation (verified 2026-05-29) — the axes are new fields appended to the canonical serialization. They consume the existing zero-pad bytes (`out[25..27]`) so no existing byte moves and the default-settings hash stays byte-identical (`design.md §5`).
- **Sidecar TLV**: the entrance permutation is per-seed state that must persist across save/load. Phase A's `randomizer-save / Forward-compat reserve (Phase C foresight)` already specs a TLV chain after the bitmap; this change defines the `TAIL_ENTRANCE_MAP` TLV.
- **`kGeneratorVersion` bumps**.
- **In-game tracker integration** (#2 trackers): entrance-shuffle mode displays the per-door mapping in the location tracker (so the player knows "this door I entered actually leads to PoD's first room").

## Capabilities

### New Capabilities

(none — entrance shuffle is a shuffle module, peer to boss/drop/prize/medallion; lives in `randomizer-shuffles`.)

### Modified Capabilities

- `randomizer-shuffles`: MODIFIED Requirement on "Entrance shuffle modes (Phase C)" (Phase A drafted this at `randomizer-shuffles/spec.md:81`). Phase C fleshes out the per-mode algorithm and the goal-preservation contract.
- `randomizer-logic`: ADDED Requirement "Per-seed entrance edge overlay" — entrance shuffle feeds reachability via a per-seed alternate edge table (Inverted's pattern), and the Phase A `RegionRemap` scaffold is **retired** (dead code + wrong abstraction; see `design.md §1`).
- `randomizer-save`: ADDED Requirement for the `TAIL_ENTRANCE_MAP` TLV chain entry; MODIFIED Requirement on the "Forward-compat reserve" section to mark this as the first realized TLV.
- `randomizer-core`: MODIFIED Requirement on `Settings canonical serialization order (normative)` to **append** the composable entrance axes (consuming existing pad bytes; default-hash byte-identical).
- `randomizer-ui`: ADDED Requirement for the entrance-shuffle mode picker on the settings screen.

## Impact

- **Code**: `src/rando/shuffle_entrance.c` (new), `src/rando/shuffle_entrance.h` (new), `src/rando/rando_logic.c` (per-seed edge overlay; retire `RegionRemap`), `src/overworld.c` (door overlay + coupling via the source-door capture idiom — composes with the merged Retro TakeAny redirect at the entry hook), `src/rando/rando_save.c` (TLV write/read), `src/rando/rando_spoiler.c` (entrance_mapping section), the native settings window + Switch picker.
- **Assets**: `assets/rando/entrance_registry.yaml` (new — enumerates every shuffleable entrance with stable IDs; mirrors ALTTPR's entrance-table convention).
- **Effort**: Large — **4-8 weeks of focused work** depending on how clean the RegionRemap activation in Phase B ended up. Insanity mode is the long pole; the algorithm has to keep retrying permutations until goal-reachable, and the success probability can be low.
- **Regression risk**: high `kGeneratorVersion` impact; corpus regenerates. Existing non-entrance-shuffle seeds (`entrance_shuffle == none`) MUST remain byte-identical.
- **Dependencies**: No hard dependency on #4a Inverted after all (the "RegionRemap activation" premise was false — see Why / `design.md §1`). The logic half reuses Inverted's *already-shipped* alternate-edge-table pattern, and the runtime coupling reuses the *already-merged* Retro TakeAny source-door-capture idiom. Benefits from #2 trackers + #6 hints (entrance shuffle is much more playable with a per-door tracker + hints).
- **ALTTPR provenance**: entrance-shuffle code in ALTTPR is in `app/EntranceRandomizer.php`, but per `CLAUDE.md` claim-grounding note, that file's docstring incorrectly claims "we use mt_rand" — the actual code shells out to a Python script. Phase C translation discipline applies: read the actual code, not just comments.

## Status (scoped)

**Scoped 2026-05-29** via `/openspec-explore` + the `pc-entrance-spike` worktree
investigation and playtest. `design.md` captures the model (one permutation π →
runtime door overlay + per-seed logic edge overlay), the RegionRemap retirement, the
coupling-via-source-door-capture mechanism, the cave/dungeon exit fault line, and the
functional-first staging. `tasks.md` is structured into Stages 0–4.

Key de-risking already done:
- Runtime door redirect **playtested clean** (57-cave rotation spike).
- Coupling mechanism **already shipped** in the merged Retro TakeAny code (reuse).
- Logic-half edge-table swap **already shipped** for Inverted (reuse the pattern).
- `entrance_shuffle` enum + canonical reserve **already declared** by Phase A.

## When work starts

1. `/openspec-apply add-rando-entrance-shuffle` to walk Stage 0 → Stage 1.
2. Ship + archive when Stage 1 (coupled cave-shuffle vertical slice) is playtested
   and audited; Stages 2–4 continue here or split into follow-on changes.
3. Keep file-disjoint from parallel Retro work where possible; sequence the shared
   seams (kGeneratorVersion+corpus, canonical serialization, the overworld entry
   hook) at merge time (`design.md §7`).
