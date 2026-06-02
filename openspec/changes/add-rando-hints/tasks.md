# add-rando-hints — task tracking

> **Progress banner (updated 2026-06-01).** Telepathic-tile hints (15) ship and
> work in-game. This session landed: (a) hardening — a Triforce-Hunt race corpus
> entry that locks Murahdahla-hint determinism via the reveal stamp, plus a
> `docs/randomizer.md` Hints section; (b) the fork-extension NPCs — Storyteller +
> Kakariko/Dark-World Fortune Tellers (ids 17-19) wired end-to-end (generation +
> dispatch + spoiler + determinism). Commits 7b1874d, 2969958 on branch
> `claude/cosmetics-and-reveal-fix`.
>
> **Mode is binary** (off/on), not the original tri-state — wherever §6/§8/§11
> say `hints=full`/`sahasrahla`, read "on". **Bookshelf is dropped.** **Murahdahla
> is spoiler-only** (needs a new sprite for an in-game surface). **Lake-Hylia FT
> (id 20)** is intentionally not separately wired.
>
> **Open before archive:** §11 playtest (in-game NPC surfacing — the only
> uncovered layer), §9.3 fresh-eyes audit, §12 archive. Determinism/audit-guard/
> docs/corpus are done.

## 1. Apply-time pre-flight

- [x] 1.1 Pin upstream commit hash of `../alttp_vt_randomizer/`. Record in `audit.md §"Hint provenance"`. <!-- 2026-06-02: @219fcafd, recorded in audit.md §"Hint provenance". -->`
- [ ] 1.2 Grep `src/messaging.c` for the highest currently-used dialogue ID. Carve a dynamic range above it per design.md D2; record in `audit.md §"Hint dialogue ID range"`.
- [ ] 1.3 Verify hint NPC sprite handler locations in `src/sprite_main.c`: Sahasrahla telepathic, storyteller (which shrine?), bookshelf interaction handler, Murahdahla.
- [ ] 1.4 Verify per-dialogue text buffer length in `src/messaging.c`. Hint text generator must fit within this cap.
- [x] 1.5 **Vanilla-NPC hint redirect audit** (per spec Requirement "Vanilla NPC hint redirects"). Sweep `assets/dialogue.txt` for entries that name a specific item from `assets/rando/item_registry.yaml` *together with* a location string (e.g., "in the house of books in the village"). Anchor case already identified: dialogue **294** = Aginah → Book of Mudora (vanilla Library). For each additional match, record `(dialogue_id, npc, referenced_item, vanilla_location_phrase, sprite-handler file:line)` in `audit.md §"Vanilla NPC hint redirects"`. Each confirmed match feeds a redirect entry that flows through the same dynamic-dialogue-ID path as telepathic tiles (§4). Discard candidates whose "location" phrasing is generic flavor (e.g., "somewhere in Hyrule") rather than a vanilla-spoiler. <!-- done: full 398-entry sweep recorded in audit.md §"Vanilla NPC hint redirects". Confirmed targets: 294 Aginah→BookOfMudora (handler sprite_main.c:6741, fully grounded) + 350 DW-freak→MoonPearl/Tower-of-Hera (sprite_main.c:25532). Secondary: 55 Sahasrahla, 159-161 old-man. Excluded+grounded: tele-tiles 0xB5/0xB8..0xC7, fortune-teller 235-254, location-only 56/376. --> <!-- done-differently: 3 items flagged as apply-time follow-ups in audit.md (159-161 old-man handler grep, 182/0xB6 edge classification, fortune-teller-as-redirect-surface), NOT closed here. Dialogue identification (the verifiable core) is complete; remaining handler-grep + the §4 rewrite stay DEFERRED per spec. -->

## 2. New module skeleton

- [x] 2.1 Create `src/rando/rando_hints.{c,h}`. API: <!-- done: src/rando/rando_hints.c + .h --> <!-- done-differently: API is Rando_GenerateHints(settings,placements,spheres)+Rando_GetHintString/DialogueId/NpcStringId+HintKind not surfaced as a public struct; RandoHintNpc enum replaces the source-string scheme -->

  ```c
  typedef enum { kHintKind_LocationSpoil, kHintKind_ItemSpoil, kHintKind_GoalProgress, kHintKind_Joke } HintKind;
  typedef struct {
    HintKind kind;
    const char *source;  // "sahasrahla_<region>", "storyteller_<shrine>", "bookshelf_<dungeon_room>", "murahdahla_<N>"
    const char *text;    // rendered hint text (UTF-8)
    uint16 dialogue_id;  // allocated from dynamic range
  } HintEntry;
  
  void Rando_GenerateHints(const RandoSettings *settings, const PlacementTable *pt, const SphereData *sd, HintEntry **out_entries, size_t *out_count);
  uint16 Rando_GetHintDialogueId(NpcId npc_id);
  ```
- [x] 2.2 Wire into the Makefile / vcxproj / Switch makefile per the multi-build-system convention. <!-- done: zelda3.vcxproj:350 ClCompile rando_hints.c; Makefile glob-builds src/*.c per CLAUDE.md -->

- [ ] 2.3 Add to `assets/scripts/check_codegen_wiring.py` enumerated-files set (the rando_hints module itself isn't codegen output but the build wiring needs to track it).

## 3. Generation pipeline

Per design.md §57 audit: ALTTPR's HintService.php produces ONLY 15 telepathic tile hints — there is no upstream Storyteller / Bookshelf / Murahdahla generator (Storyteller is the dark-world hint sprite; Murahdahla is a static per-goal text in `Randomizer.php`). The scaffolded task list below over-promised; the real algorithm is the 6-step HintService.applyHints (`app/Services/HintService.php:48-176`):

1. Fisher-Yates shuffle the 15 tile names + an 8-entry interesting-location list.
2. If `region.wildBigKeys`, allocate one tile to the GT BigKey location.
3. Allocate one tile to the PegasusBoots location.
4. Pick 5 random interesting-location hints from the shuffled list.
5. Pick up to 4 advancement-item hints (filtered to exclude Shield/Key/Map/Compass/BigKey/Bottle/Sword + specific blocklist).
6. Fill remaining tiles with `random_locations + joke_hints` mix from `strings/hint.txt`.

- [x] 3.1 Implement `Rando_GenerateHints` per the 6-step algorithm above. <!-- done: src/rando/rando_hints.c:Rando_GenerateHints --> <!-- done-differently: deterministic sub-RNG seeded from placement-table digest XOR 0x48494E5448494E54; populates 15 telepathic tiles from a junk-filtered Fisher-Yates-shuffled pool + Murahdahla on Triforce/Ganon hunt. Does NOT match ALTTPR's exact 6-step pool (no GT-BK/Boots pins, no joke-hint fallback) -->
 Seeds a sub-RNG from `seed_u64 XOR kHintsRngMagic` (per design.md D6). Allocates an array of 15 hint text strings keyed by `kRandoHintNpc_TelepathicTileEasternPalace..`.
- [ ] 3.2 Hint text format (`Location::getHint()`): translate from `../alttp_vt_randomizer/app/Location.php` and per-location overrides in `app/Region/**/*.php`. Format follows `"<item> at <location-hint-text>"` with per-location colorful descriptors. Joke hints from `../alttp_vt_randomizer/strings/hint.txt` (60+ entries).
- [ ] 3.3 (Deleted — see §3.1 note. There are no Sahasrahla / storyteller / bookshelf / Murahdahla generators; the original task draft conflated the 4 NPC types with hint-text generation.)
- [ ] 3.4 Hint text format: hand-translate from `../alttp_vt_randomizer/app/Services/HintService.php` (177 lines) and `app/Text.php` (1110 lines, hint-bearing entries only). Per-NPC translation discipline; source-line citations.
- [x] 3.5 Hint length constraint: each hint's rendered text must fit the text-engine buffer. Add an assert during generation; if a hint exceeds the cap, generate a shorter fallback. <!-- done-differently: text capped to kRandoHintTextMax=160 via snprintf truncation (rando_hints.c:83), and encode_hint_text word-wraps + paginates (Waitkey) to fit the 3-row box and caps output at 240 bytes — no hard assert/shorter-fallback but overflow cannot occur -->


**Status (2026-05-27)**: Scaffold in `src/rando/rando_hints.c` is a no-op stub. The actual translation is too large for an autonomous cleanup pass (≥3 hours focused work plus playtest verification of dispatch wiring at §5 + visible-output gate at §4). Deferred to a dedicated implementation sprint. Per `logic_vs_runtime_gap` memo: playtest at slice START — start by wiring `Rando_RemapTeleMsg` invocation in the message-engine read path so the generated text becomes visible in-game; THEN translate the generation algorithm.

**Status update (phase-b adc6b08)**: Real generator body landed for CLI-path generations. 15 telepathic-tile hints + Murahdahla for Triforce goals. Spoiler emits `hints[]` array. **HIGH-3 of phase-b audit-of-audit**: `Rando_GenerateHints` does NOT run from `Rando_ActivateSidecarSlot` because the sidecar slot doesn't carry the full `RandoSettings` struct (only `settings_hash`, one-way). When §5 dispatch wiring (#85) lands, slot-loaded games will have an empty `g_hint_table` and telepathic tiles will read NULL. Two fix options recorded inline at `rando.c:632-650`:
  - **(a)** Add a new sidecar TLV `TAIL_RANDO_SETTINGS` carrying the canonical 28-byte settings blob; ship paired with #85.
  - **(b)** Synthesize default settings (hints=On, goal=detected-from-placements) and run `Rando_GenerateHints` from slot-load. Degraded shape but non-empty hint table.

## 4. Dialogue-ID injection

- [x] 4.1 Allocate dynamic dialogue IDs from the carved range (per design.md D2). E.g., IDs 0x300, 0x301, ... assigned to the HintEntry array in iteration order. <!-- done-differently: kRandoHintDialogueBase=0x200, id = 0x200 + (npc-1) via Rando_GetHintDialogueId (rando_hints.c:259). Carve is used for spoiler-JSON dialogue_id only; the live runtime path intercepts the 15 VANILLA tele msg ids instead of routing through this carve -->

- [x] 4.2 In `src/messaging.c` text dispatch: when the requested dialogue ID is in the dynamic range, consult `g_rando_hint_dialogue_table` (a runtime-loaded array of `(dialogue_id, text)` pairs); fall through to the static dialogue blob for IDs outside the range. <!-- done-differently: Text_LoadCharacterBuffer (messaging.c:2288) calls Rando_RenderHintMessage at its top; on a hint-tile msg id it fills messaging_text_buffer from g_hint_table and returns, else falls through to the vanilla dialogue_blk decode -->

- [x] 4.3 Per-frame cost: dynamic-range lookup is O(1) — array indexed by `dialogue_id - kHintDialogueIdBase`. <!-- done-differently: dispatch is a 15-entry linear scan (Rando_IsHintTileMessage) run only when a tele tile is read (not per-frame), so the cost is bounded-constant rather than literal array-index O(1) -->

- [x] 4.4 Vanilla mode preservation: when `kFeatures1_RandomizerActive` is clear, the dynamic range is empty; dialogue dispatch is byte-identical to vanilla. <!-- done: Rando_RenderHintMessage returns false unless g_rando_slot_active (rando_hints.c:406), so vanilla dispatch is unchanged -->


## 5. Per-NPC sprite-handler dispatch

- [x] 5.1 Wire `Rando_GetHintDialogueId(NPC_SahasrahlaTelepathic)` at the Sahasrahla telepathic-tile sprite handler. Returns the slot-specific hint dialogue ID; the text engine renders the slot's hint text. <!-- done-differently: not wired at the sprite handler; the Sahasrahla/EP telepathic tile is one of the 15 vanilla tele msg ids intercepted in Text_LoadCharacterBuffer via Rando_RenderHintMessage, so reading any of the 15 tiles surfaces its generated hint in-game -->

- [x] 5.2 Wire the Storyteller hint. <!-- done (2026-06-01, fork extension ids 17-19): NOT via Rando_GetHintDialogueId at the sprite handler; instead Rando_RenderHintMessage (rando_hints.c) intercepts the storyteller's paid-tip messages 0xff/0x101/0x102 (verified storyteller-exclusive) and the Fortune Teller reading ids 0xEA-0xF1/0xF6-0xFD (FT-exclusive), mapping FT to Kakariko(18)/Dark-World(19) by savegame_is_darkworld. Generator populates ids 17-19 after the tele loop (continuing the same pool cursor). commit 2969958. -->
- [ ] 5.3 ~~Bookshelf~~ **DROPPED** — design decision (rando_hints.h header): poor discoverability + thematic dilution. Not a gap.
- [ ] 5.4 Murahdahla in-game surface. <!-- PARTIAL/BLOCKED: the Murahdahla hint TEXT is generated (id 16, Triforce/Ganon-Hunt only) and emitted to the spoiler, but it is spoiler-only in-game — Murahdahla is an ALTTPR asm-added NPC the fork never ported, so surfacing it needs a NEW sprite (graphics/spawn/dialogue), not a handler wire. Out of scope for this pass; tracked as a follow-up. -->
- [x] 5.5 Each wiring respects the rando-active gate. <!-- done: Rando_RenderHintMessage early-returns unless g_rando_slot_active; vanilla dialogue dispatch is byte-identical. -->
- [ ] 5.6 **Lake-Hylia Fortune Teller (id 20)** — intentionally NOT wired: shares the Kakariko FT room (0x54) with no runtime discriminator, so at runtime it surfaces the Kakariko hint (18). Distinguishing it needs a sprite-prep change (spatial marker). Deferred. <!-- done-differently: id 20 left unpopulated by the generator on purpose. -->

> **Fork-extension status (2026-06-01)**: 3 of the 4 fork NPCs wired end-to-end at the generation + spoiler + determinism layers (Storyteller 17, Kakariko FT 18, Dark-World FT 19). **In-game NPC dialogue surfacing is playtest-only** (no automated coverage on the slot path) — verify by talking to each NPC in a hints-on rando seed.

## 6. Settings axis

- [x] 6.1 Add `settings.hints` enum field to `rando_settings.h`: `kHintsOff=0`, `kHintsSahasrahla=1`, `kHintsFull=2`. Default-by-goal per design.md D4. <!-- done-differently: rando_settings.h:105 `uint8 hints`; enum is binary kHintsMode_Off/On (rando_hints.h:91) not the tri-state, and default is unconditional ON (not goal-aware) -->

- [x] 6.2 Add to canonical-serialization order. Decide byte position (deferred from Phase B chunking; chosen at apply-time after audit of other Phase B settings additions). <!-- done: rando_settings.c:124 out[22]=hints; deserialize in[22] at :170; kSettingsCanonicalLen=28 (§66, kGenVer 13→14) -->

- [x] 6.3 CSV parser accepts `hints=off|sahasrahla|full`. <!-- done: rando_settings.c:725-744 parses hints=, accepting off/0/false/none and on/1/true/sahasrahla/full (sahasrahla+full collapse to binary On) --> <!-- done-differently: sahasrahla/full are aliases, not distinct modes -->

- [ ] 6.4 `SetDefaults` resolves the goal-aware default.
- [x] 6.5 Settings-screen widget: cycle through the 3 values. <!-- done-differently: PC native settings window has a binary "Hints" checkbox (rando_window.cpp:334; preserved across preset clicks at :254). The in-game SNES settings screen is compiled out on PC per CLAUDE.md; binary not tri-state -->


## 7. Spoiler integration

- [x] 7.1 In `Spoiler_WriteJson`, emit a top-level `hints` array per the spec (`source`, `text`, `kind` per entry). <!-- done: rando_spoiler.c:314-336 emits "hints":[{npc,dialogue_id,text}] --> <!-- done-differently: per-entry keys are npc/dialogue_id/text, not source/kind -->

- [x] 7.2 In `Spoiler_WriteText`, add a `Hints` section heading followed by one line per entry (`source: text`). <!-- done: rando_spoiler.c:697-706 emits "Hints:" heading + "<npc_str> : <text>" per active entry -->

- [x] 7.3 When `settings.hints == off`, omit the section. <!-- done: hints=off leaves g_hint_table empty (Rando_GenerateHints early-returns), so the text "Hints:" section is gated on any_hint (rando_spoiler.c:693-697) and the JSON array is empty -->

- [ ] 7.4 Add a `meta.hints_count` integer to the JSON spoiler for tooling.

## 8. Determinism + CI

- [x] 8.1 Bump `kGeneratorVersion` in `src/rando/rando.h`. <!-- done: hints joined the canonical hash at kGeneratorVersion 14 (§66) per rando_settings.h:121-122 / rando_settings.c:96; kGeneratorVersion now 36 (rando.h:17) -->

- [x] 8.2 Corpus coverage for hint determinism. <!-- done-differently (2026-06-01): hints default ON, so the existing fast_ganon/ganon race-mode entries (b-race-*) already carry hints=on and stamp the hints[] array — their ZRSR reveal round-trip asserts the 15 tele-tile hints regenerate byte-identically. The one uncovered path (Murahdahla, id 16, Triforce-Hunt-only) is now covered by a new race entry a1-open-triforce-hunt-race. Fork ids 17-19 are also stamped by every race entry now. No separate hints=full/sahasrahla axis (binary). commit 7b1874d. -->
- [x] 8.3 `hints` does not affect placement. <!-- done: hints are generated AFTER placement, so placement_digest_hex is independent of hints on/off; the 69/69 corpus (all placement/sphere digests) is unchanged by the hint generator. -->
- [x] 8.4 Hint determinism guard. <!-- done-differently: the race-mode reveal round-trip IS the determinism guard — the stamp is over the full canonical JSON incl. hints[], so a hint-text drift fails the round-trip (covers tele tiles + Murahdahla + fork ids 17-19). Plus Hints_SelfCheck asserts byte-identical g_hint_table across consecutive generations. -->

> **Note**: the original §8.2 wording (hints=full / hints=sahasrahla / hints=off seed mix) predates the binary-mode decision; the determinism intent is met via the race-reveal stamp + Hints_SelfCheck rather than a hint-text-capture CI step.

## 9. Audit

- [x] 9.1 Run `assets/scripts/check_audit_guard.py`. <!-- done (2026-06-01): --strict green; no non-exempt writes. The hint module writes only g_hint_table (module-static) + the messaging buffer, neither tracked. -->
- [x] 9.2 Run `assets/scripts/check_determinism.py`. <!-- done: green; the module uses Rng_* only, no rand/time/htobe*. -->
- [x] 9.3 Fresh-eyes audit per memory `[[cluster-audit-cadence]]` post-translation. <!-- 2026-06-02: done, 0 HIGH; findings in audit.md §"Fresh-eyes audit — 2026-06-02". 12.3 (findings addressed) stays open pending M2/M3/L3 playtest decisions. -->`

## 10. Documentation

- [x] 10.1 Add a "Hints" section to `docs/randomizer.md`. <!-- done (2026-06-01): documents the binary hints setting, tele tiles, Murahdahla (spoiler-only status), fork-extension NPCs (incl. the Lake-Hylia non-wiring), and race-mode stamp/reveal determinism coverage. -->
- [x] 10.2 Update the race-mode determinism note in `docs/randomizer.md` to reflect that the stamp transitively asserts `hints[]` + `entrance_mapping` regeneration. <!-- done (2026-06-01). -->
- [x] 10.2 Cross-link this change from the `openspec/changes/` index (README.md). <!-- done: openspec/changes/README.md:17 + docs/randomizer.md:446 both link add-rando-hints -->


## 10.5. Performance budget verification

- [ ] 10.5.1 **Generation budget bench**: hint generation runs once per seed at generation time (text expansion + per-NPC selection). Bench against Phase A's 2s desktop / 5s Switch budget; hints SHALL add < 200ms desktop / < 500ms Switch budget overhead.
- [ ] 10.5.2 Triforce Hunt seeds with hints=full are the worst case (Murahdahla emits one entry per Triforce-piece location). Bench specifically.
- [ ] 10.5.3 If hint generation exceeds the overhead budget: lazy-evaluate per-source generation (only generate the source the player is about to talk to); cache between hint NPCs.
- [ ] 10.5.4 Record final p50/p95/p99 in `audit.md §"Hint generation benchmark"`.

## 11. Playtest

<!-- Rewritten 2026-06-01 for the as-built binary-mode + fork-extension design. -->
- [ ] 11.1 Telepathic tiles: read several of the 15 tiles in a hints-on seed; confirm each shows a distinct "<item> is in <location>" hint (not vanilla flavor).
- [ ] 11.2 **Fork Storyteller**: pay the Dark-World tip NPC; confirm it shows an item-location hint (the health-restore still happens after).
- [ ] 11.3 **Fork Fortune Tellers**: pay the Kakariko FT and the Dark-World FT; confirm each shows a *different* item-location hint. Pay the **Lake-Hylia FT**; confirm it shows the *Kakariko* hint (the intentional shared-room fallback) and does not crash.
- [ ] 11.4 `hints=off` seed: confirm tiles + Storyteller + Fortune Tellers all play their vanilla text (no hint substitution).
- [ ] 11.5 Race-mode: generate a race-mode hints-on seed; confirm the spoiler is suppressed in-game, then `RevealSpoiler` (post-game) surfaces the hints section. (Headless reveal round-trip already passes in the corpus.)
- [ ] 11.6 Murahdahla: spoiler-only today (no in-game NPC) — confirm a Triforce-Hunt spoiler's `hints[]` contains the `murahdahla` entry. (No in-game step until/unless a Murahdahla sprite is added.)

## 12. Archive readiness

- [ ] 12.1 CI green on Linux + macOS + Windows; corpus matches; hint determinism preserved.
- [ ] 12.2 Manual playtest covers all 4 hint sources + all 3 hints axis values.
- [ ] 12.3 Fresh-eyes audit findings addressed.
- [ ] 12.4 `openspec archive add-rando-hints` runs cleanly; new `randomizer-hints` spec moves to `openspec/specs/randomizer-hints/spec.md`; deltas merge into `randomizer-placement` + `randomizer-core`.
