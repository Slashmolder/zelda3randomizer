## ADDED Requirements

### Requirement: Hints settings axis

The settings struct SHALL include a `hints` enum axis with values `off | sahasrahla | full`. The axis SHALL be appended to the canonical-serialization order at a stable position (deferred to apply-time after a survey of the existing Phase A settings struct layout). Default policy:

- `off`: no hint generation; spoiler omits `hints` section; hint NPCs play vanilla text.
- `sahasrahla`: Sahasrahla telepathic + bookshelf hints active; storyteller + Murahdahla disabled.
- `full`: all four hint sources active.

The default value SHALL be `full` when `settings.goal == triforce-hunt | ganonhunt`; `sahasrahla` otherwise. The default policy is recorded in design.md.

> **Stub status**: exact default + serialization-offset detail deferred.

#### Scenario: hints axis participates in settings_hash
- **WHEN** the same seed is generated with `hints=off` and then with `hints=full`
- **THEN** the resulting `settings_hash` values differ

#### Scenario: Triforce Hunt default is full
- **WHEN** a Triforce Hunt seed is generated without explicit `hints=` override
- **THEN** the resolved setting is `hints=full`; Murahdahla generation runs

### Requirement: Hints spoiler section

The JSON spoiler SHALL include a top-level `hints` array (when `settings.hints != off`). Each entry SHALL be an object with at least:
- `source` (string): one of `sahasrahla_<region>`, `storyteller_<shrine>`, `bookshelf_<dungeon_room>`, `murahdahla_<index>`.
- `text` (string): the rendered hint text as it appears in-game.
- `kind` (string enum): `location-spoil` (hint reveals where an item is) | `item-spoil` (hint reveals what's at a location) | `goal-progress` (Murahdahla per-sphere) | `joke` (filler text).

The text spoiler SHALL mirror the JSON content under a `Hints` section heading with one line per entry.

> **Stub status**: exact field set + per-source-text-format conventions deferred.

#### Scenario: Hints array deterministic across runs
- **WHEN** the same `(share_string, generator_version)` is generated twice
- **THEN** the spoiler's `hints` array contents are byte-identical between runs (same `source`, `text`, `kind` values in the same order)

#### Scenario: Triforce Hunt hints surface piece locations
- **WHEN** a Triforce Hunt seed is generated with `hints=full`
- **THEN** the `hints` array contains at least one entry per Triforce-piece location with `kind=goal-progress` and `source=murahdahla_<N>`
