# Design: randomizer NPC reward previews

## Context

The randomizer already rewrites several reward messages after acceptance so
they name the granted placement rather than a vanilla Bottle, Flippers, or
other fixed reward. Configurable Hints adds generation-time clue profiles and a
versioned semantic plan. Neither surface answers the separate pre-commit
question: “What will this person give me if I pay, trade, or choose now?”

This change owns that disclosure without making transaction presentation part
of the semantic clue system.

## Goals and non-goals

Goals:

- make every explicitly owned person-mediated randomized reward truthful before
  payment, acceptance, trade completion, or irreversible choice;
- preserve the source's existing charge/grant/check/retry choreography;
- make Hints Off plus previews On a first-class configuration;
- resolve only against the active placement and active source context;
- format complete exact names without clipping or corrupting choice commands;
- preserve generator-156 race reveal byte-for-byte; and
- expose the Original/US limitation before seed generation.

Non-goals are the exclusions in `proposal.md`, especially new confirmation
state machines, clue-plan integration, mutable preview persistence, and partial
localization.

## Decisions

### D1. Independent zero-default seed axis

`RandoSettings.hint_npc_reward_reveal` is a strict boolean:

| Canonical byte | Bit | `0` | `1` |
|---|---:|---|---|
| `[25]` | 7 | previews Off | previews On |

The field is intentionally outside `Settings_HintsEnabled`,
`Settings_NormalizeHintPolicy`, `Settings_GetHintProfile`,
`Settings_ApplyHintProfile`, and the semantic plan builder/digest. Therefore:

- default, legacy Balanced, and legacy Off canonical bytes remain unchanged;
- `hints=off,hint_npc_reward_reveal=true` is valid;
- applying any named clue profile preserves the preview value;
- true changes settings hash and share identity;
- true does not change placement, spheres, semantic plan, assignments, rows,
  or plan digest; and
- race mode does not suppress an explicitly selected preview.

The normal CLI boolean grammar and duplicate-key rules apply to the exact key
`hint_npc_reward_reveal`.

Current generated output is generator 160. This field retains the historical
additive decoding floor at generator 157 because it joined the same canonical
schema as configurable hints. A decoder at or above that floor defines `[25]`
bit 7. The generator-156 reveal compatibility shim instead treats that bit as
required zero and returns `SettingsCorrupt` for a CRC-correct crossed artifact.

### D2. Exact owned source inventory

Scope is source-based. A message ID proven source-exclusive by the call-site
audit is itself the exact runtime discriminator; an ID shared by multiple
sources additionally requires its actor, room, entrance, shop, or Take-Any
context. A hook SHALL never use an unqualified shared ID.

| Class | Owned source | Runtime discriminator | Preview contract |
|---|---|---|---|
| paid | Bottle Merchant | source-exclusive `0x0D1` | exact item and 100-rupee price before Yes |
| paid | King Zora | source-exclusive `0x142`/`0x143` flow | exact item once in the first offer, then 500-rupee price confirmation before acceptance |
| paid | Blacksmith | source-exclusive `0x0D8` before the `0x0D9` price prompt | exact item and 10-rupee price before agreement |
| paid | Chest Game | source-exclusive `0x160` | exact randomized prize and 30-rupee entry price before play |
| paid | Digging Game | source-exclusive `0x187` | exact randomized prize and 80-rupee entry price before play |
| free/trade | Sahasrahla | source-exclusive `0x038`/`0x039` | exact randomized reward before the grant flow completes |
| free/trade | Potion Shop trade | source-exclusive `0x04B`/`0x04C` plus the pre-hand-in sprite latch | exact randomized trade reward before hand-in completes |
| free/trade | Magic Bat | source-exclusive `0x110` | exact randomized reward before the grant completes |
| free/trade | Hobo | source-exclusive `0x0D7` | exact randomized gift before grant |
| free/trade | Old Man | source-exclusive `0x09C` | exact randomized escort reward before grant |
| free/trade | Catfish | source-exclusive `0x12A` | exact randomized reward before grant |
| free/trade | Purple Chest | source-exclusive `0x109` | exact randomized return reward before grant |
| free/trade | Stumpy | source-exclusive `0x0E5` | exact randomized reward before accepting/continuing its flow |
| free/trade | next Fairy gift | source-exclusive `0x14A` plus active world/checked state | exact next randomized Fairy reward before it is committed |
| shop | shopsanity clerk | shared `0x15F`/`0x165` plus live shop room/entrance inventory | every currently unchecked slot's exact item and current price |
| choice | Take-Any clerk | shared `0x15F`/`0x165` plus snapshot-owned source entrance, host room, and live choice inventory | every currently live take-once choice before the player selects |

The free/trade rows disclose before the existing grant boundary but SHALL NOT
invent a new decline branch where vanilla has none. Non-person-mediated checks,
paid clue services, capacity upgrades, the repeatable Retro generic key, and
vanilla-only minigames remain outside this setting.

### D3. Active placement and context are authoritative

Every preview resolves from the currently installed active placement table.
The resolver requires:

1. an active randomizer slot;
2. recovered active settings with the preview boolean true;
3. Original/US dialogue grammar;
4. the exact runtime source discriminator (an audited source-exclusive message
   ID, or the full context required for a shared ID);
5. an active placement row for the owned location; and
6. a valid, fully nameable item.

It SHALL use an explicit placement lookup rather than a vanilla-item fallback.
If any gate fails, the complete existing truthful generic/vanilla buffer remains
unchanged. No result is persisted or reused across slots, restores, rooms, or
messages.

Already-checked one-time sources SHALL NOT advertise their old placement when
their replay behavior is consolation, restock, or no reward. A same-frame
repeat and a save/snapshot/slot transition re-resolve the active checked/source
state rather than using presentation cached for an earlier transaction.

### D4. Exact text is commit-after-success

Reward names include long qualified dungeon items, numbered prizes,
progressives, traps, souls, bottles, Rupoor, and Nothing. A writer SHALL:

- retain the complete useful item identity;
- wrap across pages when one choice-page heading cannot fit;
- preserve the source's explicit price;
- preserve every `WaitKey`, `Scroll`, row-select, `Choose`, and termination
  command needed by the original flow; and
- keep short item/price choices in one coherent box, and roll every row of a
  longer follow-up page through its own `Scroll` so no prior-page pixels remain
  underneath XOR-rendered text; and
- align low-two-row option labels with the vanilla messages 1/2 cursor overlay:
  selected rows use four spaces plus `> `, unselected rows use seven spaces,
  and both labels begin at the same pixel so Up/Down redraws touch only the
  prefix; and
- construct into bounded scratch storage, then replace the live buffer only
  after the complete message validates.

No partial item prefix or partially replaced choice buffer is acceptable.
Invalid item IDs, unsupported glyphs, missing names, overflow, or command-fit
failure preserves the prior complete dialogue.

For a staged offer such as King Zora's, the first prompt names the exact item
once. A later price/acceptance prompt does not repeat that item unless the
active source or placement has changed.

Stumpy's post-decode buffer is already a multi-page interactive stream: a
one-to-three-row Flute fact, then its own wait and rolled-in choice page. Its
reward preview SHALL precede that stream by rolling in the complete fact page
regardless of row count, then preserving the existing wait/choice tail
byte-for-byte.

The existing exact post-acceptance rewrites remain unconditional. The option
gates only the new pre-commit disclosure, so disabling it cannot resurrect a
known false vanilla claim.

### D5. Clerk summaries avoid new purchase state machines

Direct shopsanity items currently purchase on interaction. This change does not
insert a new confirmation state between input and purchase. Instead, the
context-matched shop clerk lists all active unchecked checks in that shop with
their exact seed item and price.

The summary is rebuilt on each talk. It excludes checked slots, which have
already returned to vanilla restock behavior. Shop identity includes every
required room/entrance discriminator, including room-only contexts, so an
adjacent shop cannot display another shop's placements.

The Take-Any clerk similarly lists only the current live irreversible choices.
It does not list inactive caves, already-taken choices, or the repeatable Retro
generic-key column.

### D6. Presentation is transactionally inert

Preparing, loading, or viewing a preview SHALL NOT alter:

- rupees;
- inventory or capacity;
- placement checked bits;
- grant/receipt globals;
- source state;
- hint discovery;
- paid clue queue state; or
- semantic plan identity.

Decline and insufficient funds leave all of those unchanged. Acceptance remains
owned by the original transaction path: exactly one payment, one grant, and one
check on success. A retryable grant failure, including bottle-capacity or
receipt-ancilla saturation, must not charge or check early and must show the
same current preview on retry. Checked or same-frame replay must not duplicate
payment or grant.

### D7. Original/US-only rich dialogue

Rich randomized-item preview text uses the Original/US font and command grammar.
German and French message buffers remain byte-identical rather than receiving
English names or US control bytes. Both native and in-game seed-generation
controls state “Original/US dialogue only.” The canonical setting still
round-trips in every locale and both spoiler formats report it.

### D8. UI ownership

The native Hints tab separates “Clue profile” from the independent “NPC reward
previews” checkbox. The checkbox remains usable while clue profile is Off and
shows the Original/US limitation. Pending next-seed state and active-slot status
are distinct.

The in-game advanced Hints page exposes the same independent row and disclosure.
Named profile cycling/reset preserves the boolean. Cursor bounds, focus,
controller/keyboard navigation, and Switch compile guards include the new row.

### D9. Spoiler, race, and lifecycle compatibility

For the historical additive schema floor (`generator_version >= 157`), including
all current generator-160 output, JSON settings include:

```json
"hint_npc_reward_reveal": true
```

The text header includes:

```text
NPC reward previews: yes (Original/US rich dialogue only)
```

Both values are decoded from canonical settings. The boolean is settings
metadata only: it does not enter `hint_plan`, `hints[]`, plan digest, topology,
discovery, or mutable runtime state.

Generator-156 compatibility output omits both fields to preserve its exact
recorded spoiler stamp. A genuine fixture with zero `[25]` bit 7 still reveals
through frozen algorithm/text 1/1; a CRC-correct mutation setting that bit is
rejected before regeneration writes output or scratch artifacts.

This option adds no sidecar or snapshot format growth. The integrated lifecycle
envelope is sidecar v14 with an exact 278-byte extension and snapshot hint TLV
type 11. Normal activation, save/load, native export, warm/cold snapshot replay,
share paste, and slot switching recover the setting from the canonical settings
already owned by those lifecycles.

### D10. Validation and release policy

Automated validation pairs the same seed with previews Off and On. Settings
hash/share/canonical bit SHALL differ; placement digest, sphere digest, clue
plan, plan digest, and hint rows SHALL match. The full corpus is not rebaselined
for presentation-only drift.

Exhaustive shared-formatter coverage walks every registered item ID through the
reward-page and inventory composition paths. Representative source-specific
templates and transaction/context paths cover paid, one-time, shop, and
Take-Any behavior, while source audits and guards retain ownership of the full
roster. This deliberately avoids claiming an item-by-source Cartesian test
matrix. Owner gameplay remains a separate closeout gate after automated and
cross-platform validation.
