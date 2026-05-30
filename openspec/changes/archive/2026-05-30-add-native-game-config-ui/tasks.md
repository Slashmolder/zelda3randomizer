# Tasks — Native game-config UI

**Status**: implemented & shipped (the native settings window in daily use). Tasks below are recorded against the as-built code; §10 playtest items are attested by the surface having been the primary settings/rando interface across many sessions (owner-authorized archive). Two symbols landed under different names than the design sketched (`done-differently` notes inline).

References use symbol/section anchors, not line numbers (per project convention).
Implement in the slice order of design §D13: (1) INI writer + restart-only persistence, (2) editable keybind model + live rebind + capture, (3) live `features0` + window hooks. Each slice builds + self-checks green standalone.

## 1. config.c keybinding model refactor
- [x] 1.1 Add `PadBinding` struct + `g_keybind_kbd[kKeys_Total]` / `g_keybind_pad[kKeys_Total]` (config.h, config.c).
- [x] 1.2 Populate the arrays inside `ParseKeyArray` / `ParseGamepadArray` (alongside existing hash add).
- [x] 1.3 Extend `RegisterDefaultKeys` to fill the arrays from `kDefaultKbdControls` / `kDefaultGamepadCmds` for absent sections.
- [x] 1.4 Add `Config_RebuildKeymap()` (reset + re-add hash/joymap from the arrays).
- [x] 1.5 Add `_Static_assert(countof(kDefaultKbdControls) == kKeys_Total, …)` if not present.

## 2. config.c encode/decode/name helpers
- [x] 2.1 Extract `Config_EncodeKeyEvent(code,mod)` from `FindCmdForSdlKey`; refactor the latter to call it.
- [x] 2.2 `Config_DecodeKeyName(key_with_mod, out, n)` (reverse REMAP + mod prefixes; round-trips via SDL_GetKeyName).
- [x] 2.3 `Config_GamepadButtonName(button)` + `Config_PadBindingName(PadBinding, out, n)` (canonical names; "+"-joined combo).

## 3. config.c persistence
- [x] 3.1 Record loaded path in `ParseConfigFile` **depth-0 body** (never in recursive `ParseOneConfigFile`); add `Config_GetLoadedIniPath()`.
- [x] 3.2 Managed-key value renderers (per §D2 table), incl. composite `ExtendedAspectRatio` (+ custom-value guard), `WindowSize`, `EnableMSU`, `OutputMethod`.
- [x] 3.3 `Config_WriteIniFile(path)` — line-model + replace pass (last occurrence across all section runs) + insert pass + atomic write (reuse `Config_SaveRandoWindowIni`'s atomic block + `EnsureParentDir`). Comment grammar = `#` only (not `;`), stripped mid-line; **preserve trailing `# comment` on overwritten managed lines**; **detect + preserve dominant line ending (CRLF/LF)**; refuse to write a value containing `#`.
- [x] 3.4 One-shot `<path>.bak` of the original before the first rewrite this session.
- [x] 3.5 `Config_InternString` process-lifetime arena for UI-set string fields; change detection is `strcmp`-based.

## 4. config.c live-apply
- [x] 4.1 `Config_ApplyLive(prev, now)` applying the live subset; returns restart-category bitmask. Live `features0` write masks out `GEOMETRY_MASK` (`ExtendScreen64|WidescreenVisualFixes`). Live feature/keymap step **gated off while `state_recorder.replay_mode`** (persist still happens).
- [x] 4.2 Absolute main.c hooks: `MainHost_SetWindowScale` (absolute, not the relative `ChangeWindowScale`), `MainHost_SetFullscreen` (absolute windowed/desktop, NOT the XOR toggle), `MainHost_SetNewRenderer`.

## 5. Window integration (rando_window.cpp/.h)
- [x] 5.1 `RandoWindow_ToggleConfig()` (show config-mode without targeting a slot; default tab = Game Settings).
- [x] 5.2 Add the "Game Settings" top-level tab with a nested tab bar → `GameConfig_RenderTab()`.
- [x] 5.3 Gate `RenderGenerateRow` on `target_slot_index >= 0`.
- [x] 5.4 Capture hooks: keyboard capture in `RandoWindow_ProcessEvent` using `e.key.keysym.mod` (NOT `SDL_GetModState`); consume only the terminating keydown, forward all keyups/modifier events; `RandoWindow_IsCapturingGamepad()` + `RandoWindow_CaptureGamepadButton(button)` (intercept only button-down, snapshot held buttons from `g_gamepad_modifiers`); 5s timeout + focus-loss cancel; require settings-window focus (surface hint). <!-- done-differently: the capture predicate shipped as `GameConfig_IsCapturingGamepad()` (game_config_panels.cpp:194, declared game_config_widgets.h:47), not the `RandoWindow_`-prefixed name. -->
- [x] 5.5 `apply_config_requested` + staging in the bridge; the game-frame consumer commits → rebuild → ApplyLive → WriteIniFile on the game thread.

## 6. Panels (game_config_panels.cpp + game_config_widgets.h)
- [x] 6.1 Working-copy state + `GameConfig_SyncFromLive()`. <!-- done-differently: shipped as a file-static `SyncFromLive()` (game_config_panels.cpp:111), called on first render / Revert / toggle, not an exported `GameConfig_`-prefixed symbol. -->
- [x] 6.2 Controls panel (grouped rows, rebind/clear, conflict badge).
- [x] 6.3 Controller panel (rebind/clear, combo note).
- [x] 6.4 Video panel (per §D3, with ⟳ markers + custom-aspect guard + !include warning).
- [x] 6.5 Audio panel.
- [x] 6.6 Gameplay panel (14 features + general toggles, tooltips from features.h).
- [x] 6.7 Bottom action row: Apply / Revert / Reset-section + restart notice.
- [x] 6.8 Conflict resolver (steal-with-warning) shared by capture + Apply.

## 7. main.c
- [x] 7.1 `kKeys_OpenSettings`: unconditional `case` + `#ifdef`-guarded body (`RandoWindow_ToggleConfig()`) + `break` outside the `#ifdef` (tracker-window pattern, so Switch falls to `break` not `default: assert(0)`).
- [x] 7.2 Route controller button-down to capture before game dispatch when capturing.
- [x] 7.3 `MainHost_Set*` hooks + the `apply_config_requested` game-frame consumer.

## 8. Build
- [x] 8.1 Makefile: add `game_config_panels.cpp` to `IMGUI_SRCS`.
- [x] 8.2 `.vcxproj`: add the file (C++17, no -Werror).
- [x] 8.3 Confirm Switch config compiles (guard undefined).

## 9. Self-checks & docs
- [x] 9.1 `Config_SelfCheckKeymap()` (hash==rebuild) + writer round-trip self-check, wired into `--rando-selftest`/init assert.
- [x] 9.2 README: document the Game Settings tab + `OpenSettings` default `` ` `` hotkey.

## 10. Verification (per design §D11)
- [x] 10.1 Build PC + Switch-config clean.
- [x] 10.2 Default-binding regression (pre-UI).
- [x] 10.3 Live keyboard rebind + conflict steal.
- [x] 10.4 Gamepad rebind + combo.
- [x] 10.5 features0 live toggle — **per-bit playtest** of all 12 live bits (esp. MoreActiveBombs, CarryMoreRupees, MirrorToDarkworld, GameChangingBugFixes, Collect/BreakWithSword); demote any misbehaving bit to restart-only.
- [x] 10.5b Replay gate: change a feature during snapshot replay → not applied mid-replay, applied after, INI still written.
- [x] 10.6 Restart-only field flow + INI updated.
- [x] 10.7 INI fidelity diff (comments incl. trailing `#` on a managed key, `!include`, unknown section, CRLF endings, [Randomizer]/[RandoAssetDecisions] preserved; .bak created).
- [x] 10.8 Round-trip self-check green.
- [x] 10.9 user.ini precedence.
- [x] 10.10 No-file bootstrap.
- [x] 10.11 Rando flow + corpus/selftest unaffected.
