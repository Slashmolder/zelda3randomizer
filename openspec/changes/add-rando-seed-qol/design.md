# Seed QoL — design

Grounded in a source read of the as-built engine (a fresh-eyes anchor pass over
`src/messaging.c`, `src/misc.c`, `src/player.c`, `src/load_gfx.c`, `src/overworld.c`,
`src/config.{c,h}`, `src/hud.c`, and `src/rando/`). Line numbers below are
**indicative** and MUST be re-grepped at implementation (the Makefile has no
header-dep tracking and files drift); the load-bearing facts are the function/flag
identities, not the line offsets. Decision labels (D1…) are referenced from
`tasks.md`.

## Context & grounded facts

- **Feature-bit headroom.** `kFeatures0_JpOverworldMusic = 524288` (bit 19) is the
  highest used bit; **bits 20-31 of the uint32 at `g_ram+0x64c` are free.** All new
  per-slot QoL enables are new `kFeatures0_*` bits there. `kFeatures0_RandoSeedQolMask`
  is the replay-safe subset carried in a slot's `recommended_features0`
  (format_version ≥ 3). Reserved free `g_ram` is only `0x66d-0x66f` (3 bytes) — the
  design avoids new `g_ram` state.
- **Text engine.** Per-character pacing comes from a fixed LUT
  `kText_WaitDurations[16]` (`src/messaging.c` ~:116); message characters are drawn/
  advanced in `RenderText_Draw_MessageCharacters()` (~:2457). **Rando hint and trap
  text decode through the SAME render path** (`Rando_RenderHintMessage` /
  `Rando_RenderTrapMessage`, ~:2319-2329) — so any text-speed change touches hint
  text too. The end-of-box **wait-for-input** to advance/close is separate from
  per-character pacing; this separation is what makes fast text hint-safe (see D4).
- **Item-receipt fanfare.** `AncillaAdd_ItemReceipt()` (`src/misc.c` ~:985) sets
  hold timers via `ancilla_aux_timer[]` (~0x68 for key/map/compass, ~0x38/0x60 for
  chest/sprite) and pokes the get-jingle sound. Shortening the hold is a per-slot
  timer clamp at that site.
- **Cutscene state machines are submodule chains with progression flags read at
  entry.** `sram_progress_indicator` (`g_ram+0xF3C5`, SRAM-persisted) is the master
  progress gate (e.g. set to 2 after Agahnim, `src/misc.c` ~:630). The Agahnim intro
  runs under the KillAgahnim module (`src/misc.c` ~:782); the death/game-over fade is
  `Module12_GameOver` (`src/messaging.c` ~:620) advancing `submodule_index`/
  `subsubmodule_index`; the GT crystal barrier is visual-only keyed on
  `link_has_crystals` (`g_ram+0xF37A`); pyramid-opening is driven by
  `sram_progress_indicator`. **GOTCHA (the load-bearing risk of this whole change):**
  these flags are SET by the same module logic the animation runs; a skip that jumps
  *past* the flag-setting submodule leaves the flag unset and the sequence re-triggers
  on reload. The safe skip compresses submodule *dwell* (advance faster / zero the
  per-stage timer), it does not jump over flag-setting stages.
- **Mirror/flute animation vs. destination are already separated.**
  `MirrorWarp_RunAnimationSubmodules()` (`src/load_gfx.c` ~:1236) is the iris/zoom
  visual; `MirrorWarp_FinalizeAndLoadDestination()` (`src/overworld.c` ~:1389) sets
  camera/screen/music AFTER the visual completes and does NOT move Link (Link is at
  the destination before the animation). Flute is `FluteMenu_FadeInAndQuack()`
  (`src/messaging.c` ~:1079) → `BirdTravel_Finish_Doit()`. Speeding the visual
  submodule chain leaves the finalize (position/camera/music) byte-identical.
- **Pause dungeon map.** Drawn under Module 0x0E via `Module0E_03_DungeonMap` /
  `Module0E_03_01_DrawMap` (`src/messaging.c` ~:67) submodules to VRAM. There is
  **no per-dungeon checked/total accessor today**; `Rando_IsLocationChecked(loc)`
  (`src/rando/rando.c` ~:1763) reads the sparse checked bitmap per location. A new
  helper must iterate the location registry filtered by dungeon. **GOTCHA:** iterating
  every frame is O(N over ~328-1163 locations); compute once per map-open and cache.
- **Goal/requirement data is C-static, not `g_ram`.** Crystal requirements live in
  `RandoSettings.crystals_ganon` / `.crystals_tower` reached via
  `Rando_GetActiveSettings()`; the hunt counter is `g_rando_triforce_piece_count`
  (`g_ram+0x65f`). **`Rando_GetActiveSettings()` returns NULL on snapshot-restore and on
  v1 (format_version < 2) slots** (`src/rando/rando.h` ~:868-889) — callers gate on
  `Rando_HasActiveSettings()` (`rando.c` ~:4073) and provide a fallback. No accessor emits
  a human-readable share string at runtime. The proposed in-game display is deferred, so
  this change does not re-serialize slot identity during gameplay.
- **Keybinds.** Command IDs are the `kKeys_*` enum (`src/config.h`); defaults in
  `kDefaultKbdControls[]` (`src/config.c` ~:51) must stay lock-step with the enum
  (assert ~:86); INI names in `kKeyNameId[]` (~:104). L/R quick-swap uses
  `hud_cur_item_l/r` (`g_ram+0x657/0x658`) via the HUD reorder path.
- **Save & Quit / spawn.** `GameOver_SaveAndOrContinue()` (`src/messaging.c` ~:771)
  → `Death_Func15()` (`src/messaging.c` ~:801) is a **flag-latched** state machine
  (`death_var4/5`, `savegame_is_darkworld`, `subsubmodule_index` branching,
  `sram_progress_indicator` gates, Inverted DW forcing) whose save branch already lands
  the player at the start point. The start point itself is **`which_starting_point`** (a
  `g_ram` byte) indexing the `kStartingPoint_*[]` asset tables, loaded by the start-point
  branch in `src/dungeon.c` (~:8835-8864). There is **no `RandoSettings.start_entrance_id`
  field and no start-location shuffle in this fork.**
- **Dash.** The Pegasus-boots **charge-up** (hold B while wearing boots to reach the dash
  trigger) accumulates in `button_b_frames` and fires the `StartDash` transition in the
  ground/sword-charge pipeline (`src/player.c` ~:1417-1426, ~:2305-2325) — **shared with
  the sword spin-charge, a JP-glitch-sensitive path** (in-file notes ~:286-295).
  `LinkState_Dashing()` (~:1256) runs *after* the dash is already active (its
  `link_countdown_for_dash` is the active-dash frame counter, already zeroed there), so it
  is the WRONG place to remove the charge wait. F6 must re-grep the real charge trigger.
- **Auto-tracker.** `AutoTracker_ServiceFrame()` (`src/rando/auto_tracker.c`) emits a
  per-frame JSON snapshot (locations/items/reachability) over TCP (default
  127.0.0.1:17400). Door/entrance overrides live in the door/entrance runtime sparse
  tables (`src/rando/door_runtime.h` + entrance runtime); a `discovered_*` field is a
  new snapshot key.
- **Settings-window pattern.** The Seed QoL tab renders `kRecBits[]` / `kRecLabels[]`
  (+ a defaults array) in `Panel_RecommendedFeatures()` (`rando_window.cpp` ~:405-460);
  adding a checkbox = add parallel entries + a `features.h` bit. Game Settings toggles
  use `FeatureCheckbox()` in `game_config_panels.cpp` editing `features0` live.
  `kSettingsCanonicalLen = 28` is **not touched** by this change.

## Decisions

### D1 — Everything is a feature bit / config / render; nothing touches placement

No `RandoSettings` axis, no `settings_hash`, no `kSettingsCanonicalLen`, no
`kGeneratorVersion` bump, no on-disk save-format change. This is the invariant that
keeps the corpus byte-identical and `--rando-selftest` untouched. Bit allocation
(all in the free 20-31 range; final values assigned at implementation, kept
contiguous):

| Bit | Flag | Feature | In `RandoSeedQolMask`? |
|---|---|---|---|
| 20 | `kFeatures0_RandoDungeonCheckCounts` | F1 map counts/dots | yes (rando-only) |
| 21 | reserved | deferred F2 seed overlay | no |
| 22 | `kFeatures0_FastFanfare` | F3 item/dungeon-item get holds | yes |
| 23 | `kFeatures0_CutsceneFastForward` | F4 cutscene/transition FF | yes |
| 24 | `kFeatures0_AutoDash` | F6 auto/hold dash | yes |
| 25 | `kFeatures0_WarpToSpawn` | F5 warp-to-spawn enable | no (local/race) |

F3's **text draw speed** is a global `zelda3.ini` selector (`text_speed`:
normal/fast/instant), NOT a bit — a multi-level value doesn't fit the mask; the
`kFeatures0_FastFanfare` bit covers the recommendable get-hold compression. F5
adds `kKeys_WarpToSpawn` / `kKeys_SoftResetToSpawn` command IDs. Rando-only features
(F1, and F5/F7 which read slot state) additionally gate on
`(enhanced_features1 & kFeatures1_RandomizerActive)`. Where a behavior would perturb
the side-by-side comparator, it is also gated `!ZeldaIsEmulatorAttached()` per the
existing Seed QoL convention.

### D2 — F1 dungeon check-info: new cached per-dungeon accessor, drawn on Module 0x0E

Add `Rando_DungeonCheckCounts(dungeon_id, &checked, &total)` that iterates the
location registry filtered by dungeon, using `Rando_IsLocationChecked`. Compute on
map-open (or on checked-bitmap change), cache in a small static (not `g_ram`) —
never per-frame (the O(N) gotcha). Draw the count on the dungeon map render.
**Phase 1** = the count; **Phase 2** = dots at located remaining checks (needs a
location→map-cell mapping; ships after the count is validated). Counts only, so
race-safe.

### D3 — F2 seed info overlay is deferred

The in-game seed info overlay is intentionally not part of this change. Playtest showed
that the gameplay HUD surface is easy to cover and visually rough, while the native
tracker/generate-copy UI already exposes the useful seed identity and requirements. Keep
bit 21 reserved so later feature values used during branch testing do not churn, but do
not expose, persist, recommend, or draw an F2 overlay in this change.

### D4 — F3 fast text is hint-safe by construction (and emulator-suppressed)

`kText_WaitDurations` is indexed by an **in-text-stream** command param
(`kText_WaitDurations[TEXTCMD_PARAM(cmd)]`, `messaging.c` ~:2527) — per-message embedded
pacing, not a global knob. F3 overrides the *effective* wait selected at draw time
(shorten, or zero when `instant`), NOT the LUT contents. `instant` also removes the
text-box dead time that is not useful reading time: the initial/post-page input latch is
clamped to one frame and text-box scroll commands complete in one frame. It still
**never auto-presses generic wait-for-input.** Because hint/telepathy/sign text still
requires the player's input to advance/close, an `instant` fill cannot blow a hint past
the player — the box fills instantly and waits. Because `text_speed` is a GLOBAL config
value (not rando-only, not a bit), the per-frame character-draw cadence it changes is
RAM/timing-observable, so the override MUST be suppressed under
`ZeldaIsEmulatorAttached()` (on BOTH the vanilla and rando paths) to keep the
side-by-side comparator clean. Fanfare
(`kFeatures0_FastFanfare`) clamps the `ancilla_aux_timer` get-holds at
`AncillaAdd_ItemReceipt` with the grant otherwise byte-identical.

### D5 — F4 cutscene FF compresses dwell, preserves every flag

Per-cutscene fast-paths gate on `kFeatures0_CutsceneFastForward` and compress
submodule dwell / zero per-stage timers so the module logic still executes every
flag-setting stage. **Invariant:** after a fast-forwarded sequence, `g_ram`+SRAM are
in the exact state the full sequence produces — verified by an F12 dump compare
(fast vs. vanilla) per cutscene. Covered sequences: prize-get, GT crystal barrier
(visual-only; trivial), pyramid-opening, Agahnim intro (KillAgahnim module), Zelda
escort dialogue, death/game-over fade (`Module12_GameOver`). Story-dialogue
fast-forward is an explicit allowlist (opening Zelda telepathy, Uncle, Zelda escort /
Sanctuary, and post-Agahnim story messages): it may auto-advance `Waitkey`/end-message
pages, but it does not auto-select choices or apply to hint-tile ids. Mirror-warp and
flute-travel **animation** speed is included (D-context: finalize is orthogonal);
overworld/dungeon **screen-scroll** timing is explicitly out of scope (muscle-memory
footgun). Each cutscene is an independent task so partial landing is safe.

### D6 — F5 warp-to-spawn REUSES the existing S&Q spawn path; race-toggleable

`Death_Func15()` is a flag-latched state machine (see Context) whose save-and-quit branch
already lands the player at `which_starting_point` via the `kStartingPoint_*[]` load in
`src/dungeon.c` (~:8835-8864). **F5 must REUSE that spawn path, not divert or re-derive
it** — the CLAUDE.md Dark Chapel lesson is explicit that repositioning by re-derivation on
a diverted branch (with its stale-flag hazards: `death_var4/5`, `savegame_is_darkworld`,
`sram_progress_indicator`, Inverted DW forcing) is a days-costly bug class. So
`Warp_ToSpawn()` drives the SAME save-then-load-start sequence (or invokes the existing
start-point loader directly after a save), preserving every flag `Death_Func15` sets or
clears. The `kKeys_WarpToSpawn` and `kKeys_SoftResetToSpawn` hotkeys both trigger the
same audited spawn-path routine; gated on `kFeatures0_WarpToSpawn` +
rando-active + `Rando_HasActiveSettings()` where slot state is read. Off (default) =
vanilla S&Q, no hotkey. The Game Settings control carries the race-legality note (the
toggle is the ban seam). **This is the highest-risk feature — 5.1 is a flag-audit task,
not a one-liner.**

### D7 — F6 auto-dash short-circuits the charge trigger (NOT `LinkState_Dashing`)

Under `kFeatures0_AutoDash`, reach the dash trigger without the vanilla charge wait by
short-circuiting the `button_b_frames`/`StartDash` accumulation in the ground-charge
pipeline (`src/player.c` ~:1417-1426, ~:2305-2325) — NOT by touching `LinkState_Dashing()`'s
active-dash counter (see Context). The charge counter is **shared with the sword
spin-charge**, so F6 MUST NOT perturb that path (JP-glitch faithfulness), and requires a
source re-grep of the exact charge trigger before coding. Composes with
`kFeatures0_TurnWhileDashing`. Off = vanilla.

### D8 — F7 auto-tracker connection feed, observation-only

Add a `discovered_connections` (entrances) / `discovered_doors` field to the
`AutoTracker_ServiceFrame` snapshot, populated from the door/entrance runtime override
tables, emitting only non-vanilla, already-traversed connections. No in-game overlay.
Empty under a non-shuffled seed. The snapshot is a wire contract external clients parse;
the new key is **purely additive** (existing clients tolerate an unknown key) and needs
**no snapshot schema-version bump** — it is documented alongside the existing snapshot
keys so clients can opt in.

### D9 — Verification: playtest is the net, corpus proves neutrality

These live at gameplay/render sites the corpus and `--rando-selftest` never exercise
(the project's dominant-bug-class discipline). Each feature carries a playtest
checkpoint in `tasks.md`. Two special checks: **F4** requires a per-cutscene F12
dump compare (fast-forwarded `g_ram`/SRAM == vanilla) to prove the flag-preservation
invariant; **F3** requires a hint-tile readability check at `instant`. A corpus regen
must be **byte-identical** with no version bump (D1) — that is the neutrality proof.

## Risks

- **F4 flag loss** (highest). Mitigation: dwell-compression not stage-skipping, plus
  the mandatory F12 compare. If a given cutscene can't be sped without stage-skipping,
  it is dropped from scope rather than shipped with a flag risk.
- **F1 per-frame cost.** Mitigation: cache per map-open (D2).
- **F3 hint blow-through.** Mitigation: never touch wait-for-input (D4).
- **RAM-compare divergence.** Mitigation: Seed-QoL-class gating + `!ZeldaIsEmulatorAttached()`
  where needed; F1/F5/F7 are rando-only and not run side-by-side.
