# Seed QoL — tasks

Ordered by the reconciled priority (F1 → F7). Each feature is an independent,
buildable, playtestable slice; the off-path stays byte-identical throughout (D1).
The load-bearing net is the per-feature **playtest** (these paths are invisible to
the corpus and `--rando-selftest`); F4 additionally needs an F12 flag-preservation
compare and F3 a hint-readability check (D9). Decision labels (D1…) reference
`design.md`. Re-grep every indicative line number before editing (Makefile has no
header-dep tracking → `make clean` after any `features.h`/`config.h` edit).

## 0. Feature-bit foundation (feature OFF, byte-identical) — D1

- [x] 0.1 Add the new `kFeatures0_*` bits (F1 `RandoDungeonCheckCounts`,
  F3 `FastFanfare`, F4 `CutsceneFastForward`, F6 `AutoDash`,
  F5 `WarpToSpawn`) in the free bit-20..31 range of
  `src/features.h`; add the per-slot subset (F1/F3/F4/F6) to
  `kFeatures0_RandoSeedQolMask`. Leave F5 out of the mask (local/race);
  bit 21 remains reserved for the deferred F2 overlay.
- [x] 0.2 Add the `text_speed` global selector to `src/config.{c,h}` +
  `zelda3.ini` parsing (F3 draw speed), and the `kKeys_WarpToSpawn` /
  `kKeys_SoftResetToSpawn` command IDs to the `kKeys_*` enum + `kDefaultKbdControls[]` +
  `kKeyNameId[]` (keep the enum/array lock-step assert green).
- [x] 0.3 `make clean` + build (`-Werror`) + `--rando-selftest` green; **corpus
  byte-identical, no `kGeneratorVersion` bump** (no registry/settings change). This
  is the neutrality checkpoint every later phase must preserve.

## 1. F1 — Dungeon check-info on the pause map — D2

- [x] 1.1 Add `Rando_DungeonCheckCounts(dungeon_id, &checked, &total)` iterating the
  location registry filtered by dungeon via `Rando_IsLocationChecked`; compute
  on map-open / checked-bitmap change and cache in a static (NOT per-frame, NOT
  `g_ram`).
- [x] 1.2 Draw the per-dungeon remaining count on the Module 0x0E dungeon map,
  gated on `kFeatures0_RandoDungeonCheckCounts` + rando-active; counts only.
- [ ] 1.3 (Phase 2) Add located-check dots using a location→map-cell mapping, gated
  the same way; ship after 1.2 is validated.
- [ ] 1.4 Build + **playtest**: counts update as a dungeon's checks are collected;
  race-mode shows counts only (no item names); off/non-rando renders vanilla.

## 2. F2 — In-game seed info panel — deferred — D3

- [x] 2.1 Defer/remove the in-game seed info overlay from this change after playtest:
  no runtime draw path, no config/UI toggle, and no per-slot mask bit. The existing
  native tracker/generate-copy surfaces remain the seed identity/requirement surface.

## 3. F3 — Message & fanfare speed (hint-safe) — D4

- [x] 3.1 Override the *effective* per-character wait at
  `RenderText_Draw_MessageCharacters` by the `text_speed` selector (the LUT is indexed
  by an in-text param — override the selection, not the table); for `instant`, also
  clamp the initial/post-page input latch and text-box scroll commands so page turns
  lose their dead time while generic hint/sign text still requires input. Because
  `text_speed` is global (not rando-only), **suppress the override under
  `ZeldaIsEmulatorAttached()`** so the side-by-side comparator stays clean on both paths.
- [x] 3.2 Clamp the `ancilla_aux_timer` get-holds in `AncillaAdd_ItemReceipt` under
  `kFeatures0_FastFanfare`, keeping the grant otherwise byte-identical.
- [ ] 3.3 Build + **playtest incl. hint-readability check**: at `instant`, dialogue
  fills instantly but a hint tile / telepathy / rando hint still waits for input and
  is readable; item/key/map/compass gets advance faster with identical effect.

## 4. F4 — Cutscene & transition fast-forward (skip animation, never flag) — D5

- [ ] 4.1 Add per-cutscene dwell-compression gated on
  `kFeatures0_CutsceneFastForward`, one commit per sequence:
  prize-get, GT crystal barrier, pyramid-opening, Agahnim intro (KillAgahnim),
  Zelda escort, death/game-over fade (`Module12_GameOver`). Compress submodule
  dwell / zero per-stage timers — never jump past a flag-setting stage.
- [x] 4.1a Add a story-dialogue allowlist for Standard intro / Uncle / Zelda escort /
  Sanctuary / post-Agahnim messages that auto-advances `Waitkey` and end-message pages
  under `kFeatures0_CutsceneFastForward`, without auto-selecting choices or touching
  hint-tile ids.
- [x] 4.2 Add mirror-warp + flute-travel **animation** speed-up (visual submodule
  chain only); leave `MirrorWarp_FinalizeAndLoadDestination` (position/camera/music)
  untouched. Do NOT change screen-scroll timing.
- [ ] 4.3 Build + **F12 flag-preservation compare per cutscene**: dump `g_ram`/SRAM
  after a fast-forwarded sequence and after the vanilla sequence — they MUST match
  (esp. `sram_progress_indicator`, `link_has_crystals`). Any cutscene that can't be
  sped without stage-skipping is DROPPED from scope, not shipped. Playtest the
  downstream triggers (reload after a skipped Agahnim/escort behaves identically).

## 5. F5 — Quick reset / warp-to-spawn (race-toggleable) — D6

- [x] 5.1 Add `Warp_ToSpawn()` that **REUSES the existing S&Q spawn path** — the
  `Death_Func15` save-and-quit branch already lands at `which_starting_point` via the
  `kStartingPoint_*[]` loader (`src/dungeon.c` ~:8835-8864). Drive that SAME
  save-then-load-start sequence (or call the start-point loader after a save); do NOT
  divert/re-derive the branch. Trigger from `kKeys_WarpToSpawn` /
  `kKeys_SoftResetToSpawn` under
  `kFeatures0_WarpToSpawn` + rando-active + `Rando_HasActiveSettings()`. Off = vanilla S&Q.
- [x] 5.1a **Flag audit (load-bearing):** enumerate every flag `Death_Func15` sets /
  clears / consumes (`death_var4/5`, `savegame_is_darkworld`, `sram_progress_indicator`,
  Inverted DW forcing, `subsubmodule_index`) and confirm `Warp_ToSpawn` leaves each in the
  exact post-S&Q state — this is the CLAUDE.md Dark Chapel bug class; a stale flag
  corrupts the NEXT spawn/transition.
- [x] 5.2 Add the Game Settings toggle + keybind with the **race-legality note**.
- [ ] 5.3 Build + **playtest**: warp returns to the correct spawn without file-select;
  save consistent; disabled = vanilla S&Q with no hotkey.

## 6. F6 — Auto / hold-to-dash — D7

- [x] 6.1 **Re-grep the actual charge trigger first** (the `button_b_frames`/`StartDash`
  accumulation in the ground-charge pipeline, `src/player.c` ~:1417-1426, ~:2305-2325 —
  NOT `LinkState_Dashing`'s active-dash counter). Under `kFeatures0_AutoDash`,
  short-circuit the charge wait there **without perturbing the shared sword spin-charge
  path** (JP-glitch faithfulness); compose with `kFeatures0_TurnWhileDashing`.
- [ ] 6.2 Build + **playtest**: hold-dash triggers without the charge wait; off =
  vanilla boots.

## 7. F7 — Entrance/door connection feed to the auto-tracker — D8

- [x] 7.1 Add a `discovered_connections`/`discovered_doors` field to the
  `AutoTracker_ServiceFrame` snapshot, populated from the door/entrance runtime
  override tables; emit only non-vanilla, already-traversed connections;
  observation-only; empty under a non-shuffled seed. The key is **purely additive**
  (no snapshot schema-version bump); document it with the existing snapshot keys.
- [ ] 7.2 Build + **playtest with an external client**: traversing a shuffled
  entrance reports its destination in the feed; no in-game overlay; a non-shuffled
  seed emits no connection data.

## 8. Close-out — D1, D9

- [x] 8.1 Final `make clean` + build (MSVC + gcc `-Werror`) + `--rando-selftest`
  green; **corpus regen byte-identical, no `kGeneratorVersion` bump** (neutrality
  proof for the whole bundle).
- [x] 8.2 Independent fresh-eyes review of the landed surface (per the project's
  review cadence) before declaring done — ask for NEW findings, cap the response.
- [ ] 8.3 Reconcile these `specs/` deltas against as-built source, then archive on
  the branch before the squash-merge.
