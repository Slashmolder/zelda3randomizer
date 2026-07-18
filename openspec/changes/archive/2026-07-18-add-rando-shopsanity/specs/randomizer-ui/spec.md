# randomizer-ui — delta for add-rando-shopsanity

## ADDED Requirements

### Requirement: Shopsanity toggle and shop-check presentation

The native settings window SHALL expose a "Shopsanity" checkbox in the Shuffles block, defaulting off, mapped to the `shopsanity` axis with the CSV-matching semantics, with a tooltip limited to 1-2 durable player-facing facts (shop slots become one-time checks at seed-random prices; a purchased slot permanently restocks its normal item at its normal price). The checkbox is not disable-coupled to any other axis. The 27 shop slot locations SHALL present as ordinary rows in the native check tracker, reach panel, and map tracker (no gating toggle — unlike pots/terrain, 27 rows need no visibility gate) and as ordinary entries in the SNES HUD location grid, counted in all completion denominators; auto-tracker rows keep their existing `Shop` location-type tag so external clients can filter them. The spoiler surfaces (JSON `shops[]`, text `Shops:` section, and the in-window spoiler view) SHALL show each shop slot's placed item together with its derived price in every world state where shop-class placements exist, keeping identity-placed Capacity Upgrade rows flagged as today and leaving axis-off non-Retro spoilers byte-identical.

#### Scenario: Toggle round-trips
- **WHEN** the user enables Shopsanity in the native window and generates a slot
- **THEN** the slot's stored canonical settings carry `shopsanity=true`, the share string reproduces it, and re-opening the window shows the checkbox checked for that slot

#### Scenario: Tracker shows shop checks as ordinary rows
- **WHEN** a `shopsanity=true` seed is active
- **THEN** the 27 shop slots appear as normal tracker/reach rows under their regions, count toward global and per-region totals, and mark checked on purchase

#### Scenario: Spoiler view includes prices
- **WHEN** the user reveals or views the spoiler of a `shopsanity=true` seed
- **THEN** every shop slot row shows the placed item and its rupee price, grouped per shop in the text form
