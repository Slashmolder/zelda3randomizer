## ADDED Requirements

### Requirement: Retro generic small-key pool

When `settings.world_state == Retro`, ALTTPR's `rom.genericKeys` SHALL be in
effect: small keys form a single shared pool and any small key opens any locked
door. At pool construction, every per-dungeon small-key item (`SmallKey_<Dungeon>`)
SHALL be substituted with the fungible `GenericKey` item (registry id 125, ROM
0xAF), matching ALTTPR `app/Location.php` (`Item\Key` → `KeyGK` under
`rom.genericKeys`). The generic keys SHALL be placed in the general/wild pool
(the `wildKeys` placement from `add-rando-retro-world-state` already routes small
keys there); per-dungeon `SmallKey_<Dungeon>` items SHALL NOT enter the Retro
pool. `genericKeys` is computed from `world_state == Retro` (no new settings
bytes; `kSettingsCanonicalLen` unchanged).

This change supersedes the deferral recorded in `add-rando-retro-world-state`'s
"Retro world-state config-flag pinning" requirement (which pinned `rupeeBow` /
`takeAnys` / `wildKeys` and explicitly left `genericKeys` to this follow-up).

> **Stub status**: the placement substitution is straightforward, but it is
> coupled to the "Generic small-key door reachability" requirement
> (`randomizer-logic`) and a shared-counter runtime; all three land together and
> are gated on an end-to-end playtest. Apply-time design in `design.md`.

#### Scenario: Small keys become a single fungible pool
- **WHEN** a Retro seed is generated
- **THEN** the placement pool contains `GenericKey` items (count = the sum of the
  per-dungeon small-key counts) and no `SmallKey_<Dungeon>` items; the seed is
  `goal_completable` with no unreachable placements

#### Scenario: Non-Retro key placement unchanged
- **WHEN** a non-Retro seed is generated (Open / Standard / Inverted)
- **THEN** small keys retain per-dungeon identity per the seed's
  `dungeon_small_keys_mode`; `placement_digest` is byte-identical to the
  pre-change baseline

#### Scenario: Determinism bump scoped to Retro
- **WHEN** the corpus is regenerated after this change
- **THEN** only Retro entries' `placement_digest` / `sphere_digest` move; every
  non-Retro entry is byte-identical, and `kGeneratorVersion` advances
