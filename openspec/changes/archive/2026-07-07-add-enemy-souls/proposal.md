# Proposal: Enemy/Boss Souls

## Why

Randomizer progression is currently gated almost entirely by equipment (swords, rods, medallions) — every enemy and boss is always present and always killable once you have firepower. An OoT-randomizer-style "souls" mode adds a new progression dimension: enemies and bosses only spawn once you have collected their soul item, so kill-gated progression (boss kills, kill-to-open-door rooms, enemy-held key drops) is gated on soul ownership. This creates novel routing decisions (a dungeon you can enter but whose boss you cannot yet fight) on top of the existing shuffle axes.

## What Changes

- **New setting axis `souls_shuffle`: `off` / `bosses` / `bosses+enemies`** (2-bit encoding in canonical byte 28 bits 2-3, alongside `enemy_drop_checks` in its low 2 bits; `kSettingsCanonicalLen` stays 29). Default `off`; `off` is placement-digest-identical to today.
- **New item family: soul items**, appended to `assets/rando/item_registry.yaml` (ids 150+):
  - Boss tier: 12 souls — one per dungeon boss (Armos Knights, Lanmolas, Moldorm, Helmasaur King, Arrghus, Mothula, Blind, Kholdstare, Vitreous, Trinexx), one Agahnim Soul (covers both tower encounters), one Ganon Soul.
  - Enemy tier: one soul per curated enemy family (grouped from the 49 randomizable killable species in `kEnemyTable`, target ~25-35 souls; grouping owned by a committed generator map).
  - Souls are direct-grant progression items (`kRandoLttpSkip` + confirmation cue, Triforce-piece pattern), placed into the general pool displacing junk padding.
- **Spawn suppression at runtime**: a sprite whose final (post-enemy-shuffle, post-boss-shuffle) species maps to an un-owned soul does not spawn. Hooks at the three spawn layers: static dungeon room load, overworld proximity load, and the `Sprite_SpawnDynamically(Ex)` funnel. Species without a soul in the active tier always spawn. Boss "parts" placed in room data (Trinexx arms, Kholdstare shell) suppress with their parent; AI-spawned children vanish automatically.
- **Kill-gates stay shut**: rooms whose kill-quota enemies were soul-suppressed do NOT trivially satisfy `Sprite_CheckIfScreenIsClear`/`Sprite_CheckIfRoomIsClear` — the door/chest/key trigger is held until the player returns with the soul(s) and clears the room. Trap-close actions are skipped while held so the player can always walk back out (no softlock).
- **Logic models soul availability**:
  - Boss/prize locations require the assigned boss's soul via the existing `OP_CAN_KILL_BOSS` + `boss_assignment[16]` mechanism (dungeon-relative, follows boss shuffle automatically).
  - Kill-gated room traversal and enemy-held key drops require the resident species' souls, from a new generated room→species table (generated from room sprite data per the no-committed-ROM-data rule).
  - GT boss refights (Armos/Lanmolas/Moldorm rooms) gate the GT climb on those three souls under any souls tier.
  - Goal predicates (Ganon kill, Agahnim kill) additionally require Ganon/Agahnim souls.
  - **Enemy-drop-sanity composition**: enemy-check locations (`LOCTYPE_Enemy`) and forced enemy-drop locations (`LOCTYPE_EnemyDrop`) require the soul of their source species under the enemies tier — the check generator already carries `source_type` per check and gains a souls term in its emitted predicates; the 13 boss/miniboss check locations are covered by the `OP_CAN_KILL_BOSS` extension and the GT-refight soul gates.
- **Enemy shuffle interaction**: when the `bosses+enemies` tier is active, enemy shuffle pins kill-gated rooms (the `kKillableRequiredRooms` set) and forced enemy-drop source slots to vanilla species so logic requirements stay static and enemy shuffle remains placement-digest-neutral (its existing spec invariant is preserved; precedent: `enemy_drop_checks` already coerces its Dungeon/All tiers to Keys under enemy shuffle).
- **Persistence**: soul ownership is an 8-byte bitfield in a new sidecar v5 extension block + a new snapshot-tail TLV type + a process-static live array (`g_rando_boomerang_owned` pattern). Old saves default to zero souls owned safely.
- **UI**: `souls_shuffle` combo in the ImGui settings window; a souls section in the tracker window; `souls_shuffle=` CSV/share-string key; spoiler lists souls like any placed item.
- `kGeneratorVersion` bump + new corpus entries covering both tiers.

## Capabilities

### New Capabilities
- `randomizer-souls`: soul item family, spawn-suppression runtime, kill-gate hold semantics, settings axis, persistence, and UI surfaces for the souls feature.

### Modified Capabilities
- `randomizer-logic`: new seed-independent souls predicates (kill-gated room requirements, enemy-check/forced-drop location gating, GT refights, goal gating) and the souls extension of `OP_CAN_KILL_BOSS` boss resolution (ADDED requirements; no existing requirement text changes). Note: the enemy-check location predicates themselves belong to the unarchived `randomizer-enemy-drop-sanity` capability — the souls terms are specified here as logic requirements so this change does not depend on that capability's archive order.
- `randomizer-shuffles`: enemy shuffle gains a souls-conditional kill-room vanilla-species pin; boss shuffle's render-redirect gains the soul gate on the assigned boss (ADDED requirements preserving the existing digest-neutrality invariant).
- `randomizer-save`: sidecar format v5 extension block + new snapshot-tail TLV for soul ownership (ADDED requirement).

## Impact

- **C runtime**: `src/sprite.c` (static/OW/dynamic spawn hooks, clear checks), `src/dungeon.c` (kill-gate room tags incl. `RoomTag_GanonDoor`), `src/rando/` (new `souls.c` module; `rando.c` grant dispatch; `shuffle_enemies.c` kill-room pin; `shuffle_boss.c` interaction; `rando_logic.c` eval; `rando_placement.c` pool + progression classification; `rando_save.c` v5; `rando_window/` settings + tracker; `auto_tracker.c`).
- **Data/codegen**: `assets/rando/item_registry.yaml`, `direct_grant_icons.yaml`, `custom_item_gfx.png` (1-2 new icon cells), `assets/rando_logic_gen.py`, new committed generator for room→species kill-gate tables (gitignored `.gen` output, fail-closed guard), souls terms in `assets/scripts/gen_enemy_check_tables.py`'s emitted predicates, logic YAML additions in `assets/rando/logic_parts/`.
- **Build/verify**: `kGeneratorVersion` bump, corpus regeneration + new souls corpus seeds, `--rando-selftest` extensions, `zelda3.vcxproj` + Makefile registration for new sources, `check_codegen_wiring.py` parity.
- **Compatibility**: `souls_shuffle=off` seeds are placement-digest-identical to current `main` (guarded by corpus regen diff). The encoding rides in canonical byte 28 (added by enemy-drop-sanity), bits 2-3; bits 4-7 remain free for future axes and `kSettingsCanonicalLen` stays 29.
