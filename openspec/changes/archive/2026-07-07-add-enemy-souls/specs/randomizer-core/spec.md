# randomizer-core Specification (delta)

## MODIFIED Requirements

### Requirement: Assumed-fill placement
The placement algorithm SHALL use assumed fill — placing progression items into locations reachable under the assumption that all remaining unplaced items are temporarily available — and SHALL retry placement with bounded rewind when no valid location exists for the current item.

**Enemies-tier fill model (add-enemy-souls)**: for seeds whose EFFECTIVE `souls_shuffle` is the bosses+enemies tier, the per-turn reachability SHALL be computed from the assumed (unplaced) inventory PLUS a fix-point collection of items already committed to locations — both items placed on earlier turns and pre-placed pins (prize assignments, event grants, TakeAny rewards, customizer pins): any committed item whose location is reachable under the current set joins the set, and reachability recomputes until stable (the upstream `RandomAssumed` "fix-point reachability expansion" contract). Pins are then NOT assumed unconditionally — a pinned grant whose location the current set cannot reach does not count (unconditionally-assumed prize pins let GT-entry certify open through crystals whose Prize locations were themselves soul-blocked, failing every attempt's validation). Collection exclusions: vanilla-MODE dungeon items are pre-granted wholesale (the ROM grants them in place; their pinned slots are skipped to avoid double-counting key thresholds), and placed copies of the item id currently being placed are never collected (a key must not sit in a slot justified by its own placed siblings — the final sphere walk cannot order the copies).

All other seeds SHALL keep the conservative pre-souls model — placed items leave the assumed set permanently and pin grants are assumed unconditionally — which the worst-case key-threshold models (pot key depths, the door-key oracle) are calibrated against; applying the collection model to them empirically degraded their fills (deep-stacked dungeon keys; door-oracle source double-counting). Under either model, every attempt is validated by the sphere walk before acceptance, so the fill model is quality guidance, never a soundness input.

**Attempt acceptance bar (add-enemy-souls)**: an attempt SHALL be accepted when it satisfies the seed's EFFECTIVE accessibility tier — `locations`: every placement reachable; `items` (default): every progression placement reachable; `none` ("beatable only"): the goal is completable — plus zero forward-fill fallbacks and the Standard-mode escape weapon/lamp constraints. Demanding unconditional full reach (the old bar) is unattainable for combos with modeling-stranded junk placements (e.g. wild keys × dungeon enemy checks permanently strand a fixed set of junk-holding checks) and burned the whole attempt budget on seeds whose first attempt already satisfied the acceptance gate.

**Phase B implementation alignment (Bug #7 fix)**: the "bounded rewind" SHALL be per-item, not whole-attempt. Phase A1's implementation uses whole-attempt retry with `kAssumedFillMaxAttempts=8`; the SHALL above (already present in Phase A spec at `randomizer-core/spec.md:344`) describes per-item rewind. This change brings the implementation in line with the existing spec.

Per-item rewind algorithm: when the current item has no valid placement, rewind the last N placements (N is the per-item rewind budget, configurable; default 10), recompute the simulated inventory state, and retry placing the current item. If the per-item rewind budget exhausts, escalate to whole-attempt retry (existing `kAssumedFillMaxAttempts` path). If both budgets exhaust, surface a clear error.

#### Scenario: Placed souls keep gating locations open (enemies tier)
- **WHEN** an enemies-tier seed placed a soul on an earlier turn at a reachable location, and a later turn evaluates a kill-gated location requiring that soul
- **THEN** the per-turn reachability collects the soul through the fix-point and the gated location remains a valid candidate (it does not go permanently dead the moment the soul leaves the assumed set)

#### Scenario: Pins are collected, not assumed (enemies tier)
- **WHEN** an enemies-tier seed's pinned grant location (e.g. a dungeon Prize holding a crystal) is unreachable under the current assumed-plus-collected set
- **THEN** the pinned item does not count toward reachability that turn, so the placer cannot certify a placement against a grant the player could not yet have

#### Scenario: Non-enemies-tier seeds keep the conservative model
- **WHEN** a seed's effective souls tier is off or bosses
- **THEN** the fill uses the pre-souls conservative reachability (placed items vanish, pins assumed) unchanged

#### Scenario: Attempt acceptance matches the accessibility tier
- **WHEN** an attempt strands only non-progression placements and the seed's effective accessibility is `items`
- **THEN** the attempt is accepted without burning the remaining attempt budget, and the stranded placements surface in the spoiler's unreachable list

#### Scenario: Per-item rewind preserves earlier valid placements
- **WHEN** the placer hits an item with no valid location and the per-item rewind budget is non-zero
- **THEN** the placer rewinds the last N placements, retries the current item, and earlier valid placements (those outside the N-rewind window) are preserved

#### Scenario: Per-item rewind budget exhausts → whole-attempt retry
- **WHEN** per-item rewind exhausts for the current item
- **THEN** the placer falls back to whole-attempt retry (Phase A1 behavior); `kAssumedFillMaxAttempts` bounds whole-attempt retries

#### Scenario: Both budgets exhausted → clear error
- **WHEN** both per-item rewind and whole-attempt budgets exhaust for a given seed
- **THEN** generation fails with an error message naming the offending item and the budgets consumed; the spoiler is not written; the CLI exits non-zero

#### Scenario: Forward-fill fallback after timeout
- **WHEN** assumed fill exceeds an explicitly-passed positive wall-clock budget (the default budget is `0` = no cutoff)
- **THEN** the generator falls back to forward fill (placing items into reachable locations in order) and surfaces a warning in the spoiler `fallback_warnings` array

#### Scenario: Same-seed determinism across budgets
- **WHEN** the same seed is generated under different `--budget-seconds` values that all succeed within budget
- **THEN** the resulting `placement_digest_hex` values are byte-identical — the budget is wall-clock fail-safe, not a determinism input
