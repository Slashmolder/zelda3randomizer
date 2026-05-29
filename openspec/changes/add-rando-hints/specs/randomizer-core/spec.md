## ADDED Requirements

### Requirement: Hints settings axis

The settings struct SHALL include a binary `hints` axis (`uint8`, `off | on`) occupying canonical-serialization byte 22. The CSV parser SHALL accept `off | 0 | false | none` as off and `on | 1 | true | sahasrahla | full` as on; `sahasrahla` and `full` are accepted aliases for `on` and do NOT select a distinct mode. The axis SHALL participate in the settings hash. The default value SHALL be `on` unconditionally (not goal-dependent).

- `off`: no hint generation; spoiler omits the `hints` section; telepathic tiles play vanilla text.
- `on`: telepathic-tile hints are generated and surfaced in-game; the spoiler emits the `hints` array.

> **As-built note**: an earlier draft specified a tri-state `off | sahasrahla | full` axis with a goal-aware default (`full` for Triforce/Ganon Hunt, `sahasrahla` otherwise). The implementation collapsed this to binary on/off (`sahasrahla`/`full` are CSV aliases) and made the default unconditionally `on`. A true tri-state is deferred. Murahdahla still emits only on Triforce/Ganon Hunt, but that is a generation-time goal check, not a settings default.

#### Scenario: hints axis participates in settings_hash
- **WHEN** the same seed is generated with `hints=off` and then with `hints=on`
- **THEN** the resulting `settings_hash` values differ

#### Scenario: hints default is on
- **WHEN** a seed is generated without an explicit `hints=` override
- **THEN** the resolved setting is `hints=on`; telepathic-tile hint generation runs

#### Scenario: tri-state aliases collapse to on
- **WHEN** a seed is generated with `hints=sahasrahla` or `hints=full`
- **THEN** the resolved setting is `hints=on` (the alias selects no distinct mode)

### Requirement: Hints spoiler section

The JSON spoiler SHALL include a top-level `hints` array, populated only when `settings.hints == on`. Each entry SHALL be an object with the fields:
- `npc` (string): the ALTTPR-compatible string id (e.g. `telepathic_tile_eastern_palace`, `murahdahla`); fork-extension ids are prefixed `fork_`.
- `dialogue_id` (integer): the spoiler-label dialogue id `kRandoHintDialogueBase (0x200) + (npc_index - 1)`. This is a label only — it is NOT a runtime dialogue-table key.
- `text` (string): the rendered hint text.

The text spoiler SHALL mirror the JSON content under a `Hints:` heading with one line per entry, and SHALL be omitted entirely when no hint entry is populated. The settings block of the JSON spoiler SHALL additionally carry `"hints": <0|1>`. There is NO `meta.hints_count` field.

> **As-built note**: an earlier draft specified `source`/`kind` fields and a `goal-progress` Murahdahla shape with one entry per piece location. The implementation emits `{npc, dialogue_id, text}` and a single Murahdahla *region-summary* entry (region count, not per-piece/per-sphere). Full per-location flavor text and joke filler are deferred.

#### Scenario: Hints array deterministic across runs
- **WHEN** the same `(settings, seed)` is generated twice
- **THEN** the spoiler's `hints` array contents are byte-identical between runs (same `npc`, `dialogue_id`, `text` values in the same order)

#### Scenario: Triforce Hunt surfaces a Murahdahla summary
- **WHEN** a seed with `goal ∈ {triforce-hunt, ganon-hunt}` is generated with `hints=on`
- **THEN** the `hints` array contains a single entry with `npc=murahdahla` whose `text` summarizes how many Triforce pieces are placed across how many regions

#### Scenario: hints off omits the section
- **WHEN** a seed is generated with `hints=off`
- **THEN** the JSON `hints` array is empty and the text spoiler `Hints:` section is omitted
