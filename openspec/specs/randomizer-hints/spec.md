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
SHALL be replaced with placement-correct text. Item-to-location redirects SHALL
name a fixed referenced item's active randomized location. A physical
location-to-item surface SHALL instead name the item placed at that location.

The implemented runtime messages are Sahasrahla's Green Pendant direction
(`0x33`), post-Agahnim Moon Pearl telepathy (`0x36`), the old mountain man's
Moon Pearl advice (`0x9E`), the Bumper Cave sign (`0xA8`), Stumpy's Flute prompt
(`0xE5`), Aginah's Book advice (`0x125`), and the Dark-World bully's Moon Pearl
advice (`0x15D`).

A redirect SHALL apply only when a randomizer slot is active, recovered active
settings exist with `hints == on`, the current dialogue buffer uses the supported
US grammar/font, the row's runtime discriminator matches, and the referenced
item or location exists in `Placement_GetActive()`. It SHALL resolve against the
currently installed table on every use. Item-to-location duplicates SHALL choose
the lowest numeric location ID deterministically. If any gate fails, the
original dialogue SHALL remain unchanged without crashing.

Noninteractive rows SHALL use the existing pre-decode hint renderer and fit
completely within its three safe rows. Stumpy SHALL use a hint-subsystem-owned
post-decode rewrite containing one item-location page and a second page with the
original Yes/No control command; the ordinary early renderer SHALL refuse this
interactive row. Redirects SHALL NOT consume or alter generated hint slots, hint
RNG, spoilers, telepathic-tile assignments, or fork-NPC assignments. Every
actively resolved redirect SHALL remain readable under story-dialogue
fast-forward.

Progressive-tier-ambiguous Master Sword prose, generic location-only flavor,
other interactive choice messages, the already-intercepted telepathic tiles,
and Fortune Teller reading ranges remain outside this reviewed table.

#### Scenario: Aginah redirects to randomized Book location
- **WHEN** runtime message `0x125` is shown in an active US randomizer slot with
  recovered settings, `hints == on`, and `ITEM_BookOfMudora` placed at Sick Kid
- **THEN** the rendered text names the Book and Sick Kid instead of the Library

#### Scenario: Library contents do not drive Book resolution
- **WHEN** `ITEM_BottleEmpty` is placed at the Library and the Book is placed at a
  different location
- **THEN** `0x125` names the Book's placement, not the item placed at the Library

#### Scenario: Moon Pearl advice follows its active placement
- **WHEN** `ITEM_MoonPearl` is present in the active placement and runtime message
  `0x36`, `0x9E`, or `0x15D` is shown under the redirect gates
- **THEN** each surface names the same resolved Moon Pearl location

#### Scenario: Green Pendant direction follows prize placement
- **WHEN** Sahasrahla shows runtime message `0x33` and
  `ITEM_Prize_GreenPendant` is placed outside Eastern Palace
- **THEN** the redirect names the Green Pendant's active placement

#### Scenario: Bumper Cave sign resolves location to item
- **WHEN** runtime `0xA8` is read outdoors on overworld screen `0x4A` and
  `LOC_Bumper_Cave` contains Hookshot
- **THEN** the sign names Hookshot rather than Piece of Heart while retaining a
  concise Cape/prize framing

#### Scenario: Other Heart Pieces do not drive Bumper Cave
- **WHEN** another location contains `ITEM_PieceOfHeart` and Bumper Cave contains
  a different item
- **THEN** `0xA8` resolves the exact `LOC_Bumper_Cave` row and ignores the other
  Heart Piece placements

#### Scenario: Bumper Cave context fails closed
- **WHEN** `0xA8` is requested indoors or on an overworld screen other than `0x4A`
- **THEN** the redirect reports a discriminator mismatch and preserves vanilla

#### Scenario: Stumpy preserves the interactive quest flow
- **WHEN** runtime `0xE5` is shown and `ITEM_OcarinaInactive` is placed at Desert
  Ledge
- **THEN** the completed buffer names Flute and Desert Ledge, presents Yes and No,
  and ends its choice page with the US `0x68` Choose command

#### Scenario: Stumpy's early renderer cannot remove gameplay control flow
- **WHEN** `0xE5` actively resolves
- **THEN** `Rando_RenderHintMessage()` returns false for that row and only the
  post-decode interactive hint hook replaces the prompt

#### Scenario: Stumpy's randomized reward remains independent
- **WHEN** the player selects Yes on the rewritten `0xE5` prompt
- **THEN** the existing handler advances normally and `0xE6` names and grants the
  item at `LOC_Stumpy` exactly once, independent of the Flute's placement

#### Scenario: Duplicate target items are deterministic
- **WHEN** a customizer placement contains multiple copies of an item-to-location
  redirect target
- **THEN** the redirect names the copy at the lowest numeric location ID,
  independent of placement-table iteration order

#### Scenario: Inapplicable redirects preserve vanilla dialogue
- **WHEN** no slot is active, active settings are unavailable, hints are off, the
  active placement is unavailable, the target item or fixed location is absent,
  the locale is unsupported, or a required discriminator does not match
- **THEN** the matching renderer returns false and the original dialogue buffer
  remains unchanged

#### Scenario: Adjacent and already-owned hint IDs are not intercepted
- **WHEN** an adjacent dialogue ID, a telepathic-tile ID, or a Fortune Teller
  reading ID is shown
- **THEN** the vanilla-dialogue redirect table does not claim it; existing
  generated hint interception continues unchanged where applicable

#### Scenario: Dynamic story hints remain readable
- **WHEN** cutscene fast-forward is enabled and `0x36` actively resolves as a
  dynamic Moon Pearl hint
- **THEN** story-dialogue fast-forward does not auto-advance that message

#### Scenario: F12 reports redirect resolution
- **WHEN** F12 is pressed while a recognized redirect message is current
- **THEN** `dump_hints.txt` identifies it as a vanilla-dialogue redirect and
  reports source, surface kind, target item, resolved location when active, or
  the exact skip reason including `location absent`

#### Scenario: Every Bumper item name remains useful
- **WHEN** any current registry item is placed at Bumper Cave
- **THEN** one complete three-row sign form contains the item name without silent
  truncation

#### Scenario: Vanilla mode preserves byte-identical dialogue
- **WHEN** a vanilla slot is active or a randomizer slot has `hints == off`
- **THEN** vanilla dialogue bytes and Stumpy's original choice flow are preserved,
  and no generated hint slot is consumed

#### Scenario: Existing generated hints remain independent
- **WHEN** a vanilla-dialogue redirect is rendered
- **THEN** the 15 telepathic tiles and fork-NPC mappings, pool cursor, RNG,
  spoiler output, and selected hint text remain unchanged
