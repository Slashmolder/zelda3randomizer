## ADDED Requirements

### Requirement: Hint generation pipeline

The randomizer SHALL generate a per-slot hint set deterministically from the placement table + sphere data + RNG state. Hint generation SHALL run once at slot creation; results SHALL be cached in the slot's spoiler JSON under a new `hints` section and consumed by the runtime via in-memory lookup tables.

Hint generation SHALL be deterministic: the same `(share_string, generator_version)` SHALL produce the same hint set on every platform and on every run. The hint subsystem participates in the `placement_digest_hex` determinism contract.

> **Stub status**: full hint-text generation algorithm + per-source-NPC contracts deferred to apply-time.

#### Scenario: Same seed produces same hints
- **WHEN** the same `(share_string, generator_version)` is generated twice
- **THEN** the resulting `hints` arrays are byte-identical (compare via SHA-256 of canonical JSON)

#### Scenario: Hints section present in spoiler
- **WHEN** a seed is generated with `settings.hints != off`
- **THEN** the JSON spoiler contains a top-level `hints` array; the text spoiler has a `Hints` section heading followed by per-source-NPC entries

#### Scenario: Hints off skips generation
- **WHEN** a seed is generated with `settings.hints == off`
- **THEN** the hint subsystem is not run; the JSON `hints` array is empty; the text spoiler `Hints` section is omitted

### Requirement: Hint source NPCs

The randomizer SHALL support hint generation for four ALTTPR-canonical hint sources:

1. **Sahasrahla telepathic tiles** — light-world early-game psychic-talk tiles (one per region in vanilla; this change retains the vanilla count).
2. **Storyteller** — hint NPCs that appear in shrines.
3. **Bookshelves** — book-of-mudora-driven hints (interaction with bookshelf sprites in dungeons).
4. **Murahdahla** — dark-world Triforce-Hunt hint NPC.

Each source's hint-text format SHALL be derived from `../alttp_vt_randomizer/app/Services/HintService.php` (177 lines) and `../alttp_vt_randomizer/app/Text.php` (1110 lines) with per-line citations recorded in `audit.md §"Hint provenance"`.

For Triforce Hunt seeds, Murahdahla SHALL surface Triforce-piece locations. Exact hint count and per-sphere grouping deferred to apply-time PHP translation.

> **Stub status**: per-source-NPC hint-count and text-format details deferred.

#### Scenario: Triforce Hunt forces Murahdahla active
- **WHEN** a seed has `settings.goal == triforce-hunt` and `settings.hints != off`
- **THEN** the Murahdahla source is active; the spoiler `hints.murahdahla` array is populated with at least one entry per Triforce-piece location

#### Scenario: Non-Triforce-Hunt may omit Murahdahla
- **WHEN** a seed has `settings.goal != triforce-hunt` and `settings.hints != off`
- **THEN** Murahdahla generation MAY produce an empty entry (no Triforce pieces to hint at); behavior is documented in design.md
