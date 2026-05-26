# Lessons from spec authoring

This change went through ten rounds of expert critical review before reaching its current state. The errors caught fell into three categories; the third is preventable and worth capturing so future spec work on this codebase doesn't repeat it.

## Claim-grounding for upstream projects

**The rule**: when an external project is referenced in the spec — license, file paths, naming conventions, behavior, line numbers — claims MUST be grounded in the actual source before they land in a committed artifact. Memory-based assertions about external projects are the single largest source of avoidable error in this conversation's history.

**Concrete failures from this session**:
- Asserted "ALTTPR is GPL-3.0" → actually MIT. Caught by the user, who had the LICENSE file checked out at `../alttp_vt_randomizer/LICENSE`.
- Asserted "ALTTPR uses Mersenne Twister" → actually CSPRNG (`random_int()`); deeper truth is database-storage reproducibility (`Seed.php`). Caught by the user asking "doesn't PHP's built-in RNG use MT?"
- Asserted "Fat Fairy is an ALTTPR location" → doesn't exist. Caught by grep.
- Asserted "Pyramid Fairy is 2 locations" → actually 4 (Sword, Bow, Left, Right).
- Asserted "`z3-randomizer-logic`" as a community upstream → unverified, likely fictional.
- Asserted goal names in PascalCase (`FastGanon`) → actually snake_case + hyphen (`fast_ganon`, `triforce-hunt`).
- Asserted setting key `item_pool_difficulty` → actually `item_pool`.
- Asserted `pyramid_bow_upgrade` is enum → actually bool.
- Asserted 216 baseline locations → actually ~212.
- Asserted `pieces_required=20, pieces_placed=30` defaults → couldn't be confirmed in `config/alttp.php`.

Every one of these would have been caught by a 30-second grep against `../alttp_vt_randomizer/` at authoring time. The checkout was visible throughout.

**Practical discipline**:
1. Before asserting any external-project fact in a spec sentence, the assertion must be backed by reading the relevant source file (LICENSE, config, region PHP, etc.).
2. Comments in source code can be stale; trust the code over the comments. (Example: `EntranceRandomizer.php:10` comments claim "We use mt_rand" but the actual `randomize()` method shells out to Python.)
3. When citing line numbers, cite them with context (e.g., "config key X is at `Randomizer.php:155` per the boss-heart-in-pool branch"), not as bare numbers. Stale line numbers from refactors will at least be obvious.
4. Speculative repo or location names are flat-out forbidden in committed artifacts; if a thing might exist, verify before citing.

## Cross-artifact consistency drift

Round 6 found that a round-1 rewrite hadn't propagated fully across the four artifacts; round 8 found round 7 had introduced five new inconsistencies. The pattern: editing one document to apply a fix leaves the others referencing the old version.

**Practical discipline**: after any non-trivial spec edit, grep across all four artifact files for the changed terms. Examples that caused real drift in this session:
- `inventory_change_counter` → renamed to `reachability_state_counter` in D13 but missed in D9 and `randomizer-ui` spec.
- `Open / Standard / Inverted / Retro` → switched to lowercase `open / standard / inverted / retro` per ALTTPR's `mode.state` convention but old PascalCase versions lingered in scenarios.
- 14 op count after `OP_HAS_PRIZE` and `OP_MEDALLION_OPENS` added → became 15 with `OP_ITEM_IS` but tasks.md said 14 in three places.
- "64-byte slot header" → expanded to 80 bytes for `reserved[16]` but design.md §D6 still showed 64 with `spoiler_path[64]` that had been struck from the save spec.

The OpenSpec status check (`openspec status --change <name>`) doesn't catch these; it only validates that the artifact files exist and have the expected sections. Cross-artifact consistency is a manual discipline.

## When iterative review pays vs. when it stops paying

This change had ten rounds of critical review (three by general-purpose, six by ALTTPR-domain expert, one self-audit). Each round caught real bugs through round 8; rounds 9-10 mostly captured soft-claim hedging and tightened operational details. The point of diminishing returns was somewhere around round 6-7.

**Useful heuristic**: stop iterating when the latest round produces only (a) doc-coherence catches (b) operational/process improvements (c) hedging-language refinement. At that point further review is buying polish, not correctness. The real test is implementation reality, which is a more efficient bug-finder than another spec round.

## Working with OpenSpec artifacts

- The four-artifact structure (proposal/design/specs/tasks) plus the schema constraint creates a natural division of concerns. Proposal is the why, design is the how, specs are the contract, tasks are the work. Resist the temptation to mix concerns across files.
- Scenarios (`#### Scenario:` blocks) must use exactly four hashtags; the schema fails silently on three.
- Design decisions get numbered (D1, D2, ...) so cross-references stay stable; specs reference "per `randomizer-X / Requirement: Y`" so the link survives requirement renames.
- The spec-drift rule (task 13.10) is the strongest tool against artifact decay during implementation: any code PR that contradicts a SHALL scenario must update the scenario in the same PR.

## What this session's review history demonstrated

The plan converged because expert reviewers (with ALTTPR domain knowledge plus deep familiarity with the codebase) caught things general-purpose review couldn't. Specifically:
- The save format model was wrong in round 1 (per-file header on a monolithic SRAM file) — caught only because someone read the actual `messaging.c:259-266` write code.
- The 4-slot file-select was wrong — caught only because someone counted bytes against the 8 KB SRAM.
- The 5-icon hash input was a critical bug (constant across same-settings seeds) — caught only because someone understood what a "hash" is supposed to discriminate.
- The Pyramid Fairy structure required reading the actual region PHP.

This is the case for domain-knowledgeable review at plan time. The cost is real (multiple review rounds) but cheaper than discovering the same issues during implementation.

## What to carry into Phase 0 / A0

When the audit deliverable is written, capture:
- Every grant-site enumeration alongside the file:line of the existing write
- Every config-key citation with the file:line in ALTTPR where it's defined
- The provenance of every macro in the named-macro set (file:line in `app/Support/ItemCollection.php`)
- A glossary mapping ALTTPR's terms to ours (e.g., `setRequirements` → our `can_reach`, `setFillRules` → `can_place`)

The audit is the single biggest debt this spec creates; landing it well makes everything downstream cheaper.
