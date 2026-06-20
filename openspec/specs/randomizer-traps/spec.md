# randomizer-traps Specification

## Purpose
TBD - created by archiving change add-rando-trap-catalog. Update Purpose after archive.
## Requirements
### Requirement: Trap effect catalog and categories

The randomizer SHALL support a catalog of masquerade-trap effects grouped into five
categories. Each effect SHALL be a distinct placed item id in a **contiguous** trap id block
(extending the existing `TrapDamage`/`TrapFreeze` ids), and SHALL belong to exactly one category:

- **HAZARD** — effects that can deal HP damage or spawn hostile entities (Damage, Bomb, Ambush,
  Cucco swarm).
- **IMPAIR** — effects that disrupt control without HP loss (Freeze, Reversed controls,
  Scrambled controls, Disarmed).
- **DRAIN** — effects that remove a consumable resource (Rupee, Magic, Ammo).
- **SCARE** — harmless visual/audio effects (Screen-shake, Darkness, Fake-teleport, Fake
  low-health alarm).
- **DISPLACE** — effects that relocate Link to a provably-safe location (Teleport).

Every trap effect SHALL, on collection, present the same masquerade and reveal as the existing
traps: a deterministic good-item decoy icon, the normal pickup chime, then a delayed "bad" cue
and reveal dialogue. No trap effect SHALL be selectable that requires item ownership or inventory
to fire safely (e.g. a Bomb effect SHALL behave correctly with zero bombs held).

#### Scenario: Every effect id resolves a decoy and a dispatch

- **WHEN** any item id in the contiguous trap block is placed and collected
- **THEN** the masquerade resolves a non-green-rupee decoy icon for it, the collection routes to
  the trap dispatch (never to the placed id's raw item grant), and the location is marked checked
  with no vanilla item leaked

#### Scenario: Bomb effect with empty inventory

- **WHEN** a Bomb trap is collected while the player holds zero bombs
- **THEN** a live bomb spawns and can damage Link, and the player's bomb count after the effect is
  exactly what it was before (no bomb is stolen, and none is permanently granted even if the
  ancilla table was full and no bomb spawned)

### Requirement: Trap category enable mask with zero-sentinel

The effective trap categories SHALL be controlled by a `trap_categories` enable mask serialized in
canonical byte [27] bits 2-6 (HAZARD=2, IMPAIR=3, DRAIN=4, SCARE=5, DISPLACE=6). When
`traps > 0` and `trap_categories == 0`, **all categories SHALL be enabled** (the zero-sentinel),
so that the default settings (traps off, mask zero) serialize byte-identically and "traps on,
untouched" enables every category. The mask SHALL NOT enlarge the canonical blob:
`kSettingsCanonicalLen` SHALL remain 28.

A trap SHALL only ever be an effect whose category is enabled for the seed. There SHALL be no
representable state of "traps on with zero categories" — that condition is expressed as
`traps == off`.

#### Scenario: Default settings unchanged by the new field

- **WHEN** a seed is generated with default settings (traps off)
- **THEN** `trap_categories` serializes as zero, the canonical 28-byte blob and the default
  `settings_hash` are byte-identical to the pre-change binary, and the corpus default entries are
  byte-identical

#### Scenario: Narrowing categories changes the seed deterministically

- **WHEN** two seeds share `(seed, all other settings)` but one sets `trap_categories` to a
  non-zero subset and the other leaves it zero (all categories)
- **THEN** their canonical blobs and `settings_hash` differ, and only effects from enabled
  categories appear at trap slots in each

### Requirement: Deterministic trap type selection

The effect placed at each trap slot SHALL be chosen deterministically from `(seed, location_id)`
and the enabled-category set, independent of placement order, using a mixing function
domain-separated from the decoy-icon mix. The selector SHALL pick an enabled category, then an
effect within that category, uniformly for the initial release (per-effect weighting is out of
scope and would require a canonical-length change). The selector SHALL run only when `traps > 0`
and SHALL NOT draw from the placer RNG stream in a way that perturbs non-trap placement.

#### Scenario: Same inputs yield the same trap types

- **WHEN** the same `(generator_version, settings, seed)` is generated twice
- **THEN** every trap slot holds the same effect id both times

#### Scenario: Traps-off seeds are unaffected

- **WHEN** a seed has `traps == off`
- **THEN** the selector does not run, no trap ids are placed, and the placement table is
  byte-identical to a binary without this change

### Requirement: Traps occupy only junk placements

Trap effects SHALL replace only eligible junk placements (consumable-grade fills) after the
assumed fill completes, and SHALL NOT occupy any location that gates logic. Adding new effect ids
SHALL NOT change this: a trap is never a progression item and never affects reachability or sphere
order.

#### Scenario: Sphere digest is unmoved by trap selection

- **WHEN** a traps-on seed is regenerated after the selector change
- **THEN** its `placement_digest` may change (the effect at each trap slot changed) but its
  `sphere_digest` is byte-identical (traps gate nothing)

### Requirement: Arm-in-trigger, apply-in-gated-tick dispatch

The collection trigger SHALL only *arm* a trap — record the effect, its duration, a pending-onset
flag, and the reveal — and SHALL NOT itself spawn sprites, warp Link, write PPU registers, or
mutate Link movement state, because the trigger fires from many grant paths in any
`main_module_index`/`submodule_index`. The per-frame effect tick SHALL perform the effect's
one-shot onset and any sustained behavior, gated to `main_module_index ∈ {7,9,11}`; effects that
spawn sprites, warp, or write PPU registers SHALL additionally require `submodule_index == 0` and
SHALL **defer** (not drop) their onset until that condition holds. Trap effects SHALL be
non-stacking: a second trap collected while one is active replaces it.

#### Scenario: Trap collected mid-transition defers its world-mutation

- **WHEN** a Bomb/Ambush/Cucco/Teleport/Darkness trap is collected while `submodule_index != 0`
  (mid-scroll or mid-transition)
- **THEN** its onset is deferred until `submodule_index == 0` within a gameplay module, and no
  sprite/warp/PPU action runs during the transition

#### Scenario: Live-input effects are not zeroed by the freeze handler

- **WHEN** a Reversed-controls or Disarmed trap is active
- **THEN** the tick applies that effect's input transform to live input and does NOT route through
  the freeze motion-neutralizer, so the player retains (remapped or masked) control rather than
  being frozen

### Requirement: Context-appropriate effect fallback

A trap effect whose runtime context guard is not satisfied SHALL invoke a declared fallback effect
that is safe in the current context (e.g. Darkness collected outdoors falls back to Screen-shake;
Cucco collected indoors falls back to Bomb), evaluated in the gated tick when context is known and
terminating at a universally-safe effect (Freeze/Damage). A trap SHALL never become a no-op or a
corrupting action because its stored effect did not match the collection context.

#### Scenario: Dungeon-only effect collected outdoors

- **WHEN** a Darkness trap is collected on the overworld
- **THEN** its fallback (Screen-shake) runs instead, and no dungeon-only register is written
  outdoors

### Requirement: Leak-safe teardown for PPU-writing effects

Effects that write g_ram-backed PPU registers SHALL snapshot the prior register value at onset and
restore it at stun expiry, and a rando-active room/area load SHALL re-derive these registers from
room state. The affected registers are the dungeon fixed-color register (Darkness) and the BG
scroll offsets (Screen-shake). No g_ram-backed PPU write SHALL outlive the file-static effect
timer, so a save or snapshot captured mid-effect can never reload into a permanently dark room or a
frozen camera.

#### Scenario: Save taken during darkness reloads lit

- **WHEN** the player saves (or takes a Ctrl+F1 snapshot) while a Darkness trap is active and then
  reloads
- **THEN** the room loads with correct lighting (the fixed-color register re-derived from room
  state), not stuck dark

### Requirement: Trap effects must not corrupt progress-proxy bytes

Resource-drain effects SHALL write only the live consumable counter and SHALL NOT write any byte
the engine uses as a permanent-progress proxy. Specifically: Magic drain SHALL write only the
magic meter (and its filler), never `link_magic_consumption` (the Half/Quarter magic upgrade
tier); Ammo drain SHALL write only `link_num_arrows` / `link_item_bombs`, never `link_arrow_filler`
(a per-frame drain counter, not a tier). No trap effect SHALL index a vanilla table or branch on a
randomizer-varied stat in a way that can read out of bounds.

#### Scenario: Magic drain does not downgrade the upgrade

- **WHEN** a Magic-drain trap fires for a player who owns the 1/2 or 1/4 magic upgrade
- **THEN** the magic meter empties but the upgrade tier (`link_magic_consumption`) is unchanged, so
  the permanent upgrade is retained

### Requirement: Medallion-spell screen-clears are not trap effects

The Quake and Bombos medallion spells SHALL NOT be used as standalone trap effects, because their
damage routine harms only enemy sprites (never Link) and would therefore aid the player. The
screen-shake visual SHALL instead be produced by the self-expiring dash-tremor ancilla.

#### Scenario: No effect clears a room for free

- **WHEN** the trap catalog is enumerated
- **THEN** no selectable effect damages enemies without a corresponding cost to the player

### Requirement: Native settings window category controls

The native settings window SHALL expose the trap categories as controls beneath the existing trap
frequency control, enabled only when frequency is non-off. Leaving all category controls unchecked
SHALL be treated as the zero-sentinel (all categories), so the UI never produces a "traps on, zero
categories" state. Changing a category control SHALL mark settings dirty so the seed regenerates.

#### Scenario: All-unchecked means all categories

- **WHEN** the user enables traps and leaves every category checkbox unchecked
- **THEN** the serialized `trap_categories` is zero and the generated seed includes effects from
  all categories

### Requirement: Trap catalog changes are version-locked

Adding or re-selecting trap effects SHALL bump `kGeneratorVersion`. The regression corpus SHALL
regenerate with only the traps-on entries' `placement_digest` moving; every traps-off entry SHALL
remain byte-identical.

#### Scenario: Only traps-on corpus entries move

- **WHEN** the corpus is regenerated at the new generator version after this change
- **THEN** exactly the traps-on entries change their `placement_digest`, and all other entries
  (and all `sphere_digest` values) are byte-identical
