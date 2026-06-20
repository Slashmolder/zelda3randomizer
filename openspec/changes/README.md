# OpenSpec changes — entry-point index

This directory holds every active and pending OpenSpec change for the zelda3 randomizer. Use this index to navigate.

## Status overview

| Phase | # | Change | Scope | Authoring | kGen bump | Effort |
|---|---|---|---|---|---|---|
| **A** | — | [`add-randomizer-support`](archive/2026-05-29-add-randomizer-support/) | Foundation, RNG, share string, predicate VM, codegen, audit, logic graph (Open + Standard), assumed-fill placement, prize/medallion shuffles, sphere computation, goal-completability, JSON+text spoilers, sidecar save format, §6 grant-site dispatch (43 sites), snapshot tail TLV. | **Archived 2026-05-29** (119/139; the 20 deferrals are tracked in the Phase B/C/D changes below) | n/a (baseline) | ✅ shipped + archived → `openspec/specs/randomizer-*` (6 capabilities) |
| **B** | 1 | [`add-rando-confirmation-icons`](archive/2026-06-04-add-rando-confirmation-icons/) | Visible direct-grant icon ancilla (extends §7.6 audio-only) | ✅ **Archived 2026-06-04** | No | ✅ shipped + archived |
| **B** | 2 | [`add-rando-trackers`](archive/2026-06-05-add-rando-trackers/) | Trackers — shipped as **3 native ImGui windows on PC** (Item / Check tri-state / Map); legacy `hud.c` OAM overlay = Switch path; checked-bitmap r/w (audited 2026-06-02, no HIGH) | ✅ **Archived 2026-06-05** (owner playtest-confirmed) | No | ✅ shipped + archived |
| **B** | 3 | [`add-rando-race-mode-reveal`](archive/2026-06-05-add-rando-race-mode-reveal/) | Spoiler suppression + CLI `--reveal-spoiler` + `RandoRevealSpoiler` keybind + SHA-256 stamp verify + native-window Race-mode toggle (built scope) | ✅ **Archived 2026-06-05** (owner playtest-confirmed; the deferred in-binary reveal-UI + settings warning were carved to #3-i before archive) | No | ✅ shipped + archived |
| **B** | 3-i | [`add-rando-race-mode-reveal-ui`](archive/2026-06-05-add-rando-race-mode-reveal-ui/) | In-binary reveal UI — delivered in the **native window** (Randomizer → General → "Race-mode spoiler"; completion-gated anti-cheat) + the settings suppression-warning string. (Spec said SNES file-select per-slot; reconciled to native window — see the change's as-built note.) | ✅ **Archived 2026-06-05** (owner playtest-confirmed) | No | ✅ shipped + archived |
| **B** | 3b | [`add-rando-beatable-accessibility`](archive/2026-06-03-add-rando-beatable-accessibility/) | ALTTPR three-way accessibility (`Accessibility_SeedAcceptable`); "beatable only" 3rd tier | ✅ **Archived 2026-06-03** (owner playtest-confirmed) | **Yes** (45→46) | ✅ shipped + archived |
| **B** | 3c | [`add-rando-boss-heart-pool-toggle`](archive/2026-06-03-add-rando-boss-heart-pool-toggle/) | UI toggle for `region_boss_hearts_in_pool` (value inverted vs name); comment fix | ✅ **Archived 2026-06-03** (owner playtest-confirmed) | No | ✅ shipped + archived |
| **B** | 4a | [`add-rando-inverted-world-state`](archive/2026-06-03-add-rando-inverted-world-state/) | Inverted region graph (2977 PHP lines) via static world-state-keyed graph (RegionRemap retired) + Bug #12 starting-inv | ✅ **Archived 2026-06-03** (owner playtest-confirmed for implemented scope; Ganon-under-HC relocation tracked separately in #4a-ii) | **Yes** | ✅ shipped + archived |
| **B** | 4a-i | [`add-rando-inverted-dark-chapel-spawn`](archive/2026-06-03-add-rando-inverted-dark-chapel-spawn/) | Inverted spawn-select Dark Chapel / Dark Mountain respawn-world fix (follow-on to #4a; depends on `add-rando-inverted-world-state`) | ✅ **Archived 2026-06-03** (playtest-confirmed) | No | ✅ shipped + archived |
| **B** | 4a-ii | [`add-rando-inverted-ganon-relocation`](archive/2026-06-08-add-rando-inverted-ganon-relocation/) | Inverted Ganon-under-HC relocation — shipped **hole-only** (no-art pit carve on screen 0x1B; the faithful pyramid-facade render is paused, see the archive's `spike-findings.md`) | ✅ **Archived 2026-06-08** | No | ✅ shipped + archived |
| **B** | 4a-iii | [`add-rando-inverted-dw-lw-warps`](archive/2026-06-08-add-rando-inverted-dw-lw-warps/) | Inverted DW→LW under-rock world-warps (walk-in draw + warp behavior) | ✅ **Archived 2026-06-08** | No | ✅ shipped + archived |
| **B** | 4b | [`add-rando-retro-world-state`](archive/2026-06-04-add-rando-retro-world-state/) | Retro shop dispatch + 3 Retro flag pin (rupeeBow/takeAnys/wildKeys); genericKeys carved to #4b-i | ✅ **Archived 2026-06-04** (owner playtest-confirmed; full goal clear pending as a final check) | **Yes** | ✅ shipped + archived |
| **B** | 4b-i | [`add-rando-retro-generic-keys`](archive/2026-06-05-add-rando-retro-generic-keys/) | Retro `rom.genericKeys` — one shared small-key pool (any key opens any door): keys → GenericKey + per-dungeon key-door **logic rewrite** + shared-counter runtime (follow-on to #4b; the deferred 4th Retro flag) | ✅ **Archived 2026-06-05** | **Yes** | ✅ shipped + archived |
| **B** | 4c | [`add-rando-retro-takeany`](archive/2026-05-29-add-rando-retro-takeany/) | 31 TakeAny shop dispatch + RNG-driven activation (promoted from 4b's design §5 split) | ✅ **Archived 2026-05-29** | **Yes** | ✅ shipped + archived |
| **B** | 5 | [`add-rando-trick-logic-and-axes`](archive/2026-06-04-add-rando-trick-logic-and-axes/) | OP_TRICK/OP_DIFFICULTY_AT_LEAST/OP_GLITCH_LEVEL_AT_LEAST + 5 settings un-pins + Bug #7 per-item rewind + swordless mode | ✅ **Archived 2026-06-04** | **Yes** | ✅ shipped + archived |
| **B** | 6 | [`add-rando-hints`](archive/2026-06-11-add-rando-hints/) | New `randomizer-hints` capability: 15 telepathic tiles (ship) + Storyteller/Fortune-Teller fork NPCs (landed 2026-06-01); Murahdahla spoiler-only (needs sprite); bookshelf dropped; binary mode | ✅ **Archived 2026-06-11** (owner playtest-confirmed; created capability `randomizer-hints`) | **Yes** | ✅ shipped + archived |
| **B** | 7 | [`add-rando-shuffles-and-minigames`](archive/2026-06-14-add-rando-shuffles-and-minigames/) | Boss + drop-pool shuffles + §6.8 minigame dispatch | ✅ **Archived 2026-06-14** | **Yes** | ✅ shipped + archived |
| **B** | 8 | [`add-rando-switch-swkbd`](add-rando-switch-swkbd/) | libnx swkbd wrapper for Switch text input | ⏸ **PARKED** (0/31) — Switch env unavailable; see "Switch work — parked" below | No | parked |
| **B** | PC | [`add-rando-native-settings-window`](archive/2026-06-03-add-rando-native-settings-window/) | Dear ImGui second OS window owns the rando settings UI on PC; in-game screen compile-guarded out behind `Z3R_NATIVE_SETTINGS_WINDOW` | ✅ **Archived 2026-06-03** (owner playtest-confirmed; created capability `randomizer-native-window`) | No | ✅ shipped + archived |
| **B** | 9 | [`add-rando-item-progression-and-swap`](archive/2026-06-05-add-rando-item-progression-and-swap/) | Progressive boomerang/magic, bow never-downgrade + item-menu Press-A swap (flute/shovel/boomerang/bow), never-downgrade clamp (sword/shield/gloves/mail; arrow-filler exempt), ownership persistence (`@73`/`@74`); + Magic Bat (missable) & Flute Spot (dupe) location-guard fixes | ✅ **Archived 2026-06-05** (owner playtest-confirmed) | No | ✅ shipped + archived |
| **C** | C1 | [`add-rando-entrance-shuffle`](archive/2026-06-05-add-rando-entrance-shuffle/) | Multi-mode composable entrance shuffle — Simple/Restricted/Crossed/Insanity presets over caves/dungeons/coupled/cross_category/decoupled axes; per-seed two-mechanism reachability (cave region-reassign + dungeon edge overlay); cave-arrival table baked (`cave_arrival_baked.h`) so decoupled is one-way from launch. Archive also `REMOVED` the retired `RegionRemap` baseline requirement | ✅ **Archived 2026-06-05** (owner playtest-confirmed; the Insanity preset is a live native-window button — its dungeon/cross-decoupled arms are built, see the change's D.7/D.8 for their playtest status. Skull Woods + Link's House are documented partial-coverage deferrals) | **Yes** | ✅ shipped + archived |
| **D** | D1 | [`add-rando-cosmetic-shuffles`](archive/2026-06-02-add-rando-cosmetic-shuffles/) | Palette (4 MVP modes) / sprite (zspr folder pick) / music (song remap) shuffles; `cosmetic_seed` is **client config**, not a slot field — no save-format change | ✅ **Archived 2026-06-02** (owner playtest-confirmed) | No | ✅ shipped + archived |
| **D** | D2 | [`add-rando-customizer-mode`](archive/2026-06-14-add-rando-customizer-mode/) | Manual per-location placement (manifest pins + assumed-fill completes) + `pool_overrides` — headless CLI (`--generate-seed`/`--generate-slot --customizer=`), playable-slot path, native-window toggle + manifest field. Race mode mutually exclusive. `customizer_active` = canonical `[26]` bit1 | ✅ **Archived 2026-06-14** (owner playtest-confirmed; `customizer_seed` share-string transport deferred to a future format change) | **Yes** | ✅ shipped + archived |
| **D** | D3 | [`add-rando-major-glitch`](archive/2026-06-14-add-rando-major-glitch/) | HybridMG + NoLogic un-pin + NoLogic reachability short-circuit; glitch-seed→JP-flag coupling; `CanOneFrameClipOW/UW` reclassification; 9 technique macros; `fake-flippers`→`verified-us10`; partial OWG disjunct follow-up; NoLogic `can_place` short-circuit | ✅ **Archived 2026-06-14** | **Yes** | ✅ shipped + archived |
| **D** | D4 | [`add-rando-auto-tracker`](archive/2026-06-05-add-rando-auto-tracker/) | Local TCP server for external tracker clients | ✅ **Archived 2026-06-05** | No | ✅ shipped + archived |
| **D** | D5 | [`add-rando-door-shuffle`](archive/2026-06-11-add-rando-door-shuffle/) | Intra-dungeon door shuffle (`door_shuffle=basic`, intensity 1) — Arch-2 oracle (the generation explorer IS the placer oracle via `OP_DOORS_ACTIVE`/`OP_DOORS_LOC_REACHABLE`), committed reference-derived door tables, runtime redirect + arrival tableau translation + key-door KIND overlay; HC+Swamp pinned, Open/Standard+NoGlitches only | ✅ **Archived 2026-06-11** (owner playtest: TT beaten end-to-end incl. maiden escort + GT spirals/multi-spiral; merged `e65872c`) | **Yes** (65→66) | ✅ shipped + archived |
| **D** | D6 | [`add-rando-field-item-sprites`](archive/2026-06-07-add-rando-field-item-sprites/) | Free-standing field locations draw the PLACED item's gfx (client-local toggle; shared draw-resolution mirroring the grant chain) | ✅ **Archived 2026-06-07** | No | ✅ shipped + archived |
| **D** | D6-i | [`add-rando-field-item-custom-art`](archive/2026-06-12-add-rando-field-item-custom-art/) | Custom art for the field items with no vanilla pickup sprite (follow-on to D6): triforce + ½/¼ magic-decanter tiles (MIT-attributed z3r custom art, local `kRandoCustomItemGfx` asset — **regenerate `zelda3_assets.dat`**), off_black rupoor recolour, SP3-upper palette loads | ✅ **Archived 2026-06-12** (owner playtest-confirmed; merged to main) | No | ✅ shipped + archived |

Total: 1 active change (`add-rando-switch-swkbd` [parked]) + 36 archived. The archived set currently contains 32 randomizer/randomizer-adjacent changes, 2 native-UI changes, and 2 non-rando JP-feature changes. Run `openspec validate --changes` to confirm all validate.

One follow-up change was carved out at archive time so the archived baseline reflected only built scope: `add-rando-race-mode-reveal-ui` (#3-i, the deferred in-binary reveal UI + settings warning) — since **built in the native window + archived 2026-06-05**. The `add-rando-entrance-shuffle` Insanity preset was **not** carved — it is built and ships as a live native-window preset (the cave-arrival table is baked); only its dungeon/cross-decoupled arms carry the usual playtest caveat.

## New proposals (2026-06-08, post-roadmap)

Changes proposed outside the original Phase A–D randomizer roadmap:

- [`add-jp-glitch-restoration`](archive/2026-06-09-add-jp-glitch-restoration/) — **non-rando game feature — shipped + archived 2026-06-09.** A single "Restore JP 1.0 glitches" gameplay checkbox (`kFeatures0` bit 18) restoring **all 6** JP-1.0-exclusive glitches patched out of US 1.0: Fake Flippers, Death Hole, Itemdash, Spindash, Superspeed, Mirror Block Erase — each gated to the exact JP↔US ROM delta (confirmed by a custom relocation-normalized 65816 differ + a 723-routine sweep). Itemdash/Spindash/Superspeed share ONE StartDash-frame guard (it skips both `Link_HandleYItem` and the spin-charge pipeline); Mirror Block Erase = a save-vs-clear reorder in `DoSwordInteractionWithTiles_Mirror`. **Owner playtest-confirmed all 6.** Default-off + `!ZeldaIsEmulatorAttached()`; no rando/corpus/`kGeneratorVersion` impact.
- [`add-jp-overworld-music`](archive/2026-06-14-add-jp-overworld-music/) — **non-rando game feature — archived 2026-06-14.** A "JP 1.0 overworld music" gameplay checkbox (`kFeatures0` bit 19) that selects the destination-screen overworld track + ambient the JP-1.0 way after a mirror warp / long screen transition. Default-off + `!ZeldaIsEmulatorAttached()`; no rando/corpus/`kGeneratorVersion` impact.
- [`add-rando-share-string-v2`](archive/2026-06-14-add-rando-share-string-v2/) — **rando UI/format change — archived 2026-06-14 (kGen: No).** Share-string **v2** embeds the full 28-byte canonical settings (`ZRS2 | generator_version | settings_len | canonical | seed_u64 | crc16` = 44 bytes → exactly 71 base32 chars) so "Paste share string" restores settings AND seed losslessly. v2 is **UI-transport only**; sidecar/ZRSR/spoiler-filename/`meta.share_string`/banner keep the v1 identity form. Customizer seeds are fenced out of v2 until `customizer_seed` share encoding lands; Switch entry stays v1.
- [`add-rando-enemy-shuffle`](archive/2026-06-14-add-rando-enemy-shuffle/) — **rando slot axis — archived 2026-06-14.** Sprite-type substitution (which enemy spawns where), GFX-sheet-constrained, deterministic per-seed, mirroring the boss-shuffle install model. Placement stays byte-identical for all seeds; `enemy_shuffle` packs into canonical byte `[26]` bit0 so `kSettingsCanonicalLen` stays 28 and the default `settings_hash` is unchanged. The all-slot sheet-group reshuffle + per-seed enemy HP/contact-damage randomization ship under the same axis; bosses are exempt from stat scaling. Killable-thief/bush/absorbables remain deferred follow-up axes.
- [`add-rando-enemy-sheet-widening`](archive/2026-06-20-add-rando-enemy-sheet-widening/) — **rando slot axis — archived 2026-06-20.** Follow-on to `add-rando-enemy-shuffle`: enables the all-slot sprite-GFX-sheet reshuffle (dungeon sprite-palette signature + verify-then-commit fillability), a position-aware sheet loaded-check, and a contact-damage class-2 exclusion. Runtime-only; placement/sphere digests stay byte-identical.
- [`add-rando-trap-catalog`](archive/2026-06-19-add-rando-trap-catalog/) — **rando slot axis — archived 2026-06-19.** Masquerade traps: eligible junk pickups disguise as real items and fire one of 16 effects across 5 categories (HAZARD/IMPAIR/DRAIN/SCARE/DISPLACE) on pickup; the per-slot effect is chosen deterministically per `(seed, location_id)` filtered by a `trap_categories` mask, with context-aware placement and an `insanity` frequency tier. `traps=off` keeps the canonical bytes byte-identical.

## Switch work — PARKED (owner, 2026-06-02)

The owner has no Switch development environment and has punted **all** Switch work until one is available. This is a standing policy, not a per-change decision:

- **`add-rando-switch-swkbd`** (the only pure-Switch change) is **parked** — authored but not being worked, and not a blocker for anything else.
- **Switch build-verification + dev-unit-bench tasks** in other changes (e.g. `add-rando-trackers` §4.5/§8.3/§10.3) are **deferred**. These are release-cut manual gates per the archived `add-randomizer-support / tasks.md §12.3a` (dev-unit bench) and `§12.3b` (build verification) — they were never per-change CI blockers, so deferring them does not block any change from archiving.
- **"Switch budget" thresholds** cited in budget-bench tasks (`add-rando-hints` §10.5.1, `add-rando-shuffles-and-minigames` §9.5.1, `add-rando-trick-logic-and-axes` §12.5.1, `add-rando-trackers` §6.4) are covered by this policy: run the **desktop** bench as normal; the Switch-budget number is verified later when a Switch env exists.
- **Editing `src/platform/switch/Makefile`** to register a new generated header (codegen-wiring tasks) is *not* parked — it needs no Switch and keeps the build wiring honest; it just isn't compile-verified on Switch until the env exists.
- **Switch is not broken** by the PC native-settings-window move: the in-game settings screen + alphabet picker were compiled out *only on PC*; Switch retains them as the text-input path. The Switch build remains *unverified* (can't test), but is designed to exclude PC-only ImGui/`rando_window` paths via the `Z3R_NATIVE_SETTINGS_WINDOW` guard.

When a Switch env returns, un-park `add-rando-switch-swkbd` and clear the `⏸ DEFERRED` markers; nothing else needs to change.

## Implementation order

Recommended ordering:

1. ✅ **Phase A archived** (2026-05-29) — baseline specs live in `openspec/specs/randomizer-*`. Phase B/C/D changes now modify those specs.
2. **Phase B warm-up**: #1 confirmation-icons → #2 trackers → #3 race-mode reveal.
3. **Phase B world-states**: #4b Retro (small) → #4a Inverted (large).
4. **Phase B logic + UX**: #5 trick logic → #6 hints → #7 shuffles+minigames. (#8 swkbd ⏸ parked — Switch env unavailable.)
5. **Phase B exit**: each change archives independently once its tasks, review, and verification complete.
6. **Phase C**: C1 entrance shuffle (requires #4a archived).
7. **Phase D**: D1-D4 (D3 requires #5 archived; D1/D2/D4 are independent).

## Dependencies graph

```
Phase A archive ✅ (2026-05-29)
       │
       ├──► #1, #2, #3 (warm-up, parallel-safe)
       ├──► #4b Retro
       ├──► #4a Inverted ──┬───────────────────────► C1 entrance shuffle
       │                   ├──► add-rando-inverted-dark-chapel-spawn ✅ (archived 2026-06-03)
       │                   └──► add-rando-inverted-ganon-relocation ✅ (archived 2026-06-08, hole-only)
       ├──► #5 trick logic ─────────────────────────► D3 major glitch
       ├──► #6 hints
       ├──► #7 shuffles + minigames
       ├──► #8 switch swkbd  [PARKED — Switch env unavailable]
       └──► D1, D2, D4 (Phase D parallel-safe)
```

## File-overlap hotspots

- **`src/sprite_main.c`**: #1, #4b, #6, #7 — serialize.
- **`src/rando/rando.{c,h}`**: #1, #3, #6 — #1 first, then #3/#6 in parallel.
- **`src/select_file.c`**: #3, #4a, #4b — #4a and #4b touch the same line (un-gate at 2520); sequence them.
- **`src/rando/rando_placement.c`**: #4a (Bug #12), #4b (BuildItemPool), #5 (per-item rewind) — serialize.

## Tools

- `openspec validate --changes` — validate all changes.
- `openspec list` — view per-change task progress.
- `openspec list --specs` — view the spec baseline (6 `randomizer-*` capabilities live as of the Phase A archive, 2026-05-29).

## When you're ready to start a change

1. Pick the change folder from the table above.
2. Read its `README.md` for the entry-point index, then `proposal.md`, then `design.md` (if present), then spec deltas, then `tasks.md`.
3. Run `openspec apply <change-name>` (or `/openspec-apply`) to walk through tasks.
4. For stub-status changes (Phase C/D + new Phase B work that hasn't been promoted), run `/openspec-explore <change-name>` first to flesh out tasks/design.

## Cross-cutting conventions

Every kGen-bump change SHALL:
- Bump `kGeneratorVersion` in `src/rando/rando.h`.
- Regenerate the regression corpus via `assets/scripts/bump_rando_corpus.py`.
- Verify default-settings digests remain byte-identical to the pre-change baseline (the canonical settings byte-layout is **append-only**; never change byte positions, widths, or enum values for existing fields — Phase A pre-declared the full enum space, so Phase B+ only *un-pins* user input to existing values).
- If `location_registry.yaml` / `item_registry.yaml` grew, verify existing IDs are unchanged (append-only registry).
- Backward-load test: a slot written by `generator_version = N` SHALL load on an `N+1` binary with a one-time informational warning (per `randomizer-save / Embedded placement table — upgrade safety`); no regeneration required.
- Pass audit-guard (`assets/scripts/check_audit_guard.py`), determinism (`check_determinism.py`), and codegen-wiring (`check_codegen_wiring.py`) checks.
