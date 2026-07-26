## ADDED Requirements

### Requirement: Hints tab owns pending generation policy

The native Randomizer window SHALL place Hints in its normal tab order
immediately after General. The Hints tab SHALL expose two explicitly labeled
inner surfaces:

- Next seed setup, which reads/writes only bridge-owned pending settings; and
- Active-slot journal, which reads only active slot plan, discovery, checked
  state, and persisted race identity.

Next seed setup SHALL provide atomic Off/Sparse/Balanced/Direct profile actions,
tile coverage, paid depth, hint mix, the derived profile label, and an exact
maximum-delivery summary. It SHALL state that paid clues are always exact and
that paid depth zero offers no clue but retains the disclosed priced healing
service.

General SHALL no longer own an editable Balanced-hints checkbox. It MAY show a
read-only derived summary and navigation affordance, but every native policy
mutation SHALL use the Hints setup surface and the authoritative settings API.

The active journal SHALL retain the Hints v2 discovered-only default,
resolved-state derivation, failed-plan unavailable state, non-race confirmed
full viewer, and race discovered-only restriction. Pending hint/race edits
SHALL NOT alter active journal visibility, policy, facts, or disclosure.
F12 diagnostics SHALL remain separately spoiler-bearing and unrestricted.

#### Scenario: Hints setup edits only pending settings
- **WHEN** the player changes an enabled active slot's pending policy to Sparse
- **THEN** the next-seed tuple/hash/share update while the active plan,
  discovery, paid queues, and journal remain unchanged

#### Scenario: Derived profile updates from components
- **WHEN** the player starts at Balanced and selects ten tiles
- **THEN** the setup surface displays Custom and its exact capacity summary
  without inventing a serialized Custom value

#### Scenario: Named profile writes a complete tuple
- **WHEN** Direct is selected
- **THEN** enabled, 15 tiles, paid depth 3, and Important mix are written
  atomically through the shared settings API

#### Scenario: Paid-zero disclosure appears before generation
- **WHEN** pending paid depth is zero
- **THEN** the setup surface says no paid clue is delivered and accepting those
  services still charges the shown price for healing

#### Scenario: General has no duplicate editor
- **WHEN** the Randomizer General tab is rendered
- **THEN** it contains no independently editable hint checkbox/control capable
  of diverging from the Hints tab

#### Scenario: Active journal is policy-owned by its slot
- **WHEN** an active race slot exists while pending hints/race settings are
  edited
- **THEN** the journal remains bound to the active slot and exposes only its
  discovered rows

#### Scenario: Non-race full view remains deliberate
- **WHEN** an active non-race configurable slot is inspected
- **THEN** undiscovered facts remain hidden until the existing separately
  confirmed read-only full-view action is used
