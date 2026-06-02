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

The entrance-shuffle module SHALL support four modes — Simple, Restricted, Crossed, Insanity — and SHALL maintain logic correctness: every required dungeon and item remains reachable for the active goal under the resulting entrance map.

#### Scenario: Simple mode swaps single-entrance dungeons only
- **WHEN** entrance shuffle is Simple
- **THEN** only single-entrance dungeons are shuffled among themselves and multi-entrance dungeons retain their vanilla entrance layout

#### Scenario: Insanity mode permits cave-to-dungeon mappings
- **WHEN** entrance shuffle is Insanity
- **THEN** any overworld entrance may map to any interior, including cross-mappings between cave and dungeon interiors

#### Scenario: Entrance shuffle preserves goal reachability
- **WHEN** an entrance-shuffled seed is generated
- **THEN** the goal-reachability predicate (per `randomizer-logic`) passes for the entrance map

### Requirement: Boss shuffle (Phase B)

The boss-shuffle module SHALL randomize the boss assigned to each dungeon's boss room from a configurable pool while keeping the dungeon's reward (crystal or pendant from prize shuffle) tied to the dungeon, not to the boss. Bosses required by the active goal (e.g., Agahnim 1 in Standard mode) SHALL remain at their required locations.

#### Scenario: Prize stays with the dungeon (not the boss)
- **WHEN** boss shuffle moves the Helmasaur King boss to Skull Woods AND prize shuffle has assigned the Skull Woods crystal as Crystal 3
- **THEN** defeating Helmasaur King in Skull Woods grants Crystal 3, not the Palace of Darkness vanilla crystal

#### Scenario: Required boss is preserved
- **WHEN** the goal is Standard and boss shuffle is enabled
- **THEN** Agahnim 1 remains at Hyrule Castle Tower regardless of shuffle settings

### Requirement: Drop-pool shuffle (Phase B)

When enabled, the drop-pool shuffle SHALL randomize the contents of the eight tiered enemy drop tables. Drop-pool generation SHALL run after item placement so reachable spheres are known.

#### Scenario: Heart drop survives early game
- **WHEN** drop-pool shuffle is enabled
- **THEN** at least one of the drop tables reachable in the first three placement spheres contains a heart-drop entry

#### Scenario: Drop table is deterministic
- **WHEN** the same seed and drop-pool-shuffle setting are used
- **THEN** the generated drop tables are byte-identical across generations

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

