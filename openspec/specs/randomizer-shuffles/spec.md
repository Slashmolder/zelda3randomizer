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

