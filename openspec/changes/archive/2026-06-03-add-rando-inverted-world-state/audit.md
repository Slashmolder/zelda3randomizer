# Audit — add-rando-inverted-world-state

This file records the apply-time provenance pre-flight and macro audit for the
Inverted world-state change. All ALTTPR facts below come from a `grep` against
the sibling checkout, never from memory.

## Inverted macro provenance

- **Upstream checkout**: `alttp_vt_randomizer`
- **Pinned commit**: `219fcafd029dab597b8db400efafd8f56f8b4edb`
  (`git rev-parse HEAD`, captured 2026-05-29).
- **License**: MIT (per `CLAUDE.md` claim-grounding section; `LICENSE` + `composer.json`).

All line ranges below are relative to this pinned commit.

### §1.1 — RegionRemap pairing-table source

`app/World/Inverted.php` **exists** (53 lines). It is a thin `World` subclass
whose constructor populates `$this->regions` with the 24 Inverted region
classes (lines 28-55) plus the shared `Standard\Medallions` and
`Standard\Fountains` regions. It carries no per-region logic — it is the
authoritative **region-registry / pairing source** for the RegionRemap overlay
table (the named-key → `Region\Inverted\…` class mapping at lines 28-55).
Per-location predicates live in the 24 `app/Region/Inverted/**/*.php` files,
not here.

### §1.3 — File + line counts (actual vs expected)

| Metric | Expected | Actual | Match |
|---|---|---|---|
| `*.php` under `app/Region/Inverted` | 24 | 24 | ✓ |
| Total `wc -l` lines | 2977 | 2977 | ✓ |

Note: `wc -l` (newline count) yields 2977; `Measure-Object -Line` reports 2665
because several Inverted PHP files lack a trailing newline. The task's expected
figure is the `wc -l` figure, which matches exactly.

### §1.4 — Per-file predicate skim + effort note

Predicate-pattern counts per file (grep of `setRequirements` / `setFillRules` /
`setAlwaysAllow` / `can_enter` / `can_complete`):

| File | setRequirements | setFillRules | setAlwaysAllow | can_enter | can_complete |
|---|---|---|---|---|---|
| DarkWorld/DeathMountain/East.php | 4 | 0 | 0 | 1 | 0 |
| DarkWorld/DeathMountain/West.php | 1 | 0 | 0 | 1 | 0 |
| DarkWorld/Mire.php | 0 | 0 | 0 | 1 | 0 |
| DarkWorld/NorthEast.php | 6 | 0 | 0 | 1 | 0 |
| DarkWorld/NorthWest.php | 6 | 0 | 0 | 0 | 0 |
| DarkWorld/South.php | 9 | 0 | 0 | 0 | 0 |
| DesertPalace.php | 2 | 0 | 0 | 1 | 0 |
| EasternPalace.php | 4 | 0 | 0 | 1 | 0 |
| GanonsTower.php | 25 | 6 | 1 | 1 | 2 |
| HyruleCastleEscape.php | 8 | 2 | 0 | 1 | 0 |
| HyruleCastleTower.php | 0 | 0 | 0 | 1 | 0 |
| IcePalace.php | 0 | 0 | 0 | 1 | 0 |
| LightWorld/DeathMountain/East.php | 12 | 0 | 0 | 1 | 0 |
| LightWorld/DeathMountain/West.php | 1 | 0 | 0 | 1 | 0 |
| LightWorld/NorthEast.php | 10 | 0 | 0 | 1 | 0 |
| LightWorld/NorthWest.php | 23 | 0 | 0 | 1 | 0 |
| LightWorld/South.php | 24 | 0 | 0 | 1 | 0 |
| MiseryMire.php | 0 | 0 | 0 | 1 | 0 |
| PalaceOfDarkness.php | 0 | 0 | 0 | 1 | 0 |
| SkullWoods.php | 2 | 0 | 0 | 1 | 0 |
| SwampPalace.php | 9 | 3 | 2 | 1 | 0 |
| ThievesTown.php | 0 | 0 | 0 | 1 | 0 |
| TowerOfHera.php | 4 | 2 | 2 | 1 | 0 |
| TurtleRock.php | 13 | 3 | 2 | 1 | 2 |

Effort note: the bulk is concentrated in **GanonsTower.php** (495 lines, 25
`setRequirements` + 6 `setFillRules` — Agahnim 2 routing and the GT chest
chain), **LightWorld/South.php** (246 lines, 24), **LightWorld/NorthEast.php**
(232), **LightWorld/NorthWest.php** (223), **TurtleRock.php** (218, 13), and
**LightWorld/DeathMountain/East.php** (206, 12). Dungeons with zero
`setRequirements` (HyruleCastleTower, IcePalace, MiseryMire, PalaceOfDarkness,
ThievesTown) carry only a `can_enter` region gate — those translate trivially.
The translation work itself (§4.1-4.24) is already shipped per `tasks.md`; this
audit covers only the provenance + macro-coverage tasks.

## §2.1/2.2 — Inverted macro coverage cross-reference

Grep of `app/Region/Inverted/**/*.php` for `$items->can*` and
`$this->world->can*` method calls:

- `$this->world->can*` calls: **none** found.
- `$items->can*` calls (12 distinct ItemCollection macros):
  `canBlockLasers`, `canBombThings`, `canBunnyRevive`, `canExtendMagic`,
  `canFly`, `canKillMostThings`, `canLiftDarkRocks`, `canLiftRocks`,
  `canLightTorches`, `canMeltThings`, `canShootArrows`, `canSpinSpeed`.

(The broader `->can*` grep also surfaces `canEnter`, `canBeat`, `canAccess`,
`canReachTop/Middle/Bottom` — these are **region-local methods**, not
`ItemCollection` macros, confirmed by grepping
`app/Support/ItemCollection.php` for `function <name>`; they are not
translation targets for `macros.yaml`.)

Cross-reference against `assets/rando/macros.yaml`: **all 12 are already
present** (authored in Phase A, mirroring `app/Support/ItemCollection.php`):

| ALTTPR `$items->` call | macros.yaml entry |
|---|---|
| canBlockLasers | CanBlockLasers |
| canBombThings | CanBombThings |
| canBunnyRevive | CanBunnyRevive |
| canExtendMagic | CanExtendMagic |
| canFly | CanFly |
| canKillMostThings | CanKillMostThings |
| canLiftDarkRocks | CanLiftDarkRocks |
| canLiftRocks | CanLiftRocks |
| canLightTorches | CanLightTorches |
| canMeltThings | CanMeltThings |
| canShootArrows | CanShootArrows / CanShootArrowsL1 / CanShootSilvers |
| canSpinSpeed | CanSpinSpeed |

**Finding: no macro is missing.** Inverted uses no `ItemCollection` macro that
Phase A did not already author. Tasks §2.1/§2.2 close with **no change to
`assets/rando/macros.yaml`**. Consequently there is no `phase: B-inverted`
macro to tag, and no `kGeneratorVersion` bump is warranted on macro grounds.

## Verification results (apply-time)

- `python assets/rando_logic_gen.py` → exit 0; `warnings: 0, macro errors: 0`;
  generated 266 locations, 132 items, 31 regions/edges. Working tree stayed
  clean (byte-identical generated output — no macro change).
- `python assets/scripts/check_codegen_wiring.py` → exit 0; "6 generated
  file(s) wired across all build systems." Generated header **names**
  unchanged.
- `python assets/scripts/check_audit_guard.py` → exit 0; "no non-exempt
  writes (38 tracked offsets)." The `[advisory]` indirect-dispatch notes are
  pre-existing informational output, not failures.

## Out of scope for this work item

Full corpus/digest verification and end-to-end playtest require a build + dev
unit and are explicitly **out of scope** here. Because no macro (and no
logic.yaml / logic_parts content) changed in this work item, default-settings
corpus output is expected to be byte-identical and no `kGeneratorVersion` bump
is performed.

## Headless verification (2026-05-29, generator_version 36, Windows x64 Release)

Done without playtest, against `bin/x64-Release/zelda3.exe`. Validates the
Inverted *logic graph + placement* (NOT in-game runtime, which still needs the
playtest in §14.1-14.3).

### Determinism / regression (§10.3, §14.4)
`run_rando_corpus.py --binary=bin/x64-Release/zelda3.exe` → **all 55 entries OK**,
byte-identical to the recorded baseline. Covers 44 Open / 10 Standard / 3 Retro
/ 3 Inverted seeds (incl. `b-inverted-fast-ganon`, `b-inverted-ganon-7-7`,
`b-race-inverted-fast-ganon`). Confirms: (a) the recent main merges
(Phase A archive, inverted-finish, native-spoiler, MED fixes) are
placement-neutral; (b) Inverted generation is deterministic.

### Inverted completability stress (§14.x logic-side precursor)
Generated 40 Inverted seeds (7 goals × multiple seeds, normal+hard pools).
**Every seed `goal_completable = true`; zero unreachable-location / forward-fill
warnings.** The only `fallback_warnings` entries are `{kind: retry_attempts}`
metadata (assumed-fill restart count), not placement failures:
- Open: 0-1 retries · Standard: ~7 · Inverted: ~24.
Inverted's higher retry count reflects its denser constraints (bunny-state /
MoonPearl gating) and is well within budget.

### Generation benchmark (§13.5.1, §13.5.3)
Inverted, 35-seed sample: **p50 = 50 ms, p95 = 622 ms, max = 633 ms** —
comfortably under the 2000 ms desktop SHALL (§13.5). Triforce-Hunt / Ganon-Hunt
are the slow goals (more pieces → more reachability iterations); all others
30-70 ms. No budget relaxation needed (§13.5.2 condition not triggered).

### Still requires playtest (not coverable headlessly)
In-game runtime: bunny-state start, MoonPearl+MagicMirror grant + save/reload
idempotency (§14.3), mirror/tile-swap topology, dark-world-first routing
(§14.1-14.2). Cross-platform corpus (§10.4) and Switch bench (§13.5.4) need
other platforms / a Switch dev unit.

## Fresh-eyes audit (§12.2, 2026-05-29)

Parallel review agent, self-contained prompt, per CLAUDE.md "Fresh-eyes audit
cadence." **No HIGH-severity logic-graph, region-binding, or vanilla-state-proxy
bugs found — the Inverted implementation is sound.** Findings triaged:

- **[FIXED] Start-region doc inconsistency** — `DarkWorld/South.yaml`'s header
  claimed DarkWorld_South was "THE INVERTED START REGION" and cited a stale
  codegen line. Verified ground truth: `kRandoStartRegionByWorldState` =
  {0x14,0x14,0x15,0x14}, i.e. Inverted start = `LinksHouse_Inverted` (id 21).
  DarkWorld_South is reached via the RegionRemap overlay (no static edge from
  LinksHouse_Inverted exists). Comment corrected; codegen output byte-identical.
- **[REJECTED] "Starting-inventory MIN-guard" (audit MED #3)** — suggestion was
  to only write MP/Mirror when the byte is 0. This would be a REGRESSION: the
  grant deliberately runs on every load (above the cold-boot dedupe) so MP+Mirror
  persist across reloads in Inverted; if a byte were ever cleared we WANT to
  re-grant. Current unconditional `=1/=2` write is correct. Left as-is.
- **[NOTED, non-blocking] Defensive-doc suggestions** — Bottle Merchant relies on
  the LW can_enter edge (MoonPearl) rather than an explicit per-location
  predicate (MED #2); HCE BigKey/Compass Phase-B placeholder (LOW #4); mirror-bonk
  rect dependency (LOW #5); Ganon location cross-file ownership (LOW #6). All
  mechanically correct today; captured here for the maintainer. Not applied
  because #2 would change the logic graph (placement/kGen impact) for
  defense-in-depth only.

## Fresh-eyes audit 2026-06-01 (archive-readiness)

Second-pass review focused on the runtime/gameplay layer (the prior audit covered
the logic graph) and on the THREE recent fixes landed 2026-06-01:
`6e2de18` (Ether/Bombos tablet overworld dungeon-exit crash), `c78c168` (boss-heart
-in-pool immobilize softlock), `9593020` (revert of the Dark-Chapel spawn change).
Reviewed the post-fix state of `Ancilla22_ItemReceipt` / `Ancilla29_MilestoneItemReceipt`
(ancilla.c) and `Sprite_HeartContainer` (sprite_main.c), and the inverted runtime hooks
in messaging.c / overworld.c.

Verified clean (the recent fixes are correct):
- Ether/Bombos fix: both halves are coordinated. Ancilla29's dungeon-handshake block is
  wrapped in `if (player_is_indoors)` so an overworld tablet skips the stale
  `dung_savegame_state_bits` gates and falls through to the normal prize-fall/collision
  path (ancilla.c:3823+); Ancilla22's `PrepareDungeonExitFromBossFight()` step-3 call is
  also gated on `player_is_indoors` so the OOB room-index lookup can't fire outdoors.
  Vanilla unaffected (boss prizes always indoors; tablets always medallions/excluded).
  The Ether/Bombos handler-state reset (ancilla.c:3830-3834) still fires for the tablet
  receive. Structurally sound — matches the dominant receipt-side-effect bug class.
- Boss-heart fix: `item_receipt_method = (lttp_code == 0x3e) ? 2 : 0` preserves the
  vanilla method-2↔0x3e invariant; a non-heart placed item routes through method-0 so
  the step!=2 path clears `flag_is_link_immobilized`. `dung_savegame_state_bits |= 0x8000`
  still runs. Skip-receive items take the `Rando_ReceiveOrConfirm` confirmation path and
  never set immobilize. Correct.
- Both fixes correctly leave vanilla byte-for-byte identical (gated on rando-active /
  boss_loc != 0xFFFF). audit-guard / determinism / codegen-wiring all PASS.

### NEW findings

(none NEW that block archive.) The two 2026-06-01 fixes are correct and address the
exact dominant bug class CLAUDE.md warns about (vanilla receive codes with side effects
reused under rando). No additional vanilla-state-proxy, region-binding, or
receipt-side-effect bug surfaced in this pass.

### Note (not a finding — provenance)
- Both recent fixes carry "PLAYTEST REQUIRED" in their commit messages; the boss-heart
  one is noted as already confirmed on a live playtest. The Ether/Bombos one
  ("place a non-medallion item at the tablet, read it, confirm no crash + receive +
  walk away") is the one remaining playtest gate before archive — it is exactly the
  no-automated-test slot path. Not a code defect; a verification item.
- The Dark-Chapel spawn change was reverted (9593020) and re-scoped into the separate
  `add-rando-inverted-dark-chapel-spawn` change (since completed + archived 2026-06-03).
  The revert is clean; no residue in this change.

### Verdict
Archive-ready (audit-wise), contingent on the one outstanding Ether/Bombos non-medallion
tablet playtest. Logic graph (prior audit) + runtime fixes (this pass) are sound; corpus
is byte-identical and Inverted generation is deterministic per the headless run above.

---

## Fresh-eyes audit — 2026-06-02 (round 2, runtime hooks)

A supplementary read-only pass focused on the Inverted *runtime* hooks (the prior
12.2/15.2 audit covered the logic graph + a first runtime pass). Read all three
`inverted_*.c`, every Inverted-gated hook in `overworld.c`/`player.c`/`misc.c`/
`ancilla.c`, the death/S&Q respawn flow, and cross-checked load-bearing values against
`z3randomizer/darkworldspawn.asm` + `Rom.php setInvertedMode`. **0 HIGH** — "markedly
more complete and internally consistent than a typical first-pass; non-rando paths
byte-identical." Detail + ready notes below. All findings
sit in the deliberately-divergent DW-cave/exit-topology layer (no static oracle):

- **IV1 (MED)** — `inverted_entrances.c:46-103` DM-cave exit-data rows transcribed from
  ALTTPR's *relocated* (pyramid→HC) topology the fork did not adopt → swapped DM caves may
  exit to the wrong screen. VERIFY=PLAYTEST.
- **IV2 (MED)** — `overworld.c:2235-2236` force-opens the TR front overworld entrance on any
  arrival at DW screen 0x47 (reachable early), not on tail access → sequence break. VERIFY=PLAYTEST.
- **IV3 (MED, softlock potential)** — the DW→LW "way out" assumes the added type-`0x82` secret
  reveals tile `0x212` whose tile-type triggers a world warp; unconfirmed. If false, mirror-to-LW
  + lift-return-rock can't cross back = hard softlock. VERIFY=PLAYTEST FIRST.
- **IV4/IV5 (LOW)** — escape respawn → LW Sanctuary; Houlihan fall-hole fallback hardcodes LW.
  Both Mirror-recoverable, not fatal.

Verified correct (this pass): Magic-Mirror direction flip + portal recording; all bunny
away-world flips; death-respawn world fix (matches `DoWorldFix_Inverted`); flute Inverted
gate + weathervane pre-gate; mirror-portal vortex flip; cross-world dungeon-exit fixup; GT/AT
swap rooms (`0x00e0`/`0x000c`, match `Rom.php:1623-1624`); `InvertedSecrets_Install` bounds-safe.
