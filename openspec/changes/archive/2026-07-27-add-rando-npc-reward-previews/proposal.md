# Proposal: Add randomizer NPC reward previews

## Why

Randomized NPC rewards are mechanically correct, but several person-mediated
sources still ask the player to pay, trade, or commit before identifying the
seed item. Shopsanity icons help when they fit, yet they are not a complete text
contract, and free/trade NPCs have no common disclosure setting.

Players need one seed option that makes these people tell the truth before the
player accepts or irreversibly chooses their randomized reward. This is distinct
from generated clues: a player may want all telepathic and paid clues Off while
still knowing what a merchant, minigame host, or Take-Any clerk is offering.

## Dependency

This change is a focused follow-up to `add-configurable-hint-options`, on top of
`enhance-rando-hints-v2`. Its setting retains the historical additive schema
floor introduced at generator 157, while the rebased integrated current output
is generator 160. It SHALL be reconciled and archived only after those
dependencies.

## What Changes

- Add independent boolean seed setting `hint_npc_reward_reveal`, default Off,
  packed in canonical byte `[25]` bit 7.
- Keep the clue profile, semantic hint plan, plan digest, discovery journal,
  telepathic tiles, paid clue queues, placement, and sphere results independent
  of the option.
- Reveal exact randomized rewards on the owned person-mediated sources:
  - paid Bottle Merchant, King Zora, Blacksmith, Chest Game, and Digging Game;
  - Sahasrahla, Potion Shop trade, Magic Bat, Hobo, Old Man, Catfish, Purple
    Chest, Stumpy, and the next Fairy gift;
  - shopsanity clerk summaries of live unchecked items and prices; and
  - Take-Any clerk summaries of live irreversible choices.
- Preserve existing exact post-acceptance reward rewrites unconditionally.
- Implement rich reward dialogue only for the Original/US grammar and disclose
  that limitation in both settings UIs.
- Add current-schema JSON/text spoiler reporting and preserve generator-156
  race-reveal output exactly by omitting the field and rejecting a crossed
  legacy artifact with the new bit set.

## Capabilities

### Modified Capabilities

- `randomizer-core`: independent setting, canonical packing, CLI/share identity,
  generator-156 compatibility, and lifecycle behavior.
- `randomizer-hints`: exact owned-source preview behavior, text safety,
  transaction boundaries, and independence from semantic clues.
- `randomizer-native-window`: pending and active preview controls/status.
- `randomizer-ui`: in-game preview control and Original/US disclosure.
- `randomizer-validation`: source, renderer, transaction, spoiler, legacy,
  lifecycle, corpus, locale, and gameplay gates.

## Impact

- **Versioning:** current output is generator 160. The option retains the
  historical additive decoding floor at generator 157; generator-156 artifacts
  never defined the bit.
- **Settings:** canonical length remains 31 and share-string v2 remains 76
  characters. The false default preserves historical bytes; true changes
  settings/share identity.
- **Placement:** none. The option is presentation-only and any placement or
  sphere digest movement is a defect.
- **Persistence:** the integrated lifecycle uses sidecar v14 with an exact
  278-byte extension and snapshot hint TLV type 11. This option adds no further
  growth because the active canonical settings recover it.
- **Runtime:** dialogue presentation changes only. Existing grant, payment,
  checked-state, retry, and replay ownership remains at each source.

## Non-goals

- No fifth clue profile, new semantic hint fact, journal row, discovery bit,
  paid-clue queue, plan-digest input, or F12 race redaction.
- No reward preview for chests, loose items, enemy drops, pots, terrain checks,
  bosses, or other non-person-mediated sources.
- No new confirmation choice where a shop/free/trade source does not already
  have one.
- No German/French rich item-name localization in this change.
- No separate generator bump beyond the integrated current version 160,
  canonical/share growth, placement rebaseline, archive, merge, push, or claim
  of completed gameplay without owner sign-off.
