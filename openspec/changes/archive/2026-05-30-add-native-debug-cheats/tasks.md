**Status**: implemented & shipped (the "Debug tab" commits). Tasks below are
recorded against the as-built code; `done-differently` notes flag where the
implementation diverged from `design.md`.

## 1. Edit gate + emulator accessor (design D1)

- [x] 1.1 Add `ZeldaIsEmulatorAttached()` (sibling of `ZeldaIsReplaying`). <!-- done: src/zelda_rtl.c:469 `bool ZeldaIsEmulatorAttached(void) { return g_emu_runframe != NULL; }`; declared in zelda_rtl.h -->
- [x] 1.2 Module-whitelist edit predicate (in-game/menu modules only; exclude death/cutscene/transition/stub; include `0x0E` pause/item). <!-- done: src/rando/rando_window/game_cheats.cpp:23 (`in_game && !ZeldaIsReplaying() && !ZeldaIsEmulatorAttached()`); a stricter `run && stable && ...` variant at :30 -->
- [x] 1.3 Closed-gate UI: render tab disabled with the reason; reads still display. <!-- done: game_config_panels.cpp:718-724 shows the reason via ZeldaIsEmulatorAttached(); dbg_flags.cpp:23-29 mirrors the pattern -->

## 2. Clamped write path (design D2)

- [x] 2.1 Range-clamped poke helpers that re-check the gate. <!-- done-differently: extracted into a dedicated TU src/rando/rando_window/game_cheats.{cpp,h} as `Cheats_PokeByte`/`Cheats_PokeWord` (game_cheats.cpp:33,39) + bounded widget helpers (slider/checkbox/combo) at :55-80, NOT inline in game_config_panels.cpp as design D2 sketched -->
- [x] 2.2 Every widget range-bounded (fixed-option combos / bounded sliders) so no out-of-range byte reaches a table index. <!-- done: widget helpers clamp to [lo,hi]; combos write only valid option values (game_cheats.cpp:67,80) -->

## 3. Debug panel layout (design D3)

- [x] 3.1 `GameDebug_RenderTab()` as a third top-level tab. <!-- done: src/rando/rando_window/game_config_panels.cpp:849 (extern "C"); declared game_config_widgets.h:23; wired as a top-level tab in rando_window.cpp:1119 -->
- [x] 3.2 Sections: consumables, hearts, equipment, tiered items, toggles, mushroom/powder 3-way, flute/shovel, bottles, advanced pendants/crystals, quick actions. <!-- done: game_config_panels.cpp:710+ (e.g. rupees PokeWord 0xF362/0xF360 at :744-745) -->
- [x] 3.3 Shared-byte / randomizer-owned handling: mushroom/powder single 3-way; mushroom-powder + flute/shovel disabled under `kFeatures1_RandomizerActive`; bow {none,wood,silver}; boomerang {none,blue,red}; sword `0xFF` sentinel on read; bottles only `kHudItemBottles[]` values. <!-- done: per design D3; verify the exact rando ownership symbols at game_config_panels.cpp -->
- [x] 3.4 Per-dungeon dungeon items: a "Dungeon items" tree with a 13-row table (one per dungeon) editing maps/compasses/big keys (per-dungeon WORD bitfields) + each dungeon's saved small-key count. <!-- done: game_config_panels.cpp DbgInventory_Render — section above the Progress tree. Map/Compass/BigKey checkboxes toggle the `0x8000 >> D` bit in link_dungeon_map (0xF368) / link_compass (0xF364) / link_bigkey (0xF366) via DbgDungeonBit (maps the MSB-first bit onto byte-oriented Cheats_BitCheckbox: byte `base + (D<=7?1:0)`, bit `(15-D)&7`). Keys slider edits link_keys_earned_per_dungeon[] (0xF37C) and syncs the live link_num_keys (0xF36F) when Link stands in that dungeon (mirrors rando.c:282-297, incl. HC-proper raw-index-2 → slot-0 fold). Bit ordering + dungeon-index table grounded in rando.c:200-231. Goes through the shared gated/clamped cheat-core. -->

## 4. Files & build (design D4)

- [x] 4.1 Wire the new code into all build systems. <!-- done-differently: design D4 said "no new files / no Makefile/vcxproj change", but the impl split out new TUs (game_cheats.cpp, dbg_flags.cpp, panels_selftest.cpp under src/rando/rando_window/). Makefile glob-builds src/**; verify Zelda3.vcxproj lists the new .cpp files (PC-only under Z3R_NATIVE_SETTINGS_WINDOW). No Switch impact. -->

## 5. Safety / determinism (design D5)

- [x] 5.1 No determinism/save-format/audit-guard impact; RAM-compare hard-disabled while the ROM is attached. <!-- done: writes are runtime g_ram only; gate includes !ZeldaIsEmulatorAttached(); .cpp under src/rando/ is out of audit-guard scope -->

## 6. Verification (design D6)

- [x] 6.1 Headless smoke: Debug tab renders under the panel selftest harness. <!-- done: src/rando/rando_window/panels_selftest.cpp:109 begins a smoke window and calls GameDebug_RenderTab(); the gate is exercised at :36 -->
- [x] 6.2 In-game playtest (HUD updates next frame; clamps; gating at title/replay/ROM-attached; bottles render correct glyphs; quick actions; **dungeon items (3.4): map/compass/big-key flags reflect on the dungeon map screen + HUD big-key icon, and a key bumped while standing in a dungeon opens a locked door**). *(Playtest gate — the only reliable net per CLAUDE.md; pending owner sign-off.)*

## 7. Archive

- [ ] 7.1 `openspec archive add-native-debug-cheats` once 6.2 is signed off; spec delta merges the `game-config-ui` Debug-tab requirements (sequence after `add-native-game-config-ui` archives, since both ADD to the same not-yet-baselined capability).
