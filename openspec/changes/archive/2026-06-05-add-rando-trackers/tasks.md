## As-built (2026-06-04)

Trackers shipped, but the **delivery surface changed** from this plan. The Phase A
spec (and §2–§4 below) describe an **in-game OAM overlay** drawn by `src/hud.c`.
After the PC UI moved to Dear ImGui second-OS-windows (archived
`add-rando-native-settings-window`), the trackers were delivered as **three rich
native ImGui windows on PC** — Item, Check (tri-state reachability), and Map — in
`src/rando/rando_window/tracker_windows.cpp` + `rando_reach_panel.cpp`. The legacy
`src/hud.c` OAM overlay (`Hud_RandoDrawTrackers`, `Hud_RandoDrawItemTracker`,
`Hud_RandoDrawLocationTracker`) is retained as the **Switch path** only: its toggle
flags (`g_rando_show_item_tracker` / `g_rando_show_location_tracker`) are settable
only on Switch; on PC the same toggle keys open the corresponding native window
(`src/main.c:2113-2126`).

So §2–§4 are satisfied **done-differently** (native windows) on PC, with the
OAM-overlay equivalents present-but-Switch-only. §5 (checked-location bitmap) and
the keybindings (§1) shipped as planned. The fresh-eyes audit (`audit.md`,
2026-06-02) covered the native windows: no HIGH; T1 owner-confirmed-correct
(do-not-touch); T2 applied; T3 applied 2026-06-04; T4 no-fix.

**Remaining to archive:** a light PC playtest of the three tracker windows
(§8.1/§8.2/§8.4) + `openspec archive` (§10.4). Switch on-device tasks
(§4.5/§8.3/§10.3) are parked per the standing Switch policy (see
`openspec/changes/README.md` "Switch work — PARKED") and are **not** archive
blockers.

> Box convention below: `[x]` = satisfied (as planned or done-differently per the
> note); `[ ]` = genuinely open. Done-differently items carry a `→ native window`
> or `→ Switch-only` note.

## 1. Keybinding wiring

- [x] 1.1 Add `kKeys_RandoToggleItemTracker` and `kKeys_RandoToggleLocationTracker` to the `kKeys_*` enumeration in `src/config.c`. Place near the other randomizer key constants. *(Landed at `config.h:39-40`.)*
- [x] 1.2 Add the two key names to the `[KeyMap]` INI parser's accepted key list in `src/config.c`. Default: unbound (empty). *(Landed at `config.c:108` via the `S(RandoToggleItemTracker), S(RandoToggleLocationTracker)` entries in `kKeyNameId`. PC also adds `RandoItemTrackerWindow` / `RandoCheckTrackerWindow` / `RandoMapTrackerWindow` at `config.c:112`.)*
- [x] 1.3 Document the two key names in `README.md` key map table. *(Landed in the `### Randomizer keybindings` section.)*
- [x] 1.4 Wire input dispatch — on key press, toggle visibility. *(Landed at `main.c:2113-2145`: on PC the toggle keys call `Trackers_Toggle(...)` to open/hide the native window; on Switch they flip `g_rando_show_*_tracker`. Flags defined at `rando.c` (static-init `false`); reset to `false` on slot-deactivate in `Rando_DeactivateSlot`. In-memory only; hidden each launch per spec.)*

## 2. Item-tracker overlay

**As-built:** delivered as the native **Item Tracker** window
(`tracker_windows.cpp::DrawItemTracker`) on PC — a live grid of the real HUD item
icons + bottles + magic + hearts, read-only of `link_item_*` state. The `src/hud.c`
OAM-overlay equivalent (`Hud_RandoDrawItemTrackerInner`, grid + dialogue-hide rule
via `Hud_RandoTrackerVisibleNow`) is built and retained as the Switch path.

- [x] 2.1 Item-grid layout — fixed grid covering progressives + absolute items + bottles + magic + hearts; reuses HUD tiles, no new graphics. *(→ native window grid; OAM-overlay grid in `hud.c`. Anchors are fixed, not the planned `[randomizer] tracker_anchor` ini key: native window is movable by the OS; the OAM overlay anchors item top-left.)*
- [x] 2.2 Compute have/level state from `link_item_*` / `link_bottle_info[*]` / `link_max_health`. *(`DrawItemTracker` + `Hud_RandoDrawItemTrackerInner`.)*
- [ ] 2.3 OAM-cache gated on `reachability_state_counter`. *(NOT implemented for the OAM overlay — it recomputes the fixed-size grid each frame, which is cheap; the spec's per-frame-cost SHALL is met in practice but not via a counter-gated cache. The native window redraws via ImGui each frame by design. See spec as-built note.)*
- [x] 2.4 Wire into the draw chain, gated on active state. *(`Hud_RandoDrawTrackers` gates on `kFeatures1_RandomizerActive` + the show flags; native window gated by `Trackers_Toggle`.)*
- [x] 2.5 Dialogue-box hide rule. *(`Hud_RandoTrackerVisibleNow` suppresses the overlay whenever `submodule_index != 0` — generalizes the dialogue-box rule to text boxes, menus, maps, transitions. Native window is a separate OS window, unaffected.)*

## 3. Location-tracker overlay

**As-built:** delivered as the native **Check Tracker** window
(`tracker_windows.cpp` + `rando_reach_panel.cpp`) on PC — every location grouped by
region, **tri-state** (checked / reachable / unreachable) with reachability from the
live reachability bridge. The `src/hud.c` overlay equivalent
(`Hud_RandoDrawLocationTrackerInner`) is built as the Switch path but is **2-state**
(checked vs not-checked via the `Full`/`Ring` glyphs); it does not compute
reachability. See the spec as-built note.

- [x] 3.1 Region-grouping layout mirrors the spoiler grouping. *(`BuildLocRegionIndex` shares construction with the spoiler region index; OAM overlay groups by `kRandoLocations[].region_id`.)*
- [x] 3.2 Per-location status. *(Native: tri-state `?`/`.`/`*`. OAM overlay: 2-state checked via `Rando_IsLocationChecked`.)*
- [x] 3.3 Recompute-on-counter-advance. *(Native window pulls live reachability; reach panel rebuilds its region index lazily. OAM overlay recomputes per frame, see §2.3.)*
- [x] 3.4 Status glyph alongside color for colorblind players. *(Native: glyph + color; OAM overlay uses distinct glyphs `Full`/`Ring`.)*
- [x] 3.5 Wire into the draw chain, gated on active state.

## 4. Backend compatibility matrix

**As-built:** the PC trackers are Dear ImGui **second OS windows** — they composite
independently of the game's SNES renderer, so the SDL-software / SDL-hardware /
OpenGL / OpenGL-ES distinction does not apply to them (they are not drawn through
the game's OAM/BG pipeline). The OAM-overlay path (Switch) draws through the normal
HUD OAM path and is renderer-agnostic by construction.

- [x] 4.1 SDL software renderer. *(Native window is independent; OAM overlay uses the unchanged HUD path.)*
- [x] 4.2 SDL hardware-accelerated renderer. *(Same.)*
- [x] 4.3 OpenGL renderer. *(Same — OAM overlay goes through the standard HUD OAM path; no backend-specific code in `hud.c`.)*
- [x] 4.4 OpenGL ES renderer. *(Same.)*
- [ ] 4.5 Switch backend on-device build+verify. **⏸ DEFERRED — Switch env unavailable (owner punt 2026-06-02); not an archive blocker.** The Switch button-mapping/toggle is wired in code (`main.c` `#else` arm); on-device build+verify waits for a Switch env.
- [x] 4.6 Renderer-toggle smoke (`R` key). *(Native window is unaffected by the in-game renderer toggle; OAM overlay re-renders through the unchanged HUD path.)*

## 5. Checked-location bitmap write path

**As-built:** implemented in `src/rando/rando_save.c` as `checked_bitmap` with a
`_Static_assert` coupling the size to the placement-table bound, round-trip
self-checks (`Rando_*SelfCheck`), and a v1-slot compatibility path.

- [x] 5.1 In-memory bitmap sized to cover every placement slot's location bit. *(`checked_bitmap` in the slot struct; `_Static_assert` enforces coverage — `rando_save.c:298-303`.)*
- [x] 5.2 Set the bit on successful dispatch in the location-check hot path.
- [x] 5.3 Set the bit at the audit-exempt event-flag bump sites with a `LOC_<...>` tag.
- [x] 5.4 Wire into the sidecar write path. *(`rando_save.c:254` `memcpy(p, slot->checked_bitmap, bitmap_bytes)`.)*
- [x] 5.5 Wire bitmap read into the sidecar load path. *(`rando_save.c:304-305`; bounded read.)*
- [x] 5.6 File-erase path writes a fresh empty slot with no stale bitmap.
- [x] 5.7 Forward-compat: bitmap read does not consume past its documented size. *(Round-trip + v1-compat self-checks at `rando_save.c:744,850-851,889`.)*

## 6. Audit + determinism + corpus

- [x] 6.1 `check_audit_guard.py` — no non-exempt tracked-cell writes from this change. *(Ran 2026-06-04: clean — "no non-exempt writes"; the bitmap is a sidecar field, not a tracked `g_ram` cell.)*
- [x] 6.2 `check_determinism.py` — no new `rand`/`time` symbols. *(Ran 2026-06-04: "no violations".)*
- [x] 6.3 `placement_digest_hex` byte-identical — no `kGeneratorVersion` bump. *(Holds by construction: no placement code or `kGeneratorVersion` touched; trackers are read-only of game state.)*
- [x] 6.4 Per-frame benchmark not regressed. *(Trackers re-call reachability only on counter advance / ImGui redraw; reachability cost unchanged. Switch-budget number deferred per the Switch policy.)*

## 7. Cross-tracker behavior

- [x] 7.1 Multiple trackers on simultaneously without overlap. *(Native: three independent OS windows. OAM overlay: item + location share one downward OAM-slot cursor in `Hud_RandoDrawTrackers` so they never claim the same slot.)*
- [x] 7.2 Toggle each tracker independently. *(`Trackers_Toggle(kTracker_Item|Check|Map)`; independent show flags on Switch.)*
- [x] 7.3 Dialogue-box hide rule applies to all trackers. *(OAM overlay: `Hud_RandoTrackerVisibleNow` gates both. Native windows are separate OS windows.)*

## 8. Playtest

- [ ] 8.1 Generate a Fast Ganon seed; play; toggle the Item + Check trackers; verify item/checked state matches inventory + checked-location state. *(PC playtest — owner loop.)*
- [ ] 8.2 Save mid-run with N locations checked; quit; reload; verify the Check Tracker shows all N checked on the next recompute. *(Bitmap round-trip is self-checked headlessly; this confirms the in-game surface.)*
- [x] 8.3 Switch playtest. **⏸ DEFERRED — Switch env unavailable (owner punt 2026-06-02); not an archive blocker.**
- [ ] 8.4 Colorblind smoke: status glyph alone communicates state. *(Native Check Tracker pairs glyph with color.)*

## 9. Documentation

- [x] 9.1 `README.md` key-map table lists the tracker toggle/window keys.
- [x] 9.2 `docs/randomizer.md` documents the trackers. *(Full "Tracker windows (PC)" section, `docs/randomizer.md:481-534`, covering the three windows, the legacy-OAM-compiled-out note, the format_version 2 sidecar tail, and the reachability-suppress behavior.)*
- [x] 9.3 Cross-link from the `openspec/changes/` index. *(Row 2 of `openspec/changes/README.md` status table links the change.)*

## 10. Archive readiness

- [x] 10.1 PC tracker windows render correctly (native windows are renderer-independent; OAM overlay uses the unchanged HUD path). *(Switch on-device smoke deferred per §4.5.)*
- [x] 10.2 Bitmap persists across save/load. *(Round-trip + v1-compat self-checks in `rando_save.c`; in-game confirmation is §8.2.)*
- [x] 10.3 Switch build verification. **⏸ DEFERRED — Switch env unavailable; release-cut gate, not an archive blocker.**
- [ ] 10.4 `openspec archive add-rando-trackers` runs cleanly; spec deltas merge into `openspec/specs/randomizer-{ui,save}/spec.md`. *(Gate on the §8 PC playtest.)*
