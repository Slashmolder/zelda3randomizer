# Seed QoL — tasks

Ordered by the reconciled priority (F1 → F8). Each feature is an independent,
buildable, playtestable slice; the off-path stays byte-identical throughout (D1).
The load-bearing net is the per-feature **playtest** (these paths are invisible to
the corpus and `--rando-selftest`); F4 additionally needs an F12 flag-preservation
compare and F3 a hint-readability check (D10). Decision labels (D1…) reference
`design.md`. Re-grep every indicative line number before editing (Makefile has no
header-dep tracking → `make clean` after any `features.h`/`config.h` edit).

## 0. Feature-bit foundation (feature OFF, byte-identical) — D1

- [ ] 0.1 Add the new `kFeatures0_*` bits (F1 `RandoDungeonCheckCounts`, F2
  `RandoSeedInfoPanel`, F3 `FastFanfare`, F4 `CutsceneFastForward`, F6 `AutoDash`,
  F5 `WarpToSpawn`, F7 `SecondQuickSlot`) in the free bit-20..31 range of
  `src/features.h`; add the per-slot subset (F1/F2/F3/F4/F6) to
  `kFeatures0_RandoSeedQolMask`. Leave F5/F7 out of the mask (local/race).
- [ ] 0.2 Add the `text_speed` global selector to `src/config.{c,h}` +
  `zelda3.ini` parsing (F3 draw speed), and the `kKeys_WarpToSpawn` /
  `kKeys_SecondItem` command IDs to the `kKeys_*` enum + `kDefaultKbdControls[]` +
  `kKeyNameId[]` (keep the enum/array lock-step assert green).
- [ ] 0.3 `make clean` + build (`-Werror`) + `--rando-selftest` green; **corpus
  byte-identical, no `kGeneratorVersion` bump** (no registry/settings change). This
  is the neutrality checkpoint every later phase must preserve.

## 1. F1 — Dungeon check-info on the pause map — D2

- [ ] 1.1 Add `Rando_DungeonCheckCounts(dungeon_id, &checked, &total)` iterating the
  location registry filtered by dungeon via `Rando_IsLocationChecked`; compute
  on map-open / checked-bitmap change and cache in a static (NOT per-frame, NOT
  `g_ram`).
- [ ] 1.2 Draw the per-dungeon remaining count on the Module 0x0E dungeon map,
  gated on `kFeatures0_RandoDungeonCheckCounts` + rando-active; counts only.
- [ ] 1.3 (Phase 2) Add located-check dots using a location→map-cell mapping, gated
  the same way; ship after 1.2 is validated.
- [ ] 1.4 Build + **playtest**: counts update as a dungeon's checks are collected;
  race-mode shows counts only (no item names); off/non-rando renders vanilla.

## 2. F2 — In-game seed info panel — D3

- [ ] 2.1 Add a seed info overlay reading crystal requirements from
  `Rando_GetActiveSettings()->crystals_ganon`/`->crystals_tower`, hunt progress from
  `g_rando_triforce_piece_count` vs. `pieces_required`, and identity from the slot's
  stored v1 identity string; gated on `kFeatures0_RandoSeedInfoPanel` + rando-active.
  **Guard on `Rando_HasActiveSettings()`** — `Rando_GetActiveSettings()` is NULL on
  snapshot-restore / v1 slots; show blank/"unknown" (the identity string may still
  show) rather than deref NULL.
- [ ] 2.2 Build + **playtest**: values correct for a crystal seed and a
  Triforce-Hunt seed; **a snapshot-restore / v1 slot shows the blank fallback, not a
  crash**; race-safe (no placement shown); off renders vanilla.

## 3. F3 — Message & fanfare speed (hint-safe) — D4

- [ ] 3.1 Override the *effective* per-character wait at
  `RenderText_Draw_MessageCharacters` by the `text_speed` selector (the LUT is indexed
  by an in-text param — override the selection, not the table); **do NOT touch the
  end-of-box wait-for-input** (hint-safety invariant). Because `text_speed` is global
  (not rando-only), **suppress the override under `ZeldaIsEmulatorAttached()`** so the
  side-by-side comparator stays clean on both paths.
- [ ] 3.2 Clamp the `ancilla_aux_timer` get-holds in `AncillaAdd_ItemReceipt` under
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
- [ ] 4.2 Add mirror-warp + flute-travel **animation** speed-up (visual submodule
  chain only); leave `MirrorWarp_FinalizeAndLoadDestination` (position/camera/music)
  untouched. Do NOT change screen-scroll timing.
- [ ] 4.3 Build + **F12 flag-preservation compare per cutscene**: dump `g_ram`/SRAM
  after a fast-forwarded sequence and after the vanilla sequence — they MUST match
  (esp. `sram_progress_indicator`, `link_has_crystals`). Any cutscene that can't be
  sped without stage-skipping is DROPPED from scope, not shipped. Playtest the
  downstream triggers (reload after a skipped Agahnim/escort behaves identically).

## 5. F5 — Quick reset / warp-to-spawn (race-toggleable) — D6

- [ ] 5.1 Add `Warp_ToSpawn()` that **REUSES the existing S&Q spawn path** — the
  `Death_Func15` save-and-quit branch already lands at `which_starting_point` via the
  `kStartingPoint_*[]` loader (`src/dungeon.c` ~:8835-8864). Drive that SAME
  save-then-load-start sequence (or call the start-point loader after a save); do NOT
  divert/re-derive the branch. Trigger from `kKeys_WarpToSpawn` under
  `kFeatures0_WarpToSpawn` + rando-active + `Rando_HasActiveSettings()`. Off = vanilla S&Q.
- [ ] 5.1a **Flag audit (load-bearing):** enumerate every flag `Death_Func15` sets /
  clears / consumes (`death_var4/5`, `savegame_is_darkworld`, `sram_progress_indicator`,
  Inverted DW forcing, `subsubmodule_index`) and confirm `Warp_ToSpawn` leaves each in the
  exact post-S&Q state — this is the CLAUDE.md Dark Chapel bug class; a stale flag
  corrupts the NEXT spawn/transition.
- [ ] 5.2 Add the Game Settings toggle + keybind with the **race-legality note**.
- [ ] 5.3 Build + **playtest**: warp returns to the correct spawn without file-select;
  save consistent; disabled = vanilla S&Q with no hotkey.

## 6. F6 — Auto / hold-to-dash — D7

- [ ] 6.1 **Re-grep the actual charge trigger first** (the `button_b_frames`/`StartDash`
  accumulation in the ground-charge pipeline, `src/player.c` ~:1417-1426, ~:2305-2325 —
  NOT `LinkState_Dashing`'s active-dash counter). Under `kFeatures0_AutoDash`,
  short-circuit the charge wait there **without perturbing the shared sword spin-charge
  path** (JP-glitch faithfulness); compose with `kFeatures0_TurnWhileDashing`.
- [ ] 6.2 Build + **playtest**: hold-dash triggers without the charge wait; off =
  vanilla boots.

## 7. F7 — Second item quick-slot / bomb hotkey — D8

- [ ] 7.1 Under `kFeatures0_SecondQuickSlot`, add a second selected-item byte (a named
  `kRam_*` in the reserved `0x66d-0x66f` block) driven by the L/R HUD reorder logic;
  wire `kKeys_SecondItem`.
- [ ] 7.2 Build + **playtest**: bound hotkey reaches a second item (e.g. bombs vs. a
  fire source) without opening the menu; unbound/off = vanilla input.

## 8. F8 — Entrance/door connection feed to the auto-tracker — D9

- [ ] 8.1 Add a `discovered_connections`/`discovered_doors` field to the
  `AutoTracker_ServiceFrame` snapshot, populated from the door/entrance runtime
  override tables; emit only non-vanilla, already-traversed connections;
  observation-only; empty under a non-shuffled seed. The key is **purely additive**
  (no snapshot schema-version bump); document it with the existing snapshot keys.
- [ ] 8.2 Build + **playtest with an external client**: traversing a shuffled
  entrance reports its destination in the feed; no in-game overlay; a non-shuffled
  seed emits no connection data.

## 9. Close-out — D1, D10

- [ ] 9.1 Final `make clean` + build (MSVC + gcc `-Werror`) + `--rando-selftest`
  green; **corpus regen byte-identical, no `kGeneratorVersion` bump** (neutrality
  proof for the whole bundle).
- [ ] 9.2 Independent fresh-eyes review of the landed surface (per the project's
  review cadence) before declaring done — ask for NEW findings, cap the response.
- [ ] 9.3 Reconcile these `specs/` deltas against as-built source, then archive on
  the branch before the squash-merge.
