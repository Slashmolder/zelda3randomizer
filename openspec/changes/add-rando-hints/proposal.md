## Why

ALTTPR's community considers hints integral to the modern randomizer experience. Per `docs/randomizer.md:294` Phase B+ roadmap, hint generation covers four sources:

- **Sahasrahla telepathic tiles** — light-world early-game psychic-talk tiles that hint at key item locations.
- **Storyteller** — a hint NPC that appears in certain shrines.
- **Bookshelves** — book-of-mudora interaction hints.
- **Murahdahla** — the dark-world Triforce-Hunt hint NPC.

Murahdahla in particular is structurally required: Triforce Hunt is *"almost unplayable without hints"* per the source doc — the goal of "collect N of M Triforce pieces" is unfindable in a 200-location seed without a hint subsystem pointing the player at sphere-aware piece locations.

The ALTTPR upstream is split across two files:
- `../alttp_vt_randomizer/app/Services/HintService.php` (177 lines — verified) — placement-aware text-generation logic.
- `../alttp_vt_randomizer/app/Text.php` (1110 lines — verified) — the hint string body / dialogue dictionary.

Total subsystem ≈ 1287 lines. The source doc claimed "~1000 lines for HintService alone" which undercounted the split.

This change is a **new capability** — `randomizer-hints` — because hints are a separable subsystem from placement and from the broader text engine. The `HintService.php` upstream is isolated; dialogue-ID injection is its own integration surface; hint generation runs once at slot-creation and persists in the spoiler. Modeling it as a peer to `randomizer-core` / `randomizer-placement` / etc. keeps the capability map clean.

(Open question: confirm during /openspec-explore that a separate capability is the right call vs. extending `randomizer-placement` + `randomizer-core`. The chunking critique flagged this as worth a design.md decision.)

## What Changes

- **New `src/rando/rando_hints.{c,h}` module.** Generates hint strings at slot-creation time, deterministically from the placement table + sphere data. Hint strings stored in the slot's spoiler JSON under a new `hints` section.
- **Dialogue-ID injection** at hint NPC sites — Sahasrahla telepathic tile sprites, storyteller sprite, bookshelf interaction, Murahdahla sprite. Each site reads the slot's hint set and dispatches a per-slot dialogue ID (allocated from a pool of unused vanilla IDs, or via a new "dynamic" ID range).
- **Assets pipeline**: hint string allocation — the per-slot hint set lives in heap (RAM-budget-friendly; not in `g_ram`), with the dialogue text rendered into the vanilla text-engine via the existing text-decoder.
- **Spoiler integration**: new `hints` array in JSON spoiler, with per-source-NPC keys (`sahasrahla_north_telepathic`, `murahdahla`, etc.). Text spoiler mirrors the JSON under a `Hints` section heading.
- **Triforce Hunt path**: Murahdahla's hints SHALL surface Triforce-piece locations grouped by sphere. The hint text format is: "A piece is in <region> (sphere N)" or similar — exact format per ALTTPR convention to be confirmed in apply-time PHP translation.
- **Settings axis**: `hints` setting (enum: `off | sahasrahla | full`) controlling which hint sources are active. Default per ALTTPR convention is `full` for Triforce Hunt, `sahasrahla` otherwise — exact default deferred to design.md.
- **No new sprite handlers required** — hint NPCs already exist in vanilla; this change just routes the dialogue ID through a rando lookup.
- **`kGeneratorVersion` bumps**; hints are deterministic from seed + placement, so they're part of the regression-corpus determinism contract.

## Capabilities

### New Capabilities

- `randomizer-hints`: hint string generation pipeline, dialogue-ID allocation, per-NPC dispatch routing, spoiler integration. Bounded scope: hints are read-only (don't affect placement output); they're emitted by the generator and rendered by the runtime; no two-way interaction with placement except as a consumer of the placement table.

### Modified Capabilities

- `randomizer-placement`: ADDED Requirement for hint NPC dialogue-ID dispatch sites (`src/sprite_main.c` hint-NPC sprite handlers route through a new `Rando_GetHintDialogueId(npc_id)` accessor).
- `randomizer-core`: ADDED Requirement for the `hints` spoiler section + `hints` settings axis canonical-serialization entry.

## Impact

- **New module**: `src/rando/rando_hints.c` (generation pipeline) + `src/rando/rando_hints.h` (API + result struct).
- **Sprite handlers**: Sahasrahla telepathic, storyteller, bookshelf, Murahdahla — dialogue-ID dispatch routed through `Rando_GetHintDialogueId`.
- **Text engine**: `src/messaging.c` — new dynamic dialogue ID range carved out (find an unused band in the vanilla ID space; document in `audit.md` §"Hint dialogue ID range").
- **Spoiler**: `src/rando/rando_spoiler.c` — emit `hints` section.
- **Asset blob**: NO new graphics; hints reuse vanilla text-engine tiles.
- **Effort**: **2-3 weeks of focused work**, mostly the text-engine integration + per-source-NPC generation logic.
- **Regression risk**: `kGeneratorVersion` bumps; corpus regenerates. Hints are deterministic, so the same seed + settings always produces the same hint set — corpus catches drift.
- **Switch parity**: text rendering uses the existing text-engine; no platform-specific work.

## Status

**Fully authored** as of 2026-05-26. Promoted from initial stub. design.md records the new-capability decision (peer `randomizer-hints`), dialogue-ID range carve strategy, goal-aware hints-axis default, and per-source generation algorithm. See [README.md](README.md) for the file index.
