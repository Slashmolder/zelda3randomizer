## MODIFIED Requirements

### Requirement: Boss shuffle (Phase B)

The boss-shuffle generator SHALL randomize the boss assigned to each dungeon's
boss room from a 7-boss pool while keeping the dungeon's reward (crystal or pendant
from prize shuffle) tied to the dungeon, not to the boss. The assignment SHALL be
deterministic from `(settings, seed)` and SHALL be emitted in the spoiler under
`boss_assignments`.

The permutation runs at generation time and is orthogonal to item placement
(`boss_shuffle == false` is byte-identical; only boss-on seeds move, via the
boss-kill predicate below). Bosses pinned at their canonical slots (NOT shuffled):
- **Goal/identity-pinned**: Agahnim 1 (Hyrule Castle Tower), Agahnim 2 (Ganon's
  Tower top), Ganon (Pyramid) — Agahnim 1/2 also share sprite_type 0x7A.
- **Environment-pinned**: Blind (Thieves' Town), Kholdstare (Ice Palace), Trinexx
  (Turtle Rock) — their fights need home-room environment the runtime render can't
  supply (see "deferred special-case bosses" below + design.md D7).

The seven shuffleable bosses — Armos Knights, Lanmolas, Moldorm, Helmasaur King,
Arrghus, Mothula, Vitreous — permute across the seven non-pinned dungeon-boss rooms
(EP, DP, ToH, PoD, SP, SW, MM).

**Runtime substitution is LIVE** (the Enemizer pointer-redirect model): a shuffled
boss room loads the assigned boss's HOME boss-room sprite list, sprite-graphics
index, AND sprite palette, and shifts the formation to the dungeon's own boss spot,
so the substituted boss spawns reachable with correct tiles, colors, and spawn
count. The `boss_shuffle` toggle SHALL be exposed in the PC native settings window.

**Beatability:** each dungeon's `"<Dungeon> - Boss"` / `- Prize` location SHALL be
gated on the kill predicate of its *currently assigned* boss (not its vanilla boss),
via the `OP_CAN_KILL_BOSS(dungeon)` predicate-VM op — so an item-gated boss (e.g.
Trinexx's FireRod+IceRod) shuffled into a dungeon cannot strand its prize
(design.md D6).

#### Scenario: Boss assignment is generated and deterministic
- **WHEN** `boss_shuffle == true` for a given seed
- **THEN** the per-dungeon boss assignment is computed deterministically from
  `(settings, seed)`, the pinned bosses (Agahnim 1/2, Ganon, Blind, Kholdstare,
  Trinexx) stay at their canonical slots, the 7 shuffleable dungeons hold a
  permutation of the 7-boss pool, and the spoiler lists the assignment under
  `boss_assignments`

#### Scenario: Boss shuffle does not perturb item placement
- **WHEN** the same seed is generated with `boss_shuffle` off
- **THEN** the `placement_digest` and `sphere_digest` are byte-identical to the
  pre-boss-shuffle baseline (only `boss_shuffle == true` seeds move, via the
  boss-kill predicate)

#### Scenario: Boss runtime substitution renders the assigned boss
- **WHEN** a `boss_shuffle == true` slot is loaded and played
- **THEN** each of the seven shuffleable dungeons renders and fights its *assigned*
  boss with correct tiles, colors, formation, and a reachable spawn position, and
  the dungeon's `- Boss` / `- Prize` gate evaluates against that assigned boss

#### Scenario: Disabled boss shuffle preserves vanilla bosses
- **WHEN** `boss_shuffle == false`
- **THEN** every dungeon's boss is its vanilla boss; the spoiler `boss_assignments`
  section is omitted

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
