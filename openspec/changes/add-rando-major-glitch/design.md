# Design — add-rando-major-glitch (Phase D)

Authored at apply-time (2026-06-07) from a fresh read of the ALTTPR PHP, the
shipped Phase B (`add-rando-trick-logic-and-axes`) foundation, and the fork's
current logic graph. Supersedes the proposal stub's effort framing.

## Context

Phase B (`add-rando-trick-logic-and-axes`, archived 2026-06-04) shipped the
predicate VM op-code `OP_GLITCH_LEVEL_AT_LEAST` (id 17, `rando_logic.c
eval_glitch`: `settings->logic >= level`) and un-pinned CSV/UI input for `logic`
values 0-2 (`NoGlitches`/`OverworldGlitches`/`MajorGlitches`). It left values
3 (`HybridMajorGlitches`) and 4 (`NoLogic`) rejected by the CSV parser and
clamped out of the UI. Phase D opens those two values, wires the NoLogic
reachability short-circuit, and refines the glitch gates toward ALTTPR fidelity.

The canonical `logic` field already exists at serialization position #7 (Phase
A); Phase D does **not** touch `kSettingsCanonicalLen`, byte positions, or field
widths — it only opens user input to the pre-declared enum values 3-4.

## STEP-0 reality map (ground truth from source, not memory)

### How ALTTPR actually models glitch tiers

ALTTPR has **no separate glitched Region files** and **no `canOWYBA` /
`canOneFrameClip*` / `canMirrorWrap` methods on `ItemCollection.php`** (only
`canSpinSpeed` at :435 and `canBunnyRevive` at :457 are methods). Glitch logic
is a set of **per-technique config flags** read inside the *same* Region
closures, e.g. `$this->world->config('canBootsClip', false)`. Each flag is
enabled by the logic level through `config('logic.{Level}.{key}')`
(`World.php:445-461`). The tier→flag table is **`config/logic.php`** — the single
ground-truth artifact:

| Technique flag        | OWG | HMG | MG | (fork enum)    |
|-----------------------|:---:|:---:|:--:|----------------|
| canSuperBunny         |  ✓  |  ✓  | ✓  | OWG group      |
| canFakeFlipper        |  ✓  |  ✓  | ✓  | OWG group      |
| canSuperSpeed         |  ✓  |  ✓  | ✓  | OWG group      |
| canBootsClip          |  ✓  |  ✓  | ✓  | OWG group      |
| canMirrorClip         |  ✓  |  ✓  | ✓  | OWG group      |
| canWaterWalk          |  ✓  |  ✓  | ✓  | OWG group      |
| canDungeonRevive      |  ✓  |  ✓  | ✓  | OWG group      |
| canOneFrameClipUW     |     |  ✓  | ✓  | HMG+MG group   |
| canMirrorWrap         |     |     | ✓  | MG-exclusive   |
| canOWYBA              |     |     | ✓  | MG-exclusive   |
| canOneFrameClipOW     |     |     | ✓  | MG-exclusive   |
| canTransitionWrapped  |     |     | ✓  | MG-exclusive   |

Source: `../alttp_vt_randomizer/config/logic.php:4-38`.

### The non-monotonic-enum problem (load-bearing)

By technique inclusion: **NoGlitches ⊂ OverworldGlitches ⊂ HybridMG ⊂
MajorGlitches** — MajorGlitches is the *most* permissive. But the fork's Phase-A
enum is `NoGlitches=0 < OverworldGlitches=1 < MajorGlitches=2 <
HybridMajorGlitches=3 < NoLogic=4`. So **HMG(3) is numerically *above* MG(2) yet
a technique *subset* of it.**

`OP_GLITCH_LEVEL_AT_LEAST` is a monotone `logic >= threshold` test. It can
express "enabled at OWG+" (`>=1`) and "enabled at HMG+MG" (`>=2`, since the only
non-short-circuited levels ≥2 are {MG=2, HMG=3}), but it **cannot** express "MG
but not HMG" with a single threshold. The four MG-exclusive techniques need:

```
OP_GLITCH_LEVEL_AT_LEAST(2) AND (NOT OP_GLITCH_LEVEL_AT_LEAST(3))
```

which is true only at `logic == 2` (MajorGlitches). This uses *existing* ops
(`OP_AND`, `OP_NOT`, `OP_GLITCH_LEVEL_AT_LEAST`) — no new op-code, no canonical
change. (At `logic==4` NoLogic this composite is false, but NoLogic
short-circuits reachability entirely, so it never matters.)

### Faithful per-technique macro gates (the codegen-DSL form)

| Technique         | Enabled-at      | Predicate                                                           |
|-------------------|-----------------|--------------------------------------------------------------------|
| OWG-group (7)     | OWG, HMG, MG    | `OP_GLITCH_LEVEL_AT_LEAST(overworld_glitches)`                      |
| canOneFrameClipUW | HMG, MG         | `OP_GLITCH_LEVEL_AT_LEAST(major_glitches)`                          |
| MG-exclusive (4)  | MG only         | `OP_GLITCH_LEVEL_AT_LEAST(major_glitches) AND (NOT OP_GLITCH_LEVEL_AT_LEAST(hybrid_major_glitches))` |

### HMG has no exclusive locations (honest scoping)

Because HMG ⊂ MG, **there are zero locations reachable at HMG but not at MG.**
The proposal/spec stub framing — "author the HybridMG-specific predicate set for
locations that ONLY become reachable under hybrid major-glitch logic" — is
mis-specified relative to ALTTPR. HMG's only delta over OWG is
`canOneFrameClipUW`; every location it opens, MG also opens (MG ⊇ HMG). So Phase
D does **not** author "HMG-only" gates. Instead it ensures the `canOneFrameClipUW`
technique gate is `>=2` (open at both HMG and MG) and the MG-exclusive gates are
`>=2 AND NOT >=3` (closed at HMG). The spec delta is reconciled to say this.

### Where the fork stands today (the collapse to un-do)

- The fork modeled `canBootsClip` / `canFakeFlipper` / `canBunnyRevive` /
  `canOWYBA` as **independent trick toggles** (`OP_TRICK` bits 0/1/2/5), *not*
  as glitch-level flags. Macros `CanBootsClip` / `CanFakeFlippers` /
  `CanBunnyRevival` / `CanPearlBypass` gate on `OP_TRICK(...)` only.
- All `OP_GLITCH_LEVEL_AT_LEAST` *level* gates in the graph are at
  `major_glitches` (tier 2); there are **zero `overworld_glitches` (tier 1)
  gates**. So `logic=OverworldGlitches` is presently a placement **no-op**
  (byte-identical to NoGlitches), and the raw `major_glitches` threshold stands
  in for `canOneFrameClipOW` (and similar) at ~40 sites.
- Consequences: an OWG seed reaches nothing extra; an MG seed only opens the
  raw-threshold sites, **not** the boots-clip / OWYBA / bunny-revive sites (those
  stay closed unless the matching *trick* bit is set). This is unfaithful (too
  strict) — ALTTPR's MG enables all of them.

Sizing the grind: 7 OWG-group techniques + 1 HMG+MG + 4 MG-exclusive = 12
techniques. 4 already have fork trick-macros (boots-clip, fake-flippers,
bunny-revival, pearl-bypass=OWYBA). ~40 raw `major_glitches` threshold sites
need per-site technique classification (which of canOneFrameClipOW /
canOneFrameClipUW / canMirrorClip / canWaterWalk / canMirrorWrap / ... each
stands for) to split OWG-group vs HMG+MG vs MG-exclusive. The macro-fold
(below) handles the 4 trick-mapped techniques across **all** their sites at once
(class-driven); the raw-threshold per-site reclassification is the larger
frontier.

## Decisions

### D1 — NoLogic short-circuits the REACHABILITY eval only (not placement)

`Predicate_EvalCtx` (`rando_logic.c`) is the single funnel for both reachability
(`Predicate_Evaluate`, `placement_context==0`) and placement `can_place`
(`Predicate_EvaluatePlacement`, `placement_context==1`). When
`settings->logic == NoLogic(4)` **and** `placement_context == 0`, return `true`
before evaluating bytecode.

Effect: reachability/spheres/`Goal_IsCompletable`/`Accessibility_SeedAcceptable`
all see a fully-open world → `goal_completable` is vacuously true →
`Goal_ShouldRefuse` is false → the `main.c` strict refusal does not fire
(spec scenario "NoLogic disables un-completable refusal"). Matches ALTTPR
`World.php:93` (regions not initialized under NoLogic) and `:396` / `Randomizer.php:325`.

**Placement `can_place` predicates are left evaluating normally** (the minimal
spec-faithful reading). Rationale: the spec's normative scenarios require only
that reachability/goal-completability pass vacuously and the seed generate +
write a spoiler — they say nothing about confinement. Keeping `can_place` live
preserves the placer's structural invariants (dungeon small/big keys stay in
placeable dungeon slots), so a NoLogic seed remains **structurally valid and
loadable** at runtime even though progression is un-gated. Short-circuiting
`can_place` too ("items literally anywhere") risks ungrantable runtime keys and
is *not* required by the spec; deferred. Documented trade-off, not an oversight.

Determinism: the short-circuit fires only at `logic==4`, which no pre-existing
corpus seed uses → **zero non-NoLogic digests move.** The NoLogic placement
itself changes (the point), captured by a new corpus entry.

### D2 — Enum un-pin

CSV (`rando_settings.c`): split the Phase-B reject into
`HybridMG|hybrid_mg|hybrid_major_glitches|3 → logic=3` and
`NoLogic|no_logic|4 → logic=4`. UI (`rando_window.cpp`): `kLogicLabels[]`
grows to 5, the `if (s->logic>2) s->logic=2` clamp is removed, `EnumCombo` count
3→5, tooltip warns that NoLogic enforces no reachability and HMG/MG expect deep
tech that is US-1.0-unverified.

### D3 — `no_logic_seed` spoiler warning

Emit `{"code":"no_logic_seed","detail":"Seed generated with logic=NoLogic;
reachability is not enforced. The seed may be un-completable."}` in
`rando_spoiler.c`'s `fallback_warnings[]` when `settings->logic == NoLogic`.
Modeled on `unverified_tricks_enabled`, which reads `s->settings` **directly**
(not via a placer counter — the proposal's "placer-warning-counter path"
parenthetical is inaccurate to the as-built; reconciled). The spec's `code`/`detail`
shape differs from the existing warnings' `kind` shape; emitted exactly as the
spec dictates for this one entry.

### D4 — Glitch-technique macro fold (OWG/MG un-collapse, class-driven)

Fold the glitch level into the four existing trick macros as an OR-branch, so
every use site becomes tier-aware at once (the CLAUDE.md "class-driven fix across
all sites" pattern, lower transcription risk than per-location edits):

| Macro          | ALTTPR flag        | New body                                                                                      |
|----------------|--------------------|-----------------------------------------------------------------------------------------------|
| CanBootsClip   | canBootsClip (OWG) | `(OP_TRICK(boots_clip) OR OP_GLITCH_LEVEL_AT_LEAST(overworld_glitches)) AND HAS_ITEM(Boots)`   |
| CanFakeFlippers| canFakeFlipper(OWG)| `OP_TRICK(fake_flippers) OR OP_GLITCH_LEVEL_AT_LEAST(overworld_glitches)`                       |
| CanBunnyRevival| canBunnyRevive(OWG)| `(OP_TRICK(bunny_revival) OR OP_GLITCH_LEVEL_AT_LEAST(overworld_glitches)) AND CanBunnyRevive(world)` |
| CanPearlBypass | canOWYBA (MG-only) | `(OP_TRICK(pearl_bypass) OR (OP_GLITCH_LEVEL_AT_LEAST(major_glitches) AND (NOT OP_GLITCH_LEVEL_AT_LEAST(hybrid_major_glitches)))) AND HasABottle()` |

Digest impact (all justified by `config/logic.php`):
- `logic=0` (NoGlitches) + any `tricks`: OWG/MG branch false → macros reduce to
  the trick-only form → **byte-identical** (verified by NoGlitches + b-tricks-*
  corpus entries staying put).
- `logic=1` (OWG): boots-clip/fake-flipper/bunny-revive now in logic → digest
  **moves** (faithful: ALTTPR OWG enables them).
- `logic=2` (MG): all four now in logic → digest **moves** (faithful: ALTTPR MG
  enables all of them; the fork previously left them trick-gated-only).
- `logic=3` (HMG): boots/fake/bunny open (OWG ⊂ HMG ✓); OWYBA stays closed
  (MG-exclusive, correct).

`CanDarkRoomNav` is **not** folded — ALTTPR's dark-room bypass is the
`item.require.Lamp=0` config, not a `logic.php` flag. `CanBombJump` /
`CanHookshotClip` / `CanLobotomy` are fork-invented placeholders with no ALTTPR
flag and zero predicate uses — untouched.

The ~40 raw `OP_GLITCH_LEVEL_AT_LEAST(major_glitches)` threshold sites are **not**
reclassified in this pass (frontier — needs per-site PHP technique ID). They
stay at `>=2`, which over-reaches at HMG for MG-exclusive techniques. Documented:
HMG currently equals MG reachability at those specific sites; the boots/bunny
fold already makes HMG correct everywhere those techniques provide a path.

### D5 — ROM-version status

ALTTPR targets JP 1.0; the fork is US 1.0. `kRandoGlitchLevelStatus[]` already
flags tiers 1-3 as `untested-on-us10`; tier 4 (NoLogic) is added with its own
`untested-on-us10` status (and the dedicated `no_logic_seed` warning). The
existing `unverified_tricks_enabled` spoiler warning fires for every reached
tier whose status is unverified. All newly-meaningful tiers default to
`unverified` until playtest confirms performability on US 1.0.

## Validation strategy

Headless validates **reachability** (a tier seed generates, is completable, and
the right locations open at the right tier per `config/logic.php`). Whether a
glitch is physically **performable on US 1.0**, and whether a NoLogic seed is
actually **beatable**, is playtest-pending — exactly what NoLogic's no-guarantee
+ `rom_version_status=untested-on-us10` flag. Every behavior-changing step:
clean build (0 warn), `--rando-selftest`, corpus, and the guard suite; every
placement-affecting step gets a kGen bump + corpus regen + a 3-way diff
classification (logic=0 unchanged; logic≥1 each PHP-cited).

## Risks / trade-offs

| Risk | Mitigation |
|---|---|
| NoLogic short-circuit leaks into a non-NoLogic path | Gated on `logic==4 && placement_context==0`; corpus proves no logic<4 digest moves. |
| Macro fold over-reaches a tier | Each technique's tier is read from `config/logic.php`; fold is OR-INSIDE the conjunction (CLAUDE.md invariant), dead at logic=0. |
| Raw-threshold HMG over-reach | Left as documented frontier; honest scoping (HMG⊂MG means net reachability already correct where an OWG path exists). |
| US-1.0 performability unknown | All tiers `untested-on-us10`; NoLogic carries `no_logic_seed`; playtest list in report. |
