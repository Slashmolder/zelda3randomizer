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

The randomizer SHALL support hint generation for the ALTTPR-canonical hint sources plus fork-specific extensions:

1. **15 telepathic tiles** (ALTTPR-canonical) — per `app/Services/HintService.php:59-75`, the upstream ships exactly 15 telepathic-tile sources (NOT one per region — there are duplicates at Ice Palace and Tower of Hera). The list is positionally stable: Eastern Palace, Tower of Hera Floor 4, Spectacle Rock, Swamp Entrance, Thieves Town Upstairs, Misery Mire, Palace of Darkness, Desert Bonk Torch Room, Castle Tower, Ice Large Room, Turtle Rock, Ice Entrance, Ice Stalfos Knights Room, Tower of Hera Entrance, South-East Darkworld Cave.
2. **Murahdahla** — static per-goal text line from `app/Randomizer.php:1029`. Emitted on `goal ∈ {triforce-hunt, ganon-hunt}` as a region summary of where Triforce pieces are placed.
3. **Fork extensions** (NOT in ALTTPR upstream) — Storyteller (Sprite_28_DarkWorldHintNPC) + 3 Fortune Tellers (Kakariko, Dark World, Lake Hylia). Spoiler-JSON keys are prefixed `fork_` so ALTTPR-diff-ability is preserved for the core 16 sources.

Earlier drafts of this requirement listed Sahasrahla, Storyteller, Bookshelves, and Murahdahla as the four ALTTPR-canonical sources; that was inaccurate per `design.md §57.1` translation review. ALTTPR has no Sahasrahla hint source distinct from the telepathic tile near Sahasrahla; Bookshelf hints were considered for this fork and dropped (poor discoverability + thematic dilution).

Each source's hint-text format SHALL be derived from `../alttp_vt_randomizer/app/Services/HintService.php` (177 lines) and `../alttp_vt_randomizer/app/Text.php` (1110 lines) with per-line citations recorded in `audit.md §"Hint provenance"`.

> **Stub status**: ALTTPR-line-by-line text-format fidelity deferred. The simplified `"<item> is at <location>"` format is the apply-time baseline; richer per-location flavor text follows in a follow-up commit alongside the runtime-message-engine intercept (#85).

#### Scenario: Triforce Hunt forces Murahdahla active
- **WHEN** a seed has `settings.goal ∈ {triforce-hunt, ganon-hunt}` and `settings.hints != off`
- **THEN** the Murahdahla source is active; the spoiler `hints` array contains an entry for `RandoHintNpc_Murahdahla` whose text is a region summary of where Triforce-piece placements live

#### Scenario: Non-hunt goals omit Murahdahla
- **WHEN** a seed has `settings.goal ∉ {triforce-hunt, ganon-hunt}` and `settings.hints != off`
- **THEN** the spoiler `hints` array does NOT contain a `RandoHintNpc_Murahdahla` entry (the goal has no Triforce pieces to hint at)

### Requirement: Vanilla NPC hint redirects

A subset of vanilla NPC dialogue spoils the vanilla *location* of a specific named item. Under randomization the referenced item is shuffled, so the vanilla line becomes misleading. The randomizer SHALL, when `kFeatures1_RandomizerActive` is set and `settings.hints != off`, replace the location-referencing portion of each such NPC's dialogue with a hint that names the *randomized* `LOC_*` of the referenced item, routed through the same dynamic-dialogue-ID path used for telepathic tiles (per Requirement "Hint generation pipeline").

**Anchor case (verified 2026-05-27)** — Aginah in Aginah's Cave (`Sprite_Aginah` handler at `src/sprite_main.c:6661`; dialogue-seen flag at `src/sprite_main.c:6528`). Dialogue ID **294** in `assets/dialogue.txt` reads in part: *"It should be in the house of books in the village[...] You must get it!"* — a location spoiler for the Book of Mudora at its vanilla Library slot. Under randomization this text SHALL be regenerated to point at the slot's actual `LOC_*` for `ITEM_BookOfMudora`.

> **Stub status**: a deeper audit of `assets/dialogue.txt` for OTHER vanilla NPCs that name a specific item plus its location is REQUIRED before apply-time. Each match becomes a candidate redirect entry. Audit owner: apply-time. Candidate sweep targets (to be confirmed/discarded during the audit, NOT load-bearing): dwarven smiths (sword tempering), Library NPCs (Book interaction flavor), the desert hint NPC, and any dialogue that string-matches an item name from `assets/rando/item_registry.yaml`. See `tasks.md §1.5`.

#### Scenario: Aginah redirects to randomized Book location
- **WHEN** a seed is generated with `kFeatures1_RandomizerActive` set and `settings.hints != off`
- **THEN** dialogue ID 294 (Aginah) is served from the dynamic-hint table; the rendered text references the randomized `LOC_*` of `ITEM_BookOfMudora` instead of the vanilla Library
- **AND** the spoiler `hints` array contains an entry whose `source` identifies the Aginah redirect

#### Scenario: Vanilla redirects gated by hints axis
- **WHEN** a seed has `settings.hints == off`
- **THEN** vanilla-NPC redirects do NOT activate; Aginah and other vanilla hint NPCs play their original (now-misleading) dialogue — opt-in spoiler-free run

#### Scenario: Vanilla mode preserves byte-identical dialogue
- **WHEN** `kFeatures1_RandomizerActive` is clear
- **THEN** dialogue dispatch for vanilla hint NPCs is byte-identical to upstream zelda3; no dynamic-table lookup occurs
