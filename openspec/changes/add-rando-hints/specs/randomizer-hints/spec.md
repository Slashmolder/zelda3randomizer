## ADDED Requirements

### Requirement: Hint generation pipeline

The randomizer SHALL generate a per-slot hint set deterministically from the placement table. `Rando_GenerateHints` SHALL run at slot creation; results SHALL be stored in a static in-memory table (`g_hint_table`) consulted by the runtime and emitted into the spoiler JSON `hints` array.

Hint generation SHALL be deterministic: the same `(settings, seed)` SHALL produce the same hint set on every platform and run. The sub-RNG SHALL be seeded from the placement-table SHA-256 digest XOR a fixed magic constant, so the hint set tracks the placement digest. (The `spheres` parameter is accepted for forward compatibility but is currently unused.)

> **As-built note**: the original draft referenced sphere data and a `placement_digest_hex` contract; the implementation seeds from the placement-table digest directly and ignores spheres. The full ALTTPR `HintService` 6-step pool algorithm and per-location flavor text are deferred — the shipped generator picks 15 non-junk placements without replacement and formats them as `"The <item> lies at <location>."`.

#### Scenario: Same seed produces same hints
- **WHEN** the same `(settings, seed)` is generated twice
- **THEN** the resulting hint set is byte-identical (asserted by `Hints_SelfCheck`)

#### Scenario: Hints section present in spoiler
- **WHEN** a seed is generated with `settings.hints == on`
- **THEN** the JSON spoiler contains a top-level `hints` array; the text spoiler has a `Hints:` heading followed by per-NPC entries

#### Scenario: Hints off skips generation
- **WHEN** a seed is generated with `settings.hints == off`
- **THEN** `Rando_GenerateHints` populates no entries; the JSON `hints` array is empty; the text spoiler `Hints:` section is omitted

### Requirement: Hint source NPCs

The randomizer SHALL populate the following hint sources:

1. **15 telepathic tiles** (ALTTPR-canonical) — per `app/Services/HintService.php:59-75`, the upstream ships exactly 15 telepathic-tile sources. The list is positionally stable: Eastern Palace, Tower of Hera Floor 4, Spectacle Rock, Swamp Entrance, Thieves Town Upstairs, Misery Mire, Palace of Darkness, Desert Bonk Torch Room, Castle Tower, Ice Large Room, Turtle Rock, Ice Entrance, Ice Stalfos Knights Room, Tower of Hera Entrance, South-East Darkworld Cave. Each is assigned one distinct non-junk placement and surfaced in-game (per the placement-capability "Telepathic-tile hint dispatch" requirement).
2. **Murahdahla** — emitted only on `goal ∈ {triforce-hunt, ganon-hunt}`, as a static region-summary line (count of pieces and count of regions holding them). Murahdahla is **spoiler-only**; no in-game NPC handler is wired.

The hint-text format SHALL be the stable form `"The <item> lies at <location>."` (Murahdahla: `"Murahdahla: N Triforce piece(s) placed across M region(s)."`).

> **As-built note**: a prior draft of this requirement listed a third group, "Fork extensions" (Storyteller + 3 Fortune Tellers), as supported. These exist only as reserved enum ids (`kRandoHintNpc_Fork*`, prefixed `fork_` in spoiler keys) and are **NOT wired in-game** — no sprite handler routes through them. Bookshelf hints were dropped (poor discoverability). ALTTPR-line-by-line text fidelity (per-location flavor from `Text.php`) and the joke-pool fallback are deferred; only the structural port shipped.

#### Scenario: Triforce Hunt populates Murahdahla
- **WHEN** a seed has `settings.goal ∈ {triforce-hunt, ganon-hunt}` and `settings.hints == on`
- **THEN** the spoiler `hints` array contains an entry for `RandoHintNpc_Murahdahla` whose text is a region summary of where Triforce-piece placements live

#### Scenario: Non-hunt goals omit Murahdahla
- **WHEN** a seed has `settings.goal ∉ {triforce-hunt, ganon-hunt}` and `settings.hints == on`
- **THEN** the spoiler `hints` array does NOT contain a `RandoHintNpc_Murahdahla` entry

#### Scenario: Fork-extension NPCs are not surfaced in-game
- **WHEN** any seed is generated with `settings.hints == on`
- **THEN** no `fork_storyteller` / `fork_fortune_teller_*` entry is populated and no in-game NPC dispatches a fork-extension hint

### Requirement: Vanilla NPC hint redirects (DEFERRED — not implemented)

A subset of vanilla NPC dialogue spoils the vanilla *location* of a specific named item (e.g. Aginah, dialogue ID 294, points at the Book of Mudora's vanilla Library slot). Under randomization the referenced item is shuffled, so the vanilla line becomes misleading. The randomizer SHALL eventually, when `kFeatures1_RandomizerActive` is set and `settings.hints == on`, replace the location-referencing portion of each such NPC's dialogue with a hint that names the *randomized* `LOC_*` of the referenced item.

> **Deferred status (2026-05-29)**: NOT implemented. The shipped runtime intercept (`Rando_RenderHintMessage` / `Rando_IsHintTileMessage`) acts on the 15 telepathic-tile message ids ONLY (`0xB5,0xB8..0xC7`); it does NOT touch dialogue ID 294 or any other vanilla NPC line. No Aginah redirect exists in code (verified: `rando_hints.c` mentions Aginah only in an example comment). This requirement is retained as a forward-looking design target; the scenarios below describe the intended (unbuilt) behavior. The vanilla-NPC audit of `assets/dialogue.txt` (dwarven smiths, Library NPCs, desert hint NPC, etc.) remains open.

#### Scenario: Aginah redirects to randomized Book location (deferred)
- **WHEN** the redirect is implemented, a seed is generated with `kFeatures1_RandomizerActive` set and `settings.hints == on`
- **THEN** dialogue ID 294 (Aginah) is served from the hint path; the rendered text references the randomized `LOC_*` of `ITEM_BookOfMudora` instead of the vanilla Library

#### Scenario: Vanilla mode preserves byte-identical dialogue
- **WHEN** `kFeatures1_RandomizerActive` is clear
- **THEN** dialogue dispatch for all NPCs is byte-identical to upstream zelda3; no hint interception occurs (this holds today — only tele-tile ids are ever intercepted, and only when a slot is active)
