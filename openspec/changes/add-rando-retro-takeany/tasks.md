# Slice 3b — Retro TakeAny: tasks

Scope: **Full subsystem** (generator + runtime), one branch `pb-retro-takeany`. See `design.md`.
Conventions: `[ ]` todo, `[x]` done. Tasks gated `world_state == Retro`; non-Retro corpus must stay byte-identical.

## 0. Resolve open decisions (BLOCKING — do before coding)
- [x] 0.1 OQ4 — DECIDED: **B** (keep 3a's shipped identity-placed regular shops; amend this change's spec to match reality + correct the item list). Reviewer-endorsed; no regular-shop corpus churn. This change touches TakeAny only. Spec amendment tracked in V4.
- [x] 0.2 OQ1 — RESOLVED (review): take-any rewards never enter the fill pool; identity-place per §D3.
- [ ] 0.3 OQ2 — 1 LOC per cave vs 2 (BluePotion + BossHeart). Decide ID block (266–296 vs 266–327).
- [x] 0.4 OQ3 — RESOLVED (review): reuse existing `g_rando_checked_bitmap` (LOC-id-keyed, save/load-persisted); no new sidecar bytes.

## 1. Generator: candidate table (derive, don't transcribe)
- [ ] G1 Write a one-shot extractor (or inline in `rando_logic_gen.py`) that parses `app/Region/Standard/**` for `new Shop\TakeAny(...)`: capture `name, door_id, host_room (room_id), dest_entrance (writes value), region`. Emit a generated table + PHP source line per row. Assert count == 31.
- [ ] G1.1 Cross-check `dest_entrance → host_room` against the ROM/extracted entrance tables (`assets/extract_resources.py` → `kEntranceData_rooms` / `kOverworld_Entrance_Id`; the per-room `dungeon-NNN.yaml` are build artifacts, not committed source — do not cite them as source). Assert each dest_entrance (0x58/0x60/0x46) resolves to one of the 3 host rooms (0x112/0x10F/0x11F).

## 2. Generator: schema + registry
- [ ] G2 Add `LOCTYPE_TakeAny = 16` to `assets/rando/logic.schema.yaml` + `assets/rando/rando_logic_gen.py` (append-only).
- [ ] G2.1 Add 31 entries to `assets/rando/location_registry.yaml` (ID block per OQ2), `type: TakeAny`, `world_state_filter: [retro]`, region from G1, PHP cite per row.
- [ ] G2.2 Wire region access predicates in `logic.yaml`/`logic_parts` (heed last-wins-merge trap — diff first; no name dupes). NOTE: under `goal=completionist` the 5 active take-any LOCs join the all-locations-reachable gate (`rando_placement.c:1410-1419`), so these predicates become seed-generation constraints — get the harder regions right (Good Bee Cave, Mire, DM caves).
- [ ] G2.3 C side: handle `LOCTYPE_TakeAny` in `rando_placement.c` (near the LOCTYPE_Shop block ~891-923): active-only emission + identity-place rewards.

## 3. Generator: selection RNG
- [ ] G3 Implement deterministic `randomCollection(4)+random()` over the canonical 31-cave list (design §D6): pick-without-replacement, fixed RNG order (TakeAny before regular shops). 5th reward = `mode_weapons ∈ {swordless,vanilla} ? Rupee300 : ProgressiveSword`.
- [ ] G3.1 Mark chosen 5 caves active; emit only their slots; identity-place. Inactive → 0 entries.
- [ ] G4 Bump `kGeneratorVersion` 35→36. Regenerate corpus (deterministic runner). Stamp normalization.
- [ ] G4.1 Second-regen byte-identity check (same settings+seed → identical digests).
- [ ] G4.2 Verify NON-Retro digests unchanged (pre/post diff).
- [ ] G5 `--rando-selftest` passes; spoiler shows exactly the active take-any locations.

## 4. Runtime: keystone spike (de-risk BEFORE full wiring)
- [ ] R1 Spike: hard-code ONE known cave (20 Rupee Cave, door 0x7B → lx 0x7A → host 0x58/room 0x112) as a forced take-any behind a debug flag. Hook `Overworld_UseEntrance` (overworld.c:3340): detect on row-index `lx` (== door_id-1), capture `g_rando_takeany_door_id = lx+1`, override `which_entrance`. Confirm in-game: player lands in host room at sane spawn; `door_id` round-trips.
- [ ] R1.1 **Host-room/regular-shop collision (BLOCKER acceptance):** host 0x112 IS the Lake Hylia Shop (slot 261). Confirm the take-any branch fires on `source_door != 0` and SUPPRESSES both the room-keyed `SpritePrep_Shopkeeper` spawns (sprite_main.c:7927-7975) and `Rando_ShopDispatch` (sprite_main.c:25937); and that a normal Lake Hylia Shop visit (own entrance, source_door==0) still shows the regular shop.
- [ ] R1.2 Confirm no take-any cave is a fall-hole (Overworld_GetPitDestination path needs no hook).
- [ ] R1.3 Decide D2a vs D2b based on spike findings.

## 5. Runtime: full wiring
- [ ] R2 `Rando_TakeAnyHostEntrance(door_id)` + `takeany_lookup((room,source_door)→LOC)` tables in `src/rando/` (mirror `kRandoShopSlots`/`shop_lookup`), populated from the active set.
- [ ] R2.1 Reset `g_rando_takeany_door_id = 0` on every non-take-any entrance (D1 hygiene).
- [ ] R3 Presentation (D2a/b): present reward(s) free; suppress regular-shop spawns AND `Rando_ShopDispatch` for take-any visitors (D1b); grant via `Rando_DispatchVanillaGrant`; skip rupee cost.
- [ ] R3.1 Taken state via existing `g_rando_checked_bitmap` (D5): `Rando_MarkLocationChecked(loc)` on grant; on host-room load, if `Rando_IsLocationChecked(loc)` present nothing. No new persistence.
- [ ] R4 Save/load: verify taken state survives slot reload (checked_bitmap already round-trips via rando_save.c:213,245-246 — confirm the take-any LOC ids are within the bitmap's covered range and reload-restored).
- [ ] R4.1 **Invariant (review):** every take-any LOC that can be marked checked MUST be present in the placement table — the saved checked-bitmap byte count is `(location_count+7)>>3` keyed off `max_loc` (rando_save.c:212; placement size `(max_loc+1)*2` rando_generate.c:173). D3+D4 guarantee this (active caves identity-placed), but add an assert/selftest so a future OQ2 scheme that checks a non-emitted LOC can't silently truncate its taken bit.

## 6. Build + verify
- [ ] B1 Build Debug x64 + Release (MSBuild via .vcxproj; `/p:SolutionDir=<worktree>/` per worktree memory). `-Werror` clean.
- [ ] B2 Worktree assets/ROM mirrored (setup_worktree.py) so playtest doesn't pop modals on the user's desktop.
- [ ] B3 check_audit_guard + any rando guard scripts pass.

## 7. Review + playtest
- [ ] V1 Self-check implementation against design.md (every D/A item).
- [ ] V2 Fresh-eyes review agent (cluster-audit cadence): ask for NEW findings; cap length. Fix; iterate until clean.
- [ ] V3 User playtest: generate a Retro seed; enter ≥2 active take-any caves; confirm free grant, correct item, taken-bit, no farm, no softlock; enter an inactive cave (still vanilla); enter a host room via its own entrance (normal store).
- [ ] V4 Spec/doc sync:
  - proposal.md: 22→31; drop the `randomizer-core: MODIFIED BuildItemPool … §2(b) TakeAny extras` capability (D3 = no pool add); remove the "1 week of focused work" estimate (per `no_week_estimates`).
  - specs/randomizer-shuffles/spec.md + specs/randomizer-placement/spec.md: replace "Fisher-Yates, take first N" with pick-without-replacement (4 via randomCollection + 1 via random); per OQ4-B correct the regular-shop requirement (only `TenBombs` unconditional; ShopArrow/ShopKey gated on out-of-scope `rom.rupeeBow`/`rom.genericKeys`).
  - `openspec validate` passes; mark change ready for archive.
- [ ] V5 Drive-by: fix stale `rando_save.h:70` header comment ("@69 reserved[11]" → @69 is `flute_shovel_owned`).

## 8. Deliverable
- [ ] D Single branch `pb-retro-takeany` off main, clean history, builds, generator + runtime, ready for user review/merge. Rebuild user's main bin if any worktree build clobbered it (worktree memory).
