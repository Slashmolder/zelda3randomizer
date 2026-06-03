## ADDED Requirements

### Requirement: Accessibility tier acceptance (ALTTPR three-way)

The generator SHALL evaluate seed acceptance against the `accessibility` axis
using a single nested-strictness predicate. **Every** tier SHALL require the
goal-completion predicate (`Goal_IsCompletable`) to hold — i.e. all three tiers
produce a beatable seed. The tiers SHALL additionally require:

- `locations` (`kAccessibility_Locations`, value 1) — every placed location is
  reachable (`unreachable_count == 0`). Strictest. (`kGoal_Completionist`
  already implies this via its own goal predicate, which iterates every
  placement; selecting `locations` with a looser goal makes that bar explicit.)
- `items` (`kAccessibility_Items`, value 0; the default) — every **progression**
  item's location is reachable (per the `is_progression_item` classifier).
  Non-progression items — junk/consumables (rupees, arrows, bombs), maps,
  compasses, and heart pieces/containers — MAY be at unreachable locations.
- `none` (`kAccessibility_None`, value 2; UI label "beatable only") — goal
  completability is the whole bar; items and locations MAY be unreachable.

Strictness SHALL nest: a placement accepted by `locations` is accepted by
`items`, and a placement accepted by `items` is accepted by `none`.

The "ship a literally unwinnable seed" behavior SHALL NOT be reachable from the
`accessibility` axis: `none`/"beatable only" still guarantees beatability. The
CLI `--allow-broken-seed` flag remains the only way to emit a diagnostic
non-completable spoiler.

This predicate SHALL gate seed acceptance on every generation path: the headless
`--generate-seed` path (including its entrance-shuffle per-permutation accept),
and the shared playable-slot path used by both the PC native settings window and
the in-game settings screen (including its entrance-shuffle per-permutation
accept and its non-entrance path).

The spoiler's `goal_completable` field SHALL remain the pure reachability
predicate (`Goal_IsCompletable`), independent of the accessibility tier. The
generator SHALL NOT emit an `accessibility_none_seed` warning; when a tier leaves
locations unreachable by design, the existing `unreachable_placements`
`fallback_warnings` entry SHALL surface that count.

#### Scenario: Beatable-only accepts a stranded non-progression item

- **WHEN** `accessibility = none` and assumed fill produces a completable
  placement in which a junk/heart/map/compass item is at an unreachable location
- **THEN** the generator accepts the seed, writes the spoiler, and (if the
  unreachable count is non-zero) records an `unreachable_placements` warning

#### Scenario: 100% Inventory rejects a stranded progression item

- **WHEN** `accessibility = items` and the placement strands a progression item
  (a weapon/utility/bottle/key/prize/triforce-piece) at an unreachable location
- **THEN** the seed is refused (unless `--allow-broken-seed`), even though the
  goal predicate alone would pass

#### Scenario: 100% Locations requires every location reachable

- **WHEN** `accessibility = locations` and the placement has any unreachable
  location (`unreachable_count > 0`)
- **THEN** the seed is refused (unless `--allow-broken-seed`)

#### Scenario: Every tier still requires beatability

- **WHEN** assumed fill produces a placement whose goal is NOT completable
- **THEN** the seed is refused for all three accessibility tiers (the prior
  `none`-opts-into-unwinnable behavior no longer applies)

#### Scenario: CSV accepts the beatable alias

- **WHEN** settings are parsed from `accessibility=beatable`
- **THEN** the value resolves to `kAccessibility_None` (identical to
  `accessibility=none`)
