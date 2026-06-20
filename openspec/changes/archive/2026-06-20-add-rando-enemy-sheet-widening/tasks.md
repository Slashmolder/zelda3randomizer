## 1. Foundation — palette model + safety, flag still OFF (behavior unchanged)

- [x] 1.1 Add `uint32 Dungeon_GetSpritePaletteSig(int room)` to `src/dungeon.c` + decl in `src/dungeon.h`: returns the packed `(pal1<<16 | pal2<<8 | pal3)` from `kDungPalinfos[GetRoomHeaderPtr(room)[1]]`, or a `0xFFFFFFFF` sentinel for out-of-range. Pure; `kDungPalinfos`/`GetRoomHeaderPtr` are already in scope there (D2).
- [x] 1.2 In `shuffle_enemies.c`, forward-declare `Dungeon_GetSpritePaletteSig` (same pattern as `Dungeon_GetRoomSpritePtr`); add a deduped distinct-signature table (built from the signatures seen during the scan, ≤41 ids) and `g_enemy_dungeon_palette[ES_TABLE_LEN]` as a `uint64` bitset over signature ids. Reset in `invalidate_vanilla_context_table` (D1, D2).
- [x] 1.3 Extend the dungeon scan: in `build_vanilla_context_table`, for each room derive its signature id (from `Dungeon_GetSpritePaletteSig`) and mark every enemy type in its sprite list — a `mark_dungeon_palette_type` analog of `mark_overworld_palette_type` (D2). Key the scan room index the same way the header table is indexed; bounds-guard.
- [x] 1.4 Gate dungeon candidates on `g_enemy_dungeon_palette[type]` for the room's signature id in a new shared `candidate_eligible` helper (used by both the picker and the verify, so they can't drift) — active only `if (ES_ENABLE_SHEET_WIDENING)`, so the widening-off path stays byte-identical (D3). Thread the dungeon signature id from `EnemyShuffle_PickDungeon` through `pick_replacement`.
- [x] 1.5 Implement verify-then-commit in `EnemyShuffle_ReshuffleCurrentRoomSheets`: after choosing a widened sheet for an owned/unpinned slot, verify the resulting live 4-slot set still admits ≥1 valid forced-substitution target under the picker's constraints (dungeon: `killable && !cannot_key` + palette-compatible + all sheets loaded; + water variant if the room/area has a water source; + non-flying if a flying-exclude room). Revert highest-slot-first to vanilla on failure. Reuses `candidate_eligible`; pure/deterministic (D4, D5). Overworld path uses the same verify minus killable/key.
- [x] 1.6 Add `--rando-selftest` invariants: signature intern/find (pure), `widen_set_fillable` base/water/empty logic, and a flag-gated dungeon palette-gate test (fabricated allowlist). Wired into `EnemyShuffle_SelfCheck`.
- [x] 1.7 Build (WSL `make -j zelda3`, `-Werror`) + `--rando-selftest` + corpus — all green and **byte-identical** (121/121, flag still 0 ⇒ no behavior change). Foundation checkpoint PASSED.

## 2. Verification net (grounding + selfchecks, no playtest) — supersedes the pixel renderer

- [x] 2.1 Ground palette-signature completeness in the engine source (D1/D7): confirmed the dungeon sprite palette is fully determined by `(sp0l,sp5l,sp6l)` — common rows 1–4 are constant (`overworld_screen_index=0` in dungeons, `dungeon.c:8641`) and `sp6r` is pinned to 10 (`dungeon.c:6732`/`:8179`); only rare half-slot-CHR rooms differ on `sp6r` (documented cosmetic residual). This direct code-level proof replaces the "renderer is itself a hypothesis" pixel tool.
- [x] 2.2 Pipeline selfchecks in `EnemyShuffle_SelfCheck`: signature intern/find (3b), `widen_set_fillable` base/water/empty (3c), dungeon scan→mark (3d), and the flag-gated palette-gate filter test. All green; corpus 121/121 byte-identical with the flag off.
- [x] 2.3 (Optional, deferred) An offline `zelda3_assets.dat` pixel renderer remains available as belt-and-suspenders if a palette regression is ever suspected; not required to ship given 2.1, so it is not an archive gate.

## 3. Enable widening

- [x] 3.1 Bumped `kGeneratorVersion` 76→77, then reconciled to **77→78** at merge (main concurrently took 77 for its MM/TR medallion-config change — the version-drift convention applied); spec note matches.
- [x] 3.2 Flipped `ES_ENABLE_SHEET_WIDENING → 1` in `shuffle_enemies.c`.
- [x] 3.3 `make clean` + WSL `make -j zelda3` (`-Werror`) clean with widening live. (MSVC build pending the owner's Windows rebuild — no `.vcxproj` edit needed; no new `.c`.)
- [x] 3.4 Corpus regenerated via `bump_rando_corpus.py --apply`: **121/121, 0 digests changed** (placement byte-identical), manifest synced to 77. `--rando-selftest` green; version-sync / placer-determinism / embedded-data / generator-version guards all pass.

## 4. Audit, reconcile, hand off

- [x] 4.1 Independent review over the committed diff — verdict: safe to ship, no garbage/softlock paths. Findings addressed: dungeon water rooms never widened (Walking Zora is the sole ESF_WATER enemy AND key-banned, so the verify's key-capable water demand was unsatisfiable in dungeons); fixed `widen_set_fillable` to require only that the Zora source keeps its own sheets loaded (it is never substituted away). Comment/doc precision (revert-loop dual exit, determinism-given-inherited-sheets wording) also applied. Re-verified: build + selftest + corpus 121/121.
- [x] 4.2 Reconciled the spec delta + design against the as-built source (the design D4 water/determinism wording now matches the corrected `widen_set_fillable`).
- [x] 4.3 Manual runtime validation disposition: with `enemy_shuffle` on, key/shutter rooms, water rooms / Swamp Palace, and dark-world dungeon palettes remain the focused playtest checklist for future regressions. The shipped archive gate is the verify-then-commit invariant plus `EnemyShuffle_SelfCheck`; broader OAM/water/feel checks remain residual watch items rather than blockers.
- [x] 4.4 Updated `docs/randomizer.md` (enemy-shuffle prose + kGen 76→77 row) and the `sheet-reshuffle-asbuilt` memory to record widening as enabled via the dungeon palette model. (No pixel renderer — the palette-completeness grounding replaced it.)
