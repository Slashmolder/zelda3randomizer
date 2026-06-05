# Audit — in-game trackers

## Fresh-eyes audit — 2026-06-02

Read-only fresh-eyes pass over `tracker_windows.cpp`, `rando_reach_panel.cpp`,
the C-side view APIs (`Rando_FillItemView`, `Rando_BuildRuntimeCounts`,
`Rando_GetLiveReachability`), the checked-bitmap API + its SRAM scatter/gather
(`rando_save.c`), `Rando_TrackerSelfCheck`, and the dungeon-index/grant tables.
**Trackers are read-only of game state (verified — no `g_ram` writes).** No HIGH.
Findings tabulated below.

| # | Sev | Site | Finding | Disposition | Verify |
|---|---|---|---|---|---|
| T1 | MED | `tracker_windows.cpp:326,373` | HC "Map" cell may never light: HC row uses `game=1` → map bit `0x4000`, but `Map_HCE` (the HC-area map) grants to game-index **0** → `link_dungeon_map=0x8000`; `0x8000 & 0x4000 == 0`. Auditor proposes reading HC's map bit at index 0. | **DEFER — DO NOT FIX FROM SOURCE.** ⚠️ This is the exact shipped-AND-reverted HC dungeon-index-bit trap (memory `tracker_dungeon_index_bit` / `ram-bit-meaning-needs-runtime-truth`): a prior F12 *runtime dump* established HC display is game-index 1 (0x4000) and that `Map_HCE`→index 0 is the **Sewers** (a separate index from HC-proper, which has no map). The auditor is "two reasoners on the same source," which the lesson says is NOT independent confirmation. **Verify with an F12 dump before touching this**; a wrong "fix" here is a known regression. | PLAYTEST (F12 dump) |
| T2 | MED | `rando_reach_panel.cpp:62-66,99-105,128-131` | Reach panel counts/lists the 2 medallion-config slots (type-13, ids 138/139, region 0xFFFF, default-TRUE predicate) that the Check Tracker filters via `LocHiddenFromChecks`, so its totals (`n_total`/`n_reachable`) disagree with the Check Tracker by up to 2 and shows phantom "Medallion" rows under "(unbound)". | **DEFER → ready patch:** apply the same type-13 `LocHiddenFromChecks` skip at the panel's three iteration sites (add `s_loc_type` to its `BuildLocRegionIndex`). Display-only, no game state. | PLAYTEST |
| T3 | LOW | `tracker_windows.cpp:269` | Normal magic (`v.magic==0`) renders as a dim grey "Magic: 1x" chip reading as "not obtained"; half/quarter (1/2) light correctly. Cosmetic. | **DEFER → ready patch:** treat the Magic chip as always-present (state 1), let the level text convey 1x/½x/¼x. | PLAYTEST |
| T4 | LOW | `tracker_windows.cpp:1930-1940` | With no slot active, the item view falls back to raw `link_item_mushroom`/`link_item_flute` decode (by design, documented "audit LOW"); a vanilla save mid-trade could momentarily show an inconsistent mushroom/powder pair. | **NO FIX** — matches documented intent. | — |

Auditor confidence: high on T2.

> ## ⛔ T1 IS NOT A BUG — OWNER-CONFIRMED CORRECT (2026-06-02). DO NOT TOUCH.
>
> The HC Map cell (`tracker_windows.cpp:326,373`, `game=1` → bit 0x4000) is
> **correct and works in-game.** This has been raised AND wrongly "fixed" AND
> reverted multiple times — every review agent that audits it from source
> re-derives the same wrong conclusion (that it should read index 0), and applying
> that "fix" BREAKS it. The runtime truth (the owner's F12 dump) is the authority,
> not source reasoning. Per memory `ram-bit-meaning-needs-runtime-truth`: two
> reasoners on the same source is not independent confirmation, and a maintainer
> statement contradicting the analysis is a STOP SIGN.
>
> **Future auditors/agents: do not raise, "fix," or re-litigate the HC dungeon-index
> bit. The current code is the correct, owner-verified behavior.**
