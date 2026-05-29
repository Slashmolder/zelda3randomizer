## Why

ALTTPR's community considers hints integral to the modern randomizer experience. Hints point the player at item locations so seeds (especially Triforce Hunt) are findable.

> **As-built note (2026-05-29).** This proposal's early "Why" text described an ALTTPR shape that the §57 translation review later corrected, and the implementation diverged further. The accurate source-of-truth is now: ALTTPR's `HintService.php` produces **one** kind of hint NPC — **15 telepathic tiles** — plus a static per-goal Murahdahla line (`Randomizer.php:1029`). There is no distinct "Sahasrahla", "Storyteller", or "Bookshelf" hint *generator* upstream. See `design.md` §57.1 for the full correction. The bullets below are kept for historical context but are **superseded**.

Superseded early framing (do not treat as ground truth):

- ~~**Sahasrahla telepathic tiles**~~ — there is no Sahasrahla generator distinct from the telepathic tile near Sahasrahla.
- ~~**Storyteller**~~ — vanilla 20-rupee tip NPC; not a hint generator upstream.
- ~~**Bookshelves**~~ — not a hint source; dropped from scope.
- **Murahdahla** — real, but as a static per-goal text line, NOT a sphere-grouped piece-location hint generator.

Murahdahla matters for Triforce Hunt, but ALTTPR's Murahdahla is a fixed "bring me N pieces" line, not a sphere-aware piece-location listing. The as-built fork emits a region *summary* (count of regions holding pieces), not a per-sphere breakdown.

The ALTTPR upstream is split across two files:
- `../alttp_vt_randomizer/app/Services/HintService.php` (177 lines — verified) — placement-aware text-generation logic.
- `../alttp_vt_randomizer/app/Text.php` (1110 lines — verified) — the hint string body / dialogue dictionary.

The as-built fork ports only the *structure* (15 tiles + Murahdahla), not the per-location flavor text in `Text.php`. Hint text is a stable `"The <item> lies at <location>."` form; the full `Text.php` translation is deferred.

This change is a **new capability** — `randomizer-hints` — because hints are a separable subsystem from placement and from the broader text engine. Hint generation runs once at slot-creation, persists in the spoiler, and surfaces in-game by intercepting the vanilla telepathic-tile dialogue read.

## What Changes

> **As-built (2026-05-29).** The list below is the *original plan*; bullets are annotated with what actually shipped. See `design.md` "As-built summary" + §57 for detail.

- **New `src/rando/rando_hints.{c,h}` module.** Generates hint strings at slot-creation time, deterministically from the placement table. Hint strings stored in the slot's spoiler JSON under a `hints` array. **As-built:** matches, except the sub-RNG is seeded from the placement-table digest (`seed_hint_rng` in `rando_hints.c`), and the `spheres` parameter is accepted but unused (`(void)spheres;`).
- **Dialogue-ID injection** at hint NPC sites. **As-built (KEY divergence):** there is NO dynamic dialogue-ID carve on the runtime read path. The 15 telepathic tiles surface in-game by intercepting the **vanilla** telepathic-tile dialogue ids at the top of `Text_LoadCharacterBuffer` (`messaging.c`), which calls `Rando_RenderHintMessage`. No storyteller / bookshelf / Murahdahla sprite handler is wired in-game.
- **Assets pipeline**: per-slot hint set lives in a static module-local table (`g_hint_table` in `rando_hints.c`), not heap and not `g_ram`. The hint text is font-encoded on the fly in `Rando_RenderHintMessage` (`encode_hint_text`) and written into the messaging buffer.
- **Spoiler integration**: a `hints` array in the JSON spoiler. **As-built:** each entry is `{"npc": "<string_id>", "dialogue_id": <uint>, "text": "<...>"}` (`rando_spoiler.c` ~L314). The text spoiler mirrors it under a `Hints:` heading. There is **NO** `meta.hints_count`. The settings block separately carries `"hints": <0|1>`.
- **Triforce Hunt path**: Murahdahla. **As-built:** emitted only when `goal ∈ {TriforceHunt, GanonHunt}`; text is a *region summary* (`"Murahdahla: N Triforce piece(s) placed across M region(s)."`), NOT sphere-grouped piece locations. Murahdahla is spoiler-only — not surfaced by any in-game NPC.
- **Settings axis**: `hints`. **As-built:** binary on/off (`uint8 hints`, `kHintsMode_Off`/`kHintsMode_On`), canonical byte 22. CSV `hints=` accepts `off|0|false|none` → off and `on|1|true|sahasrahla|full` → on (`sahasrahla`/`full` are accepted *aliases*, NOT a functional tri-state). Default is **ON unconditionally** in `Settings_SetDefaults` — NOT goal-aware.
- **No new sprite handlers required** — for the 15 tiles this holds (the read path is intercepted in messaging). The storyteller / fortune-teller "fork extension" handlers were NOT wired.
- **`kGeneratorVersion` bumps**; hints participate in the determinism contract via `Hints_SelfCheck` and the corpus.

## Capabilities

### New Capabilities

- `randomizer-hints`: hint string generation pipeline, dialogue-ID allocation, per-NPC dispatch routing, spoiler integration. Bounded scope: hints are read-only (don't affect placement output); they're emitted by the generator and rendered by the runtime; no two-way interaction with placement except as a consumer of the placement table.

### Modified Capabilities

- `randomizer-placement`: ADDED Requirement for hint NPC dialogue-ID dispatch. **As-built:** `Rando_GetHintDialogueId(npc)` exists and is used by the **spoiler** to label entries (the `dialogue_id` field = `kRandoHintDialogueBase + (npc-1)`, base `0x200`). It is NOT consulted by any in-game sprite handler — the runtime path is the `Text_LoadCharacterBuffer` interception described above.
- `randomizer-core`: ADDED Requirement for the `hints` spoiler section + `hints` settings axis canonical-serialization entry. **As-built:** both shipped; `hints` is canonical byte 22.

## Impact

- **New module**: `src/rando/rando_hints.c` (generation + runtime render) + `src/rando/rando_hints.h` (API).
- **Text engine**: `src/messaging.c` — `Text_LoadCharacterBuffer` calls `Rando_RenderHintMessage` at its top to intercept the 15 vanilla telepathic-tile dialogue ids. No dynamic dialogue-ID band was carved on the read path.
- **Spoiler**: `src/rando/rando_spoiler.c` — emits the `hints` array (and `"hints": <0|1>` in the settings block).
- **Settings**: `src/rando/rando_settings.c` — `hints` axis (canonical byte 22, kGenVer 14) + CSV parse.
- **Slot persistence**: `src/rando/rando_save.c` — `hints_setting` at slot-header byte @66.
- **Asset blob**: NO new graphics; hints reuse vanilla text-engine tiles.
- **Regression risk**: `kGeneratorVersion` bumps; corpus regenerates. Hints are deterministic from `(settings, seed)` — `Hints_SelfCheck` + corpus catch drift.
- **Switch parity**: text rendering uses the existing text-engine; no platform-specific work.

## Status

**Implemented (Phase B Slice 5), docs reconciled 2026-05-29.** The shipped subsystem (`src/rando/rando_hints.{c,h}`) generates 15 telepathic-tile hints + a Murahdahla region summary, surfaces tile hints in-game by intercepting vanilla dialogue ids in `Text_LoadCharacterBuffer`, emits a `hints` spoiler array, and carries a binary on/off `hints` settings axis (canonical byte 22). The early design (4 sources, tri-state, `0x300` carve, goal-aware default) was superseded; see `design.md` "As-built summary" + "Deferred / not implemented", and §57 for the translation analysis. See [README.md](README.md) for the file index.
