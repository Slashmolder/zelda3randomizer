# Design: Configurable randomizer hint options

## Context

`enhance-rando-hints-v2` establishes the value-owned `RandoHintPlan`, up to 24
facts, 15 telepathic sources, three independent paid queues of depth three,
24 discovery bits, a semantic SHA-256 digest, sidecar v14 identity, snapshot
type 11, and a discovered-hint journal. It intentionally exposes only Off and
Balanced and builds one fixed 18-primary/six-reserve composition.

This follow-up adds policy without weakening those contracts. In particular,
policy is generation-time seed state, not client UI state; an old certified
plan must select its original builder; and reducing delivery coverage must not
reroll clues that remain available.

## Goals / Non-goals

**Goals**

- Offer useful named profiles plus a small orthogonal custom surface.
- Preserve byte-identical legacy Off/Balanced canonical settings.
- Make partial tile coverage and paid depth deterministic nested prefixes.
- Keep every paid clue exact and every mix within existing proven templates.
- Preserve the current 24-fact/discovery and persistence layouts.
- Make pending settings and active-slot journal ownership unmistakable.
- Retain exact algorithm-1/schema-1 race-reveal support.
- Make spoilers and validation understand variable topology.

**Non-goals**

- The deferred clue semantics, sources, raw quotas, localization expansion,
  capacity growth, race/F12 changes, and closeout shortcuts listed in
  `proposal.md`.

## Dependency and archive order

This design consumes the as-built contracts of `enhance-rando-hints-v2`.
Implementation may occur on top of that branch, but archive reconciliation is
strictly ordered:

1. finish the Hints v2 automated, cross-platform, and owner-playtest gates;
2. archive `enhance-rando-hints-v2`;
3. reconcile this change against the resulting core specs;
4. finish this change's independent gates; and
5. archive this change.

The parent change's unchecked gameplay or cross-platform work cannot be marked
complete by exercising configurable policies.

## Decisions

### D1. Profiles are projections over one normalized policy

The existing `hints` byte remains binary:

- `0` = Off;
- `1` = enabled.

An enabled policy has three fields:

```text
tile_coverage ∈ {0, 5, 10, 15}
paid_depth    ∈ {0, 1, 2, 3}  # per each of three services
mix           ∈ {Variety, Important, Difficult, WorldInfo}
```

The UI derives the displayed profile from the normalized fields:

| Display profile | `hints` | Tiles | Paid depth | Mix |
|---|---:|---:|---:|---|
| Off | 0 | 0 | 0 | not applicable |
| Sparse | 1 | 5 | 1 | Variety |
| Balanced | 1 | 15 | 3 | Variety |
| Direct | 1 | 15 | 3 | Important |
| Custom | 1 | any other valid pair | any valid depth | any valid mix |

Custom is never serialized and is not accepted as a standalone CLI value. A
profile action writes its complete tuple atomically. Editing an individual
field recomputes the label; returning to an exact tuple restores its named
label.

`tile_coverage=0, paid_depth=0` has no delivery surface. Normalization converts
that combination to true Off, clears the policy bits, and therefore prevents
two canonical encodings of an empty hint system. Off wins over component
overrides independent of CSV key order.

The unconditional default and the existing `on`, `1`, `true`, `sahasrahla`,
and `full` aliases resolve to the exact Balanced tuple. New profile aliases are
`off`, `sparse`, `balanced`, and `direct`. The component keys are:

```text
hint_tiles = none | few | many | all  # 0, 5, 10, 15
hint_paid  = 0 | 1 | 2 | 3
hint_mix   = variety | important | difficult | world-info
```

The profile value is applied as a base and explicit component keys override it
after parsing, regardless of key order. Duplicate keys remain errors under the
existing settings parser. `hints=off` ignores and clears component fields.

### D2. Six required-zero bits gain exact zero-default encodings

No canonical byte is appended. The exact mapping is:

| Canonical field | Bits | Wire values |
|---|---|---|
| tile coverage | `[28]` bits 5..6 | `00=15`, `01=0`, `10=5`, `11=10` |
| hint mix low | `[28]` bit 7 | combined below |
| hint mix high | `[29]` bit 7 | `00=Variety`, `01=Important`, `10=Difficult`, `11=WorldInfo`; `[28].7` is the low bit |
| paid depth | `[30]` bits 6..7 | `00=3`, `01=0`, `10=1`, `11=2` |

All six bits are zero for existing Balanced and Off settings. Off
canonicalization clears them even if a caller constructed stale non-default
component fields.

Consequences:

- `kSettingsCanonicalLen` remains 31.
- Share-string v2 remains exactly 76 base32 characters.
- Existing Balanced and Off canonical blobs and settings hashes remain
  byte-identical.
- Every effective non-default policy changes the settings hash.
- A pre-change decoder encounters a nonzero bit in a field it currently
  requires to be zero and refuses the settings/share rather than dropping
  policy.
- The generator-version field continues to provide the ordinary newer-binary
  share-string fence.

Settings persistence, presets, CLI parsing, native controls, and in-game
controls all use the same encode/decode/normalize functions. General quick
presets preserve the whole policy. The explicit Race-safe utility preset may
set Balanced, but its label/tooltip must disclose that reset.

### D3. Algorithm 2 builds maximally, then retains nested prefixes

Algorithm 2 consumes the accepted placements/settings/layout state after the
same overlay-install boundary as algorithm 1. It first builds a maximal
candidate deck for the selected mix as though coverage were 15 and paid depth
were 3. Requested coverage/depth only prune the completed maximal plan; they
must not affect candidate RNG consumption, candidate ordering, fact selection,
or the retained source mapping.

The maximal plan targets:

- 15 ordered tile facts;
- three exact facts for Storytellers;
- three exact facts for Fortune Light; and
- three exact facts for Fortune Dark.

Facts remain globally distinct. Missing eligible facts underfill instead of
duplicating, fabricating, weakening an identity, or introducing a new template.

Algorithm 2 derives one domain-separated permutation of the 15 canonical
physical tile source ids from the accepted seed/settings identity. It pairs the
first ordered tile fact with the first ranked source, and so on. Coverage keeps
the first 0/5/10/15 pairs. Therefore both fact and physical-source sets are
nested:

```text
tiles(5) ⊂ tiles(10) ⊂ tiles(15)
```

Each paid service retains queue positions `[0, depth)`, giving:

```text
paid(1) ⊂ paid(2) ⊂ paid(3)
```

After pruning, facts and assignments are compacted to contiguous fact ids
`0..fact_count-1`; assignments never reference a removed fact. If candidate
scarcity leaves a hole within a source's maximal paid queue, later positions
shift left before the requested prefix is taken.

For a rich candidate pool:

```text
primary_count = tile_coverage + (paid_depth > 0 ? 3 : 0)
reserve_count = 3 * max(paid_depth - 1, 0)
fact_count    = primary_count + reserve_count
```

All counts report actual retained facts under scarcity. The hard maximum
remains 24.

The algorithm-2 digest extends the Hints v2 canonical stream with the normalized
enabled policy tuple before facts/assignments. This binds policy even when two
scarce decks happen to retain equal facts. Algorithm 1's digest stream is
frozen and gains no policy bytes.

### D4. Mix schedules are literal and use current fact kinds

The existing Hints v2 candidate eligibility, complete-fit rendering, aliases,
template ids, and prohibition on necessity claims remain authoritative.
Algorithm 2 adds no fact kind or rich template.

For **Variety**, algorithm 2 first selects the exact full-coverage fact
multiset and paid queues produced by algorithm 1 for the same accepted input.
It does not change selected semantics or rendered text. It reorders only the 15
tile facts with the following repeating band schedule, skipping an empty band
and retaining canonical fact order within a band:

```text
I, D, W, U, F, I, D, W, U, I, D, W, U, I, W
```

where:

- `I` = high-priority or other major exact placement;
- `D` = high-friction exact location-content;
- `W` = goal/active-setting or positive region-value;
- `U` = other useful exact placement; and
- `F` = truthful flavor.

Once the listed schedule has visited every available fact, any remaining facts
append in algorithm-1 canonical tile order. This preserves the full Variety
multiset while making five- and ten-tile prefixes category-balanced.

The other mixes select their 15 maximal tile positions by these exact target
quotas and deterministic backfill orders:

| Mix | Tile targets in selection order | Shortage backfill |
|---|---|---|
| Important | 2 high-priority exact, 8 other major exact, 5 useful exact | remaining major, then useful exact |
| Difficult | 9 high-friction exact, 2 high-priority exact, 2 other major exact, 2 useful exact | remaining difficult, then major, then useful exact |
| WorldInfo | 3 goal/active-setting, 6 positive region-value, 3 high-friction exact, 3 useful exact | remaining world-info, then difficult, then major/useful exact |

Within each quota, candidate canonicalization and the mix-specific
domain-separated RNG stream pin selection. A target is a maximum, not
permission to duplicate. After selection, the same five-band prefix scheduler
is used with empty bands skipped; Important naturally uses `I/U`, Difficult
uses `D/I/U`, and WorldInfo uses `W/D/U`.

Paid queues are always exact and use no goal, setting, region summary, or
flavor fact. Variety reuses algorithm 1's exact paid queues. Other mixes fill
each service round-robin by queue position from distinct remaining exact facts
using these preference orders:

- Important: high-priority, other major, useful;
- Difficult: high-friction, high-priority, other major, useful;
- WorldInfo: high-friction, high-priority, other major, useful.

Canonical service order is Storytellers, Fortune Light, Fortune Dark.
Canonical queue order is position 0 across all services, then position 1, then
position 2. Scarcity underfills without changing earlier assignments.

Changing mix may select a different deck. Changing only coverage or paid depth
may not reroll or move any retained semantic fact/source pair.

### D5. Paid depth zero is truthful before the choice

Paid depth zero does not remove the underlying vanilla-compatible service. It
removes only clue delivery. Before the player can accept, each Storyteller and
Fortune Teller flow must render locale-correct text that says:

1. no new clue is available;
2. the actual displayed price will still be charged if accepted; and
3. acceptance still provides the service's existing healing.

The choice command and price receipt remain in their existing control-flow
positions. Decline and insufficient-funds paths charge nothing, heal nothing,
and discover nothing. Acceptance charges exactly once and heals exactly as the
existing Hints v2 exhausted-service transaction does; it commits no discovery
and creates no paid latch.

Original/US, German, and French get complete neutral text. It may not imply
that a clue exists, remains, or was purchased. If the truthful framing cannot
fit while preserving all choice/control commands for a service/locale, paid
depth zero must fail closed during source validation and SHALL NOT ship as a
selectable value for that build.

### D6. Current identity is 2/2; 1/1 remains a narrow exact builder

The configurable-policy schema begins at historical generator floor 157, while
the rebased integrated branch lands at generator 160. Newly generated slots,
including Balanced and Off, stamp hint algorithm 2 and text schema 2. Text
schema advances because paid-depth-zero pre-payment/exhaustion framing changes
owned dialogue bytes.

The supported pair table is exactly:

| Algorithm | Text schema | Meaning |
|---:|---:|---|
| 1 | 1 | frozen `enhance-rando-hints-v2` plan, assignments, text, and digest |
| 2 | 2 | configurable policy and localized paid-zero framing |

Crossed pairs `1/2` and `2/1`, zero versions, and unknown versions are
unsupported. Dispatch always uses the persisted pair; it never substitutes the
current builder.

Algorithm 1 support is limited to deterministic reconstruction for the genuine
signed generator-156 race-reveal artifact. Its facts, assignments, rendered
bytes, digest, and legacy spoiler field set are frozen by that artifact and
independent validation. Hints identity persistence first ships in sidecar v14
and snapshot type 11 with current pair 2/2, so there is no released 1/1
sidecar/snapshot lifecycle to preserve.

The legacy ZRSR format predates explicit hint-pair fields, so race reveal maps
generator 156 to pair 1/1. Current generator 160 maps to pair 2/2 through the
ordinary exact-current path. The predecessor exception remains available on
current binaries at or above the historical configurable-policy floor,
requires the embedded share version to agree and every newly claimed policy
and NPC-preview bit to remain zero, and serializes the exact generator-156
spoiler field set for stamp verification. Every other non-current generator,
including 157, remains refused.

The sidecar remains format 14 with the 278-byte extension and the existing
algorithm/text/digest/discovery fields. Snapshot type 11 remains payload format
1 and exactly 41 bytes. No policy field is appended to either record because
the accepted canonical settings already contain it. Discovery remains 24 bits.

Unsupported or mismatched identity continues to disable only hints
non-destructively. It does not rewrite the slot, reinterpret policy, or retry
with 2/2.

### D7. The Hints UI separates pending and active ownership

On desktop, Hints becomes a normal Randomizer tab immediately after General.
It contains two inner surfaces:

1. **Next seed setup**, reading/writing only the bridge-owned pending settings;
2. **Active-slot journal**, reading only active slot identity/discovery/race
   state.

Next seed setup renders:

- the Off/Sparse/Balanced/Direct profile actions;
- tile coverage selector;
- paid-depth selector;
- mix selector;
- derived profile label; and
- an exact capacity summary, e.g. "Up to 16 delivery clues: 10 tile + 6 paid."

It also states that paid clues are always exact and that depth zero still
offers priced healing without a clue. Controls are disabled only under the
same target/settings ownership rules as other generation controls.

General no longer owns an editable Balanced checkbox. It may show the derived
summary and "Configure in Hints", but must not create a second write path.
Pending `race_mode` never modifies the active journal. An active race slot
remains discovered-only; an active non-race slot retains the separately
confirmed full viewer.

The in-game/Switch screen keeps one compact `HINTS <profile>` row. Left/right
cycles Off, Sparse, Balanced, and Direct. Activating the row opens an advanced
four-row page: Tiles, Paid, Mix, Reset to Balanced. Editing a component derives
Custom; backing out retains the pending tuple. It uses the same settings API
and canonical path as desktop.

### D8. Spoilers expose normalized policy and variable topology

The plan-level JSON identity object adds normalized policy fields:

```text
profile      "off" | "sparse" | "balanced" | "direct" | "custom"
tile_count   0 | 5 | 10 | 15
paid_depth   0 | 1 | 2 | 3
mix          "variety" | "important" | "difficult" | "world-info"
```

Off reports `profile=off`, zero delivery counts, and an explicit
not-applicable mix representation pinned by the serializer. Enabled plans
report the normalized tuple even when scarcity underfills delivery.

Legacy per-row `npc`, `dialogue_id`, and `text` fields remain. Typed facts and
assignments remain canonical, but consumers may no longer assume 18 primaries,
six reserves, 15 populated tiles, or three rows per paid source.
`meta.hints_count` continues to report actual emitted rows including the
separate Murahdahla compatibility row when present.

Text spoilers mirror the normalized policy and actual topology. Race
suppression and stamp rules remain unchanged. Discovery, checked state,
resolved state, and paid presentation state remain excluded.

### D9. Validation is pairwise plus adversarial, not Cartesian-only

Unit/self-tests exhaust all 64 raw policy bit combinations, including Off
normalization and invalid construction paths. Expensive CLI/corpus/lifecycle
tests use a reviewed pairwise matrix covering every profile, mix, coverage,
paid depth, race state, locale, scarcity class, and lifecycle entry at least
once, plus adversarial cases for:

- reversed candidate/placement iteration;
- retained prefix identity across 5/10/15 and 1/2/3;
- compact fact ids and no orphan assignments;
- zero-depth pre-payment, decline, insufficient funds, purchase, healing, and
  no discovery;
- current 2/2 sidecar/snapshot lifecycle plus signed generator-156 1/1 reveal;
- crossed/unknown/mismatched version failure;
- variable spoiler counts and independently recomputed digest;
- old zero-default settings/share identity and old-reader refusal;
- race journal policy and unchanged F12 diagnostics; and
- no placement/sphere digest movement.

MSVC Release, GCC and Clang `-Werror`, full self-test, slot/snapshot checks,
full corpus, source/codegen/version guards, strict OpenSpec, fresh-eyes review,
and focused owner gameplay are separate closeout gates.

## Risks and mitigations

- **Partial coverage accidentally rerolls clues.** Build the maximal deck before
  pruning and freeze nested-prefix vectors.
- **Policy bits are decoded differently across paths.** Centralize packing and
  normalization; exhaust all 64 bit patterns.
- **Legacy race artifacts silently gain new text.** Rebuild generator-156
  pair 1/1 exactly and freeze its rows, text, digest, and spoiler shape.
- **Paid-zero messaging misleads before charging.** Validate the complete
  localized pre-choice flow, including control commands, before making zero
  selectable.
- **The Hints tab conflates pending and active state.** Use separate inner
  surfaces and bridge ownership; test pending race/policy edits against an
  active slot.
- **Mix labels over-promise truth.** Restrict selection to existing typed
  facts/templates and describe preferences as deterministic quotas/backfill,
  never as path proof.

## Implementation order

1. Freeze the signed generator-156 algorithm-1/schema-1 race artifact and
   validate the six-bit packing assumptions.
2. Add settings fields, normalization, profiles, CLI/share/preset support, and
   exhaustive serialization tests.
3. Add algorithm dispatch, algorithm-2 mix schedules, nested source ranking,
   pruning/compaction, and policy-bound digesting.
4. Add paid-zero localized presentation and transaction tests.
5. Add spoiler metadata/validator support, current 2/2 persistence coverage,
   and genuine generator-156 1/1 race-reveal coverage.
6. Add native and in-game UI surfaces.
7. Run the full automated matrix, independent reviews, and owner playtest.
8. Reconcile docs/specs to as-built behavior and close out only in dependency
   order.
