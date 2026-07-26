# randomizer-logic Specification (delta)

## ADDED Requirements

### Requirement: Reachability compute accepts a knowledge-exclusion input

`Logic_ComputeReachability` (or a thin wrapper over it) SHALL accept an
optional knowledge-exclusion input naming hidden dungeon ids (matched against
the generated `RandoRegionDef.dungeon_id` binding), undiscovered shuffled cave
interiors (suppressing their locations via the static interior→location lists,
and their decoupled cave-exit added edges), and suppressed overlay edges
(undiscovered shuffled warps). With the input absent the compute SHALL be
byte-for-byte the existing full-knowledge flood. With it present, an excluded
region SHALL never enter the reachable set (blocking onward propagation), a
suppressed location SHALL never be reported reachable, and a suppressed edge
SHALL never fire. Only the live tracker bridge SHALL pass the
input; every generation, placer, accessibility, and selftest path SHALL call
with it absent, and the input SHALL NOT be process-global state that can leak
across calls (the resolved-tower-crystals reinstall precedent).

#### Scenario: Masked flood is a subset

- **WHEN** the same counts and settings are flooded with and without a
  knowledge-exclusion input
- **THEN** the masked result's reachable regions and locations are a subset of
  the full result, and every location in an excluded dungeon is absent from the
  masked result

#### Scenario: Placer flood unaffected after a masked live flood

- **WHEN** a masked live flood runs and a generation flood follows in the same
  process
- **THEN** the generation flood's reachable set is identical to a process that
  never ran a masked flood

### Requirement: Live reachability bridge is knowledge-limited

`Rando_GetLiveReachability()` SHALL return the knowledge-limited view: it
derives the hidden-identity set from the active slot's installed layout tables
and the persisted discovery state, folds a discovery generation counter into
its memo key, and recomputes when a discovery lands. The full-knowledge compute
SHALL be reachable only through an explicitly named internal API reserved for
generation and selftests. The NULL fail-closed contract is unchanged: unknown
settings still return NULL and surfaces still suppress reachability entirely.

#### Scenario: Discovery invalidates the memo

- **WHEN** the memoized live view exists and a new discovery bit is set
- **THEN** the next `Rando_GetLiveReachability()` call recomputes and includes
  the newly discovered content

#### Scenario: Empty mask short-circuits

- **WHEN** the active slot enables no topology axis
- **THEN** the bridge passes no exclusion input and the result is identical to
  the pre-change behavior
