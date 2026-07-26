# Design: Randomizer hints v2

## Context

`Rando_GenerateHints` currently owns one module-static `HintEntry` per legacy
NPC id. It shuffles placement rows, formats `"<item> is in <location>"`, assigns
15 rows to telepathic tiles and one row to each of three paid source ids, and
lets the spoiler and message renderer read those strings later. Murahdahla is a
separate spoiler-only row. Vanilla-dialogue redirects resolve live placement
claims but use the same US-only renderer.

That shape couples four concerns:

1. the seed truth being communicated;
2. how the fact is phrased;
3. where it is delivered; and
4. whether the player has discovered it.

It also makes a module-global table an implicit input to spoiler writing.
Native export temporarily regenerates that global and activation regenerates it
again. Snapshots deliberately clear it in some paths. None of those paths can
prove that they reconstructed the generation-time deck.

## Goals / Non-goals

**Goals**

- Make every hint a typed, inspectable truth independent of delivery source.
- Produce a useful, diverse, deterministic Balanced deck without unsupported
  path-necessity claims.
- Preserve complete item/location identity and reject an unrenderable fact.
- Reconstruct one byte-identical plan at generation, activation, spoiler
  export/reveal, warm snapshot restore, and fresh-process cold replay.
- Persist only stable identity plus discovery, not derived strings or cursors.
- Make paid hint advancement transactional and healing-safe.
- Make the native panel useful in normal and race play without making ordinary
  race UI a full spoiler.

**Non-goals**

- The deferred clue packs and modes listed in `proposal.md`.
- Rich German/French fact translation. Only neutral fallbacks are localized in
  this change.
- Protecting a race from an operator who intentionally invokes F12 developer
  diagnostics.

## Decisions

### D1. Binary setting stays byte-compatible: Off or Balanced

Canonical byte 22 and numeric values do not change:

- `0` = Off
- `1` = Balanced

`on`, `1`, `true`, `sahasrahla`, and `full` continue to parse as value 1 so old
manifests and share strings retain their meaning. UI and documentation call the
value Balanced. An internal `kHintsMode_On` compatibility alias may remain while
callers migrate to `kHintsMode_Balanced`; it is not a third mode. The default
remains value 1.

Hint generation stays downstream of accepted placement, so this change does not
intentionally move placement or sphere digests. The settings hash continues to
distinguish Off from Balanced exactly as it does today.

### D2. `HintPlan` separates facts, assignments, and discovery

The semantic model is value-owned and contains no pointers into transient
placement or UI storage. The exact C layout is implementation-local, but its
contract is:

```text
HintFact
  fact_id              stable plan index 0..23
  kind                 item-location, location-item, goal/setting, region-value
  priority/category    Balanced selection metadata
  precision            exact target or positive summary
  item/location/region optional typed ids
  template_id          stable text-schema id
  template_parameter   identity-bearing goal/setting/count/flavor parameter

HintSourceAssignment
  source_id             one of 15 tiles or 3 paid source identities
  queue_index           0 for a tile; 0..2 for a paid queue
  fact_id

HintPlan
  algorithm_version
  text_schema_version
  ordered facts[0..fact_count)
  ordered source assignments
  primary_count
  reserve_count
  digest[32]

HintDiscoveryState
  discovered_bits[24]
```

Facts do not know their source. Assignments do not own text. Discovery is not
part of `HintPlan` and cannot change its digest. A paid queue cursor is derived
by scanning its assignments for the first undiscovered, still-relevant fact; no
cursor is persisted.

Murahdahla remains a separately derived goal-summary compatibility row. It is
not in `facts[]`, is not assigned to a paid/tile delivery source, and has no
discovery bit.

### D3. The plan digest is a canonical semantic digest

The 32-byte digest is SHA-256 over a domain-separated, explicitly byte-encoded
stream. It includes:

- algorithm and text-schema versions;
- fact count, primary count, and reserve count;
- every ordered fact's id, kind, category/priority, precision, typed target ids,
  template id, and template parameter; and
- every ordered tile/paid assignment, including paid queue position.

It excludes C padding, pointers, localized/rendered text, discovery bits,
checked-location state, UI state, and Murahdahla. Multi-byte integers use the
repository's pinned little-endian encoding. Self-checks build semantically
identical inputs with different placement-row ordering and require the same
canonical bytes and digest.

`text_schema_version` makes phrasing/alias interpretation part of compatibility
without persisting text. Any change that can alter rendered fact text advances
that version. Any selection/assignment change advances `algorithm_version`.

### D4. Balanced selection is useful, diverse, and safely underfilled

The builder runs only after the accepted placement and all effective
setting-derived assignment/layout state needed by eligible facts is installed.
It is read-only and caller-owned, but not referentially pure from its explicit
arguments: effective region identity also consumes the currently installed
entrance-region overlay. Generation and reconstruction therefore build before
clearing that overlay; native export stores the generation-time plan rather
than attempting a later rebuild.
It canonicalizes candidates by stable typed ids before using a dedicated
domain-separated randomizer RNG stream. Placement-table iteration order cannot
affect the result.

The primary deck targets 18 facts:

1. 2 high-priority-item placement facts;
2. 4 other diverse major-item placement facts;
3. 3 high-friction exact location-content facts;
4. up to 3 truthful goal or active-setting facts;
5. up to 2 positive region-value facts;
6. 3 other varied useful placement facts; and
7. at most 1 truthful flavor fact.

These are target quotas, not reasons to lie or duplicate. Missing categories
backfill deterministically from major then useful exact facts. A positive
region-value fact may state only directly counted useful value; it does not call
a region required, barren, or foolish. "Required", "Way of the Hero", and path
necessity are prohibited because this change has no counterfactual proof oracle.

Paid queue heads must be exact facts. Scanning the ordered primary deck from
its tail, the builder assigns the last still-unassigned exact fact to
Storytellers, then Fortune Light, then Fortune Dark. It then assigns the
remaining primaries in deck order to the 15 canonical telepathic-tile sources.
The reserve pass targets six additional distinct, exact-useful facts: queue
positions 1 and 2 for each paid source. Reserve facts never use flavor or vague
region summaries.

Eighteen plus six are capacities and normal-seed targets, not a fill-at-all-costs
rule. If fewer eligible, semantically distinct, completely renderable facts
exist, the plan underfills and the affected sources/queues exhaust safely.
Generation does not fail merely to meet a quota and never substitutes a
duplicate, ambiguous alias, fabricated claim, or low-value filler.

### D5. Hint metadata is authoritative and render-before-accept is atomic

Item and location hint metadata has one data-driven authoritative source tied to
the registries. It classifies major/high-priority/useful/junk eligibility,
high-friction locations, and bounded aliases. Generated tables retain:

- dungeon identity for keys, key rings, maps, and compasses;
- the semantic suffix for multi-check locations;
- distinct ids even when two display names share a base phrase; and
- separate complete qualified and lossless compact-qualified item names;
- lossless compact location and region aliases; and
- an explicit "not renderable" result for unknown future identities.

`hint_registry_contract.json` separately approves the complete shipped merged
registry: 6,364 locations and 251 regions, with identity, naming, region/type,
source-provenance, and family-count fingerprints. Full-artifact codegen must
match that exact superset. The only assetless exception is the exact
324-location/40-region base, which emits base tables while binding both metadata
fingerprints to the complete approved policy object; any partial or in-between
artifact set fails closed. An approved merged-registry change therefore
advances both the algorithm and text-schema compatibility axes.

Codegen exhausts every currently eligible useful-item/location pair through the
exact three-row encoder, every redirect item/location and Bumper item form, and
every region alias at supported counts. A declared eligible registry identity
that cannot render losslessly is a source-generation error, not a silently
thinned candidate pool. Runtime eligibility still fails closed for malformed or
unknown future inputs. Rendering occurs into a scratch buffer with explicit
capacity and complete-message success. The live message buffer is replaced only
after the whole message, commands, and terminator have been validated. Silent
truncation and numeric/ambiguous compatibility fallbacks are not accepted.

The rich placement templates in this change use the original/US grammar and
font. Recognized randomizer-owned hint surfaces have short, grammar-correct
neutral templates for original/US, German, and French. They use neutral text
when:

- the active mode is Off;
- the plan is missing, unsupported, malformed, or digest-mismatched;
- a source or queue position is unfilled/exhausted; or
- the selected locale has no rich-fact encoder.

Neutral text makes no item, location, path, or remaining-clue claim beyond the
known state ("no new clue" is permitted for an exhausted paid queue). Vanilla
slots remain byte-identical. A reused dialogue id whose physical discriminator
does not match is not a recognized surface in that context and still falls
through to vanilla.

The reviewed vanilla-dialogue redirect table keeps its item-to-location,
location-to-item, and Stumpy choice-flow semantics. In an active randomizer slot
at a matching physical surface, a missing target, Off mode, or unavailable rich
renderer uses the locale's neutral text rather than a seed-invalid vanilla
claim. Stumpy's Yes/No command and separate reward flow remain intact.

### D6. One versioned builder serves every lifecycle

The as-built public lifecycle seams are:

```text
Rando_BuildHintPlan(settings, placements, spheres,
                    algorithm_version, text_schema_version, out_plan)
Rando_HintPlanExportPersistedState(plan, out_identity)
Rando_HintsImportPersistedState(expected_identity_and_discovery)
Rando_RebuildHints(settings, placements, spheres)
```

The builder computes the canonical digest internally. Its explicit arguments
plus the installed effective-region overlay comprise the immutable accepted
inputs eligible to the current algorithm. Export certifies the plan identity
written for a new slot; import records expected identity/discovery during load;
rebuild constructs that exact supported plan, verifies its digest, and only then
applies discovery. Spoiler writing receives a `const RandoHintPlan *`
explicitly; it never reads an unrelated module-global plan.

The same versioned builder is invoked for:

- initial generation before canonical spoiler/stamp creation;
- sidecar activation after all accepted overlays/assignments are installed;
- race reveal and other spoiler regeneration;
- native-window export from its generation-time settings, placement, spheres,
  and assignment snapshot;
- warm snapshot restore; and
- fresh-process cold snapshot replay after all layout TLVs are accepted.

The v2 launch supports only its current stamped algorithm/text pair. Pre-v14
sidecars, snapshots without type 11, and unknown versions do not guess a
current algorithm: the slot remains playable but the rich hint plan and journal
are disabled for that activation. Future versions may retain explicitly listed
older builders; if a version is declared supported, it must remain byte-identical
for its lifetime.

Any build failure or digest mismatch disables only hints for that load, clears
discovery/paid presentation state, and routes owned surfaces to neutral text. It
does not overwrite the save, mutate checked locations, change payment state, or
reroll with the current algorithm.

### D7. Sidecar v14 and snapshot type 11 carry identity plus discovery

The sidecar file format advances 13 to 14. The slot extension grows from 238 to
278 bytes by appending:

| Extension offset | Width | Field |
|---:|---:|---|
| 238 | 2 | `hint_algorithm_version` LE |
| 240 | 2 | `hint_text_schema_version` LE |
| 242 | 32 | `hint_plan_digest` |
| 274 | 4 | `hint_discovered_bits` LE |

Current writes always populate the fields. Off writes use the current versions,
the canonical empty-plan digest, and zero discovery. New-slot generation starts
with zero discovery; normal game saves copy the live 24-bit discovery state.
Bits 24..31 are reserved and written zero.

Before Hints v2 relies on paid discovery persistence, the normal save path must
match the existing two-file atomic-commit contract: stage the paired SRAM bytes
in memory, durably replace the sidecar carrying current discovery and the
checksum of those staged bytes, then write the vanilla SRAM file. Sidecar write
failure suppresses the paired SRAM write. An interruption between the two
durable replacements is detected by the existing checksum/drift model; it must
not silently load spent-rupee SRAM with stale discovery and repeat a paid clue.

Snapshots append `kRandoSnapshotTail_Type_HintState = 11` with a 41-byte payload:

| Payload offset | Width | Field |
|---:|---:|---|
| 0 | 1 | payload format (`1`) |
| 1 | 2 | algorithm version LE |
| 3 | 2 | text-schema version LE |
| 5 | 32 | plan digest |
| 37 | 4 | discovery bits LE |

The loader associates type 11 only with an accepted type-1 randomizer state,
holds it pending while other layout TLVs load, and rebuilds/validates the hint
plan at finish-load after `Rando_ReinstallActiveSlotLogicOverlays`. Malformed,
duplicate, orphaned, missing, unsupported, or mismatched hint metadata disables
hints without invalidating an otherwise valid snapshot.

No plan, text, paid cursor, resolved marker, or in-flight paid token is
serialized. Plan/assignments/text rebuild from certified inputs; queue position
derives from discovery; resolved markers derive from the checked-location
bitmap.

### D8. Discovery is committed by delivery, not by inspection APIs

Telepathic-tile discovery commits only after the assigned fact has rendered as a
complete message for the active plan epoch. A failed or neutral render discovers
nothing. Paid discovery follows D9's transaction. Read-only getters, spoiler
writing, F12, plan rebuild, journal rendering, and the full-deck viewer cannot
set discovery.

The native journal defaults to discovered delivery facts only and displays:

- the source at which the fact was discovered;
- the canonical complete human-readable text produced by the active plan/text
  schema (`RandoHintFact.text`, rather than the compact three-row
  `RandoHintFact.game_text` delivery rendering); and
- a resolved marker when an exact fact's target location is checked.

The marker is derived, so collecting an item after discovery updates it without
rewriting discovery. Murahdahla and live vanilla-dialogue redirects are outside
the 24-fact discovery journal.

Outside race mode, "View all seed hints — placement spoilers" requires an
explicit confirmation and reveals the full active deck for the current UI
session. It does not mark facts discovered, move paid queues, or modify gameplay.
Race mode never exposes this action and always remains discovered-only. A plan
identity failure shows an unavailable/neutral state rather than stale facts.

### D9. Three paid queues use prepared presentation and commit

Logical paid source identities are:

1. **Storytellers**: paid `Sprite_28_DarkWorldHintNPC` subtypes 0, 1, 2, and 4
   share one queue; carrier messages are `0xFF`, `0x101`, `0x102`, `0x103`.
2. **Fortune Light**: Kakariko and Lake Hylia share one queue because their
   shared interior has no discriminator; reading carriers are
   `0xEA..0xF1` and `0xF6..0xFD` while the normalized world bit is 0.
3. **Fortune Dark**: the same reading carrier ids use the Dark queue while
   `(savegame_is_darkworld >> 6) & 1` is 1.

Each queue's fixed assignment order is `[primary head, reserve 1, reserve 2]`,
with absent tail entries allowed after safe underfill. Selection scans for the
first undiscovered assignment and dynamically skips an exact fact whose target
location is already checked. A skip is neither discovery nor a paid wasted
clue.

The source transaction is:

1. **Prepare** records the active plan epoch, source, queue position, and fact,
   or an Exhausted result. It completely encodes that presentation into
   transaction-owned scratch and arms the source latch before any new hint
   charge or discovery mutation. An incomplete encoding fails Prepare.
2. The source follows its established message, affordability, payment, and
   healing choreography while every carrier load reads the same prepared
   scratch.
3. **Commit** revalidates the epoch, source, queue position, fact, and complete
   scratch. It marks a Ready fact discovered idempotently; Exhausted commits no
   discovery. The presentation remains latched until that source completes.
4. The source's existing completion state cancels the presentation latch.

Decline, insufficient funds, render failure, stale epoch, reload/replay before
commit, or a refunded commit failure does not discover or advance a fact.
Exhausted services still follow their normal price and healing behavior but
commit no discovery and never repeat an earlier clue.

Storytellers prepare before the 20-rupee deduction, deduct once, Commit (with a
refund if that validation unexpectedly fails), then show the prepared carrier
and heal in their existing completion state.

Fortune Tellers prepare and show the latched reading in state 4, preserve the
visible cost receipt in state 5, then retain the one existing
10/15/20/30-rupee deduction and Commit in state 6 before healing/completion.
There is no prepayment path or second-deduction marker. Off/unavailable states
retain the established payment/healing ordering while the owned carrier shows
truthful neutral text.

### D10. Spoiler compatibility is additive and discovery-free

The full spoiler serializes the complete plan in canonical fact/assignment
order. Every delivery entry retains the existing:

- `npc`
- `dialogue_id`
- `text`

and adds stable typed metadata sufficient to identify fact id/kind/precision,
template, source, queue position or tile assignment, and typed targets. Reserve
rows reuse their paid source's stable `npc`/dialogue label and are distinguished
by fact id plus queue position.

A plan-level object records algorithm version, text-schema version, digest,
primary count, and reserve count. Murahdahla retains its compatibility entry and
is explicitly outside those delivery counts. Off emits an empty delivery plan
with its empty-plan identity.

Discovery and checked/resolved state never enter JSON/text spoiler content or
the canonical race stamp. Thus buying or reading clues cannot make reveal
verification depend on mutable play state. Race-mode suppression and reveal
continue to protect the full spoiler and undiscovered plan on normal surfaces.

F12 is deliberately different: it is a developer diagnostic and may print the
entire plan, assignments, discovery bits, queue selection, and targets in race
mode. Its header must state that the dump contains spoilers.

### D11. Validation proves lifecycle parity and transaction choreography

One adversarial seed with a complete 24-fact plan is rebuilt through generation,
sidecar activation, native spoiler export/restoration, race reveal, warm
snapshot replay, and fresh-process-equivalent cold replay. Canonical plan bytes,
assignments, every rendered US string, and digest must match.

Additional automated coverage includes:

- candidate and placement iteration-order permutation;
- category quotas, deterministic backfill, safe underfill, and no duplicates;
- every eligible item/location/template combination, buffer canaries, complete
  fit, multi-check suffixes, and dungeon-qualified identities;
- neutral original/US, German, and French rendering for Off/unavailable states;
- sidecar v14 offsets/round-trip, reserved discovery bits, legacy disable, and
  digest/version mismatch disable;
- snapshot type-11 ordering, malformed/orphaned/duplicate handling, warm/cold
  replay, and missing-TLV disable;
- discovery idempotence, resolved derivation, journal/full-viewer non-mutation,
  and race discovered-only behavior;
- all paid decline/insufficient/render/stale/refund/success/exhaustion paths,
  shared Light queue behavior, repeated buffer loads, exactly-one charge, and
  preserved healing;
- backward-compatible spoiler fields, typed metadata, Murahdahla separation,
  discovery-free race stamps, and explicit-plan spoiler APIs; and
- generator/corpus version synchronization, MSVC/GCC self-checks, strict
  OpenSpec validation, source guards, and the full corpus.

Automation does not complete the owner gameplay gate. The focused playtest buys
three clues from each logical paid queue with a save/reload or snapshot between
purchases, confirms a fourth exhausted service still heals without repeating or
advancing, reads representative tiles, checks journal/resolved transitions,
checks race-mode journal/all-viewer behavior, preserves Stumpy/Bumper
interaction, and verifies neutral US/DE/FR presentation.

## Risks / Trade-offs

- Persisting a digest instead of text makes saves compact and avoids two sources
  of truth, but every supported algorithm/text pair becomes a compatibility
  promise.
- Pre-v14 slots cannot prove which unstamped legacy hint deck they had. Disabling
  only hints is less surprising than silently generating a new deck.
- Paid Fortune Teller payment choreography is stateful. The prepared encoded
  latch must survive the state-4 reading and state-5 receipt until the one
  existing state-6 deduction and epoch-validating Commit.
- Rich non-US localization is deferred, so German/French players receive honest
  neutral text rather than the new detailed clues in this release.
- Normal save currently calls the vanilla SRAM write before its sidecar update,
  contrary to the existing sidecar-first contract. Hints v2 makes that ordering
  player-visible through paid discovery, so the migration corrects it and adds
  interruption/failure coverage rather than relying on the stale order.

## Migration Plan

1. Land the semantic model, authoritative metadata, bounded renderer, corrected
   tile ids, and neutral fallbacks behind the existing binary mode.
2. Add versioned build/digest APIs and explicit-plan spoiler serialization.
3. Add sidecar v14 plus snapshot type 11, then make activation/replay fail the
   hint subsystem closed on identity failure.
4. Add discovery APIs and the native discovered journal/full-viewer policy.
5. Migrate Storyteller and Fortune Teller handlers to paid transactions.
6. Run the full automated stack, independent review, and focused owner playtest;
   reconcile as-built specs before archive.
