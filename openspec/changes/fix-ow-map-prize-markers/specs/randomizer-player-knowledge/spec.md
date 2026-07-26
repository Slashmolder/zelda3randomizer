# randomizer-player-knowledge Specification (delta)

## ADDED Requirements

### Requirement: In-world surfaces are knowledge-gated too

An in-world (diegetic) surface that displays a per-seed assignment — a pause-map
marker, a HUD glyph, an in-world sprite or plaque — SHALL be held to the same
invariant as the tracker surfaces: it may only assert what is true under EVERY
assignment consistent with the player's in-game observations. Where the surface
cannot make a knowledge-consistent assertion it SHALL degrade to an explicitly
unknown presentation (the vanilla "?"/blank marker form where one exists),
never to the vanilla assumption, and never to the true assignment.

Observation for a dungeon prize SHALL mean that dungeon's prize location is
checked. Ownership of the prize TYPE, ownership of the dungeon's map or compass,
and having entered or cleared the dungeon SHALL NOT count as observation of its
prize. Introducing any other disclosure path (for example an upstream-style
compass-reveals-prize axis) is a settings-level decision outside this
requirement and SHALL NOT be introduced as part of a correctness fix.

A surface whose position or presence is a vanilla fact independent of the seed
(a dungeon's vanilla overworld coordinates, the existence of a prize at a prize
dungeon) MAY be shown unconditionally; a surface whose position would encode a
shuffled assignment (a marker relocated to a shuffled entrance) SHALL NOT.

#### Scenario: Unobserved shuffled prize shows the unknown form

- **WHEN** a `prize_shuffle` slot is active and the player has not collected a
  given dungeon's prize
- **THEN** that dungeon's pause-map marker draws the unknown ("?") form, and no
  in-world surface states which prize the dungeon holds

#### Scenario: Identity roll is still hidden

- **WHEN** `prize_shuffle` is enabled and the roll happens to reproduce the
  vanilla assignment, and the player has not collected the prize
- **THEN** the marker still shows the unknown form (the player cannot know the
  roll was the identity)

#### Scenario: Unrecoverable settings fail closed

- **WHEN** the active slot's settings cannot be recovered (snapshot replay or a
  pre-settings slot) and a per-seed assignment surface must render
- **THEN** the surface treats the assignment as shuffled and renders the unknown
  form rather than the vanilla assumption

## MODIFIED Requirements

### Requirement: Knowledge-guard CI check

A CI check script SHALL fail the build when a source file outside an explicit
allowlist (generation, gameplay delivery, spoiler writer, selftests, and the
audited display sites) consumes the full-knowledge reachability API or the raw
assignment getters (prize, medallion, boss, entrance-connection, door-link
accessors, or placement item ids). An intentional exception SHALL require a
`knowledge-guard: allow <reason>` annotation at the call site.

Because the guard is file-granular, a new audited display site SHALL be placed
in a purpose-scoped source file rather than allowlisting a large general-purpose
file, so the allowlist entry cannot silently authorize unrelated future reads in
the same file.

#### Scenario: New unaudited consumer fails CI

- **WHEN** a new source file reads a raw assignment getter without the
  allowlist entry or annotation
- **THEN** the check script exits nonzero and CI fails

#### Scenario: Display site is scoped to its own file

- **WHEN** an in-world display surface needs a raw assignment getter
- **THEN** the read lives in a purpose-scoped module that the allowlist names,
  not in the vanilla engine file that draws the surface
