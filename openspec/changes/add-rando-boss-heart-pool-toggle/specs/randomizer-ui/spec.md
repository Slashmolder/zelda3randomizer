## ADDED Requirements

### Requirement: Boss-heart-container shuffle toggle

The settings UI SHALL expose the `region_boss_hearts_in_pool` axis as a
player-toggleable control, and SHALL NOT display the raw field name or value to
the player, because the field value is inverted relative to its name (`1` = boss
hearts pinned to their boss slots / NOT in the general pool; `0` = boss hearts
shuffled into the general pool).

The control SHALL be labeled **"Shuffle boss heart containers"** and map the
inversion at the UI layer:

- **checked** ⇒ `region_boss_hearts_in_pool = 0` (the 10 boss heart containers
  join the general item pool; arbitrary items may land at boss kills).
- **unchecked** ⇒ `region_boss_hearts_in_pool = 1` (each dungeon boss grants its
  own heart container — the default).

The control SHALL initialize unchecked when the field is at its default (`1`) and
toggling it SHALL refresh the live settings hash like any other seed-defining axis.
The field SHALL NOT be renamed and the canonical byte / CSV keys SHALL keep their
existing meaning for headless and share-string compatibility.

#### Scenario: Toggle off (default) pins boss hearts

- **WHEN** the "Shuffle boss heart containers" control is unchecked (the default)
- **THEN** `region_boss_hearts_in_pool` is `1` and every dungeon boss kill grants
  that dungeon's boss heart container

#### Scenario: Toggle on shuffles boss hearts into the pool

- **WHEN** the player checks "Shuffle boss heart containers"
- **THEN** `region_boss_hearts_in_pool` is set to `0`, the live settings hash
  refreshes, and generation may place non-heart items at the boss slots while the
  10 boss heart containers are placed elsewhere

#### Scenario: Raw inverted field name is never shown

- **WHEN** the settings UI is displayed
- **THEN** no control or text shows the literal "region boss hearts in pool"
  field name or its raw 0/1 value; only the "Shuffle boss heart containers"
  label is shown
