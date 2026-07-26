## MODIFIED Requirements

### Requirement: Dispatcher signature and fall-back behavior

The placement layer SHALL expose internal placement-resolution and grant-plan
functions used by approved grant transactions. Gameplay grant sites SHALL call
an animated, quiet, or deferred transaction rather than own the pairing of raw
dispatch, sentinel interpretation, and delivery. When a location is absent from
the active placement table, the transaction SHALL preserve its supplied vanilla
grant behavior. When a location is present, the substituted item SHALL be
delivered by its explicit grant plan and SHALL never silently fall back to the
location's vanilla item.

The location checked bit SHALL commit only after immediate delivery succeeds or
at the delivery point of a prepared deferred transaction. Merely resolving a
placement SHALL have no checked-state side effect.

#### Scenario: Known location grants substitute

- **WHEN** an approved grant transaction resolves a known location to Hookshot
- **THEN** Hookshot is delivered through the normal receive or lossless fallback
  path regardless of the supplied vanilla item, and the location becomes
  checked only after delivery succeeds

#### Scenario: Unknown location falls back to vanilla

- **WHEN** an approved grant transaction receives a location absent from the
  active placement table
- **THEN** it preserves the caller's vanilla grant behavior and does not invent
  a randomizer checked location

#### Scenario: Deferred resolution is not collection

- **WHEN** a tablet or falling prize resolves an item but its delivery visual is
  not yet committed
- **THEN** the location remains unchecked unless a lossless immediate fallback
  has already delivered the item

### Requirement: Single dispatch point per grant site

Every randomizer item-grant site enumerated by the checked-in audit SHALL route
through exactly one approved animated, quiet, or deferred grant transaction.
Raw `Rando_OnLocationCheck` / `Rando_DispatchVanillaGrant` pairing SHALL be
internal to the grant core and tests. Non-grant state writes SHALL retain an
explicit audit exemption, and the randomizer-inactive path SHALL preserve
vanilla behavior.

#### Scenario: Vanilla path bit-identical when rando inactive

- **WHEN** `kFeatures1_RandomizerActive` is clear and a grant handler runs,
  including when receipt allocation is unavailable
- **THEN** its RAM and caller-state behavior matches the pre-change vanilla path

#### Scenario: Audit deliverable is checked in before implementation

- **WHEN** a change adds or converts a randomized grant site
- **THEN** the tracked grant-site enumeration (the grant-consumer guard's
  per-file/function allowlist plus the audit-exemption annotations) is updated
  in the same change, and the source guards fail the build when it is not

#### Scenario: New grant site without dispatch fails the build

- **WHEN** gameplay code calls a raw randomizer resolver/dispatcher or writes a
  tracked inventory cell outside the transaction layer without an approved
  audited exemption
- **THEN** the source guards fail with the file and line

### Requirement: Item types receivable via dispatcher

Every non-virtual item declared grantable by the authoritative item registry
SHALL resolve through generated semantic opcode + payload metadata. The runtime
SHALL implement the declared progressive, absolute equipment, magic, bottle,
heart, dungeon item, rupee/filler, prize, trap, soul, and explicit no-op classes
without hand-maintained fallback to a location's vanilla item.

Recognized items may produce an accepted no-op when their effect is already at
its cap; this is a terminal successful delivery. A resource-capacity condition
that the player can change, such as no empty bottle slot, is retryable and SHALL
not commit the check. Invalid virtual items are generation/runtime errors.

#### Scenario: Progressive grant advances by one level

- **WHEN** a progressive item is below its effective cap
- **THEN** delivery advances exactly one semantic tier and the frozen display
  plan shows that granted tier

#### Scenario: Progressive grant at max is accepted no-op

- **WHEN** a progressive item is delivered at its effective maximum tier
- **THEN** no unrelated junk or vanilla item is granted, the valid placement is
  accepted, and the location commits exactly once

#### Scenario: Full bottle inventory is retryable

- **WHEN** a bottle-content item is offered with no empty bottle slot
- **THEN** delivery is not accepted, no irreversible caller state commits, and
  the location remains available after the player empties a bottle

#### Scenario: Dungeon, prize, and direct classes use placed identity

- **WHEN** a dungeon item, prize, trap, soul, magic upgrade, Rupoor, or Nothing
  is placed away from its vanilla source
- **THEN** the semantic opcode applies the placed item's destination/effect (or
  intentional no-op for Nothing) and never the current dungeon or source's
  vanilla item

#### Scenario: Small-key receive
- **WHEN** the dispatcher grants a small key from a chest in a dungeon other than that key's vanilla home
- **THEN** the small-key counter for the destination dungeon increments, a small-key receive animation plays (new code path; vanilla had no such animation), and the chest-cleared flag is set

#### Scenario: Multi-tier rupee receive — small tiers silent
- **WHEN** the dispatcher grants `Rupee1`, `Rupee5`, `Rupee20`, or `Rupee100` into any chest
- **THEN** Link's rupee counter increments by the documented value, the standard rupee-pickup SFX plays, and no item-receive cutscene fires

#### Scenario: Rupee300 plays the item-receive cutscene (ALTTPR convention)
- **WHEN** the dispatcher grants `Rupee300` (purple rupee — the canonical Misery Mire gift)
- **THEN** Link's rupee counter increments by 300, the full item-receive cutscene plays mirroring ALTTPR's behavior (player gets the "you found a purple rupee" beat), and the chest-cleared flag is set

#### Scenario: Progressive sword grant advances by one level
- **WHEN** the dispatcher grants `ProgressiveSword` while `link_item_sword == L2_MasterSword`
- **THEN** `link_item_sword` advances to L3, the appropriate receive animation plays, and HUD updates accordingly

#### Scenario: Progressive sword grant at max is a no-op or junk-fill
- **WHEN** the dispatcher grants `ProgressiveSword` while `link_item_sword == L4_GoldenSword`
- **THEN** the dispatcher records a generator error in the audit log (this case SHALL NOT occur for valid placements: the item pool contains exactly the right number of progressive items for the active mode); a debug-build asserts; release behavior is to silently grant a `Rupee5` placeholder so gameplay does not soft-lock

#### Scenario: TriforcePiece grants increment counter and HUD
- **WHEN** the dispatcher grants `TriforcePiece` and the player has `triforce_pieces < pieces_required`
- **THEN** the triforce-piece counter increments, the HUD updates, the goal predicate is re-evaluated, and the standard receive animation/SFX plays

#### Scenario: HalfMagic / QuarterMagic grant is strictly progressive
- **WHEN** the dispatcher grants a magic upgrade (`HalfMagic` OR `QuarterMagic`) while `link_magic_consumption == 0` (full)
- **THEN** `link_magic_consumption` advances to 1 (half) — the 1st magic upgrade collected is always half, regardless of which of the two items it is
- **AND WHEN** a second magic upgrade (`HalfMagic` OR `QuarterMagic`) is granted while `link_magic_consumption == 1`
- **THEN** it advances to 2 (quarter); any further magic-upgrade grant is capped at 2 (never exceeds quarter, never downgrades)

#### Scenario: PieceOfHeart vs BossHeartContainer routing
- **WHEN** the dispatcher grants `PieceOfHeart`
- **THEN** the vanilla PoH path runs (4-quarter mechanic; +1 max HP every 4 pieces)
- **AND WHEN** the dispatcher grants `BossHeartContainer`
- **THEN** `+1 max HP` is applied directly without consuming any piece-of-heart quarters

#### Scenario: Boss kill dispatches TWO locations
- **WHEN** the player defeats a dungeon boss (e.g., Helmasaur King in Palace of Darkness)
- **THEN** the boss-death code path calls `Rando_OnLocationCheck(PalaceOfDarkness_BossHeart, BossHeartContainer)` AND separately `Rando_OnLocationCheck(PalaceOfDarkness_Prize, Prize_Crystal_PoD_Vanilla)`, granting whatever the placement table has at each location ID

#### Scenario: Boss-heart slots are shuffled locations
- **WHEN** Phase A generates a seed
- **THEN** each of the 10 `<Dungeon>_BossHeart` slots is a normal shuffled drop location. The dispatcher still fires uniformly via the existing code path; if the placed item is `BossHeartContainer`, the boss kill behaves like vanilla, otherwise it grants the placed item.

#### Scenario: Great-fairy ponds grant two reach-only checks on contact (chest model)
- **WHEN** the player contacts a great-fairy pond — the Pyramid Fairy in the Dark World or the Waterfall of Wishing in the Light World — AND `kFeatures1_RandomizerActive` is set
- **THEN** the pond grants the next un-collected of its TWO checks DIRECTLY on contact, with no "throw an item in" interaction, no item-picker, and no consume/upgrade: Waterfall `waterfall_fairy_left` / `waterfall_fairy_right`, Pyramid `pyramid_fairy_left` / `pyramid_fairy_right`. This mirrors ALTTPR, which replaces both ponds with two chests. A receivable item runs the standard over-head receive; a direct-write item fires the §7.6 confirmation cue. Each pond's two checks are **reach-only** (no sword/bow/throwable-item requirement), so the runtime requirement equals the placement logic ("reach the pond")
- **AND** the Pyramid's vanilla "throw your sword/bow in to upgrade" Trade slots (`pyramid_fairy_sword`, `pyramid_fairy_bow`) are NOT used under rando and are RETIRED from the placement pool; the Waterfall has no such Trade slots
- **AND WHEN** `kFeatures1_RandomizerActive` is clear
- **THEN** the vanilla throw-in upgrade shrine runs unchanged and no `Rando_OnLocationCheck` fires

### Requirement: Enemy-drop staged grant and suppression

Active enemy-drop checks SHALL use the approved quiet grant transaction rather
than the vanilla current-dungeon key increment. The transaction SHALL check for
an already-terminal source, perform presence-aware placement resolution, accept
and deliver the placed item, then commit the checked bit. Only after that commit
may the runtime suppress future forced-key behavior. An identity placement still
uses the transaction and SHALL NOT also run the vanilla increment.

Customizer and trap eligibility SHALL retain their existing delivery-safety
rules. No enemy-drop source may mark checked merely as pickup intent before the
quiet transaction accepts delivery.

#### Scenario: Identity placement does not double grant

- **WHEN** an active enemy-drop location is identity-placed with its own small
  key
- **THEN** the quiet transaction grants and commits the key once and bypasses
  the vanilla case-12 current-dungeon increment

#### Scenario: Failed enemy delivery remains retryable

- **WHEN** the placed item cannot currently be accepted
- **THEN** the source remains unchecked and its forced pickup behavior is not
  permanently suppressed

#### Scenario: Trap eligibility is delivery-safe

- **WHEN** trap shuffle considers an enemy-drop location
- **THEN** only trap classes deliverable through the production quiet
  transaction are eligible
