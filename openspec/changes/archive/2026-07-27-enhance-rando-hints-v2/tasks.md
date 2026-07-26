# Tasks: enhance-rando-hints-v2

Work remains unarchived until automated validation and the focused owner
gameplay matrix are both complete. Hint selection is post-placement; any
placement or sphere digest movement is a stop-and-diagnose event, not an
automatic corpus rebaseline.

## 1. Specification and baseline

- [x] 1.1 Validate this focused change strictly and reconcile the current
      `randomizer-hints`, `randomizer-core`, `randomizer-placement`,
      `randomizer-save`, and `randomizer-native-window` contracts.
- [x] 1.2 Record a baseline for current hint assignments/spoiler fields, active
      slot regeneration, snapshot behavior, paid NPC payment/healing order,
      native race gating, and US/DE/FR dialogue encodings.
- [x] 1.3 Remove stale "stub/scaffold" implementation comments, correct the
      telepathic ids to `0xB4,0xB5,0xB8..0xBB,0xBE..0xC6`, exclude `0xC7`, and
      update the capability Purpose/documentation when the change archives.

## 2. Semantic plan and Balanced builder

- [x] 2.1 Define value-owned `RandoHintFact`, `RandoHintPlan`, and
      `RandoHintPersistedState` values; expose immutable source-assignment and
      24-bit discovery getters; bind paid presentations to an internal plan
      epoch; keep Murahdahla as a separate compatibility surface.
- [x] 2.2 Add one authoritative data/codegen layer for item/location hint
      categories and compact aliases. Preserve dungeon identity and multi-check
      suffixes; fail codegen/self-check on unknown lossy identities.
- [x] 2.3 Implement canonical candidate ordering and the dedicated
      domain-separated RNG stream. Build the 18-primary quota/backfill deck and
      six exact-useful reserves with no duplicate or order dependence.
- [x] 2.4 Assign 15 primary facts to canonical tiles, one primary head to each
      paid queue, and two reserve positions to each queue; safely underfill
      scarce/unrenderable pools.
- [x] 2.5 Rename player-facing mode 1 to Balanced while preserving value 1,
      byte 22, settings hash behavior, default, old aliases, and an optional
      source compatibility alias for `kHintsMode_On`.
- [x] 2.6 Implement render-to-scratch complete-fit templates, category/alias
      validation, and no-truncation acceptance. Remove ambiguous/numeric
      compatibility fallbacks from emitted facts.
- [x] 2.7 Add neutral original/US, German, and French templates for Off,
      unavailable/mismatched plans, unfilled sources, and paid exhaustion.
      Preserve byte-identical dialogue when no randomizer slot is active.
- [x] 2.8 Reconcile reviewed vanilla-dialogue redirects with neutral
      randomizer fallback while retaining live discriminator gates, dynamic
      placement resolution, Stumpy's choice command/reward flow, and
      story-fast-forward readability.

## 3. Versioned lifecycle, sidecar, and snapshot

- [x] 3.1 Implement `Rando_BuildHintPlan`, internal canonical SHA-256 digest,
      `Rando_HintPlanExportPersistedState`,
      `Rando_HintsImportPersistedState`, and `Rando_RebuildHints` for the
      current supported version pair. Ensure every failure clears only hint
      state and never falls back to another/current builder.
- [x] 3.2 Make generation, headless/race reveal, and native spoiler export pass
      explicit immutable plans to spoiler writing. Snapshot the native
      generation-time spheres/assignment inputs instead of consulting active
      globals.
- [x] 3.3 Advance sidecar format 13 to 14 and slot extension 238 to 278 bytes.
      Serialize/deserialize algorithm @238, text schema @240, digest @242, and
      discovery @274 with exact little-endian/layout tests.
- [x] 3.4 Write current identity plus zero discovery on generation, copy live
      discovery on normal game saves, and use the canonical empty plan under
      Off. Keep bits 24..31 zero.
- [x] 3.5 Rebuild after all activation overlays/assignments, verify the v14
      identity before installing discovery, and disable hints non-destructively
      for pre-v14/missing/unsupported/malformed/mismatched state.
- [x] 3.6 Add snapshot TLV type 11 with the exact 41-byte v1 payload. Scope it
      to an accepted type-1 state, defer build/validation until finish-load
      after runtime overlays, and cover warm and fresh-process cold replay.
- [x] 3.7 Clear plan, discovery, paid latches, and pending snapshot metadata
      symmetrically on deactivation, failed activation, vanilla load, slot
      switch, and replay failure.
- [x] 3.8 Correct normal paired-save ordering to the existing contract:
      stage SRAM, durably write sidecar discovery/checksum first, then write
      vanilla SRAM; propagate sidecar failure so it cannot silently persist
      spent rupees with stale discovery. Add interruption/failure/drift tests.
- [x] 3.9 Bump `kGeneratorVersion` and the corpus manifest version together.

## 4. Discovery journal and spoiler schema

- [x] 4.1 Commit tile discovery only after a complete active-plan render.
      Expose read-only fact/source/text/resolved getters; derive resolved state
      from the checked-location bitmap.
- [x] 4.2 Convert the native Hints panel to discovered-only rows with source,
      canonical complete human-readable fact text, and resolved marker. Show an
      honest unavailable state when plan validation failed.
- [x] 4.3 Add a session-local, explicitly confirmed "View all seed hints —
      placement spoilers" mode outside race mode. Prove it cannot mutate
      discovery, checked state, paid selection, or other gameplay state.
- [x] 4.4 Keep race mode discovered-only and omit the full-deck action. Preserve
      ordinary spoiler/reachability race protections while leaving F12
      intentionally unrestricted and clearly labeled spoiler-bearing.
- [x] 4.5 Preserve `npc`, `dialogue_id`, and `text` on each spoiler hint row;
      add typed fact/target/template/source/queue metadata and a plan-level
      algorithm/text-schema/digest/count object.
- [x] 4.6 Keep discovery/resolved state out of JSON/text spoilers and the race
      stamp. Retain Murahdahla as a separate compatibility row outside the
      24-fact primary/reserve counts.

## 5. Paid queue transactions

- [x] 5.1 Implement Prepare that selects and completely pre-encodes/latches a
      presentation plus idempotent Commit, carrying plan epoch, source, queue
      position, fact id, and transaction-owned scratch. Dynamically skip checked
      exact targets without discovering them.
- [x] 5.2 Add the presentation latch so repeated
      `Text_LoadCharacterBuffer` calls show the prepared fact until the owning
      source completes; stale/failed transactions cannot leak across sources,
      saves, snapshots, or slot changes.
- [x] 5.3 Migrate paid Storyteller subtypes 0/1/2/4 to their shared queue:
      render before charge, recheck/deduct 20 rupees exactly once, commit,
      present, then retain the existing `0xA0` healing state.
- [x] 5.4 Migrate Kakariko and Lake Hylia to one shared Fortune Light queue and
      Dark World to Fortune Dark. Preserve the visible price receipt, exactly
      one 10/15/20/30-rupee charge, decline/insufficient behavior, healing, and
      world-bit source identity.
- [x] 5.5 Make exhausted paid queues render localized "no new clues", retain
      normal price/healing service, commit no discovery, and never repeat an
      earlier fact.
- [x] 5.6 Preserve vanilla paid-handler ordering and messages in vanilla slots
      and hints-Off mode except for the scoped truthful neutral hint surfaces.

## 6. Automated coverage

- [x] 6.1 Add a complete 24-fact adversarial plan oracle and require identical
      canonical facts, assignments, US rendering, and digest across generation,
      sidecar activation, native export/restore, race reveal, warm snapshot, and
      fresh-process cold replay.
- [x] 6.2 Permute candidate and placement input order; cover quotas/backfill,
      safe underfill, distinct facts, reserve exactness, and no
      required/WotH/foolish claims.
- [x] 6.3 Exhaustively render every eligible item/location/template form with
      buffer canaries, complete terminators, dungeon qualification,
      multi-check suffixes, and no ambiguous/numeric fallback.
- [x] 6.4 Cover original/US, German, and French neutral text for Off,
      unavailable/mismatch, unfilled source, and exhaustion; cover
      vanilla-slot byte identity, Stumpy control commands, and cursor-safe
      choice prefixes for Stumpy and paid no-clue prompts.
- [x] 6.5 Cover sidecar v14 exact offsets/size, reserved discovery bits,
      empty-plan identity, round-trip, pre-v14 disable, unknown versions,
      malformed metadata, digest mismatch, paired sidecar-first save failure/
      interruption, and non-destructive slot playability.
- [x] 6.6 Cover snapshot type-11 exact bytes/count, missing/orphaned/duplicate/
      malformed records, accepted-overlay ordering, warm/cold parity, mismatch
      disable, and teardown.
- [x] 6.7 Cover discovery idempotence/persistence, checked-target resolved
      derivation, journal race gating, full-viewer confirmation, and every
      read-only path's non-mutation.
- [x] 6.8 Cover each paid source's decline, insufficient funds, Prepare/render
      failure, stale epoch, applicable refund, repeated buffer load, successful sequence,
      checked-target skip, save/reload, exhaustion, exactly-one charge, and
      healing. Pin Kakariko/Lake shared progression and Dark isolation.
- [x] 6.9 Cover backward-compatible spoiler fields, typed metadata/order,
      plan counts/digest, Murahdahla separation, discovery-free stamp equality,
      and Off's empty plan.

## 7. Validation, review, and documentation

- [x] 7.1 Run strict OpenSpec validation, source/codegen/version guards,
      `git diff --check`, and filtered hint/save/snapshot/UI self-tests.
- [x] 7.2 Run clean MSVC Release and GCC/Clang `-Werror` builds plus the full
      randomizer self-test and slot/snapshot paths.
- [x] 7.3 Run the full local corpus and confirm placement/sphere digests remain
      unchanged except for newly added coverage rows; verify race
      stamp/spoiler changes deliberately.
- [x] 7.4 Update `docs/randomizer.md`, settings/tooltips, spoiler schema docs,
      sidecar/snapshot layout docs, generator-version history, and contributor
      playtest guidance.
- [x] 7.5 Run independent fresh-eyes reviews of semantic truth, lifecycle
      identity, persistence compatibility, paid transaction ordering, locale
      encoding, race surfaces, and spec/as-built consistency; address concrete
      findings and revalidate.

## 8. Owner gameplay disposition and closeout

The owner completed a focused manual pass, reported the Bottle Merchant cursor/
rendering and King Zora staging defects, confirmed their fixes, and explicitly
accepted landing. The following broader suggested cases are recorded as
owner-accepted residual manual risk, not as a claim that each case was run;
their contracts remain covered by automation.

- [x] 8.1 Accept residual manual coverage for representative tile delivery,
      journal discovery, and resolved-state transitions.
- [x] 8.2 Accept residual manual coverage for every paid queue/depth,
      Kakariko/Lake sharing, save/reload, and snapshot choreography.
- [x] 8.3 Accept residual manual coverage for exhausted-service charge/heal and
      no-discovery behavior.
- [x] 8.4 Accept residual manual coverage for decline, insufficient-funds,
      Stumpy, Bumper Cave, and paid dialogue/control variants.
- [x] 8.5 Accept residual manual coverage for non-race full-view confirmation
      and discovered-only race UI; F12 remains intentionally spoiler-bearing.
- [x] 8.6 Accept residual manual coverage for neutral original/US, German, and
      French Off/unavailable-plan variants.
- [x] 8.7 Reconcile final source, tests, docs, and delta specs to as-built
      behavior. Archive, merge, and push only after owner approval.
