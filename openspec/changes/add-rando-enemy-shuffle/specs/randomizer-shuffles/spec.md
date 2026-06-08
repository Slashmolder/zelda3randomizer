## ADDED Requirements

### Requirement: Enemy shuffle (sprite-type substitution)

When the `enemy_shuffle` axis is enabled for a slot, the system SHALL randomize **which enemy sprite spawns** in each dungeon room and overworld area with a deterministically-chosen replacement, applied at the two distinct load paths (which store the type differently — the spec must not conflate them):

- **Dungeon** (`Dungeon_LoadSingleSprite`, `src/sprite.c:3807`): the type id is `src[2]`, written to `sprite_type[k]`. Substitution rewrites the chosen type into `sprite_type[k]` (a type-byte swap; the bit-packed `y`/`x` bytes are NOT touched).
- **Overworld** (`Overworld_LoadSprites`, `src/sprite.c:3895`): there is no `sprite_type` write at load — the type is stored as `sprite_where_in_overworld[...] = src[2] + 1` (`:3910`) and the sprite is spawned lazily by `Overworld_LoadProximaSpriteIfAlive` (`:3973`). Substitution rewrites the stored value as `pick + 1` (preserving the `+1` bias). The mechanism is a map rewrite, not a `sprite_type` swap.

The substitution SHALL be a self-contained module (`src/rando/shuffle_enemies.{c,h}`) that, like boss shuffle and drop-pool shuffle, is generated deterministically from `(settings, seed_u64)` (a dedicated RNG stream forked off the seed per `randomizer-core / RNG family`), installed at slot activation (`Rando_ActivateSidecarSlot`) **only on the settings-valid path** (mirroring boss/drop shuffle, which `*_Deactivate()` on the invalid / snapshot-restore / v1-slot path so a stale substitution is never leaked — `rando.c:1904-1960`), and torn down at deactivation. When `enemy_shuffle` is off, the load path SHALL take the vanilla branch and behavior SHALL be byte-identical to a non-shuffled slot.

Enemy shuffle SHALL NOT touch item placement: it draws no fill RNG and adds no logic predicate, so `placement_digest_hex` SHALL be byte-identical for every seed regardless of `enemy_shuffle`. (The `generator_version` bump it triggers version-locks the live axis and reflects the per-seed pad-bit `settings_hash` change; the canonical length stays 28 — see `randomizer-core`.)

#### Scenario: Enemy shuffle off is vanilla-identical
- **WHEN** `enemy_shuffle` is off for a slot
- **THEN** no substitution is installed, every room/area spawns its vanilla enemy set, and the slot is byte-identical to the same slot generated without the axis

#### Scenario: Placement is unaffected by enemy shuffle
- **WHEN** two slots share an identical `share_string` except one has `enemy_shuffle` on and one off
- **THEN** their `placement_digest_hex` is byte-identical (enemy shuffle is orthogonal to item placement, like boss/drop shuffle)

#### Scenario: Substitution is deterministic
- **WHEN** the same `(settings, seed_u64)` is activated on any platform
- **THEN** each room/area receives the same replacement enemy set (cross-platform self-consistency)

### Requirement: Enemy shuffle never crashes (GFX-sheet constraint)

Every replacement enemy SHALL be drawn only from the set of enemies whose graphics-sheet requirement is satisfied by the room/area's **actually-loaded** sprite sheets. The loaded set derives from `sprite_graphics_index` → `kSpriteTilesets[index][0..3]` (`src/load_gfx.c:59`), **but a `0` subgroup entry does NOT load a sheet — it retains the previously-loaded `sprite_gfx_subset_N`** (`Gfx_LoadSpritesInner` loads each subset only `if (p[N])`, `load_gfx.c:627-640`), so the live loaded set depends on load history. The constraint check SHALL therefore resolve `0` entries against the live `sprite_gfx_subset_N`, or conservatively treat a `0` slot as unknown and exclude any enemy that needs a sheet not present in the non-zero entries. A replacement whose tiles are not in an actually-loaded sheet SHALL NOT be selected.

The module SHALL NOT substitute entries flagged boss (or boss secondary) / object / NPC / do-not-randomize in the per-enemy constraint table, nor the per-context control and overlord markers — which differ between load paths:
- **Dungeon**: control entry `type == 0xe4`; overlord `x >= 0xe0` (`Dungeon_LoadSingleSprite`).
- **Overworld**: count/control marker `src[2] == 0xf4` (`Overworld_LoadSprites` `src/sprite.c:3903`); overlord `src[2] >= 0xf3` (`Overworld_LoadProximaSpriteIfAlive` `:3985-3992`) — NOT `x >= 0xe0`.

Excluded entries pass through unchanged.

#### Scenario: Replacement stays within actually-loaded graphics sheets
- **WHEN** enemy shuffle picks a replacement for a room whose loaded sheet set (after resolving `0`-inheritance against live `sprite_gfx_subset_N`) is known
- **THEN** the chosen enemy's required sheet(s) are a subset of that loaded set; no enemy needing an unloaded sheet is ever placed, including in rooms whose `kSpriteTilesets` row has `0` entries

#### Scenario: Excluded entries pass through (per-context markers)
- **WHEN** a loaded entry is a control/overlord marker (dungeon `0xe4` / `x>=0xe0`; overworld `0xf4` / `>=0xf3`), a boss (or boss secondary), an NPC, or a quest object
- **THEN** the module leaves it unchanged (bosses are owned by boss shuffle; NPCs/objects/triggers must stay intact)

### Requirement: Enemy shuffle preserves beatability (killable / water invariants)

Logic does NOT model per-room kill-clear (the rando graph has no `CanKill<X>` predicate on a non-boss enemy — see `randomizer-shuffles` digest invariant), so beatability is enforced **entirely** by the runtime constraint table. A table bug does NOT move `placement_digest_hex` and is invisible to the corpus / `--rando-selftest` — it silently ships an unbeatable seed. The table is therefore the central correctness surface and SHALL conservatively mark any sprite of unknown safety as do-not-randomize. It SHALL preserve:

- **Killable + key-capable in key / shutter rooms**: `killable` and "may carry a key" are **independent** flags (Enemizer's `CannotHaveKey` is separate from `Killable` — e.g. Keese/Buzzblob/Geldman are killable but key-banned, `SpriteRequirement.cs`). A key slot's replacement SHALL satisfy `killable && !cannot_have_key`; a shutter/kill-clear room's replacement SHALL be `killable`. The "killable ⇒ key-capable" shorthand is wrong.
- **Which rooms are shutter / kill-clear is a hand-maintained room-id list** (Enemizer's `NeedKillable_doors` via `Room.IsShutterRoom`), not derivable from room data — it must be ported.
- **Key-room detection under item shuffle**: vanilla "is there a key sprite adjacent" (Enemizer's `HasAKey` scan of the vanilla ROM sprite list) is UNRELIABLE here because rando **shuffles keys** — the vanilla key sprite may be absent and a key may be placed where vanilla had none. Key-room safety SHALL be defined against the rando **placement table** + the `NeedKillable_doors` room list, not the vanilla sprite list.
- **Directional bans**: per-enemy `never_use_dungeon` / `never_use_overworld` flags (Enemizer `NeverUseDungeon`/`NeverUseOverworld`) SHALL constrain the candidate pool by load context. (Several such sprites are additionally fully `do_not_randomize` in Enemizer; the directional flag covers the partially-restricted remainder.)
- **Water rooms**: a water-only room SHALL draw replacements only from water-capable enemies.
- **Per-room excludes**: the immovable-sprite room list (~60 rooms, Enemizer `DontUseImmovableSpritesRooms`), the flying-sprite room list, per-sprite do-not-randomize room lists, and the hard excludes (Mimic Cave, Agahnim-tower bridge) SHALL be ported into a per-room constraint map.

#### Scenario: Key room stays clearable under item shuffle
- **WHEN** a room is in the `NeedKillable_doors` / key-required set (per the rando placement table) and enemy shuffle is active
- **THEN** every substituted enemy there satisfies `killable && !cannot_have_key`, so the key/door remains obtainable — independent of where vanilla placed the key sprite

#### Scenario: Directional ban respected
- **WHEN** an enemy flagged `never_use_overworld` is a candidate for an overworld area (or `never_use_dungeon` for a dungeon room)
- **THEN** it is excluded from that context's candidate pool

#### Scenario: Water-only room keeps a swimmable enemy set
- **WHEN** a water-only room is shuffled
- **THEN** its replacements are all water-capable; no stranded non-swimmer is placed
