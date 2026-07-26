# Design: tracker-player-knowledge

## Context

The live reachability bridge (`Rando_GetLiveReachability`, `src/rando/rando.c`)
memoizes `Logic_ComputeReachability(counts, active_settings)` over the logic
graph with the ACTIVE slot's topology overrides installed —
`Entrance_ApplyEdgeOverrides` (dungeon-entrance shuffle),
`Chains_ApplyEdgeOverrides` (dungeon chains), `OwWarp_InstallLogicEdges`
(whirlpool/flute), `Rando_SetDoorLogicLayout` (door shuffle) — via
`Rando_ReinstallActiveSlotLogicOverlays` / slot activation. Four player-facing
surfaces consume it: the Check Tracker and Map Tracker
(`rando_window/tracker_windows.cpp`), the Reachability panel
(`rando_window/rando_reach_panel.cpp`), and the auto-tracker export
(`auto_tracker.c`, the `reachable` array). Because the flood walks the TRUE
shuffled graph, per-dungeon-region availability reveals shuffled identity
before first entry.

Existing player-knowledge precedents this design generalizes (rather than
inventing a parallel concept):

- Prize column renders "?" until the prize location is checked
  (`tracker_windows.cpp` `UnknownPrizeCell`), failing CLOSED on NULL settings.
- Race mode hides item names / hint text (`Rando_ActiveSlotHidesSpoiler`),
  failing closed on NULL settings.
- Entrance/door connection feeds to the auto-tracker are already
  discovery-gated: session-scoped `g_entrance_discovered` (`rando.c`) and
  `g_door_discovered` / `DoorRt_IsDiscovered` (`door_runtime.c`) — but these
  bits do not feed the reachability flood and do not persist.

Owner decisions binding this design:

1. Knowledge gating is UNCONDITIONAL — no reveal-topology toggle. The
   reveal-spoiler flow remains the escape hatch.
2. Door-shuffle gating is a flagged FINAL phase, started only after everything
   else in this change wraps.
3. Debug widgets (`dbg_*.cpp`) are out of scope (cheat tools by nature). The
   governing principle: a tracker may only reveal information the player can
   possibly know at that moment.

## Goals / Non-Goals

**Goals:**

- One source-level mechanism (knowledge-limited flood) that fixes all four
  reachability consumers at once, including the second-order pass-through
  channel (chains keep aux exits vanilla, so a mid-chain Desert Palace would
  light up Desert-ledge OW checks — display-side masking cannot fix that).
- Persisted, backfillable discovery state so save/reload and snapshot cold
  replay never regress to either all-hidden or all-revealed.
- Zero behavior change for seeds with no topology axes; zero placement change
  for all seeds (corpus byte-identical).
- A durable guard (API shape + CI check) so FUTURE player-facing surfaces are
  knowledge-limited by default, and future topology axes (e.g. the planned OW
  layout rando) must declare a discovery model.

**Non-Goals:**

- Debug widgets, the spoiler file, and the race reveal flow (intentional
  spoiler surfaces).
- Boss-shuffle kill-predicate inference and medallion-requirement inference
  (accepted LOW; rationale in the audit matrix).
- Any change to generation/placer reachability, hint content, or gameplay
  behavior. This change is display/observation-only.
- Door-shuffle gating design details beyond the deferred requirement (final
  phase owns its own detailing when it starts).

## Decisions

### D1 — The invariant

Every statement a player-facing tracker surface makes (an "avail" state, a
location name under a region, a count) must be TRUE UNDER EVERY assignment
consistent with the player's in-game observations so far. Where the surface
cannot compute that, it degrades toward showing LESS (fail-safe direction:
under-report availability, never reveal). This is the spec-level requirement in
the new `randomizer-player-knowledge` capability; per-axis discovery models
below instantiate it.

### D2 — Knowledge-limited flood, not display-site masking

Alternative considered: post-flood masking at each display site (hide avail
status for undiscovered dungeons). Rejected because (a) global counts still
leak, (b) the pass-through channel leaks OW-side availability the display site
cannot attribute, (c) every future surface re-implements the mask (the drift
farm this repo's audit history warns about). Chosen: the live compute excludes
undiscovered hidden-identity content from the region fixpoint itself — a masked
region never enters the reachable set, so nothing propagates through it.
Implementation seam: `RandoRegionDef.dungeon_id` (generated data,
`rando_logic.h`) gives region→dungeon in O(1); the flood gets an optional
exclusion input (NULL = full knowledge). Whirlpool edges are overlay edges
tagged by the warp installer; undiscovered pairs are skipped in the live view.

### D3 — Discovery choke point: per-frame observation, not per-entry hooks

Dungeon discovery is marked by observing "in dungeon module with
`cur_palace_index_x2` valid" once per frame in the existing rando frame path,
folding HC-proper into HCE (`Rando_MapDisplayDungeonFromGameDungeon`
semantics). Alternative (instrumenting every entry path — walk-ins, drop-ins,
chain seams, spiral entries, save-warp spawns) rejected: it re-opens the
"audit every vanilla one-shot" trap and misses future entry paths by default.
A newly-set bit bumps `g_reachability_state_counter`, invalidating the memo.
Whirlpool discovery marks the PAIR at the ride hook
(`OwWarp_WhirlpoolLandingIndex` call site): the layout is an involution, so one
ride reveals both directions. Cave discovery reuses the existing entry hook
that marks `g_entrance_discovered`, upgraded to persist (D5).

### D4 — Hidden-identity set derives from the ACTIVE layout tables

The set of dungeons whose identity is hidden comes from the installed layout,
never hardcoded pools: the entrance shuffle's dungeon assignment array (pool =
10 base + GT only under `shuffle_ganons_tower_entrance`; Skull Woods is not in
the pool and is never hidden) and the chains pool (`kChainsPoolDungeons`, 9).
Identity-mapped assignments STAY hidden — the player cannot know a door maps to
itself. Multi-entrance nuance: DP/TR shuffle the MAIN door only; their vanilla
side doors are technically identity-knowable, but reaching them pre-entry
requires the interior, so the conservative whole-dungeon treatment only ever
under-reports (fail-safe per D1). Seeds with no topology axes derive an empty
set — behavior and pixels identical to today.

### D5 — Persistence: one sidecar bump sized for every phase

Sidecar slot extension v12 → v13 adds a discovery block sized up front so the
cave, whirlpool, and final door phases do not churn the format again:
dungeons-entered u16, per-interior cave bits (`kEntranceMaxInteriors`),
whirlpool-pair u8, door bits (`(kDoorTbl_DoorCount+7)/8`, reserved until the
final phase), plus spare. Mirrored as a snapshot-tail TLV for M4 cold replay.
Backfill rule for v12-and-older sidecars and TLV-less snapshots: a checked
location whose region `dungeon_id` is X marks X discovered; a checked cave
location marks its interior discovered. Rationale: an in-flight save must not
regress to all-hidden (player already saw those places), and backfill from
checked state can never over-reveal (checking a location implies having been
there). All existing sidecar coupling rules apply (serialize/deserialize/size
constants/TLV in one commit).

### D6 — Unconditional gating; race interplay unchanged

No topology-reveal toggle (owner decision). Race mode keeps its existing extra
gates (item names, hints); knowledge gating applies to race and casual seeds
identically, mirroring the prize-"?" precedent. NULL-settings slots already
return NULL from `Rando_GetLiveReachability` (no reachability shown at all) —
that path stays fail-closed and needs no mask.

### D7 — Cave contents: location-level knowledge in the SAME flood (premise corrected)

Corrected during implementation grounding (claim-grounding discipline): the
original premise — that trackers list cave checks under their REBOUND region —
is FALSE. `BuildLocRegionIndex` (tracker windows, reach panel) and the
auto-tracker catalog group by the STATIC `kRandoLocations[].region_id`; the
entrance region-override store (`Entrance_ApplyRegionOverrides`) feeds ONLY the
logic flood. Cave names therefore always render under their vanilla regions —
identical for every seed, no positional leak, no search-probe oracle, and no
aggregation UI needed. The actual cave leak is the availability channel:
"cave content X — avail" reveals that SOME accessible door leads to interior X.

Fix: cave interiors join the knowledge-limited view. A location bound to a
shuffled cave interior (via the static `kCaveInteriors[].location_ids` lists;
a reverse location→interior index is built once) is suppressed from the live
reachable set until that interior's discovery bit is set. Coupled caves are
logic leaves, so location suppression is exact; the DECOUPLED variants add
cave-EXIT edges to the region graph ("every cave interior gets a decoupled
exit edge" — `Entrance_AddedEdgeWorstCase`), and those added edges are
suppressed until their interior is discovered, closing the region-propagation
channel the same way D2 does for dungeons.

### D8 — Whirlpools gated; flute NOT a leak; per-axis classification

Whirlpool edges are hidden until ridden (D3). Flute shuffle is classified SAFE
without gating: its logic edges require the flute item, and the in-game flute
map renders the shuffled destination blips (`Rando_OwWarp_FluteBlip`) the
moment the player can cast it — tracker states derived from flute edges are
therefore already player-knowledge. This classification style (edge gated on an
item whose in-game UI reveals the mapping ⇒ safe) is reusable for future axes.

### D9 — API hardening + CI guard

`Rando_GetLiveReachability()` BECOMES the knowledge-limited view (all existing
player-facing consumers inherit the fix with no call-site change). The
full-knowledge compute keeps an explicit, greppable name reserved for
generation/selftest. New script `assets/scripts/check_knowledge_consumers.py`
(pattern: `check_grant_consumers.py`) fails CI on new consumers of the
full-knowledge API or the raw assignment getters
(`Rando_Get{DungeonPrize,Medallion,Boss}Assignment`, `Rando_EntranceConnection`,
`DoorRt_GetLink`, placement `item_id` reads) outside an allowlist
(generation, gameplay delivery, spoiler writer, selftests, the audited display
sites). Escape hatch comment: `// knowledge-guard: allow <reason>`.

### D10 — Accepted-LOW inference leaks (documented, not gated)

- Boss shuffle: a boss location's avail encodes the hidden boss's kill
  predicate. Bosses are visible on approach and community boss-shuffle trackers
  accept this; closing it would need worst-case-over-pool predicate evaluation
  (noted as a possible future stretch, not planned).
- Medallion assignment: the game deliberately renders the required medallion at
  the MM/TR entrance (`medallion_icons.c`), so the requirement is one
  screen-visit from public; the tracker leaking it pre-visit is marginal.

### D11 — Door shuffle: deferred final phase

Gating intra-dungeon reachability on traversed doors changes what "avail"
means mid-dungeon (door-rando community standard, but a real UX shift) and door
shuffle is still experimental. The v13 format reserves its bits and the
deferred requirement lives in the new capability spec; implementation starts
only after every other phase (including validation/audit) wraps. Owner
re-confirms scope at that point.

### D12 — Memoization and performance

The memo key (`g_live_reach_counter` et al.) gains the discovery state (a
cheap generation counter bumped on any discovery, folded next to the existing
`sram_progress_indicator` fold). Discovery events are rare (≤ dozens per
session); per-flood overhead is an O(1) exclusion test per region visit. The
placer path passes no mask and is byte-identical in behavior and performance.

### D13 — Determinism proof

No `kGeneratorVersion` bump (placement untouched). Mandatory validation: fresh
corpus regen A/B against unmodified main built in the same environment —
byte-identical manifests required. This is the guard against accidental
generation-path contamination (house discipline: digest-neutrality is
demonstrated by regen, never claimed from code review).

## Audit matrix

Verdicts: LEAK-HIGH/MED/LOW (fix in this change unless marked accepted),
SAFE (verified no leak), INTENTIONAL (spoiler by design), OOS (out of scope,
owner decision).

### Producers — every shuffled assignment × its leak channel

| Axis (settings field) | Assignment | Channel | Verdict |
|---|---|---|---|
| `shuffle_dungeon_entrances` (+`shuffle_ganons_tower_entrance`) | door → dungeon (pool 10+GT; SW excluded) | dungeon-region avail via edge overrides | LEAK-HIGH → flood mask (D2/D4) |
| `dungeon_chains` | chain order over 9-dungeon pool | dungeon-region avail + aux-exit pass-through | LEAK-HIGH → flood mask (D2/D4) |
| `shuffle_cave_entrances` (incl. cross/decoupled variants) | door → cave interior | cave-content availability (names/grouping are static — verified, see D7); decoupled adds exit edges that propagate | LEAK-HIGH → flood location+edge suppression (D7) |
| `whirlpool_shuffle` | whirlpool perfect matching | destination-side OW avail pre-ride | LEAK-MED → edge gating (D3/D8) |
| `flute_shuffle` | flute-spot selection | edges require flute; in-game blips reveal on cast | SAFE (D8) |
| `door_shuffle` | intra-dungeon door wiring | intra-dungeon avail via door oracle ops | LEAK-LOW/MED → deferred final phase (D11) |
| `boss_shuffle` | boss per dungeon | boss-location avail encodes kill predicate | LEAK-LOW, accepted (D10) |
| `medallion_shuffle` | MM/TR requirement | MM/TR entry avail pre-visit; in-game entrance icons exist | LEAK-LOW, accepted (D10) |
| `prize_shuffle` | prize per dungeon | tracker prize cell already "?"-gated; prize-gated regions key on OWNED crystals (player-visible) | SAFE |
| Non-topology axes (pot/terrain/shop/bonk/enemy-drop/souls/traps/enemy/drop/key_rings/…) | item placement only | placement names already race-gated; no topology | SAFE (existing contracts) |

Cross-check: the producer list was derived by sweeping every installer into the
shared logic stores (`Entrance_Apply*`, `Chains_ApplyEdgeOverrides`,
`Rando_SetDoorLogicLayout`, `OwWarp_InstallLogicEdges`,
`Rando_Set*Assignment`) AND every `RandoSettings` field, so an axis absent from
this table was affirmatively classified non-topology. The planned OW-layout
feature is pre-registered as in-class (future-axis obligation, D1).

### Consumers — every player-facing surface

| Surface | Location | Verdict |
|---|---|---|
| Check Tracker | `tracker_windows.cpp` DrawCheckTracker | LEAK (availability statuses/counts; names are static — no name channel) → D2/D7 |
| Map Tracker | `tracker_windows.cpp` DrawMapTracker | LEAK (region pin/dungeon-list availability tallies) → D2/D7 |
| Reachability panel | `rando_reach_panel.cpp` | LEAK (same flood) → D2/D7 |
| Auto-tracker `reachable` array | `auto_tracker.c` | LEAK → inherits D2; spoiler-safety comment restated |
| Auto-tracker `discovered_connections`/`discovered_doors` | `auto_tracker.c` | SAFE (already discovery-gated) |
| Item Tracker (dungeon items/souls) | `tracker_windows.cpp` DrawItemTracker | SAFE (possession only, no topology; prize cell "?"-gated) |
| Hints panel | `rando_hints_panel.cpp` | SAFE (race-gated; non-race full listing is an accepted convenience, unchanged) |
| SNES HUD location grid | `hud.c` Hud_RandoDrawLocationTrackerInner | SAFE (checked-state only, no reachability, glyphs not names) |
| Medallion entrance icons | `medallion_icons.c` | INTENTIONAL (in-world reveal by design) |
| Field/shop item draws | field item sprites, shop draws | INTENTIONAL (in-world visibility at the location itself) |
| Spoiler JSON/text/in-window + reveal flow | `rando_spoiler.c`, `main.c`, native window | INTENTIONAL |
| Debug widgets | `rando_window/dbg_*.cpp` | OOS (owner decision) |

### In-game (SNES-rendered) sweep — completed, verdicts recorded

Delegated very-thorough sweep over `messaging.c`, `select_file.c`, `hud.c`,
`overworld.c`, `dungeon.c`, `load_gfx.c`, `sprite*.c`, `player.c`, `ancilla.c`,
`ending.c`, `attract.c`, `rando_dialogue.c`, and a 9-symbol getter-consumer
sweep across all of `src/`:

| Surface | Location | Verdict |
|---|---|---|
| Flute travel map blips | `messaging.c:1150` → `Rando_OwWarp_FluteBlip` | SAFE per D8 ruling: all 8 shuffled spots render on first menu-open, but the menu is available at will to any flute owner and IS the intended discovery action — the flute-edge reachability is knowledge the player "can possibly know at that moment" (owner's principle). Recorded as the canonical example of the safe classification |
| In-dungeon map check counter | `messaging.c:2122` `DungeonMap_DrawRandoCheckCount` | SAFE — totals from STATIC `kRandoRegions[].dungeon_id`, player is inside the dungeon; no shuffled input |
| OW pause-map pendant/crystal markers | `messaging.c:1492-1707` | SAFE for this class — zero rando input (vanilla tables). Separate NON-class correctness note: under `prize_shuffle` the vanilla markers mislead; tracked as its own follow-up outside this change |
| Inverted residual-portal marker | `messaging.c:1475` | SAFE — world-state setting only |
| File-select rando banner + hash badge | `select_file.c:1575-1660` | SAFE — share-string identity only, nothing placement/topology-derived |
| HUD (full sweep) | `hud.c` | SAFE — sole getter hit is the already-cleared checked-state grid; rest is owned-inventory state |
| Reward/NPC dialogue rewrites | `rando_dialogue.c:283-336` | SAFE — item naming only in DELIVERY messages at the location; pre-purchase prompts deliberately item-blind |
| Ganon crystal-requirement text | `rando_dialogue.c:261` | SAFE — in-world reveal at the fight, intended |
| Soul/trap pickup text | `rando.c:835-892` | SAFE — rendered at receipt |
| Field/shop/take-any item draws + OW glints | `rando.c:3311/3327/2021/4250`, draw sites | SAFE — render the placed item where the player is standing; current-screen only; not the topology class |
| Boss render redirect / falling-prize recolor | `dungeon.c:3850/4818` | SAFE — fires only when the boss room loads / after the kill (gameplay delivery) |
| Whirlpool re-pair hook | `overworld.c:2406` | SAFE — executes the travel; no pre-travel display |
| Credits/title | `ending.c`, `attract.c` | SAFE — no rando references |

Consumer-confinement results (hardens D9): `Rando_GetLiveReachability` has NO
consumers outside `auto_tracker.c` and `rando_window/*`; `DoorRt_GetLink` none
outside `door_runtime.c`/`auto_tracker.c`; `Rando_EntranceConnection` none
outside `rando.c`/`auto_tracker.c`; all other getter hits are generation,
selfcheck/savestate plumbing, or pre-generation settings UI. These are the
exact allowlists `check_knowledge_consumers.py` starts from.

### Fresh-eyes audit outcome (post-implementation, 2026-07-25)

Independent review of the final diff. Verdicts after grounding each claim
against source (a delegated diagnosis is a hypothesis until verified):

| # | Claim | Verified? | Action |
|---|---|---|---|
| 1 | Whirlpool discovery marks the wrong partner index — `OwWarp_WhirlpoolLandingIndex` returns the bird-ROW source `vanilla_partner(mu)`, not the per-seed partner the edge tags use, so a ride un-hid an UN-RIDDEN pair and left the ridden return trip hidden | **CONFIRMED** (real invariant violation) | FIXED: mark `wp_partner[tbl]`; `OwWarp_SelfCheck` now pins the two indices apart so a regression fails there |
| 2a | Cave suppression misses POT checks inside shuffled interiors | **REFUTED** — `Settings_PotShuffleForcedOff` == `Settings_EffectiveShuffleCaveEntrances`, so `pot_shuffle` normalizes OFF under cave shuffle; no pot check exists on such a seed | none |
| 2b | …misses ENEMY checks | **REFUTED** — `Settings_EffectiveEnemyDropChecks` lowers `All`→`Dungeon` under `Settings_EffectiveAnyEntranceShuffle`, and the cited cave/interior rows are all-tier-only | none |
| 2c | …misses SHOPSANITY slots (no normalization) | **PARTLY REAL, out of scope** — shop slots in pool interiors (e.g. Kakariko Shop room 0x11F == interior 32) are absent from `kCaveInteriors[].location_ids`, so entrance shuffle never region-overrides them. That is a PRE-EXISTING generation-soundness gap (the PLACER holds the same wrong binding), not a hidden-assignment leak: the binding is the vanilla region on every seed, so nothing seed-specific is revealed. Spun off as its own task |
| 3 | Guard leaves `Reachability_Snapshot` / `Rando_ActiveOwWarpLayout` / `Chains_RuntimeGetSession` open; headers unscanned | **CONFIRMED** | FIXED: all three guarded, headers scanned with a prototype-vs-call discriminator (unit-checked) |
| 5 | Retro take-any marks the interior of the door's table entry, but the redirect overwrites `which_entrance` afterwards | **CONFIRMED** (inert today — Retro refuses cave shuffle) | FIXED: cave marking gated on effective cave shuffle, so the stale bit is impossible rather than merely unread |
| 6 | Comments name the type-2 TLV; the code uses type-10 | **CONFIRMED** | FIXED |
| 7 | Save delta omits `discovered_exits`; proposal keeps a dissolved search-hardening bullet | **CONFIRMED** | FIXED |

Areas the audit checked and found clean: shared-`g_reachability` residue (the
masked flood always `memset`s; `Logic_ExpandReachability`'s only caller seeds
with a full flood first), region-admission coverage across base/inverted/added
edges, mask derivation against every entrance mode, v13 offsets vs the layout
comment, TLV symmetry, backfill over-reveal, slot-switch reset ordering, and
the four reachability consumers.

## Risks / Trade-offs

- [Mask leaks into generation → nondeterminism] → mask is an explicit
  parameter defaulted to full knowledge; the placer path never passes it;
  selftest asserts a generation flood after a live masked flood is unaffected;
  D13 corpus A/B is the hard proof.
- [Under-reporting availability (DP/TR side doors, conservative hiding)] →
  fail-safe direction by D1; UX affordances ("(unexplored)" tags, "N dungeons
  not yet entered" summary) tell the player exploration is expected.
- [Backfill over-reveals on tampered/edge-case saves] → backfill only ever
  derives from checked locations (being there is implied); no path derives
  discovery from placement or settings.
- [Search/name masking bypass via a new surface] → D9 CI guard + the new
  capability's future-axis obligation.
- [Sidecar v13 coupling mistakes] → single-commit rule for
  serialize/deserialize/size/TLV; existing sidecar round-trip selftests extended
  to the new block.
- [Memo staleness (discovery not reflected)] → discovery bump feeds the
  existing counter; selftest covers set-bit → recompute.

## Migration Plan

Forward: v13 readers accept v12 (and older) with backfill; snapshots without
the TLV backfill the same way. Rollback: reverting the binary loses only
discovery state (older readers ignore the unknown extension tail per the
sidecar's length-tolerant format); no save corruption in either direction.

## Open Questions

None blocking — the three scope questions were decided by the owner
(unconditional; doors last; debug widgets out of scope). The door phase
re-confirms its own scope when it starts.
