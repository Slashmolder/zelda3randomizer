# Tasks: add-rando-random-crystals

Single phase on `feature/rando-shopsanity` (owner-directed convergence
branch). Standing traps: `make clean` after header edits; explicit-path
staging; absolute worktree paths.

## 1. Settings plumbing

- [x] 1.1 `rando_settings.h`: `kCrystalsRandom = 8` enum + field comments
      (bytes [2]/[3] gain the sentinel note).
- [x] 1.2 `rando_settings.c`: `Settings_Validate` + deserialize bounds widen
      to `> kCrystalsRandom`; CSV `crystals.ganon=random` /
      `crystals.tower=random` KEYWORD-ONLY (numeric 8 stays refused — an
      existing selfcheck vector pins "8 refused" as the CSV contract; the
      sentinel is canonical-byte-level only); `Settings_SelfCheck`
      round-trip vector for the sentinel.

## 2. Resolver

- [x] 2.1 `Crystals_Resolve(s, seed_u64, *ganon, *tower)` in
      rando_placement.c + rando_placement.h — per-axis salted streams
      (`"CrystlRq"` ^ 'G'<<56 / 'T'<<56), fixed axes pass through.
- [x] 2.2 `Crystals_ResolveSelfCheck`: determinism, 0..7 range, per-axis
      independence, pinned vector (capture at first build); register in
      `Rando_RunAllSelfChecks`.

## 3. Generation choke point

- [x] 3.1 `Goal_IsCompletable` gains `uint64 seed_u64` (BASE seed — the
      two retry-loop call sites at rando_placement.c:1935/1955 MUST pass
      `seed_u64`, never `attempt_seed`; comment per the key-rings/boss
      base-seed precedent at :1765-1785). The seedless wrappers
      `Accessibility_SeedAcceptable` (:3365) and `Goal_ShouldRefuse`
      (:3385) grow the same parameter; fix all compiler-enumerated callers
      (rando_generate.c ×4, main.c, reveal, selfcheck nets — all hold the
      base seed).
- [~] 3.2 Generation-side selfcheck: a both-random `goal=ganon` seed fills,
      completes, 0 unreachable; PLUS the base-seed pin — the resolver value
      for the accepted table of a retry-exercising fixture equals
      `Crystals_Resolve(s, base_seed, ...)`.

## 4. Runtime cache + reveal

- [x] 4.1 `Rando_ActivateSidecarSlot` resolves + caches
      `g_rando_effective_crystals_ganon/_tower`; getters
      `Rando_EffectiveCrystalsGanon()/Tower()` (fail-closed 7/7 when no
      slot); deactivation resets.
- [x] 4.2 Route ALL five read sites through the getters: the two gate
      helpers, the zero-crystal GT pre-open, `rewrite_ganon_crystal_warning`
      (in-world reveal), `at_append_settings` (tracker gets RESOLVED
      values — deliberate divergence from the spoiler's requested field,
      documented).
- [x] 4.3 Rework `Rando_CrystalGateSelfCheck` (rando.c:8713 — it mutates
      `g_rando_active_settings.crystals_*` directly, invisible to the
      cache): poke the cache via a selfcheck-only setter, keep the
      gate-reaction assertions, add a sentinel prong (cache == fresh
      `Crystals_Resolve`).
- [x] 4.4 Cache population lands INSIDE the settings-valid block of
      `Rando_ActivateSidecarSlot` (deserialize + Share_Decode both
      succeeded); deactivation resets to 7/7.

## 5. Spoiler + UI + docs

- [x] 5.1 Spoiler: `crystals_*_resolved` + `crystals_salt_version: 1` when
      either axis is random (requested bytes unchanged).
- [x] 5.2 Native window: sliders 0..8, value 8 renders "Random"; tooltip.
      In-game (Switch) rows: cycle to 8, "RAND" token.
- [x] 5.3 `docs/randomizer.md`: settings-table values gain `random`;
      short section (sentinel semantics, in-world reveal via Ganon's
      dialogue, tracker/spoiler behavior).

## 6. Validation

- [x] 6.1 Corpus rows: random-ganon, random-tower, both-random
      (fast_ganon), both-random `goal=ganon`, plus one row seeded to need
      `retry_attempts > 1` (the base-seed pin); random-TOWER rows are
      placement-byte-identical to fixed-tower twins by design (GT edges
      hardcode 7 — note in row comments); recapture; all existing rows
      byte-identical.
- [x] 6.2 WSL gcc + MSVC `-Werror` clean; selftest green both binaries;
      slot-path guard; MSVC==WSL digest + resolved-count parity on a
      random-crystals seed.

## As-built notes (post-implementation-review, 2026-07-17)

- Review F1 (HIGH, FIXED): the snapshot cold-replay restore is a THIRD
  settings_valid writer and never resolved the crystal cache — a replay
  desync even for fixed counts once the five consumers moved to the cache.
  Crystals_Resolve now runs in Rando_SnapshotColdReplayRestore; deactivation
  resets the cache to 7/7 (F4).
- Review F2 (FIXED): pinned resolve vector added — seed 0x1234 both-random
  = (ganon 1, tower 3); salt/stream drift now dies in the selfcheck.
- Review F3 (PARTIAL — 3.2 stays [~]): the retry-pin corpus row moved to
  accessibility=none (the items tier made the crystal conjunct insensitive
  to the count, so the row could not catch a base-vs-attempt-seed
  mutation). A fully-forcing generation fixture (sphere-stranded crystals
  bracketing two resolved counts) was NOT built; the base-seed rule is
  enforced by the call-site comments + reviewer verification + this row's
  improved (not guaranteed) sensitivity. Candidate follow-up for the next
  audit round.
- Review F5 (FIXED): the spec delta's "and numeric 8" reconciled to the
  shipped keyword-only contract.
