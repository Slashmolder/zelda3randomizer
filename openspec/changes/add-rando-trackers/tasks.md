## 1. Keybinding wiring

- [ ] 1.1 Add `kKeys_RandoToggleItemTracker` and `kKeys_RandoToggleLocationTracker` to the `kKeys_*` enumeration in `src/config.c`. Place near the other randomizer key constants.
- [ ] 1.2 Add the two key names to the `[KeyMap]` INI parser's accepted key list in `src/config.c`. Default: unbound (empty).
- [ ] 1.3 Document the two key names in `README.md` key map table.
- [ ] 1.4 Wire input dispatch — on key press, toggle the in-memory visibility booleans `g_rando_show_item_tracker` and `g_rando_show_location_tracker`. Reset both to `false` on `ZeldaInit` so launch state is always hidden.

## 2. Item-tracker overlay (`src/hud.c`)

- [ ] 2.1 Design the item-grid layout: fixed rows × cols (e.g., 6 × 5 = 30 cells covering progressives + absolute items + bottles + magic + heart counter). Place anchor configurable via `[randomizer] tracker_anchor` (default top-left). Reuse HUD tiles — no new graphics.
- [ ] 2.2 Implement `Hud_RandoDrawItemTracker(void)` — reads from `link_item_*` + `link_bottle_info[*]` + `link_max_health` to compute the cell-by-cell "have / level" state. Writes to OAM directly via the standard HUD OAM slots.
- [ ] 2.3 Implement the OAM-cache: on first call after `reachability_state_counter` advances, recompute and write OAM. On subsequent calls until the counter advances again, copy the cached OAM directly (no recompute).
- [ ] 2.4 Wire `Hud_RandoDrawItemTracker` into the existing HUD draw chain. Gate on `g_rando_show_item_tracker && kFeatures1_RandomizerActive`.
- [ ] 2.5 Dialogue-box hide rule: when `dialogue_state != 0` (or equivalent — discover the actual vanilla state byte), suppress the draw; cached state is preserved (not invalidated).

## 3. Location-tracker overlay (`src/hud.c`)

- [ ] 3.1 Author the region-grouping layout. Use the same grouping as `Spoiler_WriteText` for byte-equivalence with the text-spoiler. Verify by `diff` of the text-spoiler region headers against the tracker's region headers.
- [ ] 3.2 Implement `Hud_RandoDrawLocationTracker(void)` — iterates the active placement table, groups by region, computes each location's status (`?`/`.`/`*`) from:
  - **Checked**: bit set in `checked_locations_bitmap` (see §5).
  - **Reachable**: location is in the cached `Logic_ComputeReachability` result.
  - **Unreachable**: not in the reachability cache.
- [ ] 3.3 Implement the recompute-on-counter-advance pattern: recomputes the reachability + builds the on-screen list only when `reachability_state_counter` advances. Caches the list to OAM for subsequent frames.
- [ ] 3.4 Render the status glyph alongside the color so colorblind players still distinguish states.
- [ ] 3.5 Wire into HUD chain with `g_rando_show_location_tracker && kFeatures1_RandomizerActive`.

## 4. Backend compatibility matrix

- [ ] 4.1 SDL software renderer: verify overlay composites correctly. The HUD path is unchanged from vanilla, so this should "just work" — confirm with a smoke test.
- [ ] 4.2 SDL hardware-accelerated renderer: same — confirm overlay tile draws via `SDL_RenderCopy` with no z-fighting.
- [ ] 4.3 OpenGL renderer: confirm the overlay tile draw path goes through the existing `opengl.c` chain. If the renderer composites BG layers via separate shader passes, ensure overlay tiles use the same OAM-priority path as the existing HUD.
- [ ] 4.4 OpenGL ES renderer: same as OpenGL; usually shares code paths.
- [ ] 4.5 Switch backend: build on DevKitPro + switch-sdl2; run on dev unit; toggle trackers via Switch button mapping; verify overlay renders at the same anchor as desktop. Measure per-frame cost via the existing `Logic_ComputeReachability` benchmark gate (per `tasks.md §11.8` of `add-randomizer-support`).
- [ ] 4.6 Renderer-toggle smoke (`README.md` "R" toggle): with tracker enabled, cycle through all backends; overlay should re-render without flicker on each toggle.

## 5. Checked-location bitmap write path

- [ ] 5.1 Allocate the in-memory bitmap. Sizing: `(placement_table_size / 2 + 7) >> 3` bytes. Lives in the slot-state struct in heap (NOT in `g_ram`).
- [ ] 5.2 Set the bit in `Rando_OnLocationCheck` after a successful dispatch resolution (the hot path). Place the bit-set BEFORE the `Rando_DispatchVanillaGrant` call so a crash in dispatch still flags the location as in-flight (or AFTER — pick one and document).
- [ ] 5.3 Set the bit at the audit-exempt event-flag bump sites enumerated in `audit.md` §"Reachability-affecting events" that have a corresponding `LOC_<...>` tag. Cross-reference the audit table for the canonical site list.
- [ ] 5.4 Wire the bitmap into the sidecar write path (`src/rando/rando_save.c::Rando_WriteSidecarSlot`). Append after the placement table, before any Phase C TLV chain (currently nonexistent).
- [ ] 5.5 Wire bitmap read into `Rando_LoadSidecarSlot`. Verify the Phase A read invariant (`randomizer-save/spec.md:98-110`) — iterate exactly `[0, placement_table_size / 2)` bits.
- [ ] 5.6 File-erase path: when the slot is erased, the next sidecar write writes `slot_kind = Empty=0` with no bitmap; verify the bitmap allocation is freed (no memory leak on slot kind change).
- [ ] 5.7 Forward-compat: confirm a future Phase C TLV chain can still append after the bitmap. Test by manually padding a slot with a hand-crafted TLV chain and verifying the bitmap reader doesn't consume past the bitmap's documented size.

## 6. Audit + determinism + corpus

- [ ] 6.1 `check_audit_guard.py` — confirm no new tracked-cell writes from this change. The bitmap is a new cell but NOT in `link_item_*` / `link_bottle_info[*]` / etc. — it's a sidecar field. No exemption needed.
- [ ] 6.2 `check_determinism.py` — no new `rand`/`time` symbols.
- [ ] 6.3 `placement_digest_hex` byte-identical before/after — no `kGeneratorVersion` bump.
- [ ] 6.4 Per-frame benchmark: `Logic_ComputeReachability` 5ms desktop / 20ms manual on Switch (per `tasks.md §1.0h`) — tracker draw must not regress this; trackers re-call reachability only when counter advances, so the benchmark is unchanged by the tracker's existence.

## 7. Cross-tracker behavior

- [ ] 7.1 Both trackers on simultaneously: verify no overlap / z-fighting; choose distinct anchors (item top-left, location top-right is a sane default).
- [ ] 7.2 Toggle each tracker independently without affecting the other.
- [ ] 7.3 Verify dialogue-box hide rule applies to both trackers, not just item.

## 8. Playtest

- [ ] 8.1 Generate a Fast Ganon seed; play 1 hour; toggle both trackers throughout; verify color/glyph state matches actual inventory + checked-location state.
- [ ] 8.2 Save mid-run with 47 locations checked; quit; reload; verify the location tracker shows all 47 `*` glyphs on the next reachability recompute.
- [ ] 8.3 Switch playtest: run on dev unit, repeat the same 1-hour play; verify no per-frame regression visible to the player.
- [ ] 8.4 Colorblind smoke: simulate red-green colorblindness; verify status glyph alone communicates state.

## 9. Documentation

- [ ] 9.1 `README.md` key-map table: add `RandoToggleItemTracker` and `RandoToggleLocationTracker` entries.
- [ ] 9.2 `docs/randomizer.md` UI section: short paragraph on the trackers, including the dialogue-box-hide rule and the OAM-cache invalidation guarantee.
- [ ] 9.3 `docs/randomizer_phase_b.md` Slice 1 status: mark complete; cross-link to this change.

## 10. Archive readiness

- [ ] 10.1 All five backends green on the smoke test.
- [ ] 10.2 Bitmap persists across save/load on at least three distinct seeds.
- [ ] 10.3 Switch build verified via the manual Switch-build-verification gate (per `add-randomizer-support / tasks.md §12.3b`).
- [ ] 10.4 `openspec archive add-rando-trackers` runs cleanly; spec deltas merge into `openspec/specs/randomizer-{ui,save}/spec.md`.
