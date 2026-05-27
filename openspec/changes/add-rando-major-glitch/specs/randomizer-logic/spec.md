## MODIFIED Requirements

### Requirement: OP_GLITCH_LEVEL_AT_LEAST predicate handler

`OP_GLITCH_LEVEL_AT_LEAST threshold` (op-code 17, added in Phase B #5) SHALL evaluate true when `settings.logic >= threshold`. Phase B handled thresholds 0-2 (`NoGlitches` / `OverworldGlitches` / `MajorGlitches`). Phase D extends the supported range:

- **threshold 3 (`HybridMajorGlitches`)**: hybrid-major-glitch predicates (combinations of glitch techniques + community-specific routing) evaluate true only when `logic >= HybridMG`. Author per-location HybridMG predicates in `assets/rando/logic_parts/*.yaml` mirroring ALTTPR's per-Region PHP closures.
- **threshold 4 (`NoLogic`)**: when `settings.logic == NoLogic`, the predicate VM SHALL short-circuit — every `OP_GLITCH_LEVEL_AT_LEAST` predicate (and arguably every predicate of any kind in the reachability check) evaluates true. Placement becomes trivial; the seed has no reachability guarantee.

When `logic == NoLogic`, the spoiler SHALL include a `fallback_warnings` entry: `{"code": "no_logic_seed", "detail": "Seed generated with logic=NoLogic; reachability is not enforced. The seed may be un-completable."}`.

> **Stub status**: per-location HybridMG predicate set deferred to Phase D apply-time.

#### Scenario: HybridMG predicate unlocks location only with hybrid logic
- **WHEN** a location's predicate is `OP_GLITCH_LEVEL_AT_LEAST hybrid_major_glitches` and the seed has `logic = major_glitches`
- **THEN** the predicate evaluates false; the location is unreachable
- **WHEN** the same predicate is evaluated with `logic = hybrid_major_glitches`
- **THEN** the predicate evaluates true (assuming any other predicate conjuncts also pass)

#### Scenario: NoLogic short-circuit makes everything reachable
- **WHEN** a seed has `logic = no_logic`
- **THEN** every `OP_GLITCH_LEVEL_AT_LEAST` predicate evaluates true; goal-completability returns true vacuously; the spoiler emits the `no_logic_seed` warning

#### Scenario: NoLogic disables un-completable refusal
- **WHEN** a seed has `logic = no_logic`
- **THEN** the strict refusal at `main.c:482` does NOT fire (the spec's per-predicate short-circuit means goal-completability passes vacuously); generation succeeds; the spoiler warning surfaces the lack of guarantee
