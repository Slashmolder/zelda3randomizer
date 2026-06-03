# Audit — PC native settings window

## Fresh-eyes audit — 2026-06-02 (tasks 23.1–23.3)

Read-only fresh-eyes pass over `rando_window.cpp`, `game_config_panels.cpp`,
`rando_window_bridge.c`, `imgui_host.cpp`, and the panels
(`rando_reach_panel.cpp`, `rando_hints_panel.cpp`, `panels_selftest.cpp`),
plus the bridge consumers in `main.c` and the `Rando_GenerateSlot` seam. Findings
and ready patches are tabulated below.

**No HIGH findings. No determinism-affecting findings (task 23.3 satisfied)** — the
UI never seeds or perturbs the placement RNG; `seed_u64` (incl. the SplitMix64
seed-roll) is a pure input; settings round-trip is integrity-checked
(serialize→hex→deserialize→re-serialize→memcmp). All findings are UI/UX, none touch
`settings_hash`, canonical bytes, `kGeneratorVersion`, or the share string.

| # | Sev | Site | Finding | Disposition | Verify |
|---|---|---|---|---|---|
| NW1 | MED | `game_config_panels.cpp:755` / `rando_window.cpp:1304` | `RandoWindow_OpenForNewSlot` (file-select "New Randomizer" entry) never re-syncs the Game Settings working copy (`SyncFromLive` runs only on first-ever open; `RandoWindow_ToggleConfig` re-syncs via `GameConfig_NotifyWindowOpened`, this path does not). If live `g_config` drifted (window scale, msuvolume, a live features0 toggle), editing any field + Apply commits the stale copy and reverts the drift. | **DEFER → ready patch:** call `GameConfig_NotifyWindowOpened()` in `RandoWindow_OpenForNewSlot`, mirroring `RandoWindow_ToggleConfig`. | PLAYTEST |
| NW2 | LOW | `game_config_panels.cpp:248-266` (`ApplyPending`) | `g_config = s_cfg` whole-struct copy re-points only 5 of 7 `Config` pointer fields; `rando_spoiler_dir` + `memory_buffer` keep sync-time values. Benign today (neither repoints between open and Apply), but latent: a future repoint between open and Apply would be silently reverted (dangling `memory_buffer` = UAF hazard). | **DEFER → ready patch:** after the struct copy, restore `g_config.memory_buffer`/`rando_spoiler_dir` from the live `prev`, or copy scalars field-wise. | BUILD |
| NW3 | LOW | `rando_window.cpp:204-211, 1304` | Stale `s_accessibility_pre_lock` after restoring a persisted `goal=Completionist` config: the lock never engages on the restore frame, so later switching off Completionist leaves accessibility pinned at "locations". UX wart; serialized value always valid. | **DEFER → ready patch:** call `ApplyAccessibilityLock(&pending)` once in the open/restore path. | PLAYTEST |

**Verified correct (no finding):** every enum combo maps UI index → canonical enum
1:1 (world_state/goal/item_pool/dungeon modes/mode_weapons/accessibility — incl.
`kAccessibility_None=2` ↔ "beatable only"); all UI defaults match
`Settings_SetDefaults`; the `region_boss_hearts_in_pool` checkbox inversion is
correct; bridge single-shot semantics (`Consume*Request` clear atomically on the
single main thread; generate→load sequenced across frames; `slot_index<0` rejected);
race-mode gating cannot leak (Spoiler tab keyed off the active slot's
`last_generated_race_mode`, self-checked at init; Hints panel count-only + Reach
panel name-hiding under race, all gating on the active slot not `pending`); event
routing dispatches each windowed event to exactly one consumer; interned strings
come from the process-lifetime arena (no UAF).

Auditor confidence: medium-high (full read of assigned files + bridge consumers +
sidecar + the `Rando_GenerateSlot` seam). Per the project's own rule, the MED is the
one to prioritize for a playtest — UI↔game-frame seam regressions are only reliably
caught end-to-end.
