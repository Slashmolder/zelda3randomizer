# Design: dungeon-chains

Grounding note: every engine/logic claim below was read from current source during
authoring (2026-07-04, main @ 204550e5, kGeneratorVersion 111). Claims that could
NOT be verified from source are marked **UNVERIFIED** and carry a verification task
in tasks.md. Symbols are cited, not line numbers.

## Context

Three shipped systems provide the seams this feature composes from:

- **Entrance shuffle** (`src/rando/shuffle_entrance.c`): permutes the overworld
  door→interior mapping via the `kOverworld_Entrance_Id` overlay (asset 126), keys
  per-seed logic overrides off unique dungeon entry regions
  (`Rando_SetEntranceEdgeOverride`), and couples exits back to the source door via
  `g_rando_entrance_exit_room` / `g_rando_entrance_force_cached`, consumed
  unconditionally at the top of `LoadOverworldFromDungeon` (`src/overworld.c`).
- **Door shuffle** (`src/rando/door_runtime.c`): proves an intra-dungeon
  transition hook exists — `Rando_DoorTransOverride(dir)` runs at the exact moment
  vanilla resolves a door transition's destination room, before
  `dungeon_room_index` is updated.
- **Boss shuffle** (`src/rando/shuffle_boss.c`): provides `kBossRoom[]` /
  `kBossHomeRoom[]` (the 10 prize-boss room ids), the per-seed
  `PredicateContext.boss_assignment` consumed by VM op `OP_CAN_KILL_BOSS` (id 19),
  and the hard-won knowledge that Blind/Kholdstare/Trinexx depend on home-room
  environment — which chains sidesteps entirely by never moving a boss out of its
  home room.

Key engine facts grounding this design:

- `Dungeon_LoadEntrance` (`src/dungeon.c`) consumes `which_entrance` against ~15
  parallel `kEntranceData_*` asset arrays (asset ptrs 11–28: room, player/camera/
  scroll coords, blockset, floor, **palace**, doorway orientation, starting BG,
  quadrants, door settings, music). The vanilla tables have 133 entries;
  `which_entrance` is byte-ranged and the C side sizes the tables from the asset,
  so appending fork records is structurally possible (`assets/compile_resources.py`
  builds `entrances = [None] * 133`).
- `Dungeon_LoadEntrance` **re-caches the `*_exit` shadow block from live variables
  on every call** (unless the death path sets `death_var5`). Mid-dungeon, the live
  link/camera variables hold *dungeon* coordinates — a naive mid-chain entrance
  load would poison the overworld-return state.
- The post-boss warp is `PrepareDungeonExitFromBossFight` (`src/dungeon.c`): it
  looks up the current boss room in `kDungeonExit_From[12]`, rewrites
  `dungeon_room_index` to `kDungeonExit_To[j]` — which is the home dungeon's
  **lobby room** (verified: entries match `kDungeons[].room`, e.g. TT 172→219 =
  0xDB, IP 222→14 = 0x0E, PoD 90→74 = 0x4A) — then transitions modules
  (pendants j<3 → module 19, crystals → module 22, AT 0x20 → module 21, GT 0xd →
  module 24). The exit ultimately resolves an overworld door from that room, where
  the coupled-exit override already intercepts.
- Boss/Prize locations in the logic YAML (`assets/rando/logic_parts/*.yaml`) carry
  a single conjunction: *path terms* AND `CanKillBoss(<dungeon>)`. There is **no
  separate "reach the boss door" predicate today**.
- Blind's fight only materializes when `dung_savegame_state_bits & 0x2000` is set
  by the Thieves' Town maiden sequence (`SpritePrep_Blind_PrepareBattle`,
  `src/sprite_main.c`) — a chain that enters Blind's room without that state gets
  no boss.
- Override capacity in `src/rando/rando_logic.c`: 512 location-region overrides,
  64 edge overrides, 256 added edges — ample for chains' ~20 entries.
- Canonical settings byte `out[25]` has exactly bits 6–7 free
  (`kSettingsCanonicalLen` stays 28); the sidecar slot header and v3 extension
  block are **full** — chains persistence needs a new extension block.
- The generation retry loop lives in `Rando_PlaceWithEntrances`
  (`src/rando/rando_generate.c`), selected by both the slot path and the headless
  `--generate-seed` path in `src/main.c`; every retry attempt must clear
  edge-override state at its top (audited entrance-shuffle bug class).

## Goals / Non-Goals

**Goals:**

- One new axis `dungeon_chains`: overworld dungeon doors each lead to an ordered
  chain of 0+ dungeons ending in a boss; every pool dungeon and pool boss appears
  exactly once across all chains.
- Bosses always fight in their vanilla home rooms with vanilla environment,
  palette, music, and palace index.
- Off = byte-identical generation (corpus) and untouched runtime.
- Deterministic, cross-platform-reproducible chain construction; slot
  persistence with regenerate-at-load and hard-fail on digest mismatch.
- Sound reachability logic: no seed ships with the goal or required locations
  unreachable; all exits from a chain return to its origin door (no stranding).

**Non-Goals (MVP):**

- Composition with entrance shuffle, door shuffle, or boss shuffle (mutually
  excluded via normalization; chains yields).
- Inverted / Retro world states; glitched logic tiers.
- Hyrule Castle, Castle Tower (Agahnim), Ganon's Tower (Agahnim 2), Ganon, and
  **Skull Woods** (with Mothula) in the pool — they stay vanilla, preserving the
  Standard opening, the Agahnim world-state trigger, the crystals→GT→Ganon
  endgame, and avoiding SW's drop-in-hole entrances (see D1). AT/GT are a
  planned **phase-2** extension (owner decision, 2026-07-04) — see the deferred
  extension note after D9; HC has no boss and no path in.
- Chain-length distribution knobs (single on/off toggle; the seed decides).
- "Chain purity" for Desert Palace (see D1/D5) — its boss approach legitimately
  routes over the outer ledge via auxiliary doors, which stay vanilla.

## Decisions

### D1. Pool and pins

Pool = 9 prize dungeons (EP, DP, ToH, PoD, SP, TT, IP, MM, TR) and their 9
vanilla bosses (`kBossRoom[]`). Nine chain-start doors = those dungeons' MAIN
overworld doors (the same main-door set entrance shuffle uses; DP/TR "contained"
doors stay vanilla, the entrance-shuffle precedent).

**Skull Woods is excluded** (owner decision, 2026-07-04), with Mothula and all
SW overworld doors staying vanilla — the same grounds on which entrance shuffle
defers SW (`entrance_registry.yaml`: front 0x28, secondaries 0x29/0x2A/0x2B,
plus drop-in holes the door overlay cannot redirect), compounded for chains by
its boss section hanging off its own burnable overworld door (SW Boss predicate
carries `HAS_ITEM(FireRod)` for exactly that excursion).

Two **pinned adjacencies** constrain the permutation (the pinned dungeon must be
the last dungeon of its chain, and its chain's terminal is its own boss — i.e.
that boss seam stays vanilla-identity at runtime):

- **TT → Blind.** Blind requires the TT-internal maiden escort +
  `dung_savegame_state_bits & 0x2000` (verified in `SpritePrep_Blind_PrepareBattle`).
  Same rationale as boss shuffle's Blind pin.
- **ToH → Moldorm.** Moldorm's boss room (7) has fall-out holes into ToH's
  interior below (door registry: "Hera Boss Inner/Outer Hole"). Pinning keeps the
  fall-and-climb-back loop inside the chain element that owns it. Un-pinning
  requires a fall-hole self-redirect (documented alternative, deferred).

Kholdstare and Trinexx are **NOT pinned** — the boss-shuffle pins existed because
foreign rooms lack their home environment; under chains they always fight at home.

**Desert Palace stays in the pool** — but only because of D5's main-door-keyed
exit rule. DP's boss route transits the front interior and exits onto the outer
ledge (side doors) to reach the back door (`CanLiftRocks` in its Boss
predicate); a blanket "every exit returns to origin" rule would teleport those
side-door exits away and make Lanmolas' seam permanently unreachable, refusing
every permutation containing DP. With aux-door exits vanilla (D5), the ledge
walk works mid-chain and DP's back-section boss seam feeds its successor
normally. Source grounding: DP is not a sealed, chain-pure pocket. Its overworld
screen carries four exterior entrance ids (9 main, 10 east, 11 west, 12
back/boss), while the door-shuffle oracle treats main as the only initial
origin, east as a destination-only vanilla portal, and west/back as a staged
walk-inaccessible ledge pair enabled only after the player reaches either from
inside. Therefore chained DP may expose vanilla exterior side pockets, but those
pockets are not chain starts, stay vanilla-coupled, and do not affect soundness.

### D2. Chain construction (generation side)

`Chains_Compute(seed, attempt)` in new `src/rando/shuffle_chains.c`, pure in its
inputs (xoshiro via rando_rng, dedicated salt), producing:

- `chain_door_first[9]` — per chain-start door: first element (dungeon id, or
  boss terminal id).
- `chain_successor[9]` — per pool dungeon: next element after its boss seam
  (dungeon id or boss terminal id).
- `terminal_boss[9]` — bijection chains→bosses.

Construction: Fisher-Yates the 7 free dungeons into a random order, distribute
them into 9 ordered (possibly empty) segments via a uniform composition, append
the 2 pinned dungeons (TT, ToH) as final segment elements of their own chains
with their pinned bosses, Fisher-Yates the remaining 7 bosses onto the remaining
terminals.
A 24-bit digest over the resolved mapping (door-shuffle pattern) guards
persistence. `Chains_SelfCheck` asserts: every pool dungeon appears exactly once,
every boss exactly once, pins respected, recompute-stability, gcc==MSVC digest
(extend `--rando-selftest`).

### D3. Runtime hop = entrance-style load (never a door transition)

Every chain arrival — from an overworld door or from a boss seam — is a full
entrance load (`Module_PreDungeon` → `Dungeon_LoadEntrance`), because only an
entrance load reinitializes palettes, tilesets, music, sprites, room bounds, and
`cur_palace_index_x2` (from `kEntranceData_palace`). Door-transition-style
redirects across dungeons would carry none of that (door shuffle's whole-tableau
lessons apply within a dungeon; across dungeons the tableau isn't even the right
object).

- **Dungeon elements**: `which_entrance` = the target dungeon's vanilla entrance
  id (from `kDungeons[]`). Death-continue then naturally respawns at the current
  hop's lobby.
- **Boss terminals**: `which_entrance` = a **synthetic entrance record**, ids
  133+, one per pool boss room, appended to the `kEntranceData_*` tables by
  `assets/compile_resources.py` (fork-asset precedent: custom item art). Fields
  (spawn at the room's vanilla entry point, camera/scroll/quadrants, blockset,
  **palace = the boss's home dungeon**, music = home dungeon theme) are emitted by
  a committed generator script that derives them from room/entrance assets — never
  hand-transcribed. **Palace = home dungeon is load-bearing**: it makes the prize
  dispatch (`RoomTag_GetHeartForPrize` → `Rando_GetBossPrizeLocation(didx)`) and
  Kholdstare's room-header environment resolve identically to vanilla.
- **Hop loads MUST skip the `*_exit` re-cache.** `Dungeon_LoadEntrance` caches the
  overworld-return block from live variables; mid-chain those are dungeon
  coordinates. A dedicated `g_chains_hop_pending`-style flag (consumed-at-top, per
  the audited pattern) suppresses the cache block the way `death_var5` does — but
  it must be a NEW flag, not `death_var4/5` (the Inverted Dark Chapel stale-flag
  lesson). The origin-door capture from the chain's first entrance thus survives
  the whole chain.
- **No tagalong/follower crosses a chain boundary.** Source audit:
  `CanEnterWithTagalong` is a vanilla overworld-door gate and admits only a small
  follower subset by entrance id; a chain-start redirect keeps that gate, but a
  mid-chain boss-seam hop bypasses it entirely. `Module_PreDungeon` then calls
  `Dungeon_LoadEntrance` and `Follower_Initialize`, clearing only the super-bomb
  follower (`follower_indicator == 13`) because normal entry already passed the
  overworld gate. Meanwhile `Follower_HandleTrigger` is hard-coded to specific
  dungeon rooms / overworld screens, and follower 6 is the TT maiden that spawns
  Blind only in room 0xAC (`Follower_BasicMover` / `Blind_SpawnFromMaiden`).
  Policy: before every chain-start redirect and every mid-chain hop load, a chains
  helper clears active tagalong state (`follower_indicator`, `follower_dropped`,
  `tagalong_var5`, transient tagalong event/message state; if clearing the super
  bomb, also clear its HUD/state as `Module_PreDungeon` does), then lets the
  destination entrance load reinitialize the ring at Link's new coordinates.
  Vanilla-internal followers may still be acquired and resolved wholly inside
  their owning dungeon. TT's maiden is preserved because TT→Blind is a pinned
  identity seam and therefore never takes a chains hop.

### D4. Seam interception

Two redirect rules, both table-driven off the 9 pool boss-room ids:

1. **Into a boss room** (only reachable via a dungeon's vanilla boss seam): hook
   the destination-resolution sites for door, stair, and hole transitions (the
   `Rando_DoorTransOverride` seam proves the door site; stair/hole resolution via
   `dung_hdr_travel_destinations` needs its own hook — the IP Kholdstare seam is
   a drop; the seam-table generator classifies every seam). When the resolved
   destination ∈ pool boss rooms and
   the successor differs from that room's own boss (pinned adjacencies are
   identity ⇒ no divert ⇒ pure vanilla), divert to an entrance-style load of
   `chain_successor[current dungeon]`.
2. **Out of a terminal boss room**: any outbound transition from a boss room
   reached as a chain terminal diverts to the chain exit (return to origin door).
   This seals the walk-backward leak into the boss's home dungeon, and makes a
   revisited cleared terminal non-sealing regardless of shutter state. Owner
   playtest (2026-07-04 local): after killing a vanilla boss, before taking the
   reward/warp, the entry door remained locked; rule 2 therefore is not expected
   to fire pre-warp through the entry shutter, but still covers any real outbound
   transition from a terminal boss room.

Overworld chain-start doors need no new hook: reuse the entrance-shuffle door
overlay (`kOverworld_Entrance_Id`, asset 126) to map the 9 main doors to their
first element's entrance id (vanilla or synthetic), plus the entry hook that arms
exit coupling.

### D5. Exit coupling: MAIN-door exits return to the origin door

Chains keeps its OWN origin-door session state (`g_chains_origin`-style), armed
at chain-start entry. It cannot piggyback the entrance-shuffle global
(`g_rando_entrance_exit_room`): that one is consumed at the top of EVERY
`LoadOverworldFromDungeon` — an aux-door exit mid-chain (DP's ledge round-trip)
would eat it and silently un-couple the rest of the chain. Instead the chains
state survives hops AND aux exits, and is consumed only when the origin
substitution actually fires (main-door exit, post-boss warp, or containment
divert), or overwritten by the next chain-start entry. When unset (e.g. a
dungeon entered purely via aux doors with no prior chain entry), exits resolve
fully vanilla.

**The origin substitution is keyed to the dungeon's MAIN door, not to every
exit.** After the room-keyed exit search resolves an overworld door, substitute
the origin only when the resolved door is the chained dungeon's main door (or
the exit is the post-boss warp / a chain-terminal containment divert).
Rationale: a blanket every-exit rule severs vanilla-required aux-exit routes —
DP's boss approach transits the outer ledge via its side doors, and TR's
balcony doors are the route to Mimic Cave. Aux-door exits therefore resolve
vanilla; they are strand-safe because the aux doors are unshuffled and
re-enterable (re-entry loads the same interior the player just left). DP's
west/back ledge pair is explicitly staged as walk-inaccessible outside terrain
in the door oracle and enabled by reaching either member from inside; DP east is
the vanilla destination-only side portal. TR's balcony/mountainface portals
(`TR Eye Bridge`, `TR Big Chest Entrance`, `TR Lazy Eyes`) have
`fork_region: null` in `door_portals.yaml`, so the logic model never treats them
as independently enterable from overworld terrain; the door oracle only enables
the TR chest/lazy ledge after the interior route reaches it. The stranding case
motivating the coupling — IP's island, only escapable with swim/mirror — is a
main-door emergence and stays covered.

Covered exits:

- walking out a chained dungeon's main door (room-keyed search branch —
  consumed-at-top override, already audited);
- the post-boss warp: `PrepareDungeonExitFromBossFight` rewrites
  `dungeon_room_index` through `kDungeonExit_From[]` / `kDungeonExit_To[]` to
  the boss's home lobby, sets `saved_module_for_menu = 8`, then routes pendant
  bosses (`j < 3`) to module 19 and crystal bosses (`j >= 3`) to module 22.
  Module 19 runs the shared `kModule_BossVictory[]` spotlight close/open path;
  `IrisSpotlight_ConfigureTable` resets `submodule_index` and assigns
  `main_module_index = saved_module_for_menu`. Module 22's
  `Module16_04_FadeAndEnd` does the same assignment directly. In both normal
  prize paths the next module-8 load reaches `PreOverworld_LoadProperties`,
  which calls `LoadOverworldFromDungeon`, so the origin substitution can resolve
  the warp to the origin door. This is a correctness fix (a vanilla warp-out at
  e.g. Vitreous would strand a flute-less player on the Mire pad). The Aga1
  (`main_module_index = 21`) and GT (`main_module_index = 24`) branches bypass
  this normal prize route and remain out of scope for v1.
- Mirror-in-dungeon warps to the current hop's entrance (vanilla per-entrance
  behavior) and stays inside the chain. **UNVERIFIED**: exact mirror-portal
  interaction with synthetic entrance ids — verification task.

Entries through untouched auxiliary doors (DP W/E/back, TR contained) do not arm
chain coupling: vanilla caching couples them back to the aux door itself,
exactly as today.

### D6. Logic model: boss-room region factoring + pure edge overrides

Static codegen restructure (`assets/rando_logic_gen.py`), active regardless of
the axis, semantically identity when off:

- For each pool dungeon `D`, emit region `<D>_BossRoom`, an edge
  `region(D) → <D>_BossRoom` carrying `boss_approach(D)`, and re-home the Boss and
  Prize locations to `<D>_BossRoom` with predicate `CanKillBoss(D)`.
- `boss_approach(D)` is **derived, not authored**: the codegen parses the Boss
  location's predicate conjunction and strips the single `CanKillBoss(D)` term,
  asserting exactly-one occurrence (generator-is-the-spec discipline). The
  conjunction `boss_approach(D) AND CanKillBoss(D)` factored through a region is
  logically identical to today's flat predicate — **corpus regen must confirm
  digest-neutrality**; any default-seed movement is a red flag, not a version bump.

Chains-on then installs only edge overrides (existing APIs, at retry-attempt top):

- Each `<D>_BossRoom` has exactly one inbound edge (satisfies the unique-to_region
  keying that `Rando_SetEntranceEdgeOverride` requires). Override it: dungeon
  `D_i`'s boss edge retargets to its successor's entry region — the successor
  therefore requires reaching `D_i`'s region (however it is reachable — chain,
  or aux doors for DP) AND `boss_approach(D_i)`: path, small/big keys,
  torches, overworld excursions, all inherited from the vanilla predicate.
- `Rando_AddEntranceEdge` supplies each terminal `<D_b>_BossRoom`'s new inbound
  edge from the chain's last element (pred = that element's `boss_approach`) or
  from the chain-start door's edge for length-0 chains (door predicates — MM/TR
  medallions — stay on the door, the playtest-confirmed medallion-on-spot model).
- Chain-start door edges retarget via the same pure edge-override mechanism
  entrance shuffle Stage 2 ships today: `Entrance_ApplyEdgeOverrides` resolves
  `from_lobby` from `dungeon_override_key(&kDungeons[ix])` and `to_lobby` from
  the assigned dungeon, then calls `Rando_SetEntranceEdgeOverride(from_lobby,
  to_lobby)`. Logic application rewrites any `kRandoEdges[e].to_region` through
  `Rando_GetEntranceEdgeOverride`, so the predicate remains attached to the
  source door/edge and only the destination changes.
- Source grounding for the DP/TR multi-inbound question: `dungeon_override_key`
  is the interior/lobby region when `interior_region_name` is present; otherwise
  it is the entry region. DP has no separate interior key, so its main door edge
  targets and keys `DesertPalace_Lobby`; the auxiliary east/west/back doors stay
  outside the shuffle/chains overlay. TR is a two-stage main door: overworld
  approach edges target `TurtleRock_Entrance`, and the gated edge
  `TurtleRock_Entrance -> TurtleRock_Lobby` carries the medallion/sword/Somaria
  predicate. The override key is `TurtleRock_Lobby`, not the multi-inbound
  approach region; this keeps the gate with the source spot while allowing the
  two approach edges to remain vanilla fan-in.
- Note on source comments: `shuffle_entrance.c` still has stale prose describing
  Stage 2 as "6 cleanly single-overworld-entrance dungeons" and saying TR's
  override remaps both approach edges. The implementation, selfcheck, and YAML
  edges above are the authority for chains; chains should key chain-start door
  retargeting on the same lobby/interior override key and keep DP/TR
  auxiliary/approach edges out of the overlay.

No new VM op is needed. `boss_assignment` stays NULL (vanilla) — `CanKillBoss(D)`
correctly evaluates D's own boss, because bosses never leave home rooms.

This edge model is also what makes **placement respect chain order** (owner
concern, 2026-07-04): a successor's entry region is reachable only through
`boss_approach(D_i)`, which includes D_i's big-key/small-key terms — so the
assumed fill (which only places an item at locations reachable *without* it)
structurally cannot place D_i's big key past D_i's boss seam. Example: chain
[EP, ToH] — EP's big key can never land in ToH or the terminal, or the seed
would be circular. Wild-keys-off constrains keys to their own dungeon as today;
wild-keys-on is the case the edges actively protect, and a chains+wild-keys
corpus entry locks it.

Generation rides `Rando_PlaceWithEntrances`: compute chains per attempt (reset
override state at attempt top — the accumulation bug class), install edges, run
`Place_AssumedFill`, and require the entrance-family **full-reachability gate**
(all locations reachable, not merely goal-completable) before accepting.

### D7. Settings, normalization, exclusions

`dungeon_chains` = canonical byte `out[25]` bit 6 (no `kSettingsCanonicalLen`
change, no cascade). `apply_derived_rules`: chains is coerced OFF unless
`entrance axes == 0 AND door_shuffle == vanilla AND boss_shuffle == off AND
world_state ∈ {Open, Standard} AND logic tier == NoGlitches`. One-directional
(chains yields to everything, matching door-yields-to-entrance precedent), so no
existing normalization rule changes. Default packs to 0 → settings-hash stable.

### D8. Persistence

New sidecar extension block (v3 block is full): `{chains_present, chains_attempt,
chains_digest24[3]}`, bump `kRandoSidecar_FileFormatVersion`, keep older files
readable (additive pattern used by hints/v3). At slot activation
(`Rando_ActivateSidecarSlot` → new `Chains_RuntimeInstall`): recompute
`Chains_Compute(seed, attempt)`, **hard-fail the slot on digest mismatch** (door
shuffle precedent — wrong cross-dungeon redirects are worse than a refused slot),
install the door overlay + seam tables + logic edges; `Rando_DeactivateSlot`
tears all of it down. M4 snapshot cold-replay rebuilds via the same install.

### D9. Spoiler, UI, share string

- Spoiler: `chains` section (JSON + text) after the entrance sections in
  `rando_spoiler.c`: `door → [dungeon, ...] → boss` per chain.
- UI (`rando_window.cpp`): one checkbox in the world-structure panel, entrance-
  panel pattern; tooltip = 1–2 durable player-facts ("Dungeon doors lead to a
  chain of 0+ dungeons ending in a boss. All exits return to the door you
  entered."). Normalization-grayed like door shuffle under conflicts.
- Share string: no new fields — chains regenerates from (seed, attempt, axis bit)
  like entrance shuffle; `kGeneratorVersion` bump covers cross-version refusal.

### Deferred extension (phase 2): Castle Tower + Ganon's Tower in the pool

Owner-confirmed as a follow-up change, NOT v1. Recording the entanglements
grounded during this design so phase 2 starts from facts, not re-derivation:

- Pool becomes 11 dungeons / 11 bosses / 11 chain-start doors (Agahnim 1 and
  Agahnim 2 join as terminals). HC stays out permanently — no boss, and the
  Standard opening runs through it.
- **Agahnim 1's post-boss branch is special**: `PrepareDungeonExitFromBossFight`
  (room 0x20 branch) sets `sram_progress_indicator = 3`, flips
  `savegame_is_darkworld`, and routes module 21 — the vanilla "dropped onto the
  pyramid" world-state transition. A chain-origin return must deliberately
  preserve (or re-site) those side effects; this is a
  vanilla-state-as-proxy-class hazard, not just a warp redirect.
- **Agahnim 2's branch** (room 0xd) routes module 24 (pyramid-hole / Ganon
  flow); Ganon access semantics after a chained Aga2 kill need an explicit
  decision.
- **GT's crystal gate lives on its overworld door edge** — chaining INTO GT
  mid-chain bypasses it unless the crystal predicate travels with GT's chain
  slot. Edge predicates support this (medallion-on-spot precedent), and the
  entrance-shuffle GT opt-in already proved the retry loop rejects circular
  crystal permutations.
- Aga1/Aga2 share sprite 0x7A discriminated by `is_in_dark_world`
  (`shuffle_boss.c`) — valid under chains since both fight at home, but both
  rooms have `kBossRoom[] == 0xFFFF` today; the chains seam tables would need
  their room ids added explicitly.

## Risks / Trade-offs

- **[Mid-dungeon entrance load has no vanilla precedent]** (agent-verified: no
  interior→interior transition exists in vanilla). → Route through the same
  module sequence the death-respawn in-dungeon reload uses (Module_PreDungeon
  from inside module 7), with a fade; instrument the whole decision chain on
  first playtest (runtime-debugging discipline). This is the highest-risk item;
  build it first behind the flag and playtest a single hop before anything else.
- **[Hole/stair seam divert (IP)]** — diverting a fall mid-animation
  may need the divert at resolution time, before the fall submodule commits. →
  Seam-table generator classifies every seam; playtest IP's drop specifically.
- **[Boss shutters do not reopen post-kill before reward/warp]** — owner F12
  playtest (2026-07-04 local) killed a vanilla boss, skipped the reward, and the
  entry door remained locked. → D4 rule 2 is still required for cleared terminal
  re-entry / real outbound transitions, but should not be relied on as a
  pre-warp entry-shutter escape path.
- **[`*_exit` poisoning by hop re-cache]** → dedicated skip flag (D3); assert via
  g_ram diagnostic counters during bring-up, revert counters before merge.
- **[Follower/tagalong state leaking across chains]** — resolved policy in D3:
  chain boundaries clear tagalongs instead of carrying them. This avoids
  room/screen-specific follower triggers firing in foreign dungeons; TT's maiden
  remains vanilla because the TT→Blind seam is pinned identity and never hops.
  Runtime implementation still needs a small clear helper and a playtest that a
  follower cannot leak through a chain-start door.
- **[Prize/cutscene one-shot re-triggers]** — re-traversing a chain re-crosses
  seams and re-enters cleared boss rooms; prize grants are already gated on
  `Rando_IsLocationChecked` (prize-shuffle fixes), but the re-enabled-one-shot
  audit (CLAUDE.md corollary) must sweep every boss room tag. → Explicit audit
  task; the vanilla-state-as-proxy checklist applies to every new gate.
- **[Region factoring moves placement digests]** → corpus regen + 3-way diff
  against fresh main; identity expected; any default-seed movement blocks merge.
- **[Asset regeneration required]** (synthetic entrance records change
  `zelda3_assets.dat`). → Asset-version bump + extractor/compiler parity; the
  chest-table fail-open trap shows missing gitignored inputs fail silently —
  add a `Chains_SelfCheck` that the synthetic records exist and are non-zero
  (fail CLOSED at slot activation).
- **[Corpus/selftest blindness]** — both seams are runtime; generation tests
  can't see them. → Playtest matrix is a first-class merge gate: chain-0 boss
  door; multi-hop chain; DP as a chain element (ledge side-door exit → back-door
  boss approach); TR as a chain element (balcony exit stays vanilla, Mimic Cave
  route intact); IP drop seam; death-continue mid-chain; S&Q; mirror; post-boss
  warp from a terminal reached via a chain (pendant AND crystal boss);
  cleared-terminal re-entry.
- **[Aux-exit substitution keying]** — the main-door-keyed substitution (D5)
  must not regress the consume-at-top discipline on the coupling globals (the
  audited stale-global class); the substitution decision happens AFTER the
  search resolves, in one place. → Self-check + the DP/TR playtest rows above.

## Migration Plan

Feature-branch loop per CLAUDE.md: branch `feature/dungeon-chains`; land codegen
restructure (D6) FIRST with a corpus-identity proof while chains is inert; then
generation + persistence (corpus entries added, kGeneratorVersion bump once, at
the end); runtime last, playtested per the matrix above; independent fresh-eyes
review before done; archive on the branch; squash-merge. Rollback = the axis
defaults off and normalizes off; the codegen factoring is identity, so reverting
runtime code cannot affect existing seeds.

## Open Questions

1. Exact module choreography for the hop load (fade timing; module 19 vs 22
   pendant/crystal differences under redirect) — resolve by instrumented playtest
   of one hop before broad implementation.
2. DP main-door edge keying (Stage-2 code read), DP W/E side-door
   ground-reachability class (chain-pure vs partially open-world — flavor
   only), and TR balcony re-enterability — resolved by the seam-table
   generator + asset reads at implementation start.
3. Follower policy at hop boundaries (drop vs carry).
4. Whether the synthetic boss-room entrance records can be fully derived offline
   or need a one-time `ZELDA3_CAPTURE_ARRIVALS`-style capture pass (fallback
   exists either way; capture is user-driven playtest time).
