## ADDED Requirements

### Requirement: Boss-heart-container pool semantics and logic safety

The placer SHALL interpret the `region_boss_hearts_in_pool` settings axis with
its (inverted-relative-to-name) value semantics: a **non-zero** value (the
default `1`) pins each of the 10 `<Dungeon> - Boss` Drop locations
(`type == Drop`, `vanilla_item == BossHeartContainer`/`51`) to `BossHeartContainer`
so those slots are identity-placed; a value of **`0`** leaves those slots as free
assumed-fill targets. The 10 `BossHeartContainer` items SHALL be added to the item
pool regardless of the axis value, so when the slots are pinned the containers end
up at their boss slots, and when unpinned they participate in the general fill.

The pin SHALL be keyed strictly on `type == Drop` together with the boss
heart-container vanilla item, so the Sanctuary heart container (`type == Chest`,
also a `BossHeartContainer`) is NEVER pinned by this rule.

To keep assumed-fill sound when the boss slots are unpinned, every `<Dungeon> -
Boss` Drop location's `can_reach` predicate SHALL require defeating that dungeon's
boss — i.e. include the dungeon's `CanKill<Boss>()` macro plus the items needed to
reach and open the boss room — in both the Standard and Inverted logic graphs.
No boss Drop location's `can_reach` may be `TRUE()` or otherwise omit the
boss-kill requirement.

#### Scenario: Pinned (default) boss-heart slots are identity-placed

- **WHEN** `region_boss_hearts_in_pool` is non-zero (the default `1`)
- **THEN** each of the 10 `<Dungeon> - Boss` Drop slots is hardcoded to
  `BossHeartContainer`; the dispatcher still fires uniformly at every boss kill,
  granting that dungeon's heart container

#### Scenario: Unpinned boss-heart slots join the assumed-fill pool

- **WHEN** `region_boss_hearts_in_pool` is `0`
- **THEN** the 10 boss Drop slots are free placement targets, non-heart items may
  be placed there, and the 10 `BossHeartContainer` items are placed elsewhere by
  assumed fill

#### Scenario: Sanctuary heart container is not pinned by the boss rule

- **WHEN** the placer applies the boss-heart pin
- **THEN** the Sanctuary location (a `BossHeartContainer` of `type == Chest`) is
  excluded, because the pin keys on `type == Drop`

#### Scenario: Boss Drop reachability requires the boss kill

- **WHEN** the assumed-fill placer evaluates reachability for any `<Dungeon> -
  Boss` Drop location with the slots unpinned
- **THEN** that location is reachable only when the inventory satisfies the
  dungeon's boss-kill predicate (the `CanKill<Boss>()` macro plus reach/open-room
  items), so progression is never stranded behind an unbeatable boss
