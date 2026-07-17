# randomizer-hints Specification

## Purpose
TBD - created by archiving change add-rando-hints. Update Purpose after archive.
## Requirements
### Requirement: Hint generation pipeline

The randomizer SHALL generate a per-slot hint set deterministically from the placement table. `Rando_GenerateHints` SHALL run at slot creation; results SHALL be stored in a static in-memory table (`g_hint_table`) consulted by the runtime and emitted into the spoiler JSON `hints` array.

Hint generation SHALL be deterministic: the same `(settings, seed)` SHALL produce the same hint set on every platform and run. The sub-RNG SHALL be seeded from the placement-table SHA-256 digest XOR a fixed magic constant, so the hint set tracks the placement digest. (The `spheres` parameter is accepted for forward compatibility but is currently unused.)

> **As-built note**: the original draft referenced sphere data and a `placement_digest_hex` contract; the implementation seeds from the placement-table digest directly and ignores spheres. The full ALTTPR `HintService` 6-step pool algorithm and per-location flavor text are deferred — the shipped generator picks 15 non-junk placements without replacement and formats them as `"<item> is in <location>"` (`rando_hints.c`, `"%s is in %s"`).

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
2. **Fork-extension NPCs** — three vanilla NPCs whose dialogue is rerouted through the hint path for additional in-game hint surfaces: the **Storyteller** (`fork_storyteller`, id 17) and the **Kakariko** + **Dark-World Fortune Tellers** (`fork_fortune_teller_kakariko` id 18 / `fork_fortune_teller_dark_world` id 19). Each draws the next pick from the same shuffled pool after the 15 tiles, so a fork hint never duplicates a tile hint. The **Lake-Hylia Fortune Teller** (`fork_fortune_teller_lake_hylia`, id 20) is intentionally NOT populated — it shares the Kakariko room with no runtime discriminator, so in-game it surfaces the Kakariko hint.
3. **Murahdahla** — emitted only on `goal ∈ {triforce-hunt, ganon-hunt}`, as a static region-summary line (count of pieces and count of regions holding them). Murahdahla is **spoiler-only**; no in-game NPC handler is wired (the fork never ported the ALTTPR Murahdahla sprite).

The hint-text format SHALL be the stable form `"<item> is in <location>"` (Murahdahla: `"Murahdahla: N Triforce piece(s) placed across M region(s)."`).

> **As-built note**: a prior draft of this requirement listed "Fork extensions" as reserved-but-unwired enum ids. They were subsequently wired end-to-end (generation + in-game dispatch + spoiler) for the Storyteller and the two distinguishable Fortune Tellers (ids 17-19); the Lake-Hylia FT (20) is a deliberate shared-room non-wiring. Bookshelf hints were dropped (poor discoverability). ALTTPR line-by-line text fidelity (per-location flavor from `Text.php`) and the joke-pool fallback are deferred; only the structural port shipped.

#### Scenario: Triforce Hunt populates Murahdahla
- **WHEN** a seed has `settings.goal ∈ {triforce-hunt, ganon-hunt}` and `settings.hints == on`
- **THEN** the spoiler `hints` array contains an entry for `RandoHintNpc_Murahdahla` whose text is a region summary of where Triforce-piece placements live

#### Scenario: Non-hunt goals omit Murahdahla
- **WHEN** a seed has `settings.goal ∉ {triforce-hunt, ganon-hunt}` and `settings.hints == on`
- **THEN** the spoiler `hints` array does NOT contain a `RandoHintNpc_Murahdahla` entry

#### Scenario: Fork-extension NPCs surface their hints in-game
- **WHEN** a seed is generated with `settings.hints == on` and the player talks to the Storyteller or reads the Kakariko / Dark-World Fortune Teller
- **THEN** the NPC's vanilla dialogue is replaced with that fork NPC's generated `"<item> is in <location>"` hint (the Lake-Hylia Fortune Teller surfaces the Kakariko hint by shared-room fallback)

### Requirement: Vanilla dialogue hint redirects

A reviewed subset of vanilla dialogue that makes a concrete item/location claim
SHALL be replaced with placement-correct text. The implemented runtime messages
are Sahasrahla's Green Pendant direction (`0x33`), post-Agahnim Moon Pearl
telepathy (`0x36`), the old mountain man's Moon Pearl advice (`0x9E`), the Bumper
Cave sign (`0xA8`), Stumpy's Flute prompt (`0xE5`), Aginah's Book advice
(`0x125`), and the Dark-World bully's Moon Pearl advice (`0x15D`).

A redirect SHALL apply only when a randomizer slot is active, recovered settings
exist with `hints == on`, the dialogue buffer uses the supported US grammar/font,
the row discriminator matches, and the referenced item or location exists in
the currently installed placement table. Item-to-location duplicate copies SHALL
choose the lowest numeric location ID. Failed gates SHALL preserve vanilla text.

Noninteractive rows SHALL use the one-box pre-decode renderer. Stumpy SHALL use a
hint-owned post-decode rewrite with one item-location page followed by a Yes/No
page ending in the original `0x68` Choose command. No redirect SHALL consume or
alter generated hint slots, hint RNG, spoilers, telepathic-tile assignments, or
fork-NPC assignments.

#### Scenario: Aginah redirects to randomized Book location
- **WHEN** runtime `0x125` is shown with hints on and `ITEM_BookOfMudora` is at Sick Kid
- **THEN** the rendered text names Book and Sick Kid instead of the Library

#### Scenario: Moon Pearl advice follows its active placement
- **WHEN** runtime `0x36`, `0x9E`, or `0x15D` is shown under the redirect gates
- **THEN** all three surfaces name the lowest-location active Moon Pearl copy

#### Scenario: Green Pendant direction follows prize placement
- **WHEN** runtime `0x33` is shown and the Green Pendant is outside Eastern Palace
- **THEN** Sahasrahla names its active placement

#### Scenario: Bumper Cave sign resolves location to item
- **WHEN** `0xA8` is read outdoors on screen `0x4A`
- **THEN** it names the item at `LOC_Bumper_Cave`, not an unrelated Piece of Heart

#### Scenario: Bumper Cave context fails closed
- **WHEN** `0xA8` is requested indoors or on another overworld screen
- **THEN** the discriminator fails and vanilla decoding is preserved

#### Scenario: Stumpy preserves the interactive quest flow
- **WHEN** `0xE5` is shown and the Flute is outside Haunted Grove
- **THEN** the first page names the Flute's placement and the second page retains
  Yes/No plus the Choose command that advances to the separate `LOC_Stumpy` reward

#### Scenario: Inapplicable redirects preserve vanilla dialogue
- **WHEN** no slot is active, settings are unavailable, hints are off, placement
  is unavailable, the target item or location is absent, the locale is unsupported,
  or a discriminator mismatches
- **THEN** the original dialogue buffer remains unchanged without crashing

#### Scenario: Existing generated hints remain independent
- **WHEN** a vanilla-dialogue redirect is rendered
- **THEN** the 15 telepathic tiles and fork-NPC mappings, pool cursor, RNG,
  spoiler output, and selected hint text remain unchanged

#### Scenario: Dynamic story hints remain readable
- **WHEN** cutscene fast-forward is enabled and a story message such as `0x36`
  actively resolves as a redirect
- **THEN** story fast-forward does not auto-advance it

#### Scenario: F12 reports redirect resolution
- **WHEN** F12 is pressed on a recognized surface
- **THEN** the dump reports vanilla-dialogue redirect, source, surface kind,
  target item, resolved location, or the exact skip reason

#### Scenario: Vanilla mode preserves byte-identical dialogue
- **WHEN** no randomizer slot is active
- **THEN** all reviewed dialogue, including Stumpy's choice flow, remains vanilla
