# Audit log — add-rando-race-mode-reveal

## Fresh-eyes audit 2026-06-01 (archive-readiness)

Reviewed: proposal.md, `src/rando/rando_spoiler.c` (`compute_stamp`, `Spoiler_Write`,
`write_spoiler_json_stream`, `Spoiler_ReadSuppressed`, suppressed file format),
`src/rando/rando.c` (`Rando_RevealSpoiler`), `src/rando/rando_generate.c` (the
generate-time spoiler that feeds the stamp), `src/main.c` (`--race-mode`,
`--reveal-spoiler`, effective_budget), the corpus reveal round-trip in
`assets/scripts/run_rando_corpus.py`, and `tests/rando_corpus/manifest.yaml`.

Verified clean:
- Stamp normalization is symmetric on writer (`compute_stamp`: race_mode=0,
  generation_wall_clock_ms=0, forward_fill_fallback_count=0, retry_attempts=1) and
  reveal (`regen` sets the identical three constants + race_mode=0). Both serialize
  via the same `write_spoiler_json_stream` (Spoiler_WriteJson is a thin wrapper), so
  byte-for-byte JSON is expected to match — for the no-entrance case.
- `effective_budget = race_mode ? 0 : budget_seconds` (main.c:681) matches reveal's
  `Place_AssumedFill(..., budget_seconds=0, ...)`, so placement is deterministic
  across generate/reveal for plain seeds.
- Reveal regenerates hints (`Rando_GenerateHints`, rando.c:2078) so hints=on seeds
  round-trip — good catch already wired.
- Idempotency: a second reveal sees first byte '{' and returns Ok without rewrite.
- Atomic write (partial+rename) and `.reveal-tmp` cleanup via `goto fail` are correct;
  stamp mismatch leaves the suppressed file untouched.
- Suppressed-file CRC32 over bytes 0..133, magic ZRSR, version gate all present.
- determinism / audit-guard / codegen-wiring PASS.

### NEW findings

**HIGH — Reveal of a race-mode + entrance-shuffle seed ALWAYS fails with a false
"tampered" stamp mismatch.**
File: `src/rando/rando.c:2085-2096` (the `regen` RandoSpoiler) vs.
`src/rando/rando_generate.c:366-378` (generate-time spoiler) and
`src/rando/rando_spoiler.c:264-268` (`Entrance_WriteSpoilerJson` /
`Entrance_WriteDungeonSpoilerJson` emit an `entrance_mapping` section).

The generate-time spoiler that feeds `compute_stamp` populates `entrance_assign`,
`dungeon_assign`, `cross_assign`, `decoupled_assign`, `dun_decoupled_assign`,
`cross_decoupled_assign`. When entrance shuffle is on, the stamped JSON therefore
includes the `entrance_mapping` / `dungeon_entrance_mapping` sections.

`Rando_RevealSpoiler` builds `regen` with `memset(...,0)` and sets only
share_string / seed / version / settings / placements / spheres / goal_completable /
the 3 normalization fields. It leaves every `*_assign` pointer NULL and every
`*_count` 0, and it never re-runs the entrance-attempt loop (it calls
`Place_AssumedFill` directly at :2062). So the regenerated JSON OMITS the
entrance_mapping sections that are present in the stamped JSON → the SHA-256 differs →
`kRandoReveal_StampMismatch`, surfaced to the user as
"Stamp mismatch — spoiler may have been tampered with."

Why it's a bug vs. baseline: a NON-race-mode entrance-shuffle seed writes the full
JSON directly at generate time (no regeneration), so it is correct — the failure is
reveal-specific. race_mode and shuffle_cave_entrances / shuffle_dungeon_entrances are
independent, un-pinned user settings with no mutual exclusion (verified: no guard in
rando_settings.c / main.c), so this combination is reachable for any tournament that
seeds an entrance-randomized race — the exact target use case.

Worse, even if the entrance sections were re-emitted, the reveal does not know the
*accepted entrance attempt* (the entrance loop tries attempts 0..N and accepts the
first that passes accessibility; that attempt index is stored in the SIDECAR slot, NOT
in the suppressed ZRSR file). And because reveal skips the entrance loop entirely, an
entrance permutation that changed reachability-driven placement would also make the
regenerated *placement* differ — a second source of mismatch.

Why the corpus misses it: `tests/rando_corpus/manifest.yaml` has race_mode entries
(:189,:330,:335,:340) and entrance-shuffle entries (:349+), but NONE combine the two,
so the corpus reveal round-trip is green. This is precisely the CLAUDE.md pattern —
corpus + selftest pass, only end-to-end catches it.

Suggested fix direction (pick one):
  1. **Block the combination** for now: refuse race_mode when any entrance-shuffle
     axis is set (CLI error + UI gating + a Settings validation rule), and add a
     manifest note. Smallest, archive-unblocking. Document as a known limitation.
  2. **Store the accepted entrance attempt in the ZRSR file** (it has reserved space
     after settings_canonical, or bump the format), then at reveal re-run the
     `Entrance_Compute*` functions for that attempt, apply the overrides before
     `Place_AssumedFill`, and populate the `regen.*_assign` pointers exactly as
     rando_generate.c does. This is the faithful fix but touches the file format and
     reveal flow.
Add a race_mode + shuffle_cave_entrances corpus entry once fixed so the round-trip
covers it.

### Verdict
BLOCKED on the HIGH above. The reveal round-trip is correct for plain / prize /
medallion / boss-drop-shuffle / hints seeds (those only affect the meta block, which
the reveal reproduces from settings), but it silently and falsely reports tampering
for any race-mode + entrance-shuffle seed. Either gate the combination out (fast) or
regenerate entrance assignments at reveal (faithful) before archiving. After the fix,
add the missing combined corpus entry and re-run the round-trip.

---

## Resolution 2026-06-01 — HIGH fixed (shared entrance-regen)

Took **fix direction 3** (cleaner than the two the audit listed — no ZRSR format
change): factored the generate-time entrance-attempt loop into a shared
`Rando_PlaceWithEntrances` + `Rando_SpoilerSetEntranceFields`
(`src/rando/rando_generate.{c,h}`), called by BOTH `Rando_GenerateSlot` and the
reveal path (`Rando_RevealSpoiler`, `src/rando/rando.c`). Reveal now re-runs the
identical deterministic loop (budget 0, matching race-mode generation), finds the
same accepted attempt + π, and repopulates `regen.*_assign` before stamping — so
the regenerated JSON's `entrance_mapping` section is byte-identical to the
generate-time stamped JSON. No file-format change; no stored attempt needed
(the loop is deterministic from seed+axes).

- The accepted π's LOGIC overrides are left active by the helper; for an active
  entrance slot this re-derives the SAME π, so it restores (not pollutes) the
  tracker override state, and the in-binary reveal is post-game-gated anyway. The
  gameplay door overlay is never touched (helper applies only logic overrides).
- The slot path refactor is behavior-preserving: corpus 67→**68/68** OK
  (placement digests byte-identical; the CLI generate path #1 was left untouched).
- **Regression guard added**: manifest entry `c-entrance-caves-open-fg-race`
  (race_mode + shuffle_cave_entrances) exercises the reveal-with-entrance ZRSR
  round-trip; it FAILS on the old code and PASSES now.
- **End-to-end verified**: `--generate-seed --race-mode --settings=...` then
  `--reveal-spoiler` returns OK for race + {none, cave, dungeon, cross} entrance
  combinations (all StampMismatch before the fix).

Verdict: **UNBLOCKED** (audit-wise). Remaining for archive = the change's own
playtest/CI tasks.
