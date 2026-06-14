# Tasks — add-rando-enemy-shuffle

> **As-built status.** Enemy type substitution, all-slot sheet-group reshuffle,
> dungeon-overlord spawned-slot decode, graveyard slot-3 pin, and per-seed
> HP/contact-damage randomization are built under the default-off
> `enemy_shuffle` axis. `kSettingsCanonicalLen` stays 28 (`enemy_shuffle` is
> byte `[26]` bit0), and placement digests remain byte-identical when item
> settings are unchanged. Remaining archive gates are playtest-only:
> render/crash/softlock, water-adjacent rooms, OAM overrun watch, HP/damage feel,
> and an optional backward-load smoke. Deferred axes are killable-thief, bush
> enemies, absorbables, and randomize-on-hit (§7.3).

## 1. Constraint table (the correctness surface — do this first)

- [x] 1.1 Per-enemy-type constraint table generated into `src/rando/shuffle_enemies.c` (`kEnemyTable[256]`), cross-checked against Enemizer `SpriteRequirement.cs` / `SpriteConstants.cs` (the Enemizer SpriteId is the SNES type byte). Flags: `ESF_RANDOMIZABLE`, `ESF_KILLABLE`, `ESF_CANNOT_KEY` (independent of killable), `ESF_WATER`, `ESF_NEVER_DUNGEON`/`ESF_NEVER_OVERWORLD`, `ESF_FLYING`, plus all required sheet slots. OAM footprint is NOT yet modelled (see §3.4 / Remaining). The table is the SOLE beatability enforcer (logic models no kill-clear).
- [x] 1.2 Table form = generated structural source table in `shuffle_enemies.c` (no hex blob — passes the embedded-data guard), alongside generated `kSheetNeed`, per-slot pools, boss ids, and `kOverlordNeed`.
- [x] 1.3 Ported the per-room data: `kHardExcludeRooms` (Mimic Cave 268 + Agahnim-tower bridge 64 — never substitute), `kFlyingExcludeRooms` (210/268), and `kKillableRequiredRooms` (a ported subset of `DontUseImmovableSpritesRooms`). NOTE: the MVP currently enforces killable+key-capable for ALL dungeon rooms (see §1.4), so the immovable/shutter lists are advisory until the pool widens past killable-only.
- [x] 1.4 Key-room safety is over-approximated CONSERVATIVELY: EVERY dungeon replacement is `killable && !cannot_have_key` (so any key a shuffled placement drops in any room is obtainable, and every shutter/kill-clear door opens) — NOT keyed to the placement table per-room. This is the simplest SOUND model; it over-restricts dungeon variety. A future refinement can relax non-key rooms by consulting the placement table (see Remaining).
- [x] 1.5 GT mini-bosses `0x09`/`0x53`/`0x54` + all bosses + secondaries (`0xA3`/`0xCC`/`0xCD`) + Agahnim `0x7A` + Ganon `0xD6` are excluded (never `ESF_RANDOMIZABLE`); `EnemyShuffle_SelfCheck` asserts this against `kMustExcludeBossIds`.

## 2. Logic sweep (confirm enemy shuffle is logic-free — re-run on any logic edit)

- [x] 2.1 Swept `assets/rando/logic*.yaml` + `logic_parts/**` + `macros.yaml` + `op_registry.yaml`: every `CanKill<X>` macro is `CanKillBoss`, a per-boss macro (used only at dungeon boss rooms + the GT mini-boss gauntlet — all EXCLUDED sprites), or the player-firepower macros `CanKillMostThings`/`CanKillEscapeThings` (pure inventory tests, the `enemizer.enemyHealth='default'` kill-clear gate explicitly dropped). No non-boss kill-gated location exists. **Logic-free confirmed.**
- [x] 2.2 No pinning beyond the bosses/mini-bosses already excluded was needed. Enemy shuffle adds no predicate / no `OP_*`; beatability rests SOLELY on the §1 table.

## 3. Module + install (mirror boss shuffle)

- [x] 3.1 New `src/rando/shuffle_enemies.{c,h}`: `EnemyShuffle_Generate(settings, seed_u64)` (dedicated xoshiro256\*\* fork, salt `kEnemyShuffleSalt`) installs a module-global + per-seed RNG seed; `EnemyShuffle_Deactivate` tears it down. NOTE divergence from boss shuffle: no precomputed assignment table — the pick is computed per-room/per-area at LOAD time (the candidate pool depends on the LIVE-loaded sheets, unknown at gen time). Determinism comes from a per-(seed, room/area, slot) RNG.
- [x] 3.2 Installed in `Rando_ActivateSidecarSlot` on the `g_rando_active_settings_valid` path only; `EnemyShuffle_Deactivate()` on the fail-closed invalid path + in `Rando_DeactivateSlot`.
- [x] 3.3 Patched BOTH load paths in `src/sprite.c`:
  - **Dungeon** `Dungeon_LoadSprites`: substitutes `ent[2]` (the type byte) before `Dungeon_LoadSingleSprite` writes `sprite_type[k]` — pure type-byte swap, the bit-packed y/x untouched. Skips control `0xe4` / overlord `x>=0xe0`.
  - **Overworld** `Overworld_LoadSprites`: rewrites the stored value as `pick + 1` (keeps the `+1` bias). Skips count marker `src[2]==0xf4` / overlord `src[2]>=0xf3`.
  - Both: the pick is constrained to ACTUALLY-loaded sheets by reading the LIVE `sprite_gfx_subset_0..3` (g_ram 0xC2FC..0xC2FF) directly — which already reflect the `0`-entry inheritance — so the static `kSpriteTilesets` row is never consulted (the 0-inheritance hazard is sidestepped entirely).
- [ ] 3.4 OAM footprint compatibility is NOT yet modelled (Remaining): the constraint table does not yet record each enemy's OAM byte footprint, so a multi-tile replacement could overrun `((sprite_flags2&0x1f)+1)*4` bytes. Mitigated for now by the conservative pool (mostly 1-2 tile enemies) but UNVERIFIED — a playtest item. No init-order fix needed (`flags2` read at `SpritePrep_LoadProperties`, after the swap).

## 4. Settings axis + canonical layout

- [x] 4.1 `enemy_shuffle` (uint8) added to `RandoSettings`; CSV parse (`enemy_shuffle=true|false`) + default-off wired.
- [x] 4.2 Packed into canonical pad byte `[26]` bit0 (`kEnemyShuffleAxis_Enabled`) in `Settings_CanonicalSerialize`/`Deserialize`; `kSettingsCanonicalLen` stays 28 (no size-coupling cascade); `Settings_SelfCheck` asserts default byte-identity + that only [26] moves when on. Follows the entrance-axis `[25]` precedent.
- [x] 4.3 Bumped `kGeneratorVersion` to version-lock the new live axis.
- [x] 4.4 The `randomizer-core` ADDED requirement documents the new byte `[26]` bit0 and the baseline canonical byte map has been reconciled.

## 5. UI

- [x] 5.1 Replaced the disabled enemy-shuffle "coming soon" placeholder with a live checkbox in `rando_window.cpp` (tooltip "Randomizes which enemies appear in each room."). Glitches remains the only disabled placeholder. Compiles under WSL g++ (`rando_window.o`) + the panel smoke check passes; full interactive MSBuild verify is pending (the WSL build does cover this TU, so the seam is exercised).

## 6. Validation

- [x] 6.1 Corpus regen verified zero placement/sphere digest changes; `run_rando_corpus.py` and `check_corpus_version_sync` pass.
- [x] 6.2 `EnemyShuffle_SelfCheck` registered in `Rando_RunAllSelfChecks` and passing under `--rando-selftest`: asserts off→passthrough, boss/mini-boss/marker exclusion, table integrity (every randomizable has a required sheet + a killable+key candidate exists), determinism, in-sheet picks, dungeon killable+key / overworld in-sheet invariants, generated reshuffle pool integrity, multi-slot-sheet completeness, overlord spawn-slot needs, and HP/contact-damage bounds.
- [ ] 6.3 **Playtest — NOT done (cannot, per scope).** The only net for render/crash/softlock. Remaining: stage slot-0/1/2/3 reshuffle rooms, dungeon-overlord-freed spawner rooms, graveyard pushable graves, overworld areas, water-adjacent enemy rooms, and HP/damage feel; watch for garbage tiles, crashes, uncleared shutter/key rooms, water strands, and OAM overruns.
- [ ] 6.4 Backward-load (a kGen-60 slot on the 61 binary surfaces the one-time warning) — NOT separately exercised (no playtest); the kGen bump + existing upgrade-warning path provide it by construction.

## 7. Follow-on axes

- [x] 7.1 Enemy HP randomization (clamp/scale `sprite_health[k]` / `kSpriteInit_Health[243]`) — built under the parent `enemy_shuffle` axis; deterministic per `(seed,type)`, bosses exempt, `base==0` passthrough.
- [x] 7.2 Enemy damage randomization (`kSpriteInit_BumpDamage[243]`) — built under the parent `enemy_shuffle` axis; plain classes 1..8 nudge by -1/0/+1, flagged/boss values passthrough.
- [ ] 7.3 Killable thief, bush-enemy spawn, absorbables-in-place-of-enemies, randomize-on-hit.
### 7.4 Sprite-group sheet reshuffle — the VARIETY UNLOCK (design D4) [BUILT — PLAYTEST-PENDING]

> **As-built.** The reshuffle is always-on when `enemy_shuffle` is enabled and
> uses no new canonical axis. It covers subgroup slots `0..3`, uses generated
> Enemizer-sourced tables, decodes dungeon overlords to pin only spawned-sprite
> sheet needs, pins graveyard slot 3 for the pushable-grave ancilla, and includes
> per-seed HP/contact-damage randomization. The runtime widening is
> placement-orthogonal; playtest remains the only net for render/crash/softlock,
> OAM, water-adjacent rooms, and HP/damage feel.

- [x] **Why / true-random.** Re-assigns any safe subgroup slot `0..3` so the picker (which reads the LIVE `sprite_gfx_subset_*`) draws a wider, cross-family pool. The vanilla-resolved sheet for each slot is ALWAYS a candidate (vanilla-inclusive).
- [x] **Where (zelda3).** Reordered `Gfx_LoadSpritesInner` to resolve all 4 ids then hook then decompress (behavior-identical); `InitializeTilesets` hook inserted after its 4-id resolve. The hook may rewrite `sprite_gfx_subset_0..3` BEFORE decompress so VRAM + picker agree; cached-redecompress paths (mirror warp etc.) reproduce it for free. Room/area sprite TYPE list walked at load time via new `Dungeon_GetRoomSpritePtr` / existing `GetOverworldSpritePtr` (asset blob — list available before parse; key on `dungeon_room_index` / `overworld_area_index`).
- [x] **Enemizer table generation.** Position whitelists from `SpriteGroupCollection` `PotentialSubsetN` are disjoint — a sheet id implies one slot, so the picker's "sheet loaded" test stays sound with no position-aware change. Generated tables now cover all slots: `kEnemyTable`, `kSheetNeed`, per-slot pools, bosses, and `kOverlordNeed`. NOTE: the Walking Zora hybrid bug taught the invariant: every multi-slot enemy MUST list all required subgroup slots.
- [x] **Crash/softlock model (design D3).** (a) ELIGIBILITY: each subgroup slot reshuffles only when FREE — every present sprite is randomizable (substituted) or a known non-randomizable type that does not need that slot. Unknown/boss types pin all; dungeon overlords pin generated spawned-slot needs; unknown/no-spawn/boss-spawning and overworld overlords pin all. (b) ANTI-GARBAGE pool: each dungeon slot pool self-contains a killable+key enemy ⇒ the picker always finds a valid substitution and key/shutter rooms stay fillable. (c) INHERITANCE leak: a snapshot-safe 4-byte shadow (`kRam_EnemyShuffleVanPos2` @ 0x662..0x665) restores vanilla-resolved sheets in inherited/pinned slots. (d) OAM footprint still unmodelled (§3.4) — playtest watch-item, mitigated by the curated pool.
- [x] **Determinism + install.** Per-(seed, room/area, slot) RNG (`kEnemyShuffleSheetSalt`, distinct from the pick salts). Only rooms that OWN an unpinned slot can reshuffle that slot; inherited/pinned slots restore the vanilla shadow, so choices are race-deterministic and not visit-order-dependent. Rides the existing `EnemyShuffle_Generate` activation (shadow reset to 0 = "not established" there). No new canonical axis (owner decision: always-on when `enemy_shuffle`).
- [ ] **Validation — PLAYTEST PENDING (the only net).** `EnemyShuffle_SelfCheck` covers the structural invariants and stat bounds — green. Render/crash/softlock need the owner: stage a few dungeons + overworld. (The `ES_RESHUFFLE_DIAG` bring-up counters have been removed; re-add temporary g_ram counters if diagnostics are needed.)

## 8. Spec Reconciliation

- [x] 8.1 Spec/source comments reconciled to the as-built all-slot reshuffle and
      stat-randomization behavior. Residual archive blockers are playtest-only
      (render/crash/softlock, water-adjacent rooms, OAM overruns, HP/damage
      feel) plus optional backward-load smoke.
