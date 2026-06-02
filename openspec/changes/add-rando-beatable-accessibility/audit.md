# Audit log — add-rando-beatable-accessibility

## Fresh-eyes audit 2026-06-01 (archive-readiness)

Reviewed: proposal.md, specs/randomizer-core + randomizer-ui deltas,
`src/rando/rando_placement.c` (`Accessibility_SeedAcceptable`,
`accessibility_reachability_ok`, `is_progression_item`, `Goal_ShouldRefuse`,
`Logic_ComputeSpheres` index correspondence, Placement_SelfCheck),
`src/rando/rando_generate.c` (both slot paths), `src/main.c` (CLI gates),
`src/rando/rando.h` (genVer 46), and the spoiler/guard cleanup.

Verified clean:
- `kGeneratorVersion` bumped 45→46 with a descriptive comment. Correct per the kGen-bump contract.
- Nesting `locations ⊇ items ⊇ beatable` is implemented correctly: every tier first requires `Goal_IsCompletable`; `none` returns true immediately (no sphere walk); `locations` requires `unreachable_count == 0`; `items` requires every progression placement reachable.
- `sphere_index_by_placement[i]` is indexed by placement index `i` over `placements->count` both in `Logic_ComputeSpheres` (populate) and `accessibility_reachability_ok` (consume) — 1:1 correspondence confirmed, no index skew.
- BOTH slot-generation paths in rando_generate.c are now gated (entrance loop at :274 per-attempt; non-entrance at :289). The previously-ungated non-entrance path is fixed. This is the no-automated-test slot path, so the gate placement matters — it is correct.
- CLI: entrance path gates per-attempt (:741); non-entrance path is caught by the final `Goal_ShouldRefuse` gate before spoiler write (:848), honoring `--allow-broken-seed`. Consistent.
- Entrance overrides remain active across the `Accessibility_SeedAcceptable` call in every loop (applied before Place_AssumedFill), so the tier check sees shuffled reachability. Failure path clears overrides (rando_generate.c:297, main parity).
- `accessibility_none_seed` warning fully removed from code (rando_spoiler.c:205 note, check_rando_invariants.py:53 note). No live emission remains.
- `is_progression_item` classifier is reasonable: progressives/weapons/bottles/magic/keys/prizes = progression; maps/compasses/rupees/junk/hearts = not. Matches ALTTPR "100% inventory."
- audit-guard / determinism / codegen-wiring all PASS.

### NEW findings

**LOW — `Goal_ShouldRefuse(NULL, ...)` and `Accessibility_SeedAcceptable(NULL, ...)` disagree on NULL settings.**
`rando_placement.c:1614` returns *false* (not acceptable) for NULL settings, but
`Goal_ShouldRefuse` (`:1633`) returns *false* (do NOT refuse) for NULL settings — i.e.
a NULL-settings seed would be shipped by the refuse-gate yet judged unacceptable by the
predicate it claims to negate. The comment calls `Goal_ShouldRefuse` "a thin negation"
but the NULL branch breaks that. No live caller passes NULL settings, so this is
unreachable in practice; flagged only because the "thin negation" invariant is now false.
Suggested fix: make `Goal_ShouldRefuse` `return settings == NULL ? true : !Accessibility_SeedAcceptable(...)` (refuse on NULL), or drop its separate NULL branch entirely.

**LOW — Stale doc reference to the deleted warning (out of audit scope to fix).**
`docs/randomizer_phase_b.md:55` still describes `fallback_warnings: [{kind: accessibility_none_seed}]`
as live behavior. The code and `check_rando_invariants.py` both removed it; this doc line
is now wrong. (proposal.md lists `docs/randomizer.md` as the doc to update but this is the
phase-b doc.) Not a code bug; sweep when convenient.

### Verdict
Archive-ready (audit-wise). No HIGH/MED. Two LOW: an unreachable NULL-settings
inconsistency and a stale phase-b doc line. The slot path (no automated test) is
correctly gated on both branches — confirm with the standard slot playtest +
`saves/sram_rando.dat` diff per CLAUDE.md, especially a `beatable only` seed that
intentionally strands a non-progression item.
