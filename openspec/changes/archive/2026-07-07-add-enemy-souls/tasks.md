# Tasks: Enemy/Boss Souls

## 1. Data & codegen foundations

- [x] 1.1 Verify Ganon's spawn call site (dynamic funnel vs room-data list) and Agahnim's, and record which hook covers them; adjust design if neither does
- [x] 1.2 Author the soul catalog generator: boss soul list (12), curated enemy-family grouping over `kEnemyTable` (~25-35 families), parts→parent map; emit species→soul C tables with generator-asserted invariants (every `kEnemyTable` species mapped exactly once; parts map only to boss parents; soul map disjoint from known non-enemy spawn types)
- [x] 1.3 Append soul entries to `assets/rando/item_registry.yaml` (ids 150-195, `category: soul`, `dispatch: direct_soul`); regenerate `item_ids.h`; verify `by_item_id[256]` headroom (196/256) and grep for any other 256-sized item-indexed tables (none found; `_Static_assert` lands in souls.c)
- [x] 1.4 Kill-gated-room generator (`gen_soul_room_tables.py`, REVISED from the kKillableRequiredRooms sketch): room-header kill tags (authoritative, verified against kDungTagroutines) × sprite-list soul sets × committed-door-graph flood → `soul_rooms.gen.yaml` (location/region/room/name wraps + pin rooms + audit waivers); reviewed cave bindings (Mimic/MiniMoldorm/Paradox) asserted against room-data door lists; fail-closed via kRandoSoulRoomsBaked → BuildItemPool refusal; mirrored in `setup_worktree.py` (copy-else-regenerate). check_codegen_wiring needs NO change — the C data rides logic_data.c (already guarded)
- [x] 1.5 Add generic soul icon cell(s) to `assets/rando/custom_item_gfx.png` + `kRandoCustomGfx_*` enum + `direct_grant_icons.yaml` entries for all soul ids
- [x] 1.6 `NeedsEnemySoul(soul_of(source_type))` terms emitted into enemy-check (dungeon/overworld/scripted; 1213 rows) and forced-drop (12 of 14 rows; TR Pokeys have no soul family and always spawn) predicates via the shared `gen_soul_tables.enemy_soul_by_species()` map; `source_soul` audit field per row; the virtual `HyruleCastleBigKey` inherits through the big-key drop row's predicate; boss/miniboss checks excluded (CanKill<Boss> macros carry the soul terms)

## 2. Settings, pool, and grant

- [x] 2.1 Add `souls_shuffle` to `RandoSettings`: canonical byte 28 bits 2-3 encode/decode (alongside `enemy_drop_checks` bits 0-1), `Settings_ParseCsv` key (`off|bosses|all`), share-string (rides canonical blob), settings-hash coverage, Settings_SelfCheck block; bits claimed in the `rando_settings.h` allocation comment (re-verify against live `main` before merge)
- [x] 2.2 ImGui settings combo in `rando_window` with a 1-2 fact player tooltip
- [x] 2.3 Pool construction: `pool_add` souls per tier; extend `is_progression_item()`; extend pool self-checks
- [x] 2.4 Grant dispatch: `soul_direct_grant` branch in `Rando_DispatchVanillaGrant` (set ownership bit, return `kRandoLttpSkip`); confirmation cue; confirm chest/field/receipt draw resolves the soul icon through the shared resolver

## 3. Runtime suppression

- [x] 3.1 New `src/rando/souls.c`: ownership bitfield, tier gating from the active slot, `Souls_SpriteAllowed(final_type)`; register in `zelda3.vcxproj` + Makefile (+Switch: source dir already globbed)
- [x] 3.2 Static dungeon hook in `Dungeon_LoadSingleSprite` (post-`EnemyShuffle_PickDungeon` types), consuming the slot index like the checked-enemy-check branch so `sprite_where_in_room` bits stay stable; count suppressed entries per room load
- [x] 3.3 Overworld hook in `Overworld_LoadProximaSpriteIfAlive`; the `overworld_sprite_was_loaded` bit is not set for suppressed spawns (block re-arms)
- [x] 3.4 Dynamic hook at the top of `Sprite_SpawnDynamicallyEx` returning the vanilla no-slot `-1` when suppressed
- [x] 3.5 Room-data boss secondaries (Trinexx arms, Kholdstare shell) suppress via the parts→parent map through the static hook (no extra code)
- [x] 3.6 Kill-gate hold: rando-gated not-clear in `Sprite_CheckIfScreenIsClear`/`Sprite_CheckIfRoomIsClear` and a dedicated hold in `RoomTag_GanonDoor` (own sprite loop). REVISED: no trap-close skipping — shutters close at room-header load, not in the tags, so door behavior stays vanilla and held shutter-entry rooms escape via Save-and-Quit (spec + design updated)

## 4. Logic

- [x] 4.1 `OP_SOULS_TIER_AT_LEAST`: op registry entry, codegen emit/parse, `eval_*` + context wiring, `NeedsSoul(X)` macro family
- [x] 4.2 Boss-soul gate inside CanKill<Boss> macro bodies (follows boss shuffle via OP_CAN_KILL_BOSS + kBossPoolSoul; both Agahnim slots -> Soul_Agahnim) (both Agahnim entries → Agahnim Soul); extend `eval_can_kill_boss` to AND the assigned boss's soul when a tier is active
- [x] 4.3 Static requirements: GT refight rooms (Armos/Lanmolas/Moldorm souls at bosses tier); Ganon/Agahnim souls on goal and Aga-route predicates
- [x] 4.4 Enemy tier logic: `_apply_soul_room_wraps` in rando_logic_gen ANDs NeedsEnemySoul terms into fork locations (by id), the Aga1 event (by name), and generated enemy-drop/enemy-check/pot locations (by door-region/room); target misses are HARD errors. `enemy_drop_checks=off` case: runtime drop-source EXEMPTION (`Rando_SoulDropSourceExempt`) keeps the vanilla free-key model true instead of soul-gating the key counts (design D4 as-built). Logic selfcheck probes the Mini Moldorm Cave wrap end-to-end (unreachable→soul→reachable, off-tier inert). ENABLING FIX: `place_assumed_fill_attempt` now collects placed pool items via a per-turn reachability fix-point (the documented RandomAssumed contract) — the conservative assumed-only reachability collapsed into forward-fill storms under 46 souls gating whole-dungeon clusters (a soul even landed on a location requiring itself)
- [x] 4.5 Enemy-shuffle vanilla pin via `kRandoSoulPinRooms` (41 rooms: kill-gated + forced-drop, whole-room — avoids the es_slot/list-index convention mismatch) consulted by EnemyShuffle_PickDungeon under the EFFECTIVE enemies tier; NEW derived rule `Settings_EffectiveSoulsShuffle` degrades souls→OFF under door shuffle at ANY tier (species-blind oracle; even bosses-tier soul gates exhausted the door-layout fill), read by pool/VM/placer/runtime/hash/tracker; settings-selfcheck asserts the degrade

## 5. Persistence & visibility

- [x] 5.1 Soul ownership bitfield (Souls_Flags, 8 bytes); sidecar format_version-6 extension block @24-32 (write + version-gated read, pre-v6 defaults to zero); capture at slot save, restore at activation
- [x] 5.2 New snapshot-tail type-8 Souls TLV; RandoState-accept clears souls, type-8 restores; cold-replay + round-trip selfchecks extended
- [x] 5.3 Tracker window souls section (tiered, owned/unowned). Spoiler/hints use generated Rando_GetItemName (souls named). Note: auto_tracker.c (external EmoTracker protocol) NOT wired — souls live outside g_ram (no free bytes); deferred as optional

## 6. Verification & close-out

- [x] 6.1 Clean rebuilds green: MSVC Release + WSL gcc `-Werror` (kGen header edit + make clean). One pre-existing imgui format-truncation warning (line 1983, not souls, not -Werror-fatal)
- [x] 6.2 kGen 128; 6 souls corpus entries added; rebaselined. souls=off proven digest-inert (base-build 3-way diff: 4 changed non-souls entries are pre-existing enemy-drop×pot-key drift, base reproduces corrected values). All 166 green on gcc AND MSVC
- [x] 6.3 Selftests extended (enemy-tier pass): settings degrade (souls→off under door shuffle, canonical hash), souls pool per tier + fail-closed arm when `kRandoSoulRoomsBaked==0` + door-degrade pool (Placement_SelfCheck), soul pin-table sanity (sorted/in-range/baked-consistent, Souls_SelfCheck), end-to-end kill-room wrap probe (Logic_SelfCheck: Mini Moldorm Cave unreachable→soul→reachable; off-tier inert). Pre-existing from the bosses pass: grant `kRandoLttpSkip`, soul-map disjointness, family completeness (generator assertion + Souls_SelfCheck)
- [~] 6.4 Playtest matrix (DEFERRED to main per owner decision 2026-07-06 — merge-now, playtest-on-main): empty boss room enter/exit + fight-after-soul; held kill rooms (trapdoor + shutter variants) escapable and open-after-soul; GT refights; Aga1 and Aga2→Ganon transitions without souls (no freeze); mid-game acquisition + kill-state persistence in mixed rooms; enemy tier on overworld; save/reload; snapshot cold replay
- [x] 6.5 Independent fresh-eyes review DONE (2026-07-06, four parallel reviewers over bd705c9c..HEAD — see design.md D11): 1 confirmed HIGH fixed (chains BossRoom approach edge lacked CanKillBoss + soul terms — successor gating; pre-existing chains gap made acute by souls), plus accept-bar hardening, generator guards (room-0xFF sentinel, boss-home-room mixing assertion, region-wrap drift warning), doc comments, and the auto-tracker spec-truth fix; 2 reviewer claims rejected against evidence. Post-review perf regression (review fixes pushed the 0xD004 door entries past the 60s corpus budget) fixed via VM short-circuit evaluation (`skip_pred` + selfcheck structural validation) + accept-bar conjunct reorder — digest-inert, net faster than pre-review; corpus budget 60s→120s (see D11). Spec deltas reconciled against as-built source; `docs/randomizer.md` souls section added. Archived on the branch 2026-07-06 as the pre-squash-merge step (playtest 6.4 deferred to main per owner decision).
