## MODIFIED Requirements

### Requirement: OP_GLITCH_LEVEL_AT_LEAST predicate handler

`OP_GLITCH_LEVEL_AT_LEAST threshold` (op-code 17, added in Phase B #5) SHALL
evaluate true when `settings.logic >= threshold`. Phase B handled thresholds 0-2
(`NoGlitches` / `OverworldGlitches` / `MajorGlitches`). Phase D extends the
supported range to `HybridMajorGlitches` (3) and `NoLogic` (4).

**Tier structure (reconciled to ALTTPR `config/logic.php` at apply-time).** The
glitch levels are sets of per-technique flags, NOT a strict numeric chain. By
technique inclusion: `NoGlitches ⊂ OverworldGlitches ⊂ HybridMajorGlitches ⊂
MajorGlitches` — `MajorGlitches` is the *most* permissive. But the Phase-A enum
numbers `MajorGlitches=2 < HybridMajorGlitches=3`, so HMG is numerically above MG
yet a technique subset of it. Consequences for the monotone `>=` handler:

- **OverworldGlitches-group techniques** (canBootsClip, canFakeFlipper,
  canSuperSpeed, canMirrorClip, canWaterWalk, canDungeonRevive, canSuperBunny —
  enabled at OWG, HMG, MG) use `OP_GLITCH_LEVEL_AT_LEAST(overworld_glitches)`.
- **canOneFrameClipUW** (enabled at HMG and MG only) uses
  `OP_GLITCH_LEVEL_AT_LEAST(major_glitches)` — true at `logic ∈ {MG=2, HMG=3}`.
- **MajorGlitches-exclusive techniques** (canMirrorWrap, canOWYBA,
  canOneFrameClipOW, canTransitionWrapped — enabled at MG only) use
  `OP_GLITCH_LEVEL_AT_LEAST(major_glitches) AND (NOT
  OP_GLITCH_LEVEL_AT_LEAST(hybrid_major_glitches))`, which is true only at
  `logic == MajorGlitches`. This uses existing ops; no new op-code is added.

Because `HybridMajorGlitches ⊂ MajorGlitches`, **there are zero locations
reachable at HybridMajorGlitches but not at MajorGlitches.** Phase D therefore
authors NO bare `OP_GLITCH_LEVEL_AT_LEAST(hybrid_major_glitches)` location gates
(such a gate would open at HMG but close at the more-permissive MG — unfaithful).
HMG's only delta over OWG is `canOneFrameClipUW`, gated at threshold 2 above.

**NoLogic (threshold 4)**: when `settings.logic == NoLogic`, the predicate VM
SHALL short-circuit the **reachability** evaluation (`placement_context == 0`) —
every predicate evaluates true — so goal-completability and the per-tier
accessibility checks pass vacuously. Placement `can_place` predicates
(`placement_context == 1`) are NOT short-circuited, preserving dungeon-item
confinement so the seed stays structurally valid/loadable. The seed carries no
reachability guarantee.

When `logic == NoLogic`, the spoiler SHALL include a `fallback_warnings` entry:
`{"code": "no_logic_seed", "detail": "Seed generated with logic=NoLogic;
reachability is not enforced. The seed may be un-completable."}`. It is emitted
by reading `settings.logic` directly in the spoiler writer (the same pattern as
`unverified_tricks_enabled`), not via a placer counter.

#### Scenario: OWG-group technique opens at OverworldGlitches and above
- **WHEN** a location's reachability depends on an OWG-group technique macro
  (e.g. CanBootsClip) and the seed has `logic = overworld_glitches`
- **THEN** the technique disjunct evaluates true and the location is reachable
- **WHEN** the same seed has `logic = no_glitches` and no trick bit is set
- **THEN** the technique disjunct evaluates false

#### Scenario: canOneFrameClipUW gate opens at HMG and MG, closed at OWG
- **WHEN** a location gates on `OP_GLITCH_LEVEL_AT_LEAST(major_glitches)` (the
  canOneFrameClipUW tier) and the seed has `logic = hybrid_major_glitches`
- **THEN** the predicate evaluates true (HMG ⊇ {OWG + canOneFrameClipUW})
- **WHEN** the same predicate is evaluated with `logic = overworld_glitches`
- **THEN** it evaluates false

#### Scenario: MG-exclusive technique closed at HMG, open at MG
- **WHEN** a location gates on an MG-exclusive technique
  (`OP_GLITCH_LEVEL_AT_LEAST(major_glitches) AND NOT
  OP_GLITCH_LEVEL_AT_LEAST(hybrid_major_glitches)`) and `logic = major_glitches`
- **THEN** the predicate evaluates true
- **WHEN** the same predicate is evaluated with `logic = hybrid_major_glitches`
- **THEN** it evaluates false (HMG does not enable the MG-exclusive techniques)

#### Scenario: NoLogic short-circuit makes everything reachable
- **WHEN** a seed has `logic = no_logic`
- **THEN** every reachability predicate evaluates true; goal-completability
  returns true vacuously; the spoiler emits the `no_logic_seed` warning

#### Scenario: NoLogic disables un-completable refusal
- **WHEN** a seed has `logic = no_logic`
- **THEN** the strict refusal at `main.c` does NOT fire (reachability short-circuit
  means goal-completability and all accessibility tiers pass vacuously);
  generation succeeds; the spoiler warning surfaces the lack of guarantee

#### Scenario: NoLogic preserves placement confinement
- **WHEN** a seed has `logic = no_logic` and `dungeon_small_keys_mode = standard`
- **THEN** placement `can_place` predicates still evaluate normally, so dungeon
  small/big keys land in placeable dungeon slots and the seed round-trips through
  the slot save
