# Tasks: add-configurable-hint-options

This change depends on `enhance-rando-hints-v2`. Do not archive this change
before that dependency, and do not use this change's testing to close the
dependency's separate GCC/Clang or owner-gameplay gates. Hint policy remains
post-placement; any placement or sphere digest drift is a stop-and-diagnose
event rather than an automatic corpus rebaseline.

## 1. Specification, dependency, and compatibility baseline

- [x] 1.1 Validate this focused OpenSpec change strictly and reconcile it
      against the as-built `enhance-rando-hints-v2` implementation and eventual
      archived core specs before implementation and again before archive.
- [x] 1.2 Record byte-level baselines for canonical 31-byte Off/Balanced
      settings, 76-character share strings, settings hashes, and the current
      2/2 sidecar-v14 extension plus snapshot type-11 payload. Separately pin
      genuine generator version 156 and hint pair 1/1 through its signed race
      artifact.
- [x] 1.3 Freeze algorithm-1/schema-1 compatibility with the genuine signed
      generator-156 race artifact: all 24 legacy rows, assignments, encoded
      reveal text, canonical digest bytes, legacy spoiler shape, and strict
      refusal of policy/preview bits. Hints identity persistence first ships
      with 2/2 in v14/type 11, so no nonexistent 1/1 sidecar/snapshot fixture
      contract is claimed.
- [x] 1.4 Confirm canonical `[28]` bits 5..7, `[29]` bit 7, and `[30]` bits
      6..7 are still strict-required-zero on every current decode path. Refuse
      implementation if another active change has claimed any bit.

## 2. Policy model and canonical compatibility

- [x] 2.1 Add typed tile-coverage, paid-depth, and hint-mix fields plus one
      authoritative normalize/validate/profile-derivation API. Normalize
      enabled zero-tiles/zero-paid to true Off and clear every policy bit under
      Off.
- [x] 2.2 Implement the exact zero-default six-bit mapping from design D2.
      Keep `kSettingsCanonicalLen=31`, existing Off/Balanced canonical bytes and
      hashes byte-identical, and make every effective custom tuple hash-distinct.
- [x] 2.3 Exhaust all 64 raw bit combinations through canonical decode,
      normalization, encode, hash, copy, equality, default, malformed-input,
      and decode-encode identity tests.
- [x] 2.4 Extend settings text/CLI parsing with Off/Sparse/Balanced/Direct and
      `hint_tiles`, `hint_paid`, and `hint_mix`. Preserve legacy aliases, make
      component overrides order-independent, reject duplicates/unknown values,
      and refuse standalone Custom.
- [x] 2.5 Keep v2 share strings exactly 76 characters. Prove legacy strings
      restore byte-identically and old/current refusal paths cannot silently
      discard nonzero policy bits.
- [x] 2.6 Make ordinary presets preserve all hint fields. Make the explicit
      Race-safe utility preset set Balanced with a visible disclosure and add
      preset/share/INI round-trip coverage.

## 3. Algorithm-2 plan construction

- [x] 3.1 Add exact builder/race dispatch for frozen 1/1 and current 2/2.
      Reject crossed/zero/unknown pairs and prohibit fallback from a persisted
      pair to the current builder.
- [x] 3.2 Implement the four literal mix schedules and deterministic shortage
      backfill using only existing candidate kinds, template ids, complete-fit
      rendering, and semantic truth guards.
- [x] 3.3 For Variety, preserve the full algorithm-1 fact multiset, rendered
      text, and three paid queues for the same accepted input. Reorder only
      tile facts through the pinned five-band prefix scheduler.
- [x] 3.4 Derive one domain-separated seed-ranked permutation of the 15
      canonical physical tiles. Build the maximal mix deck before applying
      coverage/depth, retain nested prefixes, compact fact ids/assignments, and
      safely close scarcity gaps.
- [x] 3.5 Keep every paid fact exact and globally distinct. Fill queue positions
      in canonical round-robin service order and prove underfill never changes
      an earlier retained assignment.
- [x] 3.6 Extend only the algorithm-2 canonical digest stream with normalized
      policy bytes. Preserve algorithm-1 digest bytes exactly and independently
      recompute both algorithms in self-tests/tooling.
- [x] 3.7 Establish the configurable schema floor at generator 157 and land
      the rebased integrated branch and corpus at generator 160. Advance current
      hint identity to 2/2 without changing placement/sphere behavior.

## 4. Paid-depth-zero text and transactions

- [x] 4.1 Author complete original/US, German, and French pre-choice framing
      for each Storyteller/Fortune surface: no clue, exact visible price, and
      healing on acceptance. Preserve choice/control commands and validate
      complete fit before enabling depth zero.
- [x] 4.2 Route depth-zero services through the existing prepare/render/commit
      ownership without creating a fact latch or discovery. Keep decline and
      insufficient-funds paths non-mutating.
- [x] 4.3 Prove successful acceptance charges exactly once, shows the existing
      receipt where applicable, heals exactly as the service already does, and
      discovers/advances nothing.
- [x] 4.4 Cover repeated buffer loads, stale plan epoch, save/reload, snapshot
      replay, locale fallback, hints Off, exhaustion, and vanilla-slot
      byte-identity. Remove depth zero from selectable settings if any complete
      localized pre-choice flow cannot be encoded safely.

## 5. Persistence and lifecycle

- [x] 5.1 Keep sidecar format 14, extension size 278, exact hint offsets, and
      24 discovery bits unchanged. Reconstruct policy only from the persisted
      canonical settings.
- [x] 5.2 Keep snapshot type 11 at payload format 1 and exactly 41 bytes. Add
      no policy field, cursor, fact, assignment, or rendered text.
- [x] 5.3 Exercise generation, activation, normal save, native export, full
      spoiler, current race reveal, warm replay, and fresh-process cold replay
      for persisted 2/2 identity with exact digest equality. Separately validate
      exact 1/1 dispatch through the genuine signed generator-156 race artifact;
      no 1/1 sidecar/snapshot was ever released.
- [x] 5.4 Cover missing/malformed/crossed/unknown pair, legacy 1/1 persisted
      identity, digest mismatch, settings-policy mismatch, and teardown. Every
      failure disables only hints and never rewrites, upgrades, exports,
      perpetuates, or rerolls an existing slot.
- [x] 5.5 Verify discovery maps only to compact active facts and that pruned or
      absent sources cannot discover a bit, create a paid cursor, or inherit
      process-global state.

## 6. Spoiler and validator

- [x] 6.1 Add normalized profile/tile-count/paid-depth/mix metadata to the
      plan-level JSON and text spoiler while retaining every legacy per-row
      field and Murahdahla separation.
- [x] 6.2 Emit actual primary/reserve/fact/source counts for variable topology.
      Remove assumptions that every enabled plan has 18 primaries, six
      reserves, 15 tile assignments, or depth-three paid queues.
- [x] 6.3 Extend `check_hint_spoiler.py` to validate policy, legal topology,
      compact ids, source uniqueness, queue prefixes, exact paid facts,
      normalized Off, and an independently recomputed policy-bound digest over
      every JSON spoiler in its requested scope.
- [x] 6.4 Prove discovery, checked/resolved state, paid latches, and pending UI
      state never enter JSON/text spoilers or the canonical race stamp.

## 7. Native and in-game UI

- [x] 7.1 Move Hints into normal native Randomizer tab order after General and
      add explicit Next seed setup / Active-slot journal inner surfaces.
- [x] 7.2 Render named profile actions, tile/paid/mix controls, derived profile,
      exact capacity summary, paid-exact explanation, and paid-zero
      price/healing disclosure from bridge-owned pending settings.
- [x] 7.3 Remove the editable General Balanced checkbox and retain at most a
      read-only summary/link. Prove there is only one pending-policy write path.
- [x] 7.4 Keep the active journal bound only to active slot plan/discovery/race
      identity. Pending policy/race edits must not alter it; active race mode
      remains discovered-only and non-race full view remains confirmed.
- [x] 7.5 Add the compact in-game `HINTS <profile>` cycle and Tiles/Paid/Mix/
      Reset advanced page. Use the same normalization/settings API and cover
      focus, backing out, Custom derivation, reset, and Switch compile guards.
- [x] 7.6 Update tooltips/help and keyboard/controller navigation. Race mode
      remains orthogonal and F12 remains explicitly unrestricted and
      spoiler-bearing.

## 8. Automated validation and review

- [x] 8.1 Add fast exhaustive policy serialization/self-tests and a reviewed
      pairwise CLI/spoiler/lifecycle matrix covering every profile, mix,
      coverage, depth, race state, locale, scarcity class, and lifecycle seam.
      Exhaustive 64-state serialization plus policy, CLI, spoiler, race,
      locale, scarcity, and current 2/2 lifecycle matrices are green; the
      signed generator-156 artifact covers the frozen 1/1 race-reveal vector.
- [x] 8.2 Add adversarial maximal and scarce fixtures; reverse candidate and
      placement order; verify quotas/backfill, nested prefixes, stable retained
      pairs, compact ids, distinct facts, exact paid facts, and no unsupported
      necessity/barren claims.
- [x] 8.3 Run source/codegen/version guards, filtered hint/save/snapshot/UI
      self-tests, full randomizer self-test, spoiler validator, snapshot/slot
      path checks, `git diff --check`, and strict OpenSpec validation.
- [x] 8.4 Run clean MSVC Release plus GCC and Clang `-Werror` builds. Keep
      cross-platform algorithm-1 and algorithm-2 digest/text vectors
      byte-identical.
- [x] 8.5 Run the full corpus at generator 160. Confirm placement and sphere
      digests remain unchanged from integrated generator 159; diagnose any
      drift before considering a manifest update. Verify intended
      settings/hint/spoiler identity changes.
- [x] 8.6 Run independent fresh-eyes reviews of settings compatibility,
      deterministic scheduling, persisted-version dispatch, paid transaction
      ordering/localization, UI state ownership, spoiler/race surfaces, and
      spec/as-built consistency. Address findings and revalidate.

## 9. Owner gameplay disposition and closeout

The owner completed a focused manual pass, confirmed the reported vendor/menu
fixes, and explicitly accepted landing. The broader suggested matrix below is
recorded as owner-accepted residual manual risk, not as evidence that every
combination was played; automated validation owns those unexercised contracts.

- [x] 9.1 Accept residual manual coverage for every named/custom mix and
      5/10/15 tile tier.
- [x] 9.2 Accept residual manual coverage for every paid depth/service,
      save/reload, snapshot, sharing, isolation, and exhaustion combination.
- [x] 9.3 Accept residual manual coverage for paid depth zero across
      original/US, German, and French interaction paths.
- [x] 9.4 Accept residual manual coverage for legacy/non-default share-string
      round trips and pre-change-binary refusal.
- [x] 9.5 Accept residual manual coverage for current 2/2 save/load and
      warm/cold snapshot restoration; the signed generator-156 artifact is a
      separate automated 1/1 race-reveal contract.
- [x] 9.6 Accept residual manual coverage for pending-vs-active UI, non-race
      full view, discovered-only race behavior, and race-safe disclosure.
- [x] 9.7 Reconcile final source, tests, documentation, task evidence, and
      delta specs. Archive only after `enhance-rando-hints-v2` is archived and
      the owner explicitly accepts this gameplay matrix; merge/push remain
      separate authorized actions.
