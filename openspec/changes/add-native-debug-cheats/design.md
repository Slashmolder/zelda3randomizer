# Design — Native Debug/Cheats tab (live inventory editor)

Extends the native settings window (`add-native-game-config-ui`) with a **Debug** top-level tab that live-edits Link's inventory/equipment/consumables by writing the `g_ram` save block while the game runs. File:line are anchors (verify the symbol).

> **Revision note (post fresh-eyes round 1).** Tightened the edit gate to a module *whitelist* (death/cutscene/transition/stub modules excluded; the pause/item menu `0x0E` is included). Corrected the value model: bottle contents come from the actual `kHudItemBottles[]` table (NOT vanilla folklore — verify at implementation), bow is a `{none,wood,silver}` selector (the arrow low-bit is game-derived), the sword `0xFF` "in-repair" sentinel is handled on read, and the two items with **separate randomizer ownership state** (mushroom/powder `0xF344` ↔ `g_rando_mushroom_held`; flute/shovel `0xF34C` ↔ `g_rando_flute_shovel_owned`) are disabled under `kFeatures1_RandomizerActive` to avoid the documented "vanilla state reused as a progress proxy" desync class. Rupee cap follows `CarryMoreRupees`; `health_current` is clamped to `health_capacity` on capacity edits. `ZeldaIsEmulatorAttached()` is a deliberately *conservative* gate (it disables whenever the ROM is attached, not only on compare frames).

## Ground truth

- **Inventory block** is the canonical ALTTP `$7EF3xx` table, all `uint8` unless noted, documented in `src/variables.h:1064-1115`:
  - Items `0xF340`–`0xF358`: bow, boomerang, hookshot, bombs, mushroom(/powder), fire_rod, ice_rod, bombos/ether/quake medallion, torch(lamp), hammer, flute, bug_net, book, bottle_index, cane_somaria, cane_byrna, cape, mirror, gloves, boots, flippers, moon_pearl.
  - Equipment: `0xF359` sword_type, `0xF35A` shield_type, `0xF35B` armor.
  - Bottles: `0xF35C`–`0xF35F` (4 bytes, `link_bottle_info`).
  - `0xF362` rupees_actual (**uint16**, LE), `0xF360` rupees_goal (uint16 — the HUD counts toward goal; set both).
  - `0xF36B` heart_pieces (0–3), `0xF36C` health_capacity, `0xF36D` health_current (both in **1/8-heart units**, 8/heart, 20 hearts = `0xA0`).
  - `0xF36E` magic_power (0–`0x80`), `0xF36F` num_keys, `0xF370` bomb_upgrades, `0xF371` arrow_upgrades, `0xF377` num_arrows.
  - Advanced bitfields: `0xF374` which_pendants, `0xF37A` has_crystals, `0xF379` ability_flags.
- **Gate signals:** `main_module_index` = `g_ram[0x10]` (gameplay modules are `0x07` dungeon / `0x09` overworld and the in-play range; below `0x07` is intro/title/file-select where the inventory isn't loaded). Replay: `ZeldaIsReplaying()` (added in the prior change). Emulator/RAM-compare attached: `g_emu_runframe != NULL` (static in `zelda_rtl.c:337`) — add `ZeldaIsEmulatorAttached()`.
- **`g_ram`** is `extern uint8 g_ram[131072]` (`variables.h:1522`); the panel reads it directly for display.
- **Audit guard** (`check_audit_guard.py`) rglobs `src/**/*.c` excluding any path containing `rando`, and never scans `.cpp`. The cheat writes live in `game_config_panels.cpp` (already wired) → out of scope. No `// rando-exempt:` needed.
- The HUD reads hearts/magic/rupees/bombs/arrows/keys from these cells **every frame**, so edits show on-screen next frame. The pause/item sub-screen and the selected Y-item glyph refresh on next menu open (acceptable; noted).

## D1. Edit gate (the single most important safety piece)

A new C accessor + a panel-side predicate gate **all writes**:

```c
// zelda_rtl.c / .h  (sibling of ZeldaIsReplaying)
bool ZeldaIsEmulatorAttached(void) { return g_emu_runframe != NULL; }
```

```c
// game_config_panels.cpp
static bool CheatsCanEdit(void) {
  // Whitelist of STABLE gameplay/menu modules (kMainRouting, src/misc.c). Excludes
  // death (0x12), prize/victory cutscene (0x13), screen transitions (0x0F-0x11),
  // and the unknown stubs (0x0C/0x0D) where the HUD/equip state is mid-rebuild.
  // 0x0E (Interface) IS the pause/item menu — the most-wanted edit context.
  uint8 m = g_ram[0x10];                 // main_module_index
  bool in_game = (m == 0x07 || m == 0x09 || m == 0x0B || m == 0x0E);
  return in_game && !ZeldaIsReplaying() && !ZeldaIsEmulatorAttached();
}
```

Rationale for each clause:
- **in_game** — the inventory block is only valid/meaningful in a stable gameplay or menu module; editing at the title/file-select writes into uninitialized save RAM, and editing during death/cutscene/transition races a state machine that is rewriting HUD/equip state. (`0x05 LoadFile` populates the inventory before `0x07`, so the lower bound is conservative — fine.)
- **!replay** — writing `g_ram` mid input-replay corrupts the replay stream (the same hazard the config change gates `features0` on).
- **!emulator** — with the original ROM attached for side-by-side RAM compare, a UI write the emulated ROM never made diverges and asserts on the next frame.

When `!CheatsCanEdit()` the whole tab renders **disabled** with the reason shown ("Load a save and enter the world", "disabled during replay", "disabled while the original ROM is attached"). Reads still display current bytes.

## D2. Write path (clamped, .cpp-local)

All writes go through small panel-local helpers that **re-check the gate** (defense in depth) and clamp:

```c
static void PokeByte(uint32 addr, int v, int lo, int hi) {
  if (!CheatsCanEdit()) return;
  if (v < lo) v = lo; if (v > hi) v = hi;
  g_ram[addr] = (uint8)v;
}
static void PokeWord(uint32 addr, int v, int lo, int hi) {  // little-endian
  if (!CheatsCanEdit()) return;
  if (v < lo) v = lo; if (v > hi) v = hi;
  g_ram[addr] = (uint8)(v & 0xFF); g_ram[addr + 1] = (uint8)((v >> 8) & 0xFF);
}
```

Every widget is range-bounded (combos with fixed option sets, sliders with min/max), so an out-of-range/garbage byte can never reach a table-index in game code. No write is a partial multi-byte tear that the game could observe mid-update at a problematic time, because writes happen between frames (ImGui render is after `ZeldaRunFrame`, same thread).

## D3. Panel layout (`GameDebug_RenderTab`, a new top-level "Debug" tab)

Rendered as a **third top-level tab** in `rando_window.cpp`'s tab bar (after Game Settings and Randomizer), via a new `extern "C" void GameDebug_RenderTab(void)` in `game_config_panels.cpp`. It is NOT part of `GameConfig_RenderTab` (no Apply/INI — it writes RAM live, not config).

Top of the tab: a status line. If `!CheatsCanEdit()`, an explanatory disabled banner (and `ImGui::BeginDisabled()` wraps the controls). Sections (each a `SeparatorText`):

- **Consumables:** rupees (Slider 0–`MaxRupees()` → `PokeWord(0xF362)` AND `0xF360`; the cap is 999, or 9999 with `kFeatures0_CarryMoreRupees` — bound the slider to the feature-aware cap), bombs (0–50/`0xF343`), arrows (0–70/`0xF377`), keys (0–99/`0xF36F`), magic (slider 0–128/`0xF36E`, full = `0x80`; `magic_consumption` `0xF37B` is the separate half/quarter upgrade — do not conflate), heart pieces (0–3/`0xF36B`).
- **Hearts:** containers slider 1–20 → `PokeByte(0xF36C, n*8)`; a "Refill" button → set `0xF36D = 0xF36C`. On any capacity write, **clamp** `health_current = min(current, capacity)` so a lowered capacity can't leave `current > capacity` (which `Hud_RefillLogic`/death logic could underflow).
- **Equipment:** sword (combo 0–4), shield (0–3), armor (0–2: green/blue/red), gloves (0–2: none/power/titan). **Sword display must handle the `0xFF` sentinel** ("sword temporarily removed" during smithy tempering, `hud.c` guards `== 0xff`): a read of `> 4` shows "none / in-repair" rather than indexing a garbage combo entry. Setting 0–4 is fine.
- **Items (tiered):**
  - **bow** — a `{none, wood, silver}` combo writing `{0, 1, 3}` to `0xF340`. Bow is NOT a linear 0–4 tier: the low (arrow) bit is **re-derived from `link_num_arrows` every time the item menu opens** (`bow = arrows ? 4 : 3` / `? 2 : 1`, see `rando.c`/`hud.c`), so letting the user pick the +arrows variant is meaningless. The arrow *count* (`0xF377`) is the real lever for that bit.
  - **boomerang** (`0xF341`, 0–2: none/blue/red — NO "both"; red replaces blue, and `kHudItemBoomerang[]` has only 3 entries so a value of 3 is an out-of-bounds HUD read).
- **Items (toggles):** hookshot, lamp(torch), fire_rod, ice_rod, hammer, bug_net, book, cane_somaria, cane_byrna, cape, mirror, boots, flippers, moon_pearl, and the three medallions — each a checkbox writing 0/1.
- **Mushroom/Powder** (shared byte `0xF344`): a single 3-way combo {none, mushroom, powder} (1=mushroom, 2=powder) — NOT two toggles — per the documented shared-byte softlock. **Disabled under `kFeatures1_RandomizerActive`** (tooltip "managed by randomizer") because rando tracks held-mushroom separately in `g_rando_mushroom_held`; a raw `0xF344` write would desync it.
- **Flute / shovel** (`0xF34C`, 1=shovel, 2=flute, 3=active flute): a combo {none, shovel, flute}. **Disabled under `kFeatures1_RandomizerActive`** because rando tracks ownership separately in `g_rando_flute_shovel_owned`; raw writes desync it. (Both this and Mushroom/Powder are the same "vanilla state as a progress proxy" class — verify the exact rando symbols at implementation and gate on them.)
- **Bottles (×4):** per-bottle combo writing `0xF35C+i`, over the values `kHudItemBottles[link_bottle_info[i]]` (`hud.c`) actually indexes. Verified mapping (confirm `kHudItemBottles[]` at implementation): `0`=no bottle, `2`=empty, `3`=red potion, `4`=green potion, `5`=blue potion, `6`=fairy, `7`=bee, `8`=good bee. **Omit index `1`** from the combo (unused placeholder glyph). So the combo exposes {no bottle(0), empty(2), red(3), green(4), blue(5), fairy(6), bee(7), good bee(8)}.
- **Progress (advanced, collapsed `TreeNode`):** pendants (`0xF374` — 3 checkboxes over the bitfield), crystals (`0xF37A` — 7 checkboxes). Clearly labeled "advanced — may desync rando prize logic." Abilities (`0xF379`) deferred (bit semantics fuzzy; out of v1 scope).
- **Quick actions:** buttons — "Refill hearts & magic", "Max rupees/bombs/arrows", "Give all equipment & items" (sets the toggles + tiers to max). Each is just a batch of `Poke*` calls, gated identically.

All reads for display come straight from `g_ram` so the widgets track pickups/usage live.

## D4. Files & build
- **New code, no new files:** add `GameDebug_RenderTab()` + the cheat helpers to the existing `src/rando/rando_window/game_config_panels.cpp` (already in the build). Add the "Debug" top-level tab in `src/rando/rando_window/rando_window.cpp`. Declare `GameDebug_RenderTab` in `game_config_widgets.h`.
- **`zelda_rtl.c/.h`:** add `ZeldaIsEmulatorAttached()`.
- No Makefile/vcxproj change (no new TU). No Switch impact (panel TU is PC-only under `Z3R_NATIVE_SETTINGS_WINDOW`; the accessor is plain C and harmless).

## D5. Determinism / safety / scope
- **No determinism impact:** cheats write `g_ram` at runtime only; they touch no `RandoSettings`, generation, canonical/hash, or `kGeneratorVersion`. Corpus/selftest unaffected.
- **No save-format change:** edits land in live `g_ram`; they persist to SRAM only if/when the game saves normally (expected — the user cheated, then saved).
- **No audit-guard impact** (see Ground truth).
- **RAM-compare:** hard-disabled while the ROM is attached (`ZeldaIsEmulatorAttached()` = `g_emu_runframe != NULL`), so it can never diverge the comparator. Deliberately *conservative* — the comparator actually diffs on only a subset of those frames (the per-frame emu call short-circuits when `enhanced_features0 != 0 || g_zenv.dialogue_flags` at `zelda_rtl.c:847`, and the snapshot diff is skipped under `kFeatures1_RandomizerActive` in `zelda_cpu_infra.c`), so the gate over-disables rather than risk a false-enable.
- **Out of scope (v1):** ability_flags editing; arbitrary address poking; entrance/room warps; sprite spawns; a memory viewer. This is an *inventory* editor, not a general memory debugger.

## D6. Test plan
1. Build clean (MSVC) + `--rando-selftest` still green (no new self-check needed; cheats have no headless surface).
2. In-game: open Debug tab, set rupees/bombs/arrows/keys/hearts/magic → HUD updates next frame; values clamp at bounds.
3. Equipment/items: grant sword/shield/gloves/bow tiers + toggles → reflected in the item menu on next open; no crash on any combo value.
4. Mushroom/Powder combo: switching never leaves both set; Witch/Potion-shop check still reachable (the documented shared-byte trap).
5. Gating: at the title/file-select the tab is disabled with the reason; during a snapshot replay it is disabled; with the original ROM attached (side-by-side) it is disabled and no RAM-compare assert fires.
6. Bottles: each content value renders the right bottle in the menu; no invalid index.
7. Quick actions apply the batch and the HUD/menu reflect it.
