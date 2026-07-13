# randomizer-shuffles Specification

## Purpose
TBD - created by archiving change add-randomizer-support. Update Purpose after archive.
## Requirements
### Requirement: Three orthogonal dungeon-reward shuffles (Phase A scope split)

The system SHALL distinguish three independent shuffles affecting dungeon rewards. These are conflated in casual discussion but separate in implementation per the ALTTPR reference:

1. **Prize shuffle** (Phase A): which dungeon hands out which of the 7 crystals / 3 pendants (Green, Red, Blue). Phase A randomizes this. Has logic interactions — Green Pendant gates Sahasrahla's gift, all 7 crystals gate Ganon, the three colored pendants gate the Sword Pedestal. The predicate VM SHALL include `OP_HAS_PRIZE <prize_id>` (or equivalent expression via `OP_HAS_ITEM` over Prize_Crystal1..7, Prize_GreenPendant, Prize_RedPendant, Prize_BluePendant when the placement layer treats prizes as items).
2. **Medallion shuffle** (Phase A): which of Bombos / Ether / Quake opens the Misery Mire entrance, and independently which of those three opens the Turtle Rock entrance. Even default ALTTPR seeds randomize these (per `Randomizer.php` defaults). Required predicates: `OP_MEDALLION_OPENS <entrance>` evaluating against the per-seed assignment.
3. **Boss shuffle** (Phase B): which boss occupies which boss room. Independent from prize and medallion shuffles. Default Phase A leaves bosses in vanilla rooms.

#### Scenario: Phase A randomizes prize and medallion by default
- **WHEN** a Phase A seed is generated with default settings
- **THEN** crystal/pendant assignments are shuffled across dungeons, Misery Mire and Turtle Rock have randomized medallion requirements, and the spoiler records both maps

#### Scenario: Prize shuffle disabled — vanilla mapping populates placement table
- **WHEN** `prize_shuffle = false` and a seed is generated
- **THEN** each of the 10 prize-location slots in the placement table holds the **vanilla** prize for that dungeon (e.g., Eastern Palace → `Prize_GreenPendant`, Palace of Darkness → `Prize_Crystal_PoD`, etc.). `OP_HAS_PRIZE` predicates evaluate against this identity mapping; the placement RNG is not advanced by prize-shuffle decisions

#### Scenario: Medallion shuffle disabled — vanilla mapping populates predicate
- **WHEN** `medallion_shuffle = false` and a seed is generated
- **THEN** Misery Mire's required medallion is `Ether` (vanilla) and Turtle Rock's is `Quake` (vanilla); `OP_MEDALLION_OPENS` predicates evaluate against this identity mapping; the placement RNG is not advanced by medallion-shuffle decisions

#### Scenario: Boss shuffle is Phase B (disabled in Phase A)
- **WHEN** a Phase A seed is generated
- **THEN** bosses remain at their vanilla rooms; `boss_shuffle` setting is grayed-out / labelled "Phase B" in the settings screen

### Requirement: Independent shuffle modules

Each shuffle (prize, medallion, dungeon-items, entrance, boss, drop-pool, palette, sprite) SHALL be a self-contained module that can be enabled or disabled independently of the others. Disabled modules SHALL NOT advance the placement RNG.

#### Scenario: Disabling a Phase A shuffle reverts to vanilla
- **WHEN** prize shuffle is disabled (vanilla mode)
- **THEN** crystals and pendants are placed at their vanilla dungeons, the placement RNG is not advanced by prize-shuffle decisions, and the same seed produces a different overall placement than with prize-shuffle enabled

#### Scenario: Shuffle toggles are reflected in settings hash
- **WHEN** any shuffle is toggled
- **THEN** the settings hash component of the share string changes

### Requirement: Dungeon-item shuffle modes

The dungeon-item shuffle SHALL be a separate setting **per item class** (small keys, big keys, maps, compasses). Each class SHALL have three modes: `Vanilla` (original location), `Dungeon` (anywhere within source dungeon — ALTTPR's `wildKeys=false`), `Wild` (anywhere in the world pool — keysanity). These modes affect `|pool|` and `|locations|` cardinality and the junk-padding calculation.

#### Scenario: Wild small keys adds locations to the world pool
- **WHEN** small-key dungeon-item mode is `Wild` and the same goal/world-state is otherwise configured identically
- **THEN** the set of placeable locations includes every dungeon's small-key locations, the item pool includes the small-key items at world-pool granularity, and total cardinality after junk-padding is correct

#### Scenario: Mixed modes per item class
- **WHEN** small keys are `Wild` but maps and compasses are `Vanilla`
- **THEN** small keys shuffle into the world pool, maps and compasses remain at their vanilla dungeon locations, and the settings hash distinguishes this from "all wild" or "all vanilla"

### Requirement: Prize-shuffle logic interactions

Prize shuffle SHALL preserve all logic interactions that the vanilla game baked into the prize → event mapping:

- The dungeon whose prize is `Prize_GreenPendant` gates Sahasrahla's NPC gift.
- The set of dungeons whose prizes total to 7 crystals gates Ganon's vulnerability (`crystals.ganon` setting governs the *count*; prize shuffle determines *which* dungeons hold them).
- The dungeon whose prize is `Prize_GreenPendant`, the dungeon whose prize is `Prize_RedPendant`, and the dungeon whose prize is `Prize_BluePendant` together gate the Sword Pedestal.

The logic graph SHALL encode these via `OP_HAS_PRIZE <prize_id>` predicates that resolve against the per-seed prize assignment.

#### Scenario: Sahasrahla unlocks when Green Pendant dungeon is cleared
- **WHEN** prize shuffle has assigned the Green Pendant to Eastern Palace and the player clears Eastern Palace
- **THEN** Sahasrahla's gift location becomes reachable per the logic graph, regardless of which dungeon vanilla put the Green Pendant in

#### Scenario: Sword Pedestal requires all three colored pendants
- **WHEN** prize shuffle has distributed the Green/Red/Blue Pendants across three dungeons
- **THEN** the Sword Pedestal is reachable only after clearing all three of those dungeons

### Requirement: Medallion-shuffle logic interactions

Medallion shuffle SHALL randomize the medallion requirements for Misery Mire and Turtle Rock entrances independently. The predicate VM SHALL evaluate `OP_MEDALLION_OPENS <entrance>` against the per-seed assignment combined with `OP_HAS_ITEM Bombos | Ether | Quake`.

#### Scenario: Misery Mire entrance gates on its assigned medallion
- **WHEN** medallion shuffle assigned Ether to Misery Mire
- **THEN** the Misery Mire entrance is reachable only when the inventory contains Ether (and other prerequisites)

#### Scenario: Misery Mire and Turtle Rock have independent assignments
- **WHEN** medallion shuffle is enabled
- **THEN** the medallion required for Misery Mire and the medallion required for Turtle Rock are drawn independently and may match or differ

### Requirement: Entrance shuffle modes (Phase C)

The entrance-shuffle module SHALL be exposed as **composable boolean axes** —
`shuffle_cave_entrances`, `shuffle_dungeon_entrances`, `coupled` (default on),
`cross_category`, `decoupled` — and SHALL maintain logic correctness: every required
dungeon and item remains reachable for the active goal under the resulting entrance
map. The four named ALTTPR modes (Simple, Restricted, Crossed, Insanity) SHALL be
realized as **presets** over these axes (plus a Custom mode); each mode scenario
below is the contract its preset must satisfy. See change `design.md §5a`.

**Phase C activation**: the composable entrance axes are added to the settings. The entrance permutation π SHALL be computed deterministically from `(share_string, generator_version)` and SHALL drive BOTH a runtime door overlay (`kOverworld_Entrance_Id`) and a per-seed logic edge overlay (per `randomizer-logic` — the retired `RegionRemap` scaffold is NOT used; see change `design.md §1`). The shuffle module SHALL run during generation such that the placer sees the entrance-remapped graph when computing reachability.

When all entrance axes are off (the default), the module SHALL be a no-op; no overlay is installed; non-shuffled seeds remain byte-identical in `placement_digest_hex`.

#### Scenario: Simple mode swaps single-entrance dungeons only
- **WHEN** entrance shuffle is Simple
- **THEN** only single-entrance dungeons are shuffled among themselves and multi-entrance dungeons retain their vanilla entrance layout

#### Scenario: Insanity mode permits cave-to-dungeon mappings
- **WHEN** entrance shuffle is Insanity
- **THEN** any overworld entrance may map to any interior, including cross-mappings between cave and dungeon interiors

#### Scenario: Entrance shuffle preserves goal reachability
- **WHEN** an entrance-shuffled seed is generated
- **THEN** the goal-reachability predicate (per `randomizer-logic`) passes for the entrance map

#### Scenario: No entrance shuffle preserves Phase A behavior
- **WHEN** all entrance axes are off (the default)
- **THEN** no entrance overlay is installed; the logic graph behaves identically to Phase A + B non-Inverted seeds; `placement_digest_hex` for the seed matches the equivalent Phase B seed byte-for-byte

#### Scenario: Coupled is the default (enter A returns to A)
- **WHEN** `coupled` is enabled (the default) and the player enters shuffled door A then exits the interior
- **THEN** the player returns to overworld door A (not the interior's vanilla door)

#### Scenario: Single-entrance dungeon swap (low-risk subset)
- **WHEN** `shuffle_dungeon_entrances` is enabled and Eastern Palace's entrance maps to Palace of Darkness's interior
- **THEN** entering EP's overworld door loads PoD's interior, reachability treats PoD's interior as reached via EP's overworld region, and the dungeon's prize/medallion gates remain tied to the dungeon (not the door)

#### Scenario: Restricted mode shuffles within categories
- **WHEN** entrance shuffle is Restricted
- **THEN** overworld-to-cave entrances shuffle among themselves; dungeon-entrance pairs shuffle among themselves; no cross-category mappings

#### Scenario: Crossed mode permits cross-category mappings
- **WHEN** entrance shuffle is Crossed
- **THEN** cross-category mappings are permitted (an overworld door may lead to a dungeon's first room) but mapping pairs are still 1-to-1

### Requirement: Boss shuffle (Phase B)

The boss-shuffle module SHALL randomize the boss assigned to each dungeon's boss room from a configurable pool while keeping the dungeon's reward (crystal or pendant from prize shuffle) tied to the dungeon, not to the boss. Bosses required by the active goal (e.g., Agahnim 1 in Standard mode) SHALL remain at their required locations.

#### Scenario: Prize stays with the dungeon (not the boss)
- **WHEN** boss shuffle moves the Helmasaur King boss to Skull Woods AND prize shuffle has assigned the Skull Woods crystal as Crystal 3
- **THEN** defeating Helmasaur King in Skull Woods grants Crystal 3, not the Palace of Darkness vanilla crystal

#### Scenario: Required boss is preserved
- **WHEN** the goal is Standard and boss shuffle is enabled
- **THEN** Agahnim 1 remains at Hyrule Castle Tower regardless of shuffle settings

### Requirement: Drop-pool shuffle (Phase B)

When enabled, the drop-pool shuffle SHALL randomize the enemy drop-prize table
(the 56-entry `kPrizeItems` table — 7 packs × 8 slots) as a deterministic
permutation keyed on `(settings, seed)`, and SHALL enforce a heart-drop floor so
weak early-game enemies still drop hearts. It SHALL be installed at slot load
(`Rando_ActivateSidecarSlot`) and consumed at the sprite-drop site
(`ForcePrizeDrop`); drop sprites use the always-loaded common prize graphics, so
shuffled drops render correctly. It is orthogonal to item placement (never changes
`placement_digest` / `sphere_digest`).

**Heart-drop floor**: pack 0 — the vanilla heart-heavy pack that weak overworld
enemies draw from, hence reachable from sphere 0 — SHALL keep at least one heart
entry after the shuffle. A violating draw is re-rolled on the same RNG stream
within a bounded budget; if the budget is exhausted the table falls back to the
vanilla identity and the spoiler records a `drop_heart_floor_fallback` warning.
Because the enemy→pack binding is static (per sprite type, not sphere-indexed),
the floor is enforced structurally on pack 0 rather than against live sphere data
— the faithful realization, in this fork's drop model, of "a tier reachable in
spheres 0-2 keeps a heart".

#### Scenario: Heart drop survives early game
- **WHEN** drop-pool shuffle is enabled
- **THEN** pack 0 of the shuffled drop table contains at least one heart entry

#### Scenario: Drop table is deterministic
- **WHEN** the same seed and drop-pool-shuffle setting are used
- **THEN** the generated drop table is byte-identical across generations and
  across host platforms

#### Scenario: Drop shuffle does not perturb item placement
- **WHEN** the same seed is generated with `drop_shuffle` on versus off
- **THEN** the `placement_digest` and `sphere_digest` are byte-identical; with the
  shuffle on, the spoiler `drop_tables` section is populated

#### Scenario: Disabled drop-pool preserves vanilla drops
- **WHEN** `drop_pool_shuffle == false`
- **THEN** the drop table is the vanilla identity; the spoiler `drop_tables`
  section is omitted

### Requirement: Cosmetic shuffles do not affect logic (Phase D)

Palette, sprite, and music shuffles SHALL be cosmetic-only and SHALL NOT alter the placement table, predicate evaluation, or the `settings_hash`. A separate `cosmetic_seed` SHALL drive cosmetic outputs.

`cosmetic_seed` SHALL be a **client-local configuration value** (a `[Graphics]` key in `zelda3.ini`), NOT a slot-header field and NOT part of any canonical serialization. It therefore never travels with the `share_string`: two players given the same `share_string` MAY each set their own `cosmetic_seed` and obtain gameplay-identical seeds with visually-distinct presentation. A `cosmetic_seed` of `0` SHALL resolve to the active slot's `seed_u64`, so a default install still yields a reproducible cosmetic result without INI editing.

Each axis is an independent per-axis setting, defaulting to **off** so that unopted play (rando or vanilla) is rendering-identical to the unmodified game:

- **Palette** SHALL support modes `vanilla` / `shuffled` / `grayscale` / `negative`, each a deterministic one-shot transform over the BGR555 palette buffers. (ALTTPR's animated gimmick modes — dizzy/sick/puke/blackout — are explicitly out of scope for this change.)
- **Sprite** SHALL, when pointed at a folder of `.zspr` files, deterministically pick one (stable filename sort) and load it through the existing ZSPR path; off preserves the configured single sprite.
- **Music** SHALL, when enabled, deterministically remap the song the engine queues per area; when an MSU-1 pack is loaded the remapped id SHALL drive MSU track selection.

Cosmetic outputs SHALL be derived from a dedicated RNG stream forked off `cosmetic_seed` (the `randomizer-core / RNG family` xoshiro256\*\*), never the fill RNG; the guarantee is cross-platform self-consistency, not byte-parity with ALTTPR's JS transforms.

#### Scenario: Cosmetic shuffle leaves placement untouched
- **WHEN** sprite shuffle is enabled with a fixed `cosmetic_seed`
- **THEN** the placement table is byte-identical to a non-sprite-shuffled run with the same `share_string`; only the rendered Link sprite differs

#### Scenario: cosmetic_seed is independent of settings_hash
- **WHEN** two runs share an identical `share_string` but differ in `cosmetic_seed`
- **THEN** their `settings_hash` and `placement_digest_hex` are identical; their on-screen palette / sprite / music differ

#### Scenario: Tournament cosmetic decoupling
- **WHEN** a tournament distributes one `share_string` to multiple players, each with their own `cosmetic_seed`
- **THEN** every player plays the same placement with personal cosmetic state; screenshots are visually distinct

#### Scenario: Default cosmetic_seed tracks the slot seed
- **WHEN** `cosmetic_seed` is `0` (default)
- **THEN** cosmetic outputs derive deterministically from the active slot's `seed_u64`, and re-loading the same slot reproduces the same look

#### Scenario: All axes off is vanilla-identical
- **WHEN** palette / sprite / music shuffle are all off
- **THEN** rendering and audio are byte-identical to the unmodified game (RAM/PPU compare clean)

#### Scenario: Music shuffle interacts cleanly with MSU-1
- **WHEN** music shuffle is enabled AND an MSU-1 pack is loaded
- **THEN** the remapped song id drives MSU-1 track selection; there is no MSU-1 incompatibility

### Requirement: Retro TakeAny selection RNG model

The TakeAny activation SHALL be driven by **pick-without-replacement** (ALTTPR `randomCollection` / `array_splice` semantics — NOT take-N-of-a-Fisher-Yates-shuffle) over the static 31-cave candidate list, seeded deterministically from a dedicated RNG forked off the seed (`seed_u64 ^ salt`) so the main fill RNG stream is unperturbed. Selection picks:

1. 4 "potion" caves (each gains `BluePotion@slot0` + `BossHeartContainer@slot1`).
2. A 5th distinct "weapon" cave from the remaining inactive caves (gains `ProgressiveSword`, or `Rupee300` when `mode.weapons ∈ {swordless, vanilla}`).

The fork does NOT reproduce ALTTPR's `mt_rand` byte-for-byte (it pins `xoshiro256**` per `randomizer-core / RNG family`); the determinism guarantee is **self-consistency** — identical `(settings, seed_u64)` yields an identical activated set on every platform. The regular-shop inventory is NOT part of this selection (Slice 3a shipped the 9 regular shops as identity-placed inventory; see `randomizer-placement` §6).

#### Scenario: Same seed produces same activated set
- **WHEN** two generations run with identical `(settings, seed_u64)` against Retro world-state
- **THEN** the 4 potion caves AND the 5th weapon cave are identical between the two runs
- **AND** the placement table is byte-identical regardless of build configuration or platform

#### Scenario: Cross-platform RNG determinism
- **WHEN** the same `(settings, seed_u64)` is generated on Linux, macOS, Windows, and Switch
- **THEN** the activated TakeAny set is identical across all four platforms

### Requirement: Door shuffle generation (basic, intensity 1, original door types)

The generator SHALL support a `door_shuffle` axis that randomizes the internal
door-to-door connections of dungeons. The committed scope is `door_shuffle ∈
{vanilla, basic}` at `intensity = 1` (Normal + spiral-staircase doors only) with
`door_type_mode = original` — vanilla per-dungeon SMALL-key-door **counts** with
positions re-chosen and prover-validated; big-key doors are NOT relocated. `basic`
SHALL shuffle door connections **within a single dungeon** (no cross-dungeon
pools).

MVP compatibility pins (normalized in `apply_derived_rules`, the entrance-axis
convention — silently coerced so the `settings_hash` matches the actually-generated
seed):

- Door shuffle is honored only on **Open/Standard** world states and only under
  **NoGlitches** logic (the door oracle models no glitch traversal; Retro collapses
  per-key-door thresholds; Inverted has its own logic tree); otherwise
  `door_shuffle` coerces to `vanilla`.
- Door shuffle is **mutually exclusive with entrance shuffle** (both redirect
  dungeon topology); door shuffle yields to an explicit entrance-shuffle request.
- Door shuffle **forces in-dungeon small AND big keys** (the prover's containment
  assumption + the `bk_restricted` ban require both).
- **Hyrule Castle is pinned in ALL world states** (forced escape start, Zelda
  escort, standard-mode key special-casing, sewers cross-exit modeling) and
  **Swamp Palace is pinned** (the only negated-event rule cluster + drain/flood
  runtime risk) — `kDoorShuffle_MvpDungeonMask` clears both bits; pinned dungeons
  keep their vanilla layout AND vanilla key doors.

The generated layout for each shuffled dungeon SHALL be **fully connected** (every
required room reachable) and **softlock-free**: no reachable ordering of key
collection forces spending the last available small key (or the big key) on a door
that strands all remaining progress. Generation SHALL be deterministic for a given
`(seed, settings, door_attempt)` — RNG seeded from `(base_seed, door_attempt)`,
every candidate list door-id-ordered before a draw, iteration-bounded, never
wall-clock (`budget_seconds = 0`).

The generator (implemented against the `shuffle_doors.h` contract:
`DoorShuffle_Generate` / `DoorShuffle_LayoutDigest` / `DoorExplore_Run`) SHALL
port, from the reference's **live** pipeline:

- the per-dungeon pool/builder path (`main_dungeon_pool` →
  `simple_dungeon_builder`) with the shared **portal analysis** fed by the same
  committed portal table the logic oracle uses — including reachable-vs-
  inaccessible portals, sole-entrance/required-passage marking, and the **Desert
  Back intensity-1 waiver** (the back lobby's split-dungeon constraint is waived at
  intensity 1, per the reference);
- the live stitcher `generate_dungeon` (`source/dungeon/DungeonStitcher.py` — NOT
  the deprecated `DungeonGenerator` sibling): `create_random_proposal` →
  `explore_proposal` (dual blue/orange crystal exploration) → `check_valid` (all
  regions + required paths) → `modify_proposal` local repairs, capped, exhaustion
  bumps `door_attempt`;
- the key-door prover: candidate search (including the pos<4 both-halves
  stateful-door constraint from `randomizer-door-runtime`),
  `find_valid_combination` with **deterministic sampling** (integer-only unbiased
  bounded draws, multiplicative `ncr` with overflow assert — no doubles, no
  platform-dependent modulo), and `validate_key_layout` (worst-case memoized
  search, drop-key economy, big-chest exclusion, self-locking-key rejection)
  emitting per-door worst-case thresholds and `bk_restricted`.

#### Scenario: Vanilla door shuffle is a no-op

- **WHEN** `door_shuffle == vanilla` (the default)
- **THEN** no door layout is generated or installed, reachability/placement match
  the baseline byte-for-byte, and the regression corpus digests are unchanged

#### Scenario: Incompatible settings coerce door shuffle off

- **WHEN** `door_shuffle == basic` is requested together with Inverted or Retro
  world state, a glitched logic tier, or entrance shuffle
- **THEN** `apply_derived_rules` normalizes `door_shuffle` to `vanilla` before
  serialization, so the canonical settings (and `settings_hash`) reflect what was
  actually generated

#### Scenario: Pinned dungeons keep vanilla layout and keys

- **WHEN** a basic seed is generated
- **THEN** Hyrule Castle and Swamp Palace are absent from the shuffled mask, keep
  their vanilla connections and vanilla key doors, and their logic evaluates the
  vanilla predicates

#### Scenario: Basic shuffle produces a connected, beatable dungeon

- **WHEN** `door_shuffle == basic` and a dungeon is shuffled
- **THEN** every required room (boss, prize, dungeon-item locations) is reachable
  under the generated layout from the dungeon's enterable portals, and the seed is
  beatable

#### Scenario: Key doors cannot softlock

- **WHEN** the key-door prover validates a candidate layout
- **THEN** it accepts the layout only if no reachable key-collection ordering
  spends the last available small/big key on a door leading solely to a dead end;
  otherwise it reduces the key-door count and retries, and exhaustion bumps
  `door_attempt`

#### Scenario: Determinism across platforms

- **WHEN** the same `(seed, settings, door_attempt)` is generated on different
  platforms
- **THEN** the door layout, key-door placement, per-door thresholds, and
  `DoorShuffle_LayoutDigest` are identical (id-sorted draw lists,
  iteration-bounded, no time budget, no floating point in sampling)

#### Scenario: Original door-type counts preserved

- **WHEN** `door_type_mode == original`
- **THEN** each shuffled dungeon keeps its vanilla count of small-key doors
  (relocated onto the new connections) and its big-key doors stay at their vanilla
  positions (no added or removed door types)

### Requirement: Boss shuffle — deferred special-case bosses

Blind, Kholdstare, and Trinexx SHALL remain pinned to their vanilla dungeons until a
follow-up supplies the home-room ENVIRONMENT their fights require — environment the
sprite/gfx/palette redirect does not carry. Each is playtest- or Enemizer-confirmed:

- **Blind (Thieves' Town)** — TT's boss room has no Blind sprite (`0xCE`); Blind is
  produced by a maiden-follower sequence and only materializes when
  `dung_savegame_state_bits & 0x2000` (set by a TT-only trigger) is true. Un-pinning
  requires (forward) a synthetic `0xCE` spawn + forcing that bit, and (reverse)
  suppressing the maiden when TT's assigned boss ≠ Blind.
- **Kholdstare (Ice Palace)** and **Trinexx (Turtle Rock)** — their encase / floor
  fights need the home room's "effect" byte (`$00AD` / header byte 4) plus a BG2
  object (the ice block / lava floor). Un-pinning requires carrying that header byte
  and injecting the room object into the destination room.

Because no headless test covers boss rendering or fight mechanics, un-pinning any of
the three SHALL be validated by end-to-end playtest. See design.md D7 for the full
per-boss requirements (with Enemizer references).

#### Scenario: Special-case bosses stay pinned
- **WHEN** `boss_shuffle == true`
- **THEN** Blind is at Thieves' Town, Kholdstare at Ice Palace, and Trinexx at Turtle
  Rock in every assignment; none appears in another dungeon and no other boss appears
  in theirs

#### Scenario: Un-pinning a special-case boss requires playtest validation
- **WHEN** a follow-up adds the home-room environment to make one of the three
  shuffleable
- **THEN** the change is validated by end-to-end playtest (the corpus and
  `--rando-selftest` do not exercise boss rendering or fight mechanics)

### Requirement: Enemy shuffle runtime install and type substitution

When the `enemy_shuffle` axis is enabled for a slot, the system SHALL randomize **which enemy sprite spawns** in each dungeon room and overworld area with a deterministically-chosen replacement, applied at the two distinct load paths (which store the type differently — the spec must not conflate them):

- **Dungeon** (`Dungeon_LoadSingleSprite`, `src/sprite.c:3807`): the type id is `src[2]`, written to `sprite_type[k]`. Substitution rewrites the chosen type into `sprite_type[k]` (a type-byte swap; the bit-packed `y`/`x` bytes are NOT touched).
- **Overworld** (`Overworld_LoadSprites`, `src/sprite.c:3895`): there is no `sprite_type` write at load — the type is stored as `sprite_where_in_overworld[...] = src[2] + 1` (`:3910`) and the sprite is spawned lazily by `Overworld_LoadProximaSpriteIfAlive` (`:3973`). Substitution rewrites the stored value as `pick + 1` (preserving the `+1` bias). The mechanism is a map rewrite, not a `sprite_type` swap.

The substitution SHALL be a self-contained module (`src/rando/shuffle_enemies.{c,h}`) that, like boss shuffle and drop-pool shuffle, is generated deterministically from `(settings, seed_u64)` (a dedicated RNG stream forked off the seed per `randomizer-core / RNG family`), installed at slot activation (`Rando_ActivateSidecarSlot`) **only on the settings-valid path** (mirroring boss/drop shuffle, which `*_Deactivate()` on the invalid / snapshot-restore / v1-slot path so a stale substitution is never leaked — `rando.c:1904-1960`), and torn down at deactivation. When `enemy_shuffle` is off, the load path SHALL take the vanilla branch and behavior SHALL be byte-identical to a non-shuffled slot.

Enemy shuffle SHALL NOT touch item placement: it draws no fill RNG and adds no logic predicate, so `placement_digest_hex` SHALL be byte-identical for every seed regardless of `enemy_shuffle`. (The `generator_version` bump it triggers version-locks the live axis and reflects the per-seed pad-bit `settings_hash` change; this axis itself did not grow the canonical settings layout — see `randomizer-core`.)

#### Scenario: Enemy shuffle off is vanilla-identical
- **WHEN** `enemy_shuffle` is off for a slot
- **THEN** no substitution is installed, every room/area spawns its vanilla enemy set, and the slot is byte-identical to the same slot generated without the axis

#### Scenario: Placement is unaffected by enemy shuffle
- **WHEN** two slots share an identical `share_string` except one has `enemy_shuffle` on and one off
- **THEN** their `placement_digest_hex` is byte-identical (enemy shuffle is orthogonal to item placement, like boss/drop shuffle)

#### Scenario: Substitution is deterministic
- **WHEN** the same `(settings, seed_u64)` is activated on any platform
- **THEN** each room/area receives the same replacement enemy set (cross-platform self-consistency)

### Requirement: Enemy shuffle GFX-sheet reshuffle and replacement safety

Every replacement enemy SHALL be drawn only from the set of enemies whose graphics-sheet requirement is satisfied by the room/area's **actually-loaded** sprite sheets and whose sprite type is observed in the same vanilla loader context (dungeon or overworld). For overworld areas, the replacement SHALL also require the current area's `overworld_sprite_palettes` value to be one that vanilla uses with that sprite type, using the same light/dark/special-area index that vanilla uses when it uploads the sprite palette. For dungeon rooms, when palette-aware widening is enabled, the replacement SHALL also require the room's resolved sprite-palette signature — the `(palette_sp0l, palette_sp5l, palette_sp6l)` triple from `kDungPalinfos[GetRoomHeaderPtr(room)[1]]` — to be one that vanilla uses with that sprite type. The loaded set derives from `sprite_graphics_index` → `kSpriteTilesets[index][0..3]` (`src/load_gfx.c:59`), **but a `0` subgroup entry does NOT load a sheet — it retains the previously-loaded `sprite_gfx_subset_N`** (`Gfx_LoadSpritesInner` loads each subset only `if (p[N])`, `load_gfx.c:627-640`), so the live loaded set depends on load history. The constraint check SHALL resolve safety against the live `sprite_gfx_subset_0..3` values. A replacement whose required sheets are not all present in the actually-loaded set, whose type is not present in vanilla sprite data for the current loader context, whose overworld palette is not observed with that type in vanilla overworld data, or — in a dungeon with widening enabled — whose dungeon palette signature is not observed with that type in vanilla dungeon data, SHALL NOT be selected.

When `enemy_shuffle` is active during a dungeon or overworld room/area sheet load, `EnemyShuffle_ReshuffleCurrentRoomSheets(row)` SHALL resolve the room/area's loaded sprite subgroup sheets and snapshot that resolved set before sprite graphics are decompressed. Runtime sheet widening rewrites **owned, unpinned** subgroup slots among slots `0,1,2,3` to palette-safe sheets, and SHALL commit a widened slot only when the resulting loaded set still admits a valid forced-substitution target for the room/area (the verify-then-commit guarantee below); otherwise that slot SHALL revert to the vanilla-resolved sheet. Widening is gated by a build flag (`ES_ENABLE_SHEET_WIDENING`) for rollback; the shipped default SHALL be enabled, and when the flag is disabled the dungeon palette gate and the widening both go inert so all subgroup slots resolve to the room/area's vanilla-resolved sheets (byte-identical to the pre-widening behavior). The hook SHALL be runtime-only and SHALL use the same parent `enemy_shuffle` axis (no additional canonical setting). For each subgroup slot:

- The current room/area's vanilla-resolved sheet is the row's non-zero sheet, or the per-slot inherited vanilla shadow when the row has `0`.
- A slot may reshuffle only if the row owns that slot, no present sprite/control object pins it, and the chosen sheet is palette-safe for the room/area (its candidate enemies render under a sprite palette this room/area loads).
- The chosen sheet SHALL be deterministic from `(seed, room_or_area, slot)`, SHALL be drawn from that slot's generated dungeon/overworld pool plus the vanilla-resolved sheet, and SHALL never be `0`; a widened sheet SHALL be committed only if it passes the forced-substitution verify, otherwise the slot SHALL resolve to the vanilla-resolved sheet.
- Inherited or pinned slots SHALL restore the vanilla-resolved sheet so a prior room's reshuffle cannot leak through `0`-inheritance.
- The resolved sheet set SHALL be snapshotted with the room/area key that produced it. The sprite-type picker SHALL substitute only when that snapshot key matches the sprite list being loaded and still matches the live `sprite_gfx_subset_0..3`; if the snapshot is missing, stale, or overwritten by a later transition step, the picker SHALL leave the source sprite unchanged.

**Verify-then-commit (forced-substitution fillability).** Widening a slot from its vanilla-resolved sheet removes that sheet from the loaded set, so every randomizable enemy the room/area carries on that slot is *forced* to substitute (its own sheet is no longer present). The implementation SHALL NOT commit a widened sheet for a slot unless, over the resulting live 4-slot set, the room/area still admits at least one valid substitute under the same constraints the picker applies — for dungeons `killable && !cannot_have_key` and palette-compatible with all required sheets loaded (and a water-capable such substitute when the room has a water source); for overworld, palette-compatible with all required sheets loaded (and water-capable when the area has a water source). When no such substitute exists, the slot SHALL revert to the vanilla-resolved sheet, which by construction restores fillability. This guarantee SHALL hold for every reachable widened set, so widening can never strand a forced substitution into an unloaded-sheet (garbage) render or render a key/shutter room unclearable.

The generated tables SHALL be sourced from Enemizer's MIT `SpriteRequirement.cs` / `SpriteConstants.cs` through `assets/scripts/gen_enemy_shuffle_tables.py`, not from hand summaries. The generated data SHALL include:

- `kEnemyTable[256]` with every randomizable enemy's complete required-sheet set; multi-slot enemies SHALL list every required subgroup slot, not only one representative sheet.
- `kSheetNeed[256]` with `KNOWN` and per-slot pin bits for every known type; group-level/NPC/object/boss/unknown types SHALL pin conservatively.
- Per-slot dungeon and overworld sheet pools whose members are disjoint by slot, preserving the position-unaware picker invariant.
- `kOverlordNeed[32]` for dungeon overlord spawn-slot needs; a known spawning overlord pins only the slots its spawned sprite needs, while unknown/no-spawn/boss-spawning overlords pin all slots. Overworld overlords still pin all slots.

The runtime SHALL also derive a compact context allowlist from the shipped vanilla sprite blobs (`kDungeonSprites`/`kDungeonSpriteOffs` and all stages of `kOverworldSprites`/`kOverworldSpriteOffs`). During the overworld scan it SHALL record the `kOverworldSpritePalettes` value for each list and derive a per-type overworld palette allowlist. During the dungeon scan it SHALL record each room's resolved sprite-palette signature (`kDungPalinfos[GetRoomHeaderPtr(room)[1]]`, the `(sp0l,sp5l,sp6l)` triple) and derive a per-type dungeon palette allowlist; two palinfo indices that resolve to the same triple SHALL count as one signature. Candidate selection SHALL require those allowlists in addition to generated sheet requirements and manual `never_use_*` bans, so a sprite that merely has compatible sheets is not enough to appear in the other loader context, under the wrong overworld sprite palette, or — with widening enabled — under a dungeon sprite-palette signature it is not observed with in vanilla.

The module SHALL NOT substitute entries flagged boss (or boss secondary) / object / NPC / do-not-randomize in the per-enemy constraint table, nor the per-context control and overlord markers — which differ between load paths:
- **Dungeon**: control entry `type == 0xe4`; overlord `x >= 0xe0` (`Dungeon_LoadSingleSprite`).
- **Overworld**: count/control marker `src[2] == 0xf4` (`Overworld_LoadSprites` `src/sprite.c:3903`); overlord `src[2] >= 0xf3` (`Overworld_LoadProximaSpriteIfAlive` `:3985-3992`) — NOT `x >= 0xe0`.

Excluded entries pass through unchanged.

Enabling widening SHALL NOT alter item placement: it draws no fill RNG and adds no logic predicate, so `placement_digest_hex` SHALL be byte-identical for every seed and the regression corpus SHALL regenerate byte-identical. The change SHALL version-lock the now-live runtime behavior via a `kGeneratorVersion` bump.

> `kGeneratorVersion` 77→78 (main concurrently took 77 for its MM/TR medallion-config change, so the version-drift convention applied at merge); only the version field moves for the widening — every seed's `placement_digest_hex` is byte-identical and `kSettingsCanonicalLen` is unchanged (widening rides the existing `enemy_shuffle` axis with no new canonical setting).

#### Scenario: Replacement stays within actually-loaded graphics sheets
- **WHEN** enemy shuffle picks a replacement for a room whose loaded sheet set (after resolving `0`-inheritance against live `sprite_gfx_subset_N`) is known
- **THEN** the chosen enemy's required sheet(s) are a subset of that loaded set; no enemy needing an unloaded sheet is ever placed, including in rooms whose `kSpriteTilesets` row has `0` entries

#### Scenario: Replacement stays within vanilla loader context
- **WHEN** enemy shuffle builds the candidate pool for a dungeon room or overworld area
- **THEN** it excludes any enemy type that is not present in vanilla sprite data for that same loader context, even if that enemy's sheets are currently loaded

#### Scenario: Overworld replacement stays within vanilla sprite palette context
- **WHEN** enemy shuffle builds the candidate pool for an overworld area
- **THEN** it excludes any enemy type whose vanilla overworld occurrences do not use the area's current sprite-palette id, even if that enemy's sheets are currently loaded

#### Scenario: Dungeon replacement stays within vanilla sprite palette signature
- **WHEN** enemy shuffle builds the candidate pool for a dungeon room and palette-aware widening is enabled
- **THEN** it excludes any enemy type whose vanilla dungeon occurrences do not use the room's current `(sp0l,sp5l,sp6l)` palette signature, even if that enemy's sheets are currently loaded

#### Scenario: Widened slot keeps the room fillable
- **WHEN** widening would rewrite an owned, unpinned slot to a sheet under which the resulting loaded set has no valid forced-substitution target for the room/area (for a dungeon: no `killable && !cannot_have_key`, palette-compatible, all-sheets-loaded enemy; for a room with a water source: additionally no such water-capable enemy)
- **THEN** that slot reverts to the vanilla-resolved sheet, so the room/area always retains a valid substitute and no forced substitution renders garbage or leaves a key/shutter room unclearable

#### Scenario: Sheet resolver commits only palette-safe, fillable widened slots
- **WHEN** a dungeon/overworld room owns subgroup slot `N`, no present sprite/overlord/object pins that slot, and palette-aware widening is enabled
- **THEN** the runtime may load a deterministic sheet from the generated slot-`N` pool or the vanilla sheet before decompression only if that sheet is palette-safe for the room/area and passes the verify-then-commit fillability check; otherwise the vanilla-resolved sheet is restored

#### Scenario: Widening disabled is vanilla-resolved and byte-identical
- **WHEN** the build flag `ES_ENABLE_SHEET_WIDENING` is disabled
- **THEN** every subgroup slot resolves to the room/area's vanilla-resolved sheet, the dungeon palette gate is inert, and runtime substitution behavior is byte-identical to the pre-widening build

#### Scenario: Multi-slot enemy requirements are complete
- **WHEN** a candidate enemy requires sheets in more than one subgroup slot
- **THEN** every required sheet must be present in the live loaded set before that enemy can be selected

#### Scenario: Stale transition sheet state fails closed
- **WHEN** a dungeon/overworld sprite list is loaded after a different room/area resolved sprite sheets
- **THEN** enemy shuffle does not use the stale sheet set to select replacements; affected entries pass through vanilla unless a matching snapshot exists for the current room/area

#### Scenario: Overlord rooms pin only known spawned-sheet needs
- **WHEN** a dungeon room contains an overlord marker with a generated `kOverlordNeed` entry
- **THEN** only the spawned sprite's required subgroup slots are pinned; unknown, no-spawn, boss-spawning, and overworld overlords pin all subgroup slots

#### Scenario: Excluded entries pass through (per-context markers)
- **WHEN** a loaded entry is a control/overlord marker (dungeon `0xe4` / `x>=0xe0`; overworld `0xf4` / `>=0xf3`), a boss (or boss secondary), an NPC, or a quest object
- **THEN** the module leaves it unchanged (bosses are owned by boss shuffle; NPCs/objects/triggers must stay intact)

#### Scenario: Placement is unaffected by enabling widening
- **WHEN** two seeds are generated with identical `(generator_version, settings, seed_u64)`, one on a build with widening enabled and one disabled
- **THEN** their `placement_digest_hex` is byte-identical (widening is runtime-only and orthogonal to item placement, like boss/drop shuffle)

### Requirement: Enemy shuffle preserves beatability where logic is blind

Logic does NOT model per-room kill-clear (the rando graph has no `CanKill<X>` predicate on a non-boss enemy — see `randomizer-shuffles` digest invariant), so beatability is enforced **entirely** by the runtime constraint table. A table bug does NOT move `placement_digest_hex` and is invisible to the corpus / `--rando-selftest` — it silently ships an unbeatable seed. The table is therefore the central correctness surface and SHALL conservatively mark any sprite of unknown safety as do-not-randomize. It SHALL preserve:

- **Dungeon-global killable + key-capable replacements**: `killable` and "may carry a key" are **independent** flags (Enemizer's `CannotHaveKey` is separate from `Killable` — e.g. Keese/Buzzblob/Geldman are killable but key-banned, `SpriteRequirement.cs`). As built, every substituted dungeon enemy SHALL satisfy `killable && !cannot_have_key`, not only rooms that are known key/shutter rooms. This over-approximates key/shutter safety and avoids relying on vanilla key-sprite scans, which are unreliable under item shuffle.
- **Room hard excludes**: Mimic Cave and Agahnim's Tower final bridge SHALL never substitute any enemy.
- **Flying restrictions**: rooms in the flying-exclude list SHALL not receive flying replacements.
- **Context safety**: the candidate pool SHALL be constrained by load context using both generated/manual directional bans (`never_use_dungeon` / `never_use_overworld`) and the runtime-derived vanilla-context allowlist. In overworld, it SHALL also be constrained by the runtime-derived per-type sprite-palette allowlist. A sprite whose sheets happen to be loaded is not eligible unless vanilla uses that sprite type in the same dungeon/overworld loader context and, for overworld, under the current sprite palette.
- **Boss / mini-boss / environment-dependent enemies**: bosses, GT mini-bosses, boss secondaries, Agahnim, Ganon, NPCs, quest objects, and unknown-safety sprites SHALL never be substituted or used to free reshuffle slots.

> **As-built note:** `ESF_WATER` is recorded in the table and the picker uses it for water-source sprites, so a vanilla water-capable source (for example Walking Zora) only substitutes to a water-capable replacement. This change does not implement an independent water-only room classifier for rooms whose source sprites are not themselves tagged water-capable; broader water stranding remains a playtest watch item / future refinement.

#### Scenario: Key room stays clearable under item shuffle
- **WHEN** a dungeon room is shuffled and item placement could put a key behind an enemy clear
- **THEN** every substituted dungeon enemy satisfies `killable && !cannot_have_key`, so the key/door remains obtainable independent of where vanilla placed key sprites

#### Scenario: Hard-excluded rooms pass through
- **WHEN** the room is Mimic Cave or Agahnim's Tower final bridge
- **THEN** enemy shuffle leaves every sprite unchanged

#### Scenario: Context restrictions respected
- **WHEN** an enemy is flagged `never_use_overworld` / `never_use_dungeon`, or does not appear in vanilla data for the current loader context
- **THEN** it is excluded from that context's candidate pool

#### Scenario: Overworld palette restrictions respected
- **WHEN** an enemy appears in vanilla overworld data but not with the current area's overworld sprite palette id
- **THEN** it is excluded from that overworld area's candidate pool

#### Scenario: Boss and mini-boss logic remains valid
- **WHEN** a dungeon or GT mini-boss location is gated by `CanKillBoss` or a direct `CanKill<Boss>` macro
- **THEN** enemy shuffle has not substituted the boss/mini-boss sprite that the logic predicate assumes

### Requirement: Enemy shuffle HP and contact-damage randomization

When `enemy_shuffle` is active, the same parent axis SHALL also apply deterministic per-seed, per-sprite-type stat variation to non-boss enemies at `SpritePrep_LoadProperties`:

- Health SHALL be scaled by a deterministic factor in `[0.5x, 2.0x]`, clamped to `[1,255]`.
- `base == 0` health SHALL remain `0`, so NPCs/objects/non-killable sprites are not made killable or otherwise changed.
- Boss and boss-secondary sprite types SHALL keep vanilla health, avoiding boss-specific table-index hazards such as Helmasaur King's `sprite_health >> 2` lookup.
- Plain contact-damage classes `1..8` SHALL be nudged by deterministic `-1/0/+1`, clamped to `[1,8]`.
- Damage values `0`, values with flag bits, and all boss/boss-secondary damage values SHALL pass through unchanged.

This stat variation SHALL be runtime-only under `enemy_shuffle`; it SHALL NOT add a canonical axis, placement predicate, or fill RNG draw.

#### Scenario: HP scaling is bounded and never creates zero-HP enemies
- **WHEN** a non-boss enemy with non-zero base HP is prepared under `enemy_shuffle`
- **THEN** its HP is deterministic for `(seed, sprite_type)`, is at least `1`, and is within the `[0.5x, 2.0x]` scaled bound after clamping

#### Scenario: Boss stats stay vanilla
- **WHEN** a boss, boss secondary, Agahnim, or Ganon sprite is prepared under `enemy_shuffle`
- **THEN** its HP and contact-damage values are unchanged from vanilla

#### Scenario: Damage flags are preserved
- **WHEN** a sprite's vanilla contact-damage byte is `0` or has high flag bits / special semantics
- **THEN** enemy shuffle leaves that byte unchanged

### Requirement: Door-shuffle layout digest and pot-tier input

Door-shuffle layout generation SHALL include the effective pot tier and the generated
pot-door bridge digest as explicit inputs. The accepted layout's digest SHALL cover
that model data so a slot generated with active pot locations cannot later be
activated with a door layout proven under the pots-off model or under different local
pot-door metadata.

#### Scenario: Door layout regenerated with matching pot model

- **WHEN** a saved door+pot seed is activated
- **THEN** runtime regeneration uses the canonical settings' effective pot tier
- **AND** the regenerated bridge digest and layout digest match the digests accepted
  during generation

#### Scenario: Pot-tier drift fails activation

- **WHEN** code changes cause the same saved door+pot seed to regenerate a different
  layout or prover model
- **THEN** the door digest check fails activation instead of installing mismatched
  runtime reachability

#### Scenario: Bridge drift fails activation

- **WHEN** a door+pot slot was generated with one pot-door bridge and the current
  build has different bridge rows, predicates, drop-index joins, or no bridge
- **THEN** activation refuses the slot through the same hard-fail path as door layout
  digest drift

#### Scenario: Snapshot replay restores the matching door graph

- **WHEN** a state snapshot was written from an active door+pot seed
- **THEN** the snapshot carries the accepted door attempt and layout digest
- **AND** replay clears any currently installed door graph before restoring snapshot
  state
- **AND** replay reinstalls a door graph only after regenerating it from the snapshot
  settings and seed and matching the saved digest
- **AND** installing that door graph tears down any currently installed entrance graph

### Requirement: Enemy shuffle pins kill-gated and forced-drop rooms under the enemy-souls tier
When `souls_shuffle=bosses+enemies` (effective) and enemy shuffle are both active, the enemy-shuffle picker SHALL return the vanilla species for every room in the generated pin set (`kRandoSoulPinRooms`, baked from `soul_rooms.gen.yaml`): the kill-gated rooms with enemy-souled residents and, whole-room, the forced enemy-drop rooms — so generated soul requirements and the runtime drop-source exemption (both computed from vanilla species/slots) remain correct at runtime. All other rooms shuffle freely and suppression keys on the final substituted species; soul-less kill rooms need no pin because enemy shuffle only substitutes ESF_RANDOMIZABLE species, all of which carry souls. (Ordinary enemy-check locations never coexist with enemy shuffle — `enemy_drop_checks` already coerces Dungeon/All to Keys under enemy shuffle.)

#### Scenario: Kill room keeps vanilla species
- **WHEN** a `bosses+enemies` seed with enemy shuffle on loads a room from the kill-gated set
- **THEN** the room's enemies are the vanilla species, and the kill-gate hold/requirements match the generated soul table

#### Scenario: Key-drop guard keeps vanilla species
- **WHEN** a `bosses+enemies` seed with enemy shuffle on loads a room containing a forced enemy-drop source slot
- **THEN** that slot's enemy is the vanilla species, so its generated soul requirement matches the enemy actually present

#### Scenario: Enemy shuffle stays placement-neutral
- **WHEN** two seeds differ only in the enemy-shuffle toggle (same `souls_shuffle` tier and seed)
- **THEN** their `placement_digest` values are identical (the existing enemy-shuffle digest-neutrality invariant holds under all souls tiers)

### Requirement: Boss shuffle composes with boss souls via species-keyed suppression
Boss-soul suppression SHALL act on the sprite species loaded through the boss-shuffle render redirect, so the soul that gates a dungeon is the soul of the boss actually assigned there; room-data secondary sprites (Trinexx arms, Kholdstare shell) SHALL suppress together with their parent boss.

#### Scenario: Redirected boss room gates on assigned boss
- **WHEN** boss shuffle assigns Arrghus to Skull Woods and the player lacks the Arrghus Soul
- **THEN** entering the Skull Woods boss room spawns neither Arrghus nor its puffs, and the fight starts normally once the Arrghus Soul is owned

#### Scenario: Compound boss suppresses completely
- **WHEN** the player enters a boss room for Trinexx or Kholdstare without the matching soul
- **THEN** the parent and all room-data secondary sprites (arms, shell) are suppressed together

### Requirement: Dungeon chains axis and mutual exclusion

The settings surface SHALL expose a `dungeon_chains` boolean axis (default off)
packed into the existing canonical entrance-axis byte without changing the
canonical settings length. `apply_derived_rules` SHALL normalize `dungeon_chains`
to off unless ALL of the following hold: no entrance-shuffle axis is active,
`door_shuffle` is vanilla, `boss_shuffle` is off, the world state is Open or
Standard, and the logic tier is NoGlitches. Normalization is one-directional:
enabling `dungeon_chains` SHALL never coerce another axis; conflicting axes win
and chains yields. The default-off packing SHALL leave existing canonical
serializations and settings hashes byte-identical.

#### Scenario: Chains force in-dungeon keys

- **WHEN** `dungeon_chains` remains on through normalization
- **THEN** small keys and big keys serialize as in-dungeon mode, regardless of
  their requested raw modes

#### Scenario: Chains yields to entrance shuffle

- **WHEN** `dungeon_chains` is requested together with any entrance-shuffle axis
- **THEN** `apply_derived_rules` normalizes `dungeon_chains` to off before
  serialization, and the canonical settings reflect what was actually generated

#### Scenario: Chains yields to incompatible world states and tiers

- **WHEN** `dungeon_chains` is requested with Inverted or Retro world state, a
  glitched logic tier, `door_shuffle == basic`, or `boss_shuffle` on
- **THEN** `dungeon_chains` normalizes to off

#### Scenario: Compatible settings keep chains on

- **WHEN** `dungeon_chains` is requested with Open world state, NoGlitches, and
  no entrance/door/boss shuffle active
- **THEN** `dungeon_chains` remains on through normalization and is reflected in
  the settings hash

#### Scenario: Default is hash-stable

- **WHEN** settings leave `dungeon_chains` at its default (off)
- **THEN** the canonical serialization and `settings_hash` are byte-identical to
  builds predating the axis

### Requirement: Enemy-drop checks and existing shuffle interactions

Enemy-drop checks SHALL be orthogonal to `drop_shuffle`. `drop_shuffle` continues to
randomize the non-forced prize-pack table. An active enemy-drop check bypasses that
table and grants the item placed at its location; it SHALL NOT also emit the vanilla
forced small key or a prize-pack substitute.

`enemy_shuffle` SHALL compose with forced-key enemy-drop checks. Enemy shuffle may
substitute the real sprite type, but it SHALL NOT reorder the dungeon sprite-list
entry, shuffle the forced-key marker, or change the runtime source slot used by
the generated enemy-drop lookup. Active forced-key enemy-drop checks SHALL continue
to resolve by `(room, source_slot, drop_kind)`, not by substituted enemy type.
Ordinary dungeon-enemy checks SHALL be disabled by downgrading requested
`enemy_drop_checks = Dungeon` to effective `Keys` while enemy shuffle is active,
because placement does not currently know the shuffled enemy type or HP scaling.

Pot shuffle MAY compose with enemy-drop checks in Wild/Retro mode through the
generated reach predicates and existing pot behavior. Pot shuffle SHALL also compose
with Dungeon enemy-drop checks through combined free-drop accounting. Door shuffle
SHALL compose with enemy-drop checks through the generated door x enemy-drop bridge:
active enemy DROP rows are removed from vanilla free-drop accounting and counted as
itemized key sources when their door regions are reached.

#### Scenario: Drop shuffle affects only non-check drops
- **WHEN** `drop_shuffle` and active enemy-drop keys are both enabled
- **THEN** non-check enemy prize-pack drops use the shuffled prize table, while active
  enemy-drop checks grant their placed item and do not emit a prize-pack substitute

#### Scenario: Enemy shuffle preserves source-slot identity
- **WHEN** `enemy_shuffle` is active and the user requests `enemy_drop_checks = Keys`
- **THEN** effective settings keep enemy-drop checks active in supported key modes,
  and each forced-drop source resolves through its vanilla room/source-slot lookup

#### Scenario: Door shuffle models itemized enemy drops
- **WHEN** door shuffle and enemy-drop checks are requested together
- **THEN** effective settings keep enemy-drop checks active and the door layout digest
  includes the active enemy-drop bridge digest

### Requirement: All-enemy tier composes with shuffle axes explicitly

`enemy_drop_checks=all` SHALL compose explicitly with `drop_shuffle`: an ordinary
active enemy check grants its placed item instead of producing a prize-pack pickup,
while non-check enemies keep using the vanilla or shuffled drop table. Suppressed
pickup events SHALL still preserve prize-pack cursor, luck, RNG, and drop-shuffle
sequencing. Forced key-drop pickups, Pikit-held items, boss prizes/hearts, and
scripted progression SHALL retain their dedicated behavior.

With `enemy_shuffle`, forced-key `EnemyDrop` rows keep the existing vanilla
room/source-slot identity and requested `All` normalizes to the highest lower tier
allowed by existing derived rules, normally `Keys` but `Off` when the keys tier is
unsupported. Non-key `Enemy` rows in `Dungeon` or `All` SHALL remain inactive while
enemy shuffle is active until a future change makes enemy shuffle placement-affecting
for all-enemy logic, including substituted type, HP, damage, killability,
settings-hash/corpus expectations, and digest behavior.

With door shuffle, requested `All` SHALL remain effective only when non-key
all-enemy door-region bridges cover every emitted non-key source. The bridge rows,
bridge digest, and effective all-enemy tier SHALL participate in door layout
generation, the accepted `DoorShuffleLayout` identity/digest, sidecar activation,
and snapshot replay validation. Without that bridge/digest/replay support, requested
`All` SHALL normalize to `Keys`.

With boss shuffle, requested `All` SHALL remain effective because dungeon bosses
are excluded from all-enemy checks and the emitted GT-miniboss checks are outside
the shuffleable dungeon-boss room set.

With entrance shuffle, including cave entrance shuffle, requested `All` SHALL remain
effective only after all-enemy overworld/domain reachability is modeled against the
entrance graph. Until then, requested `All` SHALL normalize to `Dungeon` unless
another rule lowers the effective tier further. Existing cave-entrance pot/key
derived rules still apply before this normalization.

Generated thrown-pot routes SHALL require effective pot shuffle to be off. While any
effective pot-sanity tier is active, enemy checks SHALL fall back to their reviewed
inventory-combat routes so no shuffled pot can be counted both as a required item
check and a future thrown weapon.

#### Scenario: Drop shuffle remains prize-pack-only
- **WHEN** `drop_shuffle` and effective `enemy_drop_checks=all` are both active
- **THEN** non-check enemy prize-pack drops use the shuffled prize table
- **AND** active enemy checks grant placed items through their location dispatch path
- **AND** an ordinary active enemy check does not also produce a prize-pack pickup

#### Scenario: Enemy shuffle normalizes all to supported lower tier
- **WHEN** `enemy_shuffle` is active and settings request `enemy_drop_checks=all`
- **THEN** derived settings normalize to the highest lower tier allowed by existing
  derived rules
- **AND** ordinary dungeon, overworld, boss, and scripted all-enemy rows are inactive

#### Scenario: Door shuffle requires all-enemy bridge digest
- **WHEN** door shuffle and effective `enemy_drop_checks=all` are active
- **THEN** door layout generation and activation include the non-key all-enemy bridge
  digest and effective all-enemy tier

#### Scenario: Missing all-enemy door bridge normalizes to keys
- **WHEN** door shuffle is active and non-key all-enemy door bridge support is absent
- **THEN** requested `All` normalizes to `Keys`

#### Scenario: Boss shuffle preserves all
- **WHEN** boss shuffle and effective `enemy_drop_checks=all` are active
- **THEN** `All` remains effective because dungeon bosses are excluded and the
  reviewed GT-miniboss checks are outside the shuffleable dungeon-boss room set

#### Scenario: Entrance shuffle excludes all-domain rows until modeled
- **WHEN** entrance shuffle is active before all-enemy entrance-graph reachability is
  modeled
- **THEN** requested `All` normalizes to `Dungeon` unless another rule lowers the
  effective tier further

#### Scenario: Pot route does not double-count required pot check
- **WHEN** a pot must be lifted to collect a required pot-sanity item before an enemy
  check
- **THEN** the thrown-pot route is inactive for that enemy check
- **AND** the reviewed inventory-combat route remains required

