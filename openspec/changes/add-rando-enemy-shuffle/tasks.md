# Tasks — add-rando-enemy-shuffle

## 1. Constraint table (the correctness surface — do this first)

- [ ] 1.1 Author a per-enemy-type constraint table over the sprite ids, cross-checking Enemizer's `C:\src\Enemizer\EnemizerLibrary\EnemyRandomizer\SpriteRequirement.cs` against this fork's `Sprite_HEX_*` ids (`src/sprite_main.h`, 178 symbols). Per type: required sheet(s); flags `killable`, **`cannot_have_key` (independent of `killable`)**, `is_water`, **`never_use_dungeon` / `never_use_overworld`**, `is_overlord`/`is_object`/`is_npc`/`is_boss` (exclude), floor-vs-flying, OAM footprint. Conservatively mark unknowns `do_not_randomize`.
- [ ] 1.2 Decide table form: a source table alongside the existing `kSpriteInit_*[243]` tables (`src/sprite.c:125-251`), or codegen from a YAML. Keep any extracted ROM data gitignored per `CLAUDE.md`; structural sprite-id/flag facts may be inline (named initializers, not a hex blob — the embedded-data guard fails on blobs).
- [ ] 1.3 Port Enemizer's per-room data that is NOT derivable from room bytes: the shutter/kill-clear room-id list (`NeedKillable_doors` via `Room.IsShutterRoom`), the immovable-sprite room list (~60 rooms, `DontUseImmovableSpritesRooms` `SpriteRequirement.cs:897-956`), the flying-sprite room list, per-sprite `AddDontRandomizeRooms`, and hard excludes (Mimic Cave, Agahnim-tower bridge). Key into a room/area constraint map.
- [ ] 1.4 Define **key-room safety against the rando placement table** + `NeedKillable_doors` — NOT Enemizer's vanilla key-sprite scan (`DungeonSprite.cs:66`), which is unreliable under item shuffle (the key may be absent / placed elsewhere). A key room's replacement must be `killable && !cannot_have_key`.
- [ ] 1.5 Ensure the `is_boss` / do-not-randomize exclusion covers **GT mini-boss sprites** (`0x09`/`0x53`/`0x54`, cf. `shuffle_boss.c:214-216`), not just dungeon-boss-room sprites — the GT mini-boss gauntlet gates non-boss chest locations on `CanKill<Boss>`, so substituting those enemies would break the gauntlet (and the boss fight). The excluded set is "all boss + mini-boss sprites."

## 2. Logic sweep (confirm enemy shuffle is logic-free — re-run on any logic edit)

- [ ] 2.1 Grep `assets/rando/logic*.yaml` + `logic_parts/**` + **`macros.yaml`** + **`op_registry.yaml`** for any `CanKill<X>` predicate on a **non-boss** enemy, or any location gated on killing a specific enemy type. (The fresh-eyes sweep found only `CanKillBoss(<dungeon>)` + player-firepower macros `CanKillMostThings`/`CanKillEscapeThings` — logic-free confirmed; re-confirm on any future logic change.)
- [ ] 2.2 If any non-boss kill-gated location ever exists, pin its enemy (exclude from substitution) — the minimal fix, no logic change. Record "enemy shuffle adds no predicate / no `OP_*`; beatability rests SOLELY on the §1 table."

## 3. Module + install (mirror boss shuffle)

- [ ] 3.1 New `src/rando/shuffle_enemies.{c,h}`: `EnemyShuffle_Generate(settings, seed_u64)` (xoshiro256\*\* fork off the seed) builds the per-(room/area) substitution; `EnemyShuffle_Deactivate` tears it down. Pattern: `src/rando/shuffle_boss.{c,h}`.
- [ ] 3.2 Install from `Rando_ActivateSidecarSlot` **only on the `g_rando_active_settings_valid` path**, and `EnemyShuffle_Deactivate()` on the invalid / snapshot-restore / v1-slot path + in `Rando_DeactivateSlot` — mirror boss/drop's fail-closed teardown (`rando.c:1904-1960`) so a stale substitution can't leak into a snapshot-restored slot.
- [ ] 3.3 Patch BOTH load paths (they differ):
  - **Dungeon** `Dungeon_LoadSingleSprite` (`src/sprite.c:3807`): rewrite `sprite_type[k]` (type-byte swap; do NOT touch the bit-packed y/x — cf. `:3791-3796`). Skip control `0xe4` / overlord `x>=0xe0`.
  - **Overworld** `Overworld_LoadSprites` (`:3895`): rewrite the stored `sprite_where_in_overworld` value as `pick + 1` (keep the `+1` bias); the spawn is lazy in `Overworld_LoadProximaSpriteIfAlive` (`:3973`). Skip count marker `src[2]==0xf4` / overlord `src[2]>=0xf3` (NOT `x>=0xe0`).
  - Both: constrain the pick to the **actually-loaded** sheets — resolve `kSpriteTilesets` `0` entries against live `sprite_gfx_subset_N` (`load_gfx.c:627-640`), not the static row.
- [ ] 3.4 OAM: pick a footprint-compatible replacement so `((sprite_flags2&0x1f)+1)*4` bytes (`src/sprite.c:1164`) isn't overrun. (No init-order fix needed — `flags2` is read at `SpritePrep_LoadProperties` `:4144`, after the load-time swap.)

## 4. Settings axis + canonical layout

- [ ] 4.1 Add `enemy_shuffle` (uint8) to `RandoSettings` (`src/rando/rando_settings.h`); wire CSV parse + default-off.
- [ ] 4.2 Pack it into a reserved canonical pad bit (e.g. byte `[26]` bit 0) in `Settings_CanonicalSerialize`/`Deserialize` (`rando_settings.c:206-235`) — do **NOT** grow `kSettingsCanonicalLen` (stays 28; no size-coupling cascade). Follow the entrance-axis pad-byte `[25]` precedent (`:210-219`).
- [ ] 4.3 Bump `kGeneratorVersion` 58 → 59 (`src/rando/rando.h`).
- [ ] 4.4 Reconcile the `randomizer-core / Settings canonical serialization order` normative field list to as-built (it lags by `hints`/`boss_shuffle`/`drop_shuffle` + the entrance pad byte) and document the new pad bit — read `rando_settings.{h,c}` for the authoritative byte map.

## 5. UI

- [ ] 5.1 Replace the disabled enemy-shuffle "coming soon" placeholder with a live checkbox in `src/rando/rando_window/rando_window.cpp`, with a one-line durable tooltip ("Randomizes which enemies appear in each room").

## 6. Validation

- [ ] 6.1 Corpus regen + 3-way diff (WSL `make zelda3`; `bump_rando_corpus.py --apply`; diff vs unmodified `main` built fresh, `rm src/rando/logic_data.c` first) — expect **zero** placement movement (settings-hash-only change). Tooling gotchas per `CLAUDE.md` (absolute `--binary`, manifest version, CRLF).
- [ ] 6.2 `EnemyShuffle_SelfCheck`: an active shuffle never picks outside the resolved loaded-sheet set, never replaces an excluded type/marker, and preserves the killable+key / water / directional invariants over a sampled room set. **Register it in `Rando_RunAllSelfChecks` (`rando.c:3620-3621`)** or it won't run under `--rando-selftest`.
- [ ] 6.3 **Playtest** (the only net for render/crash/softlock — corpus + `--rando-selftest` are generation-only). Stage at slice start (`[[logic-vs-runtime-gap]]`): a small sheet-fixed pool across a few dungeons + the overworld; watch for garbage tiles, crashes, uncleared shutter/key rooms, water strands. Then widen.
- [ ] 6.4 Backward-load: a kGen-58 slot loads on the 59 binary with the one-time warning.

## 7. Deferred follow-on axes (out of scope here — separate changes)

- [ ] 7.1 Enemy HP randomization (clamp/scale `sprite_health[k]` / `kSpriteInit_Health[243]`).
- [ ] 7.2 Enemy damage randomization (`kSpriteInit_BumpDamage[243]`).
- [ ] 7.3 Killable thief, bush-enemy spawn, absorbables-in-place-of-enemies, randomize-on-hit.
- [ ] 7.4 Enemizer's sprite-group subgroup re-shuffle (widen the per-room pool by re-assigning loaded sheets) — design D4.

## 8. Audit

- [ ] 8.1 Fresh-eyes audit pass after the slice lands (`CLAUDE.md` cadence) — focus: GFX-sheet mismatch escapes, the bit-packed-coord hazard, room-env boss exclusion completeness, OAM overruns, any non-boss kill-gated location missed in §2.
