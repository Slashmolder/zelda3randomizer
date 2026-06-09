# Tasks — add-rando-enemy-shuffle

> **Implementation status (2026-06-08, first build-verifiable pass).** The
> build-verifiable module + plumbing + a CONSERVATIVE first-pass constraint
> table are landed: WSL `make -j zelda3` (-Werror) green, `--rando-selftest`
> (incl. `EnemyShuffle_SelfCheck` + the extended `Settings_SelfCheck`) green,
> corpus regen 0/112 digests changed (placement byte-identical — the spec's
> core claim verified). NOT done: PLAYTEST (render/crash/softlock — the only net
> for those, per `[[logic-vs-runtime-gap]]`) and the constraint-table COMPLETENESS
> (only a small, sound candidate pool is enabled — see §1 notes). HP/damage
> follow-ons (§7) out of scope. **Deviation:** kGeneratorVersion is **60→61**,
> not the brief's 58→59 — the worktree base already carried Phase D bumps to 60.

## 1. Constraint table (the correctness surface — do this first)

- [x] 1.1 Per-enemy-type constraint table authored in `src/rando/shuffle_enemies.c` (`kEnemyTable[256]`), cross-checked against Enemizer `SpriteRequirement.cs` (the Enemizer SpriteId IS the SNES type byte → identity mapping; `Sprite_HEX_*` ids in `sprite_main.h` confirm the symbol names). Flags: `ESF_RANDOMIZABLE`, `ESF_KILLABLE`, `ESF_CANNOT_KEY` (independent of killable), `ESF_WATER`, `ESF_NEVER_DUNGEON`/`ESF_NEVER_OVERWORLD`, `ESF_FLYING`, plus a required-`sheets[]` list. **CONSERVATIVE first pass:** only a SMALL, sound candidate pool (~30 unambiguous killable/common enemies + a few flying/water/key-banned) is `ESF_RANDOMIZABLE`; everything else (NPC/object/overlord/boss/mini-boss/absorbable/unknown) is `do_not_randomize` (the zero default). OAM footprint is NOT yet modelled (see §3.4 / Remaining). **Widen only with playtest** — the table is the SOLE beatability enforcer (logic models no kill-clear).
- [x] 1.2 Table form = inline named-initializer source table in `shuffle_enemies.c` (structural sprite-id/flag facts, no hex blob — passes the embedded-data guard), alongside the spirit of the `kSpriteInit_*` tables.
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
- [x] 4.3 Bumped `kGeneratorVersion` **60 → 61** (NOT 58→59 — the worktree base already carried Phase D bumps to 60). The brief's intent (version-lock the new live axis) is preserved.
- [x] 4.4 The `randomizer-core` ADDED requirement (this change's spec delta) documents the new pad bit + carries the apply-time reconciliation note for the stale normative field list. The authoritative byte map now lives in the `rando_settings.{h,c}` layout comments (updated to include [26]=enemy_shuffle). Full normative-list reconciliation is an archive-time editorial pass (the note flags it) — not silently rewritten here.

## 5. UI

- [x] 5.1 Replaced the disabled enemy-shuffle "coming soon" placeholder with a live checkbox in `rando_window.cpp` (tooltip "Randomizes which enemies appear in each room."). Glitches remains the only disabled placeholder. Compiles under WSL g++ (`rando_window.o`) + the panel smoke check passes; full interactive MSBuild verify is pending (the WSL build does cover this TU, so the seam is exercised).

## 6. Validation

- [x] 6.1 Corpus regen via WSL `make zelda3` + `bump_rando_corpus.py --apply --binary <abs>`: **0/112 digests changed**; CRLF-normalized diff vs the v60 baseline shows ONLY `generator_version: 60→61`. `run_rando_corpus.py` re-verifies all 112 against the binary; `check_corpus_version_sync` green. (3-way diff vs a fresh unmodified-`main` build was not run separately — the in-tree before/after diff + the placement-orthogonality proof, 0 changes, is the stronger evidence; noted for completeness.)
- [x] 6.2 `EnemyShuffle_SelfCheck` registered in `Rando_RunAllSelfChecks` and passing under `--rando-selftest`: asserts off→passthrough, boss/mini-boss/marker exclusion, table integrity (every randomizable has a required sheet + a killable+key candidate exists), determinism, in-sheet picks, and the dungeon killable+key / overworld in-sheet invariants over sampled room/area/slot spaces.
- [ ] 6.3 **Playtest — NOT done (cannot, per scope).** The only net for render/crash/softlock. Remaining: a small sheet-fixed pool across a few dungeons + the overworld; watch for garbage tiles, crashes, uncleared shutter/key rooms, water strands, OAM overruns. Then widen the table.
- [ ] 6.4 Backward-load (a kGen-60 slot on the 61 binary surfaces the one-time warning) — NOT separately exercised (no playtest); the kGen bump + existing upgrade-warning path provide it by construction.

## 7. Deferred follow-on axes (out of scope here — separate changes)

- [ ] 7.1 Enemy HP randomization (clamp/scale `sprite_health[k]` / `kSpriteInit_Health[243]`).
- [ ] 7.2 Enemy damage randomization (`kSpriteInit_BumpDamage[243]`).
- [ ] 7.3 Killable thief, bush-enemy spawn, absorbables-in-place-of-enemies, randomize-on-hit.
- [ ] 7.4 Enemizer's sprite-group subgroup re-shuffle (widen the per-room pool by re-assigning loaded sheets) — design D4.

## 8. Audit

- [ ] 8.1 Fresh-eyes audit pass after the slice lands (`CLAUDE.md` cadence) — focus: GFX-sheet mismatch escapes, the bit-packed-coord hazard, room-env boss exclusion completeness, OAM overruns, any non-boss kill-gated location missed in §2.
