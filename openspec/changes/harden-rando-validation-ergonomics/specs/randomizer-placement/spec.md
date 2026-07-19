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

#### Scenario: New raw grant site fails the build

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
