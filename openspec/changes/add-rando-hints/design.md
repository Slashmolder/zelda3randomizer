## As-built summary (2026-05-29 — read this first)

The implementation (Phase B Slice 5, `src/rando/rando_hints.{c,h}`) diverged from the original design recorded in the "Decisions D1–D6" section below. The reality, verified against code:

- **Sources:** 15 telepathic-tile hints (`Rando_GenerateHints`, `rando_hints.c`) + 1 Murahdahla region-summary line (Triforce/Ganon Hunt only). NOT "4 sources". Storyteller/Bookshelf are not generated. The enum (`rando_hints.h`) reserves 4 "fork extension" ids (storyteller + 3 fortune tellers) but they are **not wired in-game**.
- **Runtime display (KEY divergence):** NO dynamic dialogue-ID carve is used on the read path — neither D2's `0x300+` nor §57.3's `0x200..0x20E`. Instead `Text_LoadCharacterBuffer` (`src/messaging.c`, the call site is the line guarded by `Rando_RenderHintMessage(dialogue_message_index, ...)`) intercepts the **15 vanilla US telepathic-tile message ids** (`kHintTileMsgIds[]` = `0xB5,0xB8,0xB9,0xBA,0xBB,0xBE,0xBF,0xC0..0xC7`; `0xB4` generic-default excluded) and renders the generated hint directly into the character buffer, skipping the vanilla decode. `Rando_RemapTeleMsg` is a vestigial stub, never called.
- **`0x200` is a spoiler label only.** `Rando_GetHintDialogueId(npc)` returns `kRandoHintDialogueBase (0x200) + (npc-1)`; this value appears at runtime ONLY as the `dialogue_id` field in the spoiler JSON (`rando_spoiler.c`). It is never used as a runtime dialogue-table key.
- **Settings axis:** `uint8 hints` (`kHintsMode_Off`/`kHintsMode_On`), canonical byte 22 (kGenVer 14). CSV `hints=` accepts `off|0|false|none` → off, `on|1|true|sahasrahla|full` → on. So it is **binary on/off**, with `sahasrahla`/`full` as accepted *aliases*, not a functional tri-state. Default is **ON unconditionally** (`Settings_SetDefaults`), NOT goal-aware.
- **Hint text:** stable `"The <item> lies at <location>."`; Murahdahla `"Murahdahla: N Triforce piece(s) placed across M region(s)."`. NO joke-pool fallback, NO `Text.php` per-location flavor translation, NO per-sphere grouping.
- **Determinism RNG:** sub-RNG seeded from the placement-table digest XOR `0x48494E5448494E54` ("HINTHINT"), via `seed_hint_rng` — not D6's `seed_u64 XOR magic`. Same `(settings, seed)` → same placement → same digest → same hints. Asserted by `Hints_SelfCheck`.
- **Spoiler:** a `hints` array of `{"npc","dialogue_id","text"}` objects (`rando_spoiler.c`), plus `"hints": <0|1>` in the settings block. There is NO `meta.hints_count`.
- **Slot persistence:** `hints_setting` carried at slot-header byte @66 (`rando_save.c`).
- **Storage:** static module-local `g_hint_table[]` (not heap, not `g_ram`), contra D3.

### Deferred / not implemented

- Storyteller + 3 fortune-teller "fork extension" sprite wiring (enum/spoiler ids exist; no in-game handler).
- Bookshelf hint source (dropped — see §57.1).
- Murahdahla as an in-game NPC (spoiler-only; static, no per-sphere piece locations).
- Full ALTTPR `Text.php` per-location flavor text + joke-pool (`strings/hint.txt`) fallback.
- Goal-aware default (default is unconditionally ON).
- A true functional tri-state `hints` axis (`sahasrahla`/`full` collapse to `on`).
- `meta.hints_count` spoiler field.
- Aginah / vanilla-NPC location-redirect hints (see the now-deferred spec requirement).

---

## Context

ALTTPR's hint generation lives in `app/Services/HintService.php` (177 lines, verified) and the hint text body in `app/Text.php` (1110 lines). Combined ≈ 1287 lines.

Phase A roadmap (`docs/randomizer.md:294`) named four hint sources: Sahasrahla telepathic, storyteller, bookshelf, Murahdahla. **That framing was wrong** — see §57.1: ALTTPR's generator produces only 15 telepathic tiles, plus a static per-goal Murahdahla line. The as-built fork follows §57.1, not the four-source framing.

The chunking critique flagged the capability-shape decision as worth resolving in design.md. This change records the decision.

## Goals / Non-Goals

**Goals** (as-built — original goals annotated):
- New `randomizer-hints` capability that owns hint generation + spoiler integration. **As-built:** done.
- ~~4 hint sources: Sahasrahla telepathic, storyteller, bookshelf, Murahdahla.~~ **As-built:** 15 telepathic tiles + a static Murahdahla region-summary line (§57.1). Storyteller/bookshelf NOT generated.
- ~~Hints settings axis (`off | sahasrahla | full`) with goal-aware default.~~ **As-built:** binary `off | on` axis (canonical byte 22); `sahasrahla`/`full` are accepted CSV aliases for `on`. Default is unconditionally ON, not goal-aware.
- Deterministic hint generation: same `(settings, seed)` → byte-identical hint set. **As-built:** done, via a placement-digest-seeded sub-RNG; `Hints_SelfCheck` asserts it.
- JSON + text spoiler integration with `hints` section. **As-built:** done (`{npc, dialogue_id, text}` array; no `hints_count`).
- **As-built addition:** in-game display of telepathic-tile hints via `Text_LoadCharacterBuffer` interception of vanilla tile dialogue ids.

**Non-Goals**:
- Inverted-specific hints (follow-on after #4a if any new NPC sites emerge).
- Murahdahla in non-Triforce-Hunt goals (the source NPC exists but its hint role is Triforce-Hunt-specific; empty entry for other goals).
- Cosmetic / customizer hint customization (Phase D).
- Voice-acted / animated hint delivery (out of scope).

## Decisions

### D1: New `randomizer-hints` capability vs. extending existing capabilities

**Options**:
- (a) **New capability `randomizer-hints`**. Peer to `randomizer-core` / `randomizer-placement` / `randomizer-shuffles` / etc. Clean boundary; hint regressions don't bleed into other capabilities.
- (b) **Extend `randomizer-placement`**: hint NPCs are dispatch sites; their dialogue ID resolution is a placement concern. Lower-coupling argument: no new capability surface.
- (c) **Extend `randomizer-core`**: hints are spoiler-integrated and generation-time; spoiler emission is already in `randomizer-core`.

**Decision**: **(a) new `randomizer-hints` capability.** Reasoning:
- Hint generation is a separable subsystem (the upstream `HintService.php` is a self-contained service class).
- The integration surface (dialogue ID injection, text-engine hooks) is distinct from placement (dispatch returns items, not text) and from spoiler (spoiler emits hints, doesn't generate them).
- Future Phase C/D hint refinements (e.g., voice acted, animated) localize to this capability.
- Cost: one more spec file. Benefit: clean ownership.

Spec deltas in this change:
- `randomizer-hints/spec.md` — generation pipeline + per-source-NPC contracts (NEW).
- `randomizer-placement/spec.md` — hint-NPC dispatch routing (ADDED requirement).
- `randomizer-core/spec.md` — hints settings axis + spoiler section (ADDED requirements).

### D2: Dialogue-ID range carve-out — SUPERSEDED

> **As-built:** This decision was NOT implemented. There is no runtime dialogue-ID carve (neither this `0x300..0x3FF` band nor §57.3's `0x200..0x20E`). Instead the runtime intercepts the **vanilla** telepathic-tile dialogue ids directly: `Text_LoadCharacterBuffer` (`src/messaging.c`) calls `Rando_RenderHintMessage(dialogue_message_index, messaging_text_buffer)` at its top, and when the slot is active and the id is one of the 15 vanilla tele ids (`kHintTileMsgIds[]`), renders the generated hint into the character buffer and returns — skipping the vanilla decode. The `0x200` base (`kRandoHintDialogueBase`, `Rando_GetHintDialogueId`) survives only as the `dialogue_id` *label* in the spoiler JSON, not as a runtime table key. See "As-built summary" and §57.4.

Original (superseded) decision: ALTTPR injects hint text by replacing existing vanilla dialogue IDs with per-slot hint strings. zelda3's text engine in `src/messaging.c` has a fixed dialogue table. The plan was to carve a "dynamic" dialogue ID range — IDs `0x300..0x3FF` (256 entries) — that the runtime treats as a slot-specific lookup against a `g_rando_hint_dialogue_table`. Not built; the vanilla-id interception above replaced it.

### D3: Hint-text storage — SUPERSEDED (option (a) shipped, not (b))

> **As-built:** The shipped storage is a **static module-local table** `g_hint_table[kRandoHintNpc__Count]` (`rando_hints.c`), each entry holding a fixed `char text[160]` plus the placement loc/item ids — NOT a heap allocation. It is cleared (`Rando_ClearHints`) rather than freed. This is closer to original option (a) than (b).

Original options / decision (superseded): (a) static dictionary vs. (b) heap-allocated per-slot table; the decision recorded (b). The implementation instead used a static fixed-size table — the hint count is bounded (≤21) so no heap is needed.

### D4: Goal-aware hints default — SUPERSEDED

> **As-built:** The default is **unconditionally ON** — `Settings_SetDefaults` (`rando_settings.c`) sets `s->hints = kHintsMode_On` with no goal dependence. There is no `full`/`sahasrahla` distinction (the axis is binary; both are aliases for `on`). Murahdahla still only emits on Triforce/Ganon Hunt, but that's a generation-time check inside `Rando_GenerateHints`, not a default-resolution rule.

Original (superseded) decision: a goal-aware default (`full` for Triforce/Ganon Hunt, `sahasrahla` otherwise) in `SetDefaults`. Not built — the binary axis made it moot, and ALTTPR's own default is `on` everywhere (§57.6).

### D5: Murahdahla in non-Triforce-Hunt goals

ALTTPR shows Murahdahla in dark world; the NPC is always-present. The hint role is Triforce-specific.

> **As-built:** Murahdahla is **spoiler-only** (no in-game NPC handler is wired). The Murahdahla hint entry is populated by `Rando_GenerateHints` only when `goal ∈ {kGoal_TriforceHunt, kGoal_GanonHunt}`; for all other goals no entry is created (so the question of "generic line vs. silent" never reaches the player — it simply doesn't appear in the spoiler).

### D6: Hint determinism RNG

Hint generation runs against the placement table. The order in which hints are selected SHALL be deterministic.

> **As-built:** Implemented, with a different (and stronger) seed source than recorded here. `seed_hint_rng` (`rando_hints.c`) computes the SHA-256 placement-table digest (`PlacementTable_ComputeDigest`), takes its first 8 bytes as a `uint64`, and XORs the magic constant `0x48494E5448494E54` ("HINTHINT"). The hintable pool is Fisher-Yates shuffled (`shuffle_u16`) and the 15 tiles sample without replacement. Because the seed derives from the placement digest (itself deterministic from `(settings, seed)`), changing placement DOES shift hint assignment — but that is the intended "same seed → same hints" contract, asserted by `Hints_SelfCheck`. Note the `spheres` parameter is accepted but unused (`(void)spheres;`).

Original (superseded) wording: a `Rng_NextU32`-derived sub-RNG seeded from `(seed_u64 XOR 0xH1_HINTS)`, described as "distinct from the placement RNG so changing placement doesn't shift hint text." The as-built seed is *derived from* the placement digest, so it intentionally tracks placement.

## Risks / Trade-offs

| Risk | Mitigation (as-built) |
|---|---|
| Hint text format divergence from ALTTPR convention | Accepted — the fork uses a stable `"The <item> lies at <location>."` form, not ALTTPR's per-location flavor text. Determinism (not ALTTPR-byte-equivalence) is enforced by `Hints_SelfCheck` + the corpus. |
| ~~Dynamic dialogue ID range collides with future vanilla content~~ | N/A — no runtime dialogue-ID range was carved (D2 superseded). The runtime intercepts existing vanilla tele ids instead, so there is no new ID space to collide. |
| Hint text body in `Text.php` (1110 lines) is hard to translate | Deferred entirely — the `Text.php` per-location flavor translation was not done; only the structural port (15 tiles + Murahdahla) shipped. |
| Triforce Hunt hints leak placement | Murahdahla is spoiler-only here, so the leak is only via the spoiler file (race-mode reveal already suppresses on-disk full spoiler — §57.5). |

## Migration Plan

No user-data migration. Hints default **ON** for all new slots (`Settings_SetDefaults`). Slot files written before the hints extension read `hints_setting = 0` at byte @66; `Rando_ActivateSidecarSlot` applies the hints-on default for such pre-extension files (`rando_save.c`). On load the static `g_hint_table` is repopulated from the slot's settings + seed; with hints off it stays empty and tiles play vanilla text.

## Open Questions — RESOLVED by the implementation

1. ~~Sahasrahla telepathic tile count?~~ **Resolved:** 15 hint-bearing tiles (`kHintTileMsgIds[]`, the vanilla US tele ids `0xB5,0xB8,0xB9,0xBA,0xBB,0xBE,0xBF,0xC0..0xC7`; `0xB4` generic-default excluded). Each gets a distinct generated hint sampled without replacement from the shuffled hintable pool — no shared pool of text.
2. ~~Bookshelf hint vs. book-of-mudora collision?~~ **Resolved:** moot — bookshelf hints were dropped from scope (§57.1, `rando_hints.h` header comment).
3. ~~Storyteller NPC location?~~ **Resolved:** moot in-game — the storyteller (and 3 fortune tellers) exist only as reserved spoiler enum ids (`kRandoHintNpc_Fork*`); no in-game handler is wired.
4. ~~Per-hint length cap?~~ **Resolved:** `HintEntry.text` is capped at `kRandoHintTextMax` (160) ASCII bytes; at render time `encode_hint_text` word-wraps to ~13 glyphs/row, paginates with a wait-key when over three rows, and writes into the ≥256-byte `messaging_text_buffer`, terminating with `0x7f`.

## Runtime intercept NOT wired (task #85 — verified by grep 2026-05-27)

`Rando_RemapTeleMsg(uint16 vanilla_id, uint16 room_or_area, bool is_overworld)` is defined at `src/rando/rando_hints.c:68` and exported from `src/rando/rando_hints.h:132`, but is **never called from any other source file** (`grep -rn "Rando_RemapTeleMsg" src/` returns only the definition + selftest references). Even when the hint-generator body lands and populates the `g_rando_hint_dialogue_table` with hint text, the runtime telepathic-tile read path will continue to source from the vanilla asset blob — players will see no hint text in-game.

**Wire site**: per the §57.4 design notes, the intercept lives at `src/messaging.c:2273-2276` in `Text_LoadCharacterBuffer`. Logic shape:

```c
// at messaging.c:2273-2276 (the FindIndexInMemblk call site):
if (Rando_DialogueIdIsHint(dialogue_message_index)) {
  uint16 idx = dialogue_message_index - kRandoHintDialogueBase;
  // source bytes from g_rando_hint_dialogue_table[idx].encoded_bytes
  // instead of the asset blob.
} else {
  // vanilla path: FindIndexInMemblk(g_zenv.dialogue_blk, 1, ...)
}
```

The tile-side wrap (per §57.4) at `src/player.c:3825-3833` (`Link_PerformRead`'s `dialogue_message_index = Dungeon_GetTeleMsg(...)` / `Overworld_GetSignText(...)`) similarly needs to wrap through `Rando_RemapTeleMsg` to translate vanilla IDs to dynamic hint IDs.

Both intercepts are pre-requisites for the hint generator body to produce any visible behavior; lands as one focused commit when the generator body work happens.

## §57 Translation status

This section translates ALTTPR's `app/Services/HintService.php` (177 lines, verified `wc -l` 2026-05-27) + hint-bearing text bodies in `app/Text.php` (1110 lines) into a concrete shape for `Rando_GenerateHints`. The audit corrects several capability-shape and source-count claims earlier in this file; see §57.1 and §57.7.

> **Resolution (2026-05-29).** The §57 analysis below is still accurate as a *translation reference* and its recommendations were largely followed: drop Storyteller/Bookshelf (§57.1), binary on/off axis (§57.6), no sphere data (§57.2). Several open "TBD"/"decision pending" markers are now resolved — see the "As-built summary" at the top of this file for what shipped. Notably, the §57.2 generator algorithm (5-step pool) and the §57.3 `0x200..0x20E` runtime dialogue carve were **NOT** built as written: the shipped generator picks 15 non-junk placements at random without replacement, and the runtime intercepts the vanilla telepathic-tile message ids directly (no carve). Keep §57 for provenance; trust the As-built summary for current behavior.

### §57.1 Hint sources enumeration — actual ALTTPR shape

Critical finding: ALTTPR's `HintService.php` produces **one and only one** kind of hint NPC — **15 telepathic tiles**. There is no "Storyteller hint" or "Bookshelf hint" upstream. Earlier proposal/design text ("4 sources: Sahasrahla telepathic, storyteller, bookshelf, Murahdahla") conflated the in-engine dark-world hint sprite ("Storyteller" = `Sprite_28_DarkWorldHintNPC` at `src/sprite_main.c:9852`, the 20-rupee health-restoring NPC) with hint-text *generation*. Murahdahla is a separate static text-string set per goal in `Randomizer.php`, NOT a generator output.

**A. Telepathic tiles (sole HintService.php output)**
- `app/Services/HintService.php:59-75` lists 15 string-IDs: `telepathic_tile_eastern_palace`, `..._tower_of_hera_floor_4`, `..._spectacle_rock`, `..._swamp_entrance`, `..._thieves_town_upstairs`, `..._misery_mire`, `..._palace_of_darkness`, `..._desert_bonk_torch_room`, `..._castle_tower`, `..._ice_large_room`, `..._turtle_rock`, `..._ice_entrace` [sic], `..._ice_stalfos_knights_room`, `..._tower_of_hera_entrance`, `..._south_east_darkworld_cave`.
- Bodies in `app/Text.php:437, 439, 445, 447, 449, 451, 467, 469, 471, 473, 475, 477, 479, 481, 858` (vanilla fallback strings — overwritten by HintService when `spoil.Hints === 'on'`).
- **N per slot = 15 tiles** with content allocated as follows (`HintService.php:89-174`):
  1. *Conditional* GT-Big-Key hint (1 tile) — `region.wildBigKeys=true` only (`HintService.php:89-99`).
  2. *Always* Pegasus Boots location hint (1 tile, `HintService.php:101-108`).
  3. *Always* 5 location-pool hints drawn without replacement from a fixed 10-entry pool (Sahasrahla item, Mimic Cave, Catfish, Graveyard Ledge, Purple Chest, ToH Big Key Chest, SP Big Chest, MM-pair, SP-pair, Pyramid Fairy-pair) (`HintService.php:76-87, 110-130`).
  4. *Up to 4* advancement-item hints filtered by class — excluding `Shield, Key, Map, Compass, BigKey (unless wildBigKeys), Bottle, Sword, TenBombs, HalfMagic, BugCatchingNet, Powder, Mushroom` (`HintService.php:132-152`).
  5. *Remaining tiles filled* with random-location hints, with joke-fallback from `strings/hint.txt` (146 lines, `HintService.php:34-43, 168-174`).
- Hint text format: `getHint()` returns `ucfirst("$item_name $location_name")` (`Location.php:195-218`); item and location names come from Laravel translation tables `__('hint.item.<RawName>')` and `__('hint.location.<name>')` which can be arrays — one entry picked via `fy_shuffle()`.

**B. Murahdahla (NOT a HintService output — static per-goal text in Randomizer.php)**
- `app/Randomizer.php:1029` sets the Murahdahla text *only* when `goal === 'triforce-hunt'`. The body is a fixed `sprintf("Hello @. … If you bring %d triforce pieces, I can reassemble it.", item.Goal.Required)` — NO sphere-grouped Triforce-piece-location hints upstream.
- The proposal's claim "Murahdahla surfaces Triforce piece locations grouped by sphere" is **NOT** what ALTTPR does. **TBD: verify** whether we want to (a) match ALTTPR exactly (static "bring me N pieces" line) or (b) extend it with sphere-aware piece-location hints (out of scope for ALTTPR-equivalence).
- N per slot = 1 message (only on Triforce Hunt goal).

**C. Storyteller / Bookshelf — NO upstream hint generation**
- `grep -rin "storyteller\|bookshelf" app/` returns only `app/Sprite.php:134` (`new Sprite("Storytellers", [0x28])` — entity definition, not hint source).
- Storyteller text in vanilla is `0x149` (rendered via `Sprite_ShowSolicitedMessage(k, 0x149)` at `src/sprite_main.c:9913`, e.g. "I can give you a tip for 20 rupees" mechanic). HintService never touches it.
- Recommendation: **drop Storyteller and Bookshelf from this slice's scope.** Update the proposal "4 sources" claim. If desired in Phase D, add them as a *zelda3-original* hint extension (not ALTTPR-equivalent), and document accordingly.

### §57.2 Hint generation algorithm — pseudo-C for `Rando_GenerateHints`

```c
// Input: settings, placement table, sphere data.
// Output: g_hint_table[15] populated with strings.
// Determinism: derive a sub-RNG from settings.seed_u64 XOR 0xH1NTS_5L1C3.
bool Rando_GenerateHints(settings, placements, spheres) {
  if (settings->hints == kHintsMode_Off) { ClearTable(); return true; }
  RandoRng rng; Rando_RngInit(&rng, settings->seed_u64 ^ 0xH1NTS5L1C3);

  // 15 tiles (kHintTileIds[] mirrors HintService.php:59-75)
  int tile_count = 15, slot = 0;
  uint16 shuffled_tiles[15]; ShuffleU16(rng, shuffled_tiles, kHintTileIds, 15);

  // Step 1: conditional GT-Big-Key hint (rando "wildBigKeys" — TBD: verify
  // settings axis name; default false in ALTTPR but on for most zelda3-fork
  // shuffles per add-rando-shuffles).
  if (settings->shuffle_big_keys) {
    Location gtbk = FindLocationOfItem(placements, kItem_BigKeyA2);
    if (gtbk.valid) WriteHint(shuffled_tiles[slot++], FormatGetHint(gtbk));
  }
  // Step 2: Pegasus Boots hint (always)
  Location boots = FindLocationOfItem(placements, kItem_PegasusBoots);
  if (boots.valid) WriteHint(shuffled_tiles[slot++], FormatGetHint(boots));

  // Step 3: 5 fixed-pool location hints (HintService.php:76-87, 110-130)
  uint8 picks[10]; ShuffleU8(rng, picks, kFixedPoolLocations, 10);
  for (int i = 0; i < 5 && slot < tile_count; i++) {
    Location loc = ResolveFixedPoolEntry(placements, picks[i]);
    if (loc.valid) WriteHint(shuffled_tiles[slot++], FormatGetHint(loc));
  }
  // Step 4: up to 4 advancement-item hints (HintService.php:132-152)
  int budget = (tile_count - slot) < 4 ? (tile_count - slot) : 4;
  Item items[64]; int n = FilterAdvancementItems(placements, items, settings);
  ShuffleItem(rng, items, n);
  for (int i = 0; i < budget && i < n; i++) {
    Location loc = FindLocationOfItem(placements, items[i]);
    if (LocationIsHintable(loc)) WriteHint(shuffled_tiles[slot++],
                                           FormatGetHint(loc));
  }
  // Step 5: fill remaining with random hintable locations + joke fallback
  while (slot < tile_count) {
    Location loc = PickRandomHintable(rng, placements);
    const char *txt = loc.valid ? FormatGetHint(loc) : JokeFromTable(rng);
    WriteHint(shuffled_tiles[slot++], txt);
  }

  // Step 6 (Triforce Hunt only): Murahdahla static line
  if (settings->goal == kGoal_TriforceHunt) {
    WriteMurahdahla(settings->pieces_required);
  }
  return true;
}
```

Sphere data is **not** used by upstream HintService — it operates on the static placement table. The proposal's claim "Sahasrahla tells about items NOT YET PLACED in their inventory; relies on sphere data" is incorrect with respect to ALTTPR. `Rando_GenerateHints` should accept the `spheres` parameter only for forward-compatibility (mark `(void)spheres;` until / unless a sphere-aware mode is added).

Hint-string format reproduction: see `Location.php:205, 211` — need lookup tables `kRandoItemHintName[]` and `kRandoLocationHintName[]` keyed off `ITEM_*` / `LOC_*` ids. Translations source: Laravel `resources/lang/en/hint.php` (TBD: verify path; not in scope of this section but a translation prerequisite).

### §57.3 Dialogue-ID range carve

Vanilla zelda3 dialogue IDs (16-bit, `variables.h:833 — #define dialogue_message_index (*(uint16*)(g_ram+0x1CF0))`) range over the asset blob indexed via `FindIndexInMemblk(g_zenv.dialogue_blk, 1)` at `src/messaging.c:2275-2276`. The highest-assigned vanilla ID observed by grep across all `dialogue_message_index = ...` writes is `0x18a` (`src/sprite_main.c:19466`). ALTTPR's header comment `app/Text.php:13-15` says "394 required things in table … extended a bit by the randomizer" (394 = 0x18A). Strong agreement.

**Allocation proposal**:
- Carve range `0x200..0x20E` (15 IDs) as the dynamic hint range — one ID per telepathic tile, fixed positional mapping (`tile_index = dialogue_id - 0x200`).
- Reserve `0x20F` for Murahdahla dynamic text (Triforce Hunt N value).
- Worst-case allocation: **16 IDs total** (15 tiles + 1 Murahdahla). Storyteller/Bookshelf are dropped from scope (§57.1).
- Intercept point: modify `Text_LoadCharacterBuffer` at `src/messaging.c:2273-2276` so that when `dialogue_message_index >= 0x200 && dialogue_message_index < 0x200 + kRandoHintNpc__Count`, the source bytes come from `g_rando_hint_dialogue_table[idx].encoded_bytes` instead of `FindIndexInMemblk(dialogue, ...)`. The buffer is malloc'd in `Rando_GenerateHints`, pre-encoded via a port of `Dialog::convertDialogCompressed` (TBD: verify byte format — references at `app/Support/Dialog.php`, not yet read).
- The carve assumes the asset blob's index space doesn't extend into `0x200+`; verify at apply-time by `printf("dialogue_blk[1] count = %d\n", FindIndexInMemblk(...).count)` once.

### §57.4 Text-engine integration touch-points

**Tile-side (telepathic tile read)**: `src/player.c:3825-3833` `Link_PerformRead` writes `dialogue_message_index = Dungeon_GetTeleMsg(dungeon_room_index)` (indoors) or `Overworld_GetSignText(overworld_screen_index)` (outdoors). Both functions return a `uint16` from asset tables (`kDungeonRoomTeleMsg` at `src/assets.h:29`; `kOverworld_SignText` at `src/overworld.c:299-301`).

Cleanest hook: **wrap these two functions** behind a rando helper:
```c
// in src/rando/rando_hints.h:
uint16 Rando_RemapTeleMsg(uint16 vanilla_id, uint8 room_index, bool is_overworld);
```
Inside `Rando_RemapTeleMsg`, look up `(room_or_area, vanilla_id) → kRandoHintNpc` via a small table mirroring the 15 ALTTPR sites (need to map each `telepathic_tile_*` string-ID to a room number; TBD: build the table by cross-referencing `kDungeonRoomTeleMsg` against `app/Text.php` IDs). If a match and `Rando_GetHintDialogueId(npc) != 0xFFFF`, return the dynamic ID; otherwise return `vanilla_id`.

**Murahdahla**: Murahdahla is rendered via `Sprite_28_DarkWorldHintNPC` (`src/sprite_main.c:9852`) — **wait, this is the wrong sprite**. `Sprite_28_DarkWorldHintNPC` is the 20-rupee health-restorer. Murahdahla is a separate dark-world NPC. **TBD: verify** the sprite handler for Murahdahla; grep for the text id binding. Worst case: Murahdahla shares one of the existing dialogue sites (e.g. `0xFE`/`0xFF`/`0x100..0x103` used at `sprite_main.c:9866, 9881, 9896, 9925`). For now, scope Murahdahla as **statically overriding** dialogue ID 0xFF (or whichever ALTTPR uses) at slot-load time by writing a per-slot string into the dialogue-blob shadow.

**Storyteller, Bookshelf**: scope-dropped (§57.1).

### §57.5 Spoiler JSON shape for `hints`

```json
"hints": {
  "telepathic_tile_eastern_palace":           "<encoded text>",
  "telepathic_tile_tower_of_hera_floor_4":    "...",
  "telepathic_tile_spectacle_rock":           "...",
  "telepathic_tile_swamp_entrance":           "...",
  "telepathic_tile_thieves_town_upstairs":    "...",
  "telepathic_tile_misery_mire":              "...",
  "telepathic_tile_palace_of_darkness":       "...",
  "telepathic_tile_desert_bonk_torch_room":   "...",
  "telepathic_tile_castle_tower":             "...",
  "telepathic_tile_ice_large_room":           "...",
  "telepathic_tile_turtle_rock":              "...",
  "telepathic_tile_ice_entrace":              "...",
  "telepathic_tile_ice_stalfos_knights_room": "...",
  "telepathic_tile_tower_of_hera_entrance":   "...",
  "telepathic_tile_south_east_darkworld_cave":"...",
  "murahdahla":                               "..."   // only if goal == triforce-hunt
}
```

Keys match ALTTPR string-IDs (15 + 1) exactly for cross-tool diff-ability. Strings are UTF-8 (post-decoding, human-readable — the on-wire dialogue bytes stay encapsulated in the runtime table; spoiler shows the source string before encoding).

**Race-mode behavior**: Per `src/rando/rando_spoiler.c:301-491` (Slice 6 contract), race mode suppresses the on-disk full spoiler entirely and writes a stub. Confirmed: `hints` MUST be in the full canonical JSON when race mode is active (it participates in the stamp at `rando_spoiler.c:391-446` — `norm_settings.race_mode = 0` ensures the stamp is over the full content), but the full JSON is not written to disk until post-race reveal. No extra carve needed.

### §57.6 CSV settings axis grammar

- Key: `hints` (lowercase, matches ALTTPR config key naming).
- Values: `off | sahasrahla | full` — **OR** `off | on` matching ALTTPR exactly (`HintService.php:54` tests `=== 'on'`). **Decision pending**: if zelda3-fork honors §57.1 and drops Storyteller/Bookshelf, the tri-state collapses to binary. The 3-state grammar makes sense only if Phase D adds new sources. **Recommendation**: ship binary `off | on` now; reserve 2 bits in canonical serialization for forward compat.
- Default per goal: `on` for `triforce-hunt | ganonhunt`; `on` otherwise (ALTTPR default is `on` everywhere — `RandomizerController.php:158` `$request->input('hints', 'on')`). The proposal's "sahasrahla default for non-Triforce-Hunt" is **NOT** what ALTTPR does — it's always `on`. **TBD: verify** whether zelda3-fork wants to diverge.
- Bit width in canonical serialization: **2 bits** (matches proposal §D4 reserved space; allows 0=off, 1=on, 2=reserved, 3=reserved).
- Parse site: `Settings_ParseCsv` at `src/rando/rando_settings.c:689` — add a row to the value-table dispatch (pattern mirrors `goal=...` and `race_mode=...` already present at lines 264, 274).

### §57.7 Risks / unknowns (block implementation until resolved)

1. **Source count divergence** (HIGH): Proposal claims 4 hint sources; ALTTPR has 1 (telepathic tiles only) + Murahdahla as static-per-goal text. Recommend updating `proposal.md` to match before implementation begins. If scope-extension to Storyteller/Bookshelf is desired, it's a *new feature* not an *ALTTPR port* and should be a separate slice / capability.
2. **Murahdahla sphere-grouped piece-location claim** (HIGH): Proposal claims "Triforce piece locations grouped by sphere"; ALTTPR has only a static "bring me N pieces" line at `Randomizer.php:1029`. **User clarification needed**: ALTTPR-equivalent (static line) or extension (sphere-aware locations)?
3. **`spoil.Hints` value grammar** (MED): ALTTPR is binary `on`/anything-else (`HintService.php:54`); proposal §D4 specifies tri-state `off|sahasrahla|full`. Recommend either matching ALTTPR (binary) or documenting the divergence explicitly. The bit-width allocation (2 bits) is forward-compatible either way.
4. **Sphere parameter shape** (LOW): `Rando_GenerateHints` signature accepts `spheres` but upstream doesn't use it. Keep for forward-compat; mark unused.
5. **Dialogue encoding pipeline** (MED): `Dialog::convertDialogCompressed` in `app/Support/Dialog.php` (not read) converts ASCII to the SNES text-engine byte stream (control codes `{BOTTOM}`, `{NOBORDER}`, `{SPEED6}`, `{HARP}`, `{PAUSE3}`, etc.). zelda3-fork already decodes these in `src/messaging.c` — verify that we can *encode* them too, or that hint strings can be authored in pre-encoded byte form. Port cost: ~200-400 LOC.
6. **Hint-blob length cap per dialogue** (MED): vanilla telepathic tile bodies in `Text.php` are ≤ 96 chars (e.g. line 437 = 53 chars after `{NOBORDER}\n`). `getHint()`-generated strings like "Pegasus Boots is at Pyramid Fairy - Left" can run 40-60 chars; safe. But "Bow {And, &} 10 Arrows is at Ganon's Tower - Big Key Chest" can exceed 80. **Verify**: `messaging_text_buffer` size at `src/messaging.c:2278` and the per-message buffer cap.
7. **`hint.item.<RawName>` and `hint.location.<name>` translation tables** (MED): ~150 item names + ~140 location names of hint-name aliases. Translation effort ≈ 2-3 days of focused work; manageable.
8. **Determinism under setting drift** (LOW): goal-aware default (§57.6) means changing `goal=...` flips hints-on by default — corpus catches this; non-issue.
9. **`fy_shuffle`/`get_random_int` semantics** (LOW): Both are Mersenne-Twister-seeded helpers in PHP. zelda3-fork uses a custom RNG (`Rando_RngInit` at `src/rando/rando_rng.c`, not read). For byte-exact ALTTPR-equivalent hints, the RNG sequence must match — which it won't, ever. Accept hint-text drift from upstream; rely on internal corpus for determinism.

