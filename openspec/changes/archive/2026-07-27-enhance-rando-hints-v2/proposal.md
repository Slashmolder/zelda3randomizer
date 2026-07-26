# Proposal: Enhance randomizer hints v2

## Why

The shipped hint generator is deterministic, but it is still a flat table of
preformatted strings selected by shuffling non-junk placements. It does not
distinguish a hint's truth from its delivery source, prioritize information by
player value, preserve every dungeon/location qualifier, or retain discovery
state. The three paid services repeat one fixed clue forever, and the native
Hints panel is either a complete placement spoiler or completely hidden in race
mode.

The flat table is also too weak for the randomizer's lifecycle. Slot activation,
native spoiler export, snapshot replay, and race reveal can rebuild or replace
the module-global table at different times. There is no persisted algorithm or
text-schema identity and no digest proving that a reconstructed deck is the
deck certified at generation. A future hint-algorithm edit could therefore
silently give an existing slot different clues.

Finally, a generated sentence is useful only when the entire item and location
identity fits in the message box. Current friendly-name handling can discard
multi-check suffixes or dungeon identity, and non-US dialogue falls back to
vanilla text that can make seed-invalid placement claims.

## What Changes

- Replace the flat string table with a versioned semantic `HintPlan` composed of
  typed `HintFact`, source assignment, and separately mutable discovery state.
- Keep the public setting binary and serialization-compatible, but name its two
  player-facing modes **Off** and **Balanced**. Existing `on`, `true`,
  `sahasrahla`, and `full` inputs remain aliases for Balanced.
- Build a deterministic Balanced deck targeting up to 18 primary facts: 15
  telepathic-tile assignments plus the head fact of each of three paid queues.
  Add up to six exact useful reserve facts, two per paid queue, for a maximum of
  24 delivery facts. Scarce or unrenderable candidate pools underfill safely
  instead of duplicating, fabricating, or adding misleading filler.
- Give the Storytellers, shared Kakariko/Lake-Hylia Fortune Tellers, and
  Dark-World Fortune Teller independent three-position queues. Each service
  uses a prepare/render/commit transaction, advances only after a successful
  paid interaction, skips already-checked exact targets, and keeps its vanilla
  healing/service behavior after exhaustion.
- Make item/location identity and bounded rendering authoritative. A fact that
  cannot preserve its full semantic identity in a complete message is not
  emitted.
- Add grammar-correct neutral US, German, and French text for recognized
  randomizer hint surfaces when hints are Off or a rich plan is unavailable.
  Full localized rich placement hints remain deferred.
- Persist the hint algorithm version, text-schema version, SHA-256 plan digest,
  and 24 discovery bits in sidecar format v14 and snapshot TLV type 11. Do not
  persist plan text or queue cursors; reconstruct and digest-check the complete
  plan after all accepted shuffle/layout state is installed.
- Turn the native Hints panel into a discovered-hint journal. Outside race mode,
  a separately confirmed "View all seed hints — placement spoilers" action may
  show the full deck without mutating discovery. Race mode never offers that
  action and shows discovered facts only.
- Keep the existing spoiler fields for compatibility while adding typed fact,
  assignment, and plan-identity metadata. Discovery is runtime state and never
  enters the canonical spoiler or race stamp.
- Retain Murahdahla as a separate spoiler-only goal-summary compatibility
  surface. It is not one of the 24 delivery facts and has no discovery bit.
- Keep F12 as an unrestricted developer diagnostic, including in race mode. It
  shall label its output as spoiler-bearing but is not required to redact plan
  facts or queue state.

## Capabilities

### Modified Capabilities

- `randomizer-hints`: semantic plan construction, Balanced composition,
  truthful rendering/fallbacks, discovery, source assignment, paid queues, and
  versioned lifecycle behavior.
- `randomizer-core`: rename binary mode 1 to Balanced without changing its
  canonical value, and extend the backward-compatible hint spoiler schema.
- `randomizer-placement`: correct the authoritative telepathic-tile message-id
  list and route tile/paid presentation through assignments and transactions.
- `randomizer-save`: persist and validate hint-plan identity plus discovery in
  sidecar and snapshot state.
- `randomizer-native-window`: replace the all-or-nothing hint viewer with the
  discovered journal and explicit non-race full-deck viewer.

## Impact

- **Runtime:** `src/rando/rando_hints.{c,h}`, hint activation/rebuild seams in
  `rando.c`, `messaging.c`, paid handlers in `sprite_main.c`, and the native
  Hints panel/bridge.
- **Persistence:** sidecar file format 13 to 14; slot extension 238 to 278 bytes;
  new snapshot TLV type 11.
- **Spoilers:** compatible `npc`, `dialogue_id`, and `text` fields remain;
  deterministic typed metadata and plan identity are additive.
- **Generated data:** item/location hint classification and compact aliases gain
  one authoritative data/codegen path instead of accumulating C-only name
  exceptions.
- **Versioning:** `kGeneratorVersion` and the corpus manifest advance together.
  Hints remain post-placement; no placement or sphere digest is intentionally
  changed.

## Non-goals

- No Guidance/strong-hints mode, cryptic/riddle mode, progressive-strength
  paywall, or dynamic "where should I go next" oracle.
- No claim that an item is required, "Way of the Hero", or a region is foolish
  without a separately proven counterfactual oracle.
- No entrance, door, boss, dungeon-chain, overworld-warp, tracker-marker,
  shop/key/soul-specific, or other shuffle-aware clue packs in this change.
- No Murahdahla sprite, new in-game hint NPC, or full rich-hint localization.
- No F12 race-mode redaction.
- No archive, merge, or claim that gameplay is complete before the focused owner
  playtest is performed.
