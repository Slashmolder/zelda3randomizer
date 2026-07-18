# add-rando-vanilla-npc-hint-redirects

## Why

Several vanilla dialogue surfaces name the vanilla location of an item even
while a randomizer slot has changed that relationship. The motivating case is Aginah:
runtime dialogue `0x125` says the Book of Mudora is in the Library even when the
Library visibly holds another item. The baseline `randomizer-hints` capability
tracks this class of redirect as deferred.

The archived `2026-06-11-add-rando-hints/audit.md` is useful historical input,
but it mixed one-based, user-facing `assets/dialogue.txt` numbers with zero-based
runtime `dialogue_message_index` values. This change re-audits the current text
and handlers before wiring any runtime IDs.

## What Changes

- Add a table-driven vanilla-dialogue redirect layer to the existing hint renderer.
- Resolve fixed referenced items by reverse-searching the active placement table;
  duplicate items choose the lowest location ID.
- Resolve the Bumper Cave sign in the opposite direction—from its fixed physical
  location to the item placed there—with an outdoors/screen discriminator.
- Rewrite Stumpy's Flute-location prompt after vanilla decoding so its Yes/No
  command and randomized `LOC_Stumpy` reward flow remain intact.
- Require an active slot, recovered active settings, `hints == on`, a supported
  US dialogue buffer, and a matching runtime discriminator before replacing text.
- Keep generated tile/fork hints and their RNG/pool unchanged.
- Keep active dynamic hints readable when story-dialogue fast-forward is enabled.
- Extend F12 hint diagnostics with redirect source, item, resolved location, and
  explicit skip reasons.
- Deliberately leave other interactive choice messages and progressive-tier-
  ambiguous Master Sword claims outside this focused layer.

## Capabilities

### Modified

- `randomizer-hints`: implement the reviewed vanilla-dialogue redirect subset and
  replace the deferred baseline requirement with its as-built gates and fallback
  behavior when this change is archived after playtest.

## Non-goals

- ALTTPR's full flavor-text or joke-hint algorithms.
- New hint RNG, generated hint slots, or spoiler entries.
- Entrance-aware prose.
- General interactive-dialogue rewriting or resolving progressive item tiers.

## Impact

- Runtime: `src/rando/rando_hints.{c,h}`, `src/messaging.c`.
- Diagnostics: `Rando_DumpHintDebug()` / `dump_hints.txt`.
- Tests: `Hints_SelfCheck()` and existing randomizer self-test/build/corpus gates.
- Versioning: repository policy requires `kGeneratorVersion` and the corpus
  manifest version to move from 145 to 146 for edits under `src/rando/`.
