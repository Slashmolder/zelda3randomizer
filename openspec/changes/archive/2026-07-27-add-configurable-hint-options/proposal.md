# Proposal: Add configurable randomizer hint options

## Why

`enhance-rando-hints-v2` replaces the original flat hint table with a
deterministic, versioned semantic plan, but deliberately keeps the seed setting
binary: Off or one built-in Balanced composition. The native Hints tab is
therefore useful only after a slot is active; it does not let the player choose
how many delivery surfaces participate or what kind of safe information the
seed should favor.

Players need a small, understandable configuration surface rather than raw
quota sliders. The configuration must also preserve the strong Hints v2
lifecycle contract: old slots keep their exact certified deck, retained clues
do not reroll when only coverage is reduced, custom policy is represented in
the settings hash and spoiler, and no setting creates unsupported path,
required-item, or barren-region claims.

## Dependency

This change is a focused follow-up to
`enhance-rando-hints-v2` and depends on that change's semantic fact model,
24-fact capacity, paid transactions, discovered journal, sidecar v14, and
snapshot type-11 identity contract. `enhance-rando-hints-v2` SHALL be applied
and archived first. This change SHALL NOT be archived ahead of its dependency;
their task ledgers, owner-playtest gates, and archive decisions remain separate.

## What Changes

- Keep the serialized `hints` axis binary (`Off=0`, enabled/Balanced=`1`) and
  add three enabled policy fields:
  - telepathic-tile coverage: 0, 5, 10, or 15;
  - paid clues per service: 0, 1, 2, or 3; and
  - hint mix: Variety, Important Items, Difficult Checks, or World Info.
- Present four named profiles:
  - **Off** = 0 tiles, 0 paid clues;
  - **Sparse** = 5 tiles, 1 paid clue per service, Variety;
  - **Balanced** = 15 tiles, 3 paid clues per service, Variety; and
  - **Direct** = 15 tiles, 3 paid clues per service, Important Items.
  **Custom** is a derived label whenever an enabled policy does not match one
  of the three enabled named profiles; it is not another serialized mode.
- Pack the three policy fields into six currently strict-reserved canonical
  bits with all-zero meaning the existing Balanced policy. Keep canonical
  settings at 31 bytes, share-string v2 at 76 characters, sidecar v14 at its
  current size, and snapshot hint TLV payload format 1 at 41 bytes.
- Establish algorithm 2 / text schema 2 at the historical configurable-policy
  schema floor 157 and land the integrated branch at generator 160. Retain the
  frozen algorithm-1/schema-1 builder for the genuine signed generator-156
  race-reveal compatibility path; no 1/1 sidecar or snapshot was released.
- Make algorithm 2 build a maximal deck for the selected mix, order its tile
  facts with a pinned prefix-balanced schedule, pair them with one
  domain-separated seed-ranked ordering of the 15 physical tiles, and retain
  prefixes for 5/10/15 coverage. Paid depth similarly retains queue prefixes.
  Facts and assignments are compacted after pruning without exceeding the
  existing 24-fact/discovery capacity.
- Keep paid clues exact for every mix. At paid depth zero, every paid service
  truthfully states before the choice that no clue is available, displays the
  real price, and explains that accepting still provides the existing healing
  service. The no-clue framing is grammar-correct in original/US, German, and
  French.
- Turn the native Hints tab into two explicit surfaces: pending next-seed
  configuration and the active-slot discovered journal. Remove the duplicate
  editable Balanced checkbox from General. Keep a compact in-game profile row
  and a four-row advanced options page.
- Extend JSON/text spoilers with normalized policy metadata and variable
  delivery topology. Update the independent spoiler validator to derive counts
  and assignments rather than assuming 18 primaries plus six reserves.
- Keep race mode orthogonal to selection. Race journals remain
  discovered-only, the non-race full-view action remains confirmed, and F12
  remains the explicitly spoiler-bearing unrestricted developer diagnostic.

## Capabilities

### Modified Capabilities

- `randomizer-core`: policy fields, exact canonical packing, profiles/CLI
  normalization, settings/share compatibility, and variable hint spoiler
  metadata.
- `randomizer-hints`: algorithm-2 policy composition, prefix-balanced/nested
  delivery selection, policy-bound digest, paid-depth-zero behavior, and exact
  algorithm-1/schema-1 compatibility.
- `randomizer-placement`: configurable tile activation and truthful
  pre-payment paid-service framing.
- `randomizer-save`: policy reconstruction with unchanged v14/type-11
  layouts.
- `randomizer-native-window`: pending-policy controls and active-slot journal
  separation in the Hints tab.
- `randomizer-ui`: compact in-game profile selection and advanced policy page.
- `randomizer-validation`: adversarial policy, topology, compatibility,
  platform, corpus, and owner-playtest gates.

## Impact

- **Settings:** six reserved bits become defined, but canonical settings remain
  31 bytes and old zero-default blobs remain byte-identical.
- **Determinism:** the configurable schema begins at the historical floor 157;
  the integrated branch lands at generator 160. Hints remain post-placement,
  so placement and sphere digests must not move.
- **Persistence:** no sidecar or snapshot layout growth. Persisted canonical
  settings recover policy for current 2/2 slots. The exact 1/1 builder is
  retained only for the authenticated generator-156 race-reveal path.
- **Runtime/text:** localized paid-depth-zero pre-payment text is new; all rich
  fact kinds and templates remain within the Hints v2 semantic vocabulary.
- **UI:** desktop configuration moves into Hints; the active-slot journal
  remains separately scoped. Switch/in-game configuration retains parity
  without introducing a native-window dependency.
- **Spoilers/tooling:** policy metadata and variable counts are additive;
  legacy row fields remain.

## Non-goals

- No arbitrary per-category quota sliders, arbitrary tile count, per-service
  paid depth, per-NPC mix, or independently serialized Custom profile.
- No item-to-region clues, Way of the Hero, required-item, barren/foolish
  region, counterfactual path, sphere/path-necessity, or live "where next"
  oracle.
- No cryptic/riddle text, progressive clue precision, paid-strength paywall,
  new NPC/source, tracker marker, or map overlay.
- No new semantic fact kind or rich placement template in this change.
- No increase beyond 24 delivery facts or growth of discovery, sidecar v14,
  snapshot TLV 11, or share-string v2.
- No race-mode policy override and no F12 debug-dump redaction.
- No automatic corpus rebaseline for placement/sphere drift.
- No archive, merge, push, or claim that gameplay is complete before the
  dependency gates and this change's separate owner playtest are complete.
