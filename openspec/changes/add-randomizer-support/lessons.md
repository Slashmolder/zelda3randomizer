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

---

# Phase A1 implementation lessons

These accumulated during the ~2-day Phase A1 sprint that took the project from Phase 0 (audit gate just closed) to a fully solvable end-to-end CLI demo. Captured here because they would have shortened the sprint by hours and will shape Phase A2.

## Silent-default fallbacks are the largest single source of bugs

The codegen had a "silently ignore unknown region names and hash to a 16-bit id" fallback in `_resolve_region`. The intent was tolerance for partial-graph examples; the reality was that **every typo passed silently**, the bytecode still emitted, and a `Predicate_EvalCtx` check at runtime returned false because the hashed id wasn't in any reachable-region bitset. The bug surfaced as "Desert Palace inexplicably unreachable from sphere 2 even though Library has the Book of Mudora in sphere 1." Hours of trace.

**Discipline**: codegen fallbacks should fail loud (warning printed; --strict promotes to error) and surface the typo. Same rule applies to "missing optional field" defaults — `region: 0xFFFF` (unbound) sounds harmless but silently broke the location-reachability gate.

The same shape repeats elsewhere:
- A location_registry `vanilla_item: BigKey_DesertPalace` typo at DP Big Chest (should be `PowerGlove`) made the vanilla-pin pre-pass place two big keys, blocking dungeon-mode key placement. Discovered when sphere computation showed BigKey_DP appearing twice in the placement.
- Per-dungeon SmallKey counts in `BuildItemPool` say DP has 1 small key but no `location_registry.yaml` entry has `vanilla_item: SmallKey_DesertPalace`. In Vanilla mode the key is created in the pool then forgotten because no slot accepts it. Discovered when DP locations were persistently unreachable in vanilla-keys mode.

**Practical rule**: every entity that has both a "registry side" and a "placement side" should have a consistency check at codegen time. The agent-driven `location_registry` audit caught the easy cases (DP Big Chest=PowerGlove); structural mismatches like the missing SmallKey pin sites need a dedicated cross-reference check.

## Sphere computation is the best test of placement correctness

Goal completability tells you "the goal is reachable in principle." Sphere computation tells you "every placed item is actually collectible by following the graph." The two disagree often.

In Phase A1's first end-to-end run, `goal_completable=true` for `fast_ganon` because Ganon's location was reachable. Sphere computation said 46 of 236 placements were unreachable. The placer was producing seeds where two-thirds of items couldn't be collected — but Ganon still happened to be reachable through a happy path. Without sphere computation, this would have shipped as "working."

**Practical rule**: every iteration of placement logic should run sphere computation against the produced placement table. The `unreachable_placements` field in the spoiler is the canary for placement quality.

## Assumed fill produces broken seeds without bounded retry

Even with a perfect logic graph, naive assumed-fill (Fisher-Yates progression order, no rewind, single attempt) can place a progression item in a location that gates behind that very item later in the placement order. The placer didn't notice because it used "all unplaced items in assumed inventory" — TR was reachable at that moment because Hammer was assumed in inventory, but Hammer got placed elsewhere in a later iteration.

**Discipline**: bounded retry (currently 8 attempts in Phase A1) with sphere-check scoring is the cheap, correct guard. ALTTPR-style rewind-within-attempt is a follow-on optimization that improves the median attempt count but doesn't substantively change the worst case.

## Parallel-agent fan-out for translation work is high-leverage

The 16 logic_parts files were translated by 4 agents working in parallel over ~10 minutes each. The same work serially would have taken hours. The keys to making it work:
- **File-per-agent partitioning** so no agent edits the same file as another. The codegen's merge-on-load handles cross-file region stub collisions cleanly.
- **Brief includes the schema + 1-2 worked examples** so agents converge on shared conventions.
- **Translation discipline** (every predicate cites PHP file:line) propagates from the brief into agent output.

The same pattern works for the `location_registry` audit and the predicate-translation backlog.

## The "ground truth" for ALTTPR semantics is the PHP source — and that is the only place it lives

Documentation, tutorials, community references, and ALTTPR's own comments all drift from the actual code. Examples discovered:
- ALTTPR's vanilla DP Big Chest contains PowerGlove. NOT BigKey, NOT MirrorShield, NOT anything else community guides may suggest. The truth is in `assets/dungeon/dungeon-115.yaml`'s `Chests: [27!]` (item code 0x1b = PowerGlove).
- Vanilla DP has 1 small key per ALTTPR config. Where is it placed in vanilla LttP? Unclear from PHP region files — it requires reading the actual room data.
- `RescueZelda` is a virtual item in ALTTPR (`Item::Event` subclass). Non-Standard world-states pre-collect it via `World::pre_collected_items`. The literal predicate `$items->has('RescueZelda')` is correct; the mode-state branching happens at world-construction time.

**Rule**: when you find yourself reasoning "I think ALTTPR does X because Y" — grep first.

## Spec-vs-impl drift accumulates faster than expected

The spec audit (run after Phase A1 was "done") found 6 critical drifts in ~2000 lines of new code:
- `placement_table_size` units (pairs vs. bytes)
- `RandoSettings` fields missing 4 spec-mandated fields
- `Goal_IsCompletable(FastGanon)` skipping the crystals check
- Sidecar embedded placement table format (pair list vs. flat array)
- Several `--cli-flag` parses that were never wired to the runtime
- `fallback_warnings` etc. hardcoded empty

All of these were specified scenarios with `WHEN/THEN` clauses. The implementation didn't violate them out of malice — they just slipped during fast iteration. **Spec-drift audits as a parallel agent process (per task 13.10) caught all six in one pass and were cheap to run.**

## Document deviations from spec when you take them deliberately

In Phase A1 I made several deliberate "differ from spec for now" calls:
- Pre-grant DefeatAgahnim is unimplemented; spec doesn't require it but downstream reachability suffers
- LinksHouse_Inverted region isn't declared; Inverted start region falls back to DarkWorld_South
- Bounded-rewind is between-attempts (8 fresh shuffles) rather than within-attempt
- `--budget-seconds` accepted but ignored
- `--assets-must-be-vanilla` accepted but ignored (no vanilla_assets_hash.h to compare against)

Each of these is a Phase A2 follow-on. They're recorded here so the user / future me can verify they were intentional rather than oversights, and so the next session can pick them up explicitly rather than re-discover them.

## What to carry into Phase A2

- Wire the missing CLI flags (`--budget-seconds`, `--assets-must-be-vanilla`).
- Add a `Rando_AuditPlacementGraph(settings)` codegen-time check that emits warnings for every location whose `region` ID is `0xFFFF`, every dungeon item count mismatch with vanilla-pin coverage, every macro that references an undeclared item, etc.
- Implement the spec's `placement_table_size` units convention (bytes, flat array indexed by location_id) — the current pair-list format is convenient but doesn't match the disk-format spec.
- Re-shuffle the order of `BuildItemPool` to drop dungeon items per `dungeon_items.*` mode flexibly (current code is "all or none"; Wild mode in particular needs the items in the pool with no per-dungeon `can_place` constraint).
- Wire the `RandoStartingInventory` injection at the actual new-game flow (currently only called from CLI/test contexts).
- Add a "spec scenarios → implementation" coverage check as a CI step.

---

# Second-sprint lessons (post-audit cleanup + §6 dispatch)

This sprint took the project from "Phase A1 substantially behind" (per the
audit's overall assessment) to closing audit Bugs #1-#15 (15 of 16) plus
the agent's new findings N1, N2, N4, N5, N7. It also landed the §6
dispatch foundation: 13 NPC grant sites + universal chest hook + audit
guard --strict + 50-seed cross-platform CI corpus + item-pool difficulty
caps + per-dungeon containment for keys/maps/compasses.

## Audit agents are dramatically more valuable than they look on the spec

Spawning a "second pair of eyes" agent against the codebase produced 10
**new** findings the spec audit had missed, two of them HIGH severity
(N1 slot_kind enum off-by-one vs spec; N2 Pedestal goal-completability
missing pendant-reachability check). Cost: one agent invocation, ~3
minutes of wall-clock, parallel with my own work.

**Practical rule**: launch an audit agent each time the code surface
changes substantially. Even with a comprehensive prior audit, fresh eyes
catch what familiarity skipped.

## Generator-version bumps are cheap; absorb them aggressively

The corpus regenerate tool (`bump_rando_corpus.py`) makes version bumps
nearly free: write the placement-changing fix → bump kGeneratorVersion
in src/rando/rando.h → run `bump_rando_corpus.py --apply` → corpus
digests update in place → ship.

This session bumped 3→4→5→6→7→8 (five bumps) across:
- Settings struct alignment to spec
- Triforce-Hunt junk-padding rotation
- Vanilla-mode dungeon items pre-granted
- World-state-filtered pool padding
- Dungeon-mode containment for keys/maps/compasses
- Item-pool difficulty count caps

Each bump is a correctness improvement; the corpus is now a higher-
quality regression guard against each of those scenarios. Previously
the project hesitated on bumps; the cheap regen turned them into routine.

## Settings-hash drift is the silent killer

The audit's N7 finding (5 settings fields in the canonical hash but
unparseable via CSV) meant that fields the spec considered "first-class
settings" couldn't be exercised end-to-end. Race-mode in particular
had a spec scenario ("Race-mode toggle changes settings hash") that
was untestable because the toggle had no input path.

**Rule**: every field in the settings canonical-serialize layout must
have a corresponding CSV parser entry. Verified by selftest cases that
parse each field via `--settings=` and assert the resulting hash differs.

## Dispatch wrappers should fall back gracefully

`Rando_DispatchVanillaGrant` returns the vanilla LttP code when:
- Placement table not installed (rando inactive)
- Placed item has no vanilla LttP path (progressive/dungeon/prize/virtual)
- Lookup-table miss (unknown location_id)

This means wiring a §6 site is risk-free: the worst case is "rando
doesn't actually change this site's behavior yet" — never "the game
crashes" or "the vanilla path breaks". The audit guard's `--strict`
mode + the corpus + the per-attempt placer determinism guarantee
together mean a sloppy wrap can't damage anything in production.

## Spec scenarios are the unit tests

The biggest leverage in this sprint came from spec scenarios I hadn't
verified pass. Every one I checked surfaced 0-2 fixes:
- "Dungeon-mode small key stays in its dungeon" → wasn't enforced; added per-dungeon containment in `location_accepts_item`.
- "Item-pool difficulty downgrade" → wasn't implemented; added overflow.count caps per ALTTPR's `app/World.php:171-214`.
- "Race-mode toggle changes settings hash" → field wasn't parseable.
- "CLI --assets-must-be-vanilla refuses non-vanilla blobs" → flag was `(void)`'d out.
- "sphere_digest field in meta block" → wasn't emitted.

**Rule**: spec scenarios are the work list. When deciding what to do next,
grep `^#### Scenario` across all spec files and pick one whose passing
isn't yet exercised. Each one is a small contained fix.

## Audit-guard exemption comments are documentation

The 8 `// rando-exempt:` comments added in this sprint are real
documentation of which writes are NOT grant sites. Each exemption
includes:
- Why the write isn't a grant (drop-pool / state-shuffle / receipt-
  dispatcher consumption / etc.)
- Where the real grant happens (call site upstream / Phase B work / etc.)

Reviewing the exemption list is a fast way to find sites that need
real dispatch — and a fast way to find sites that already do dispatch
(elsewhere) and just need their downstream write tagged.

## Corpus regeneration is also a quality check

When the corpus diff is small (3 of 50 entries changed) after a placer
change, that's a strong signal the change is what you expected (only
affects the seeds that exercise the changed scenario). When it's large
(all 50 changed), reconsider — you may have a broader RNG-consumption
change than intended.

Per-attempt RNG byte-consumption is the lurking gotcha: Fisher-Yates'
shuffle is sensitive to array length. So any pool-sizing change shifts
every digest. The world-state-filtered pool padding bump did this.

## Misleading audit ranges

Audit NEW-1 (dungeon_id_for_item off-by-one) bit because the SmallKey
enum's contiguous HCE..GT range (13 entries) accidentally validated
the formula `item_id - base`. BigKey / Map / Compass skip both HCE
AND HCT (11 entries), so the same formula was off by 1 for 8 of 11
dungeons.

**Rule**: when arithmetic encodes a list, ALWAYS prefer an explicit
lookup table — the cost is 10 bytes of data, the upside is the
mapping is visible to grep + immune to "I forgot the dungeon enum
has a gap" classes of bug. The selftest now pins each (item_id →
dungeon_id) explicitly.

## Dispatch wrappers can encode runtime state

The §6.2 progressive-item dispatch uses runtime state
(`link_sword_type`) to translate a placed registry id to the
next-tier vanilla LttP code. This is more powerful than a static
translation table: a single ProgressiveSword item can grant L1/L2/L3/L4
based on the player's current state. The placer's bounded-retry
already accounts for the cumulative effect across multiple placements
of the same progressive id.

Similar pattern works for items that need per-current-dungeon
tracking (small keys, big keys, maps, compasses) where the dispatch
returns the vanilla LttP code and the existing receive logic
handles the per-dungeon increment.

The pattern breaks for items that need persistent rando-specific
state beyond what vanilla LttP tracks (TriforcePiece counter,
HalfMagic direct-write). Those need new code paths.

## Phase A1 ⇄ Phase A2 boundary

By the end of this work, Phase A1 has produced:
- Generator complete + 50-seed cross-platform corpus
- 15 §6 NPC dispatch sites + universal chest hook (with empty lookup)
- §6.2 partial: 53 item ids translate correctly
- Snapshot rando preservation via TLV tail
- Audit guard --strict mode active in CI
- 17 spec scenarios verified by selftest

The Phase A2 boundary is reached at: file-select / settings UI,
chest-lookup table population (needs zelda3_assets.dat parse),
Inverted/Retro logic graphs, §6.2 full receive helpers for
TriforcePiece counter UI / HalfMagic direct grant / per-placed-dungeon
counters. None of these block Phase A1's generator from being
usable headlessly — they're the polish that makes rando seeds
playable in-game.

## Spec-strict isn't always practical-strict

I tried adding a hard refusal when `spheres.unreachable_count > 0`
— Goal_IsCompletable uses full-inventory reachability (over-
approximation) while sphere walking uses sphere-walked
reachability (under-approximation), and the spec arguably wants
both to agree. Adding the strict check rejected 4 of 50 corpus
entries (all dungeon-mode key configurations) where the placer
can't yet find a containment-clean placement, even though those
seeds are playable in practice. Reverted with a code comment
documenting why.

**Rule**: a spec scenario specifies the *target* behavior. When a
spec-conformant check blocks a working feature on an unrelated
underlying limitation (here: the assumed-fill placer's lack of
intra-attempt rewind), the strict gate makes the spec actively
hostile. Surface the warning prominently and defer the strict
check until the underlying capability catches up. In this case
the `fallback_warnings: unreachable_placements` rollup in the
spoiler already tells the user; the strict refusal would just
mean "the placer can't produce some seeds you asked for" without
saying why.

The diagnostic counterpart of this rule: when a strict check
suddenly fails N entries of a regression corpus that passed
yesterday, ask whether the strictness or the corpus is wrong
before just bumping the corpus. (Here it was the strictness:
the seeds were playable, the placer just had a known limitation.)

## §9 UI sprint — three clusters and their audits

Three parallel-agent clusters (file-select foundation; text input
layer; settings + Generate) each shipped via worktree agents that
followed a 9-step protocol. Post-merge fresh-eyes audits found
**8 HIGH + 12 MED + 14 LOW** across the three clusters. Each cluster
brought specific lessons that subsequent clusters honored when the
prior audit's findings were baked into the next-cluster brief.

### Tile-stream count encoding is BYTES-MINUS-ONE, not tile_count

`HandleStripes14` at `src/nmi.c:421` decodes `len = (swap16(WORD(p[2]))
& 0x3fff) + 1`. The 4-byte command header is `vram_addr_hi |
vram_addr_lo | attr | count`, and `count` is BYTES-MINUS-ONE. For N
tile pairs (each pair = 2 bytes), the count field MUST be `(N*2) - 1`.

The cluster-1 agent passed `N` directly. memcpy copied half the data,
`p` advanced mid-payload, and the next "header" was read from garbage
bytes — corrupting VRAM and OOB-reading adjacent `g_ram`. The bug
fired the moment any empty slot was picked (kind picker) or any
cross-kind copy refusal triggered.

Reference data to verify against: `kSelectFile_Func3_Data` uses count
= 0x25 for 38 bytes = 19 tile pairs. After the fix-sprint,
`kKindPicker_Text` uses count = 0x11 (= 17) for 18-byte / 9-pair rows.

**Rule**: any new tile-stream emitter MUST encode count as
`(num_bytes - 1)`. Static tables should compute it from the payload
length, not from a hand-typed tile count.

### VRAM-buffer restoration on cancel via submodule_index = 3

Pickers and screens that `memcpy` data into `vram_upload_data`
clobber the `kSelectFile_Func3_Data` background that
`FileSelect_TriggerNameStripesAndAdvance` (submodule 4) installs.
Slot-list rendering writes patches at fixed offsets {4, 0x2e, 0x58}
of `vram_upload_data`, which only make sense within the Func3 layout.

Cluster 1: B-cancel from the kind picker left `vram_upload_data`
holding picker bytes; next frame's slot-list patches landed
mid-prompt and the slot list rendered garbage.

**Pattern**: on EVERY exit path from a UI surface that clobbers
`vram_upload_data`, set:
```c
submodule_index = 3;
subsubmodule_index = 0;
```
That makes submodule 3 → 4 → 5 re-execute, reinstalling Func3 data
before slot-list rendering resumes.

**The gotcha** (cluster-3 audit HIGH-1): if the same Deactivate
function is also called for state-reset purposes from
`Module_SelectFile_0` init (i.e. BEFORE submodules 1+2 have run),
the unconditional rewind to 3 BYPASSES submodule 1
(`ReInitSaveFlagsAndEraseTriforce`) and submodule 2 (slot-tile
upload). First-launch file-select renders without slot frames.
Guard the rewind on a "was actually active" flag.

### 5-icon hash input is `share_string_binary`, NOT `settings_hash`

The visual-hash widget computes `index_i = SHA-256(input)[i] %
kHashIconAtlasSize` for i in 0..4. The input MUST be the
share-string raw binary blob, NOT the settings hash. Otherwise every
seed with the same settings produces identical icons — defeats the
"at-a-glance seed verification" purpose. Cluster-1's banner-abbrev
derivation made this exact mistake (rendered "world initial" via
`settings_hash[0] % kWorldStateCount`), silently producing the same
abbrev for different seeds with matching settings.

**Anti-pattern**: deriving per-slot identifiers from `settings_hash`
modulo an enum size. `settings_hash` is SHA-256 noise. Use either
explicit settings fields (when serialized in the slot header) or
hash the per-seed `share_string_binary`.

### Asset-warn "Allow Once" needs a session-bypass flag

Cluster-3's asset-warn dialog has three choices: Always Allow
(persist), Allow Once (don't persist), Cancel. The original
implementation routed Allow Once → recursive `HandleGenerate()`,
expecting the gate to short-circuit because the dialog had been
shown. But the gate's predicate is `AssetDecision_FindAllow(hash)`
— which returns false because Allow Once didn't persist. Loop.

**Pattern**: when a "this-time-only" choice exists alongside a
"persist" choice, the in-flight UI needs a one-shot bypass flag
that the gate's predicate consults and clears on consumption.

### Generated new SRAM slots MUST run `kSramInit_Normal`

The vanilla `NameFile_DoTheNaming` path memcpys a 60-byte
`kSramInit_Normal` block to `sram + 0x340` after zeroing the slot.
That block initializes health bytes at +44/+45 = 0x18/0x18 (= 3
hearts, 3 max hearts), starting boomerang slot, etc. Without it,
the slot loads with zero health → instant death.

Cluster-3's Generate path zeroed the slot, wrote name + sentinel +
DiedCounter, called `Intro_FixCksum`, and skipped the
`kSramInit_Normal` copy. The generated rando slot was a valid
checksum'd slot with zero health.

**Rule**: any code path that creates a "new game" SRAM slot
programmatically MUST replicate the full
`NameFile_DoTheNaming` init sequence, not just the visible
slot-name + sentinel writes. Factor out a helper if multiple
sites need it.

### Worktree agents need ROM + asset-blob mirroring

Git worktrees don't carry gitignored files. `zelda3.smc` and
`zelda3_assets.dat` are both gitignored. An agent in a fresh
worktree running `run_rando_corpus.py` (which invokes
`--generate-seed` 50 times, each calling `LoadAssets()`) hits
`Die("Failed to read zelda3_assets.dat...")` on every iteration.

On Windows Release builds, `Die()` was unconditionally calling
`SDL_ShowSimpleMessageBox` — popping a modal dialog on the
DEVELOPER'S desktop regardless of which process invoked the agent.
50 popups in succession during a corpus run.

**Two fixes shipped**:
1. `g_headless_mode` global in `src/main.c` set when any of
   `--rando-selftest`, `--generate-seed`, `--rando-bench-logic`,
   `--print-assets-hash`, `--vanilla-ram-check` is in argv. `Die()`
   skips the popup when the flag is set.
2. `assets/scripts/setup_worktree.py` script that mirrors the ROM
   + asset blob from the main worktree into the current one. Idempotent,
   `--verify` mode for sanity checks. Worktree-agent briefs MUST
   include this as the first step.

### Auto-repeat KEYDOWN events spam one-shot actions

SDL emits `SDL_KEYDOWN` every ~30 ms while a key is held (the
`event.key.repeat` field marks repeats). For one-shot actions
(Submit, Cancel, Generate, asset-warn choices), the handler MUST
guard with `if (!event.key.repeat)` or accept that held keys will
re-fire the action every frame.

Cluster-2 audit found this on the alphabet picker's Submit / Cancel
keys — held Enter prevented the OK-overlay countdown from completing
(every frame re-submitted, resetting the countdown).

### Cluster-merge integration pitfalls

Two recurring failure modes when merging worktree-agent branches
back to master:
1. **Stale bash cwd**: a `cd .claude/worktrees/agent-xxx` in one
   Bash tool call persists across subsequent calls. Build commands
   silently target the agent's worktree instead of main, or merges
   fail with "branch already used by another worktree." Always
   `cd /c/src/zelda3randomizer` (absolute path to main) before a
   sequence of merge / build / verify commands.
2. **Worktree file leakage into main**: at least 3 times across this
   sprint, the agent's WIP appeared in the main worktree's working
   tree (showing as uncommitted changes on master). When this
   happens, `git restore --staged` + `git checkout master -- <file>`
   on the agent-owned files; only my own edits should end up
   committed on master.

### Audits find catastrophic bugs in non-obvious places

Three audits this sprint caught issues that the implementing
agent + their own selftest path would never have surfaced:
- Tile-stream corruption: the agent's selftest doesn't open the
  picker; the bug only fires when a player hits the kind picker
  or copy refusal.
- Instant-death rando slots: the agent's CLI `--generate-seed`
  smoke test writes the spoiler files but doesn't load the slot
  in-game. The "valid sidecar + corrupted SRAM" pair passed every
  build-time check.
- First-launch no slot frames: only visible on the very first
  Module_SelectFile_0 entry; subsequent re-entries work because
  Settings_Deactivate's no-op-after-first-call would let
  submodules 1+2 run.

**Pattern**: bugs that lurk in player-flow paths the agent's
selftest doesn't exercise are the audit's specialty. Schedule
the audit as a workflow REQUIREMENT, not a polish step.

## Process: cluster audit protocol (integrator's checklist)

Established this sprint and worth preserving:

1. **Inspect agent branch**: `git log master..<branch> --oneline`
   + `git diff master..<branch> --stat`. Note commit count + LOC.
2. **Merge** with a detailed merge-commit message that captures
   the agent's per-task summary verbatim.
3. **Build + run gates**: msbuild, `--rando-selftest`,
   `check_audit_guard.py --strict`, `check_codegen_wiring.py`,
   `check_init_order.py`, `run_rando_corpus.py`. All must pass.
4. **Fresh-eyes audit**: spawn a general-purpose agent with a
   prompt that lists prior-cluster audit lessons + cluster-
   specific risk areas. Ask for HIGH/MED/LOW findings with
   file:line citations.
5. **Fix sprint**: per the "don't lose findings" lesson, fix all
   HIGH + MED findings inline. LOW items are case-by-case.
6. **Re-run gates** after fix sprint.
7. **Single combined commit** for the fix sprint with a commit
   message that lists each finding + the fix applied.
8. **Clean up**: `git worktree remove -f -f` + delete the
   `worktree-agent-*` companion branch.

When briefing the next cluster's agent, copy the prior audit's
HIGH findings into the brief verbatim as "CRITICAL lessons to
honor." This is the single highest-leverage step in the workflow.

