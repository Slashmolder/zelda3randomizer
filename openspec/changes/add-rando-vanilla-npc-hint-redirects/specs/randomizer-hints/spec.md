## MODIFIED Requirements

### Requirement: Vanilla NPC hint redirects

A reviewed subset of vanilla NPC dialogue that names the vanilla location of a
specific fixed registry item SHALL be replaced with a concise hint naming that
item's active randomized location. The implemented runtime messages are
Sahasrahla's Green Pendant direction (`0x33`), post-Agahnim Moon Pearl telepathy
(`0x36`), the old mountain man's Moon Pearl advice (`0x9E`), Aginah's Book advice
(`0x125`), and the Dark-World bully's Moon Pearl advice (`0x15D`).

The redirect SHALL apply only when a randomizer slot is active, recovered active
settings exist with `hints == on`, the current dialogue buffer uses the supported
US grammar/font, the redirect's runtime discriminator matches, and the referenced
item appears in `Placement_GetActive()`. It SHALL resolve against the currently
installed placement table on every use. If duplicate copies exist, the lowest
numeric location ID SHALL win deterministically. If any gate fails, the renderer
SHALL return false and preserve vanilla decoding without crashing.

Redirects SHALL use the existing pre-decode hint encoder, fit completely within
its three safe rows, and SHALL NOT consume or alter generated hint slots, hint RNG,
spoilers, telepathic-tile assignments, or fork-NPC assignments. An actively
resolved redirect SHALL remain readable under story-dialogue fast-forward.

Interactive choice messages, progressive-tier-ambiguous Master Sword prose,
location-only flavor that names no fixed item, the already-intercepted telepathic
tiles, and Fortune Teller reading ranges are outside this fixed-item redirect
table.

#### Scenario: Aginah redirects to randomized Book location
- **WHEN** runtime message `0x125` is shown in an active US randomizer slot with
  recovered settings, `hints == on`, and `ITEM_BookOfMudora` placed at Sick Kid
- **THEN** the rendered text names the Book and Sick Kid instead of the Library

#### Scenario: Library contents do not drive Book resolution
- **WHEN** `ITEM_BottleEmpty` is placed at the Library and the Book is placed at a
  different location
- **THEN** `0x125` names the Book's placement, not the item placed at the Library

#### Scenario: Moon Pearl advice uses one fixed-item resolver
- **WHEN** `ITEM_MoonPearl` is present in the active placement and runtime message
  `0x36`, `0x9E`, or `0x15D` is shown under the redirect gates
- **THEN** each surface names the same resolved Moon Pearl location

#### Scenario: Green Pendant direction follows prize placement
- **WHEN** Sahasrahla shows runtime message `0x33` and
  `ITEM_Prize_GreenPendant` is placed outside Eastern Palace
- **THEN** the redirect names the Green Pendant's active placement

#### Scenario: Duplicate target items are deterministic
- **WHEN** a customizer placement contains multiple copies of a redirected item
- **THEN** the redirect names the copy at the lowest numeric location ID,
  independent of placement-table iteration order

#### Scenario: Inapplicable redirects preserve vanilla dialogue
- **WHEN** no slot is active, active settings are unavailable, hints are off, the
  active placement is unavailable, the target item is absent, the locale is
  unsupported, or a required discriminator does not match
- **THEN** the hint renderer returns false and the original dialogue decodes
  unchanged

#### Scenario: Adjacent and already-owned hint IDs are not intercepted
- **WHEN** an adjacent dialogue ID, a telepathic-tile ID, or a Fortune Teller
  reading ID is shown
- **THEN** the fixed-item redirect table does not claim it; existing generated
  hint interception continues unchanged where applicable

#### Scenario: Dynamic post-Agahnim hint remains readable
- **WHEN** cutscene fast-forward is enabled and `0x36` actively resolves as a
  dynamic Moon Pearl hint
- **THEN** story-dialogue fast-forward does not auto-advance that message

#### Scenario: F12 identifies redirect resolution
- **WHEN** F12 is pressed while a recognized redirect message is current
- **THEN** `dump_hints.txt` identifies it as a vanilla-NPC redirect and reports
  source name, target item, resolved location when active, or the exact skip reason

#### Scenario: Vanilla and hints-off dialogue are preserved
- **WHEN** a vanilla slot is active or a randomizer slot has `hints == off`
- **THEN** vanilla NPC dialogue bytes are not replaced and no generated hint slot
  is consumed
