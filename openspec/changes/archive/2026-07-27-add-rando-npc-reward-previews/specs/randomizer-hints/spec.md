## ADDED Requirements

### Requirement: Exact pre-commit NPC reward previews

When `hint_npc_reward_reveal` is enabled, an active Original/US randomizer slot
SHALL identify the exact randomized reward before the player pays, accepts,
trades, or irreversibly chooses it at every owned person-mediated source.

The owned paid sources SHALL be Bottle Merchant, King Zora, Blacksmith, Chest
Game, and Digging Game. The owned free/trade sources SHALL be Sahasrahla,
Potion Shop trade, Magic Bat, Hobo, Old Man, Catfish, Purple Chest, Stumpy, and
the next Fairy gift. Shopsanity clerks SHALL summarize their active unchecked
items and prices. Take-Any clerks SHALL summarize their currently live
irreversible choices.

The preview SHALL resolve from the exact active placement and exact runtime
source context on every use. An audited source-exclusive message ID MAY serve
as that context; a message ID shared by multiple sources SHALL additionally
require its actor, room, entrance, shop, or Take-Any discriminator. A
missing/invalid placement, checked source, stale slot, wrong discriminator,
invalid item, unsupported locale, or incomplete text fit SHALL preserve the
complete existing truthful generic/vanilla dialogue. The implementation SHALL
NOT use a vanilla-placement fallback to fabricate a reward.

Existing exact post-acceptance randomized-reward rewrites SHALL remain
unconditional. The setting SHALL NOT consume or discover a semantic clue,
advance a paid clue queue, enter the journal, or affect the clue-plan digest.

#### Scenario: Paid NPC states item and price before choice
- **WHEN** an unchecked owned paid source offers a randomized item with previews
  enabled
- **THEN** its complete pre-choice dialogue names that exact item and its actual
  price before any rupees are charged

#### Scenario: King Zora does not repeat the item
- **WHEN** the player advances from King Zora's exact-item offer to the
  500-rupee Pay/Quit confirmation
- **THEN** the first prompt names the item once and the second prompt shows only
  the price and choices

#### Scenario: Free or trade source identifies its next grant
- **WHEN** an owned free/trade source is about to grant its unchecked
  randomized reward with previews enabled
- **THEN** the dialogue names that exact reward before the existing grant
  boundary without adding a new decline branch

#### Scenario: Stumpy preserves both facts and its choice
- **WHEN** Stumpy's Flute-location fact occupies one, two, or three rows and
  reward previews are enabled
- **THEN** the reward page, complete Flute fact page, and cursor-safe Yes/No
  page all render in order with the original choice command intact

#### Scenario: Shopsanity clerk reports only live checks
- **WHEN** the player talks to a correctly context-matched shopsanity clerk
- **THEN** the clerk lists each unchecked slot's exact randomized item and
  seed-derived price and omits already-checked vanilla restocks

#### Scenario: Take-Any clerk reports live irreversible choices
- **WHEN** the player talks to an active Retro Take-Any clerk before choosing
- **THEN** the clerk lists only that cave's currently available take-once
  choices and omits inactive, already-taken, and repeatable-key entries

#### Scenario: Context mismatch fails closed
- **WHEN** a shared dialogue ID is decoded in the wrong actor, room, entrance,
  shop, or Take-Any context
- **THEN** no placement from another source is named and the prior complete
  dialogue remains unchanged

#### Scenario: Checked replay does not promise an old reward
- **WHEN** a one-time source is already checked and its replay behavior is
  consolation, restock, or no reward
- **THEN** its old randomized placement is not advertised

#### Scenario: Preview setting does not gate post-acceptance truth
- **WHEN** previews are disabled and an existing post-acceptance exact reward
  message is shown
- **THEN** the message still identifies the randomized grant instead of
  restoring a false vanilla item claim

### Requirement: Reward-preview text and transaction safety

NPC reward previews SHALL preserve the complete useful identity of every
eligible item and every price. Text SHALL be composed into bounded scratch
storage and installed only after the full message and all control commands fit.
Multi-page choice flows SHALL retain their `WaitKey`, `Scroll`, row-selection,
`Choose`, and termination commands. Low-two-row choices SHALL use the
message-engine's canonical selected and unselected prefixes so its messages
1/2 cursor overlays never erase or redraw option-label glyphs.

Loading or viewing a preview SHALL be read-only. Decline and insufficient-funds
paths SHALL change no rupees, inventory, checked bits, grant/receipt state,
source state, hint discovery, or paid clue queue. Existing acceptance paths
SHALL retain exactly-once charge/grant/check behavior. Retryable grant failure
SHALL not charge or check early and SHALL re-present the same current reward.

Rich reward dialogue SHALL be Original/US only. German and French message
buffers SHALL remain byte-identical, and both seed-generation UIs SHALL disclose
that limitation.

#### Scenario: Long exact identity uses complete pages
- **WHEN** an owned source contains the longest supported qualified item name
- **THEN** the installed dialogue includes the full unambiguous name and price
  without clipping, partial replacement, retained prior-page pixels, or lost
  choice commands

#### Scenario: Moving the choice cursor preserves both labels
- **WHEN** the player presses Up or Down on a generated paid or acceptance
  choice
- **THEN** the vanilla cursor overlay changes only the canonical row prefix and
  both option labels remain complete and visually unchanged

#### Scenario: Render failure preserves prior dialogue
- **WHEN** an item name/glyph is invalid or the complete message cannot fit
- **THEN** no part of the live dialogue buffer is replaced

#### Scenario: Viewing and declining are inert
- **WHEN** a player opens a preview and declines
- **THEN** rupees, inventory, checked state, source state, grant state, and all
  semantic hint state remain unchanged

#### Scenario: Retryable grant failure does not commit
- **WHEN** acceptance reaches a retryable bottle-capacity or receipt-allocation
  failure
- **THEN** the source remains unchecked, no charge/grant is duplicated, and the
  same active reward is previewed on retry

#### Scenario: Non-US locale remains intact
- **WHEN** German or French dialogue reaches an owned reward source
- **THEN** its message buffer remains byte-identical and no Original/US item
  name or control command is injected
