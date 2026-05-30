# Design — Native game-config UI

This document is the implementation spec. It is grounded in the current source (file:line are anchors at authoring time — verify the symbol, not the line, per the project's "comment anchors not line numbers" rule).

> **Revision note (post fresh-eyes round 1).** Incorporates: `#`-only comment grammar (`util.c:NextLineStripComments` strips only `#`, mid-line; `;` is NOT a comment; `\r` is tolerated on read); trailing-comment + line-ending preservation in the writer; loaded-path captured at depth-0; capture uses the event's `keysym.mod` (not `SDL_GetModState`); live-apply runs on the **game thread** via a bridge request (not mid-ImGui-render); live `features0` masks out the two restart-only geometry bits and is **gated off during replay**; absolute (not toggle/relative) window hooks; functional (not byte) hash-equivalence self-check.

## Ground truth (what exists today)

- **`g_config`** (`src/config.h:63-104`) holds all non-rando configuration, populated once by `ParseConfigFile` (`config.c:697`) and **never written back**. The only INI writer is `Config_SaveRandoWindowIni` (`config.c:790`) → the *sidecar* `saves/rando_window.ini`.
- **Comment grammar** (`util.c:NextLineStripComments`, :59): only `#` introduces a comment, and it is stripped **anywhere on the line** (so a value is truncated at the first `#`). `;` is an ordinary character. Trailing `\r`/space/tab are stripped on read (CRLF tolerated). `!include <file>` is handled by the caller (`config.c:676`).
- **Load order** (`config.c:706-711`): explicit filename arg → else `zelda3.user.ini` (if present) → else `zelda3.ini`. `ParseOneConfigFile` is **recursive via `!include`** and re-points `g_config.memory_buffer` to each file it reads (`config.c:667`), so the "which file did we load" decision can only be made in the depth-0 `ParseConfigFile` body, never inside `ParseOneConfigFile`.
- **String-valued fields** (`link_graphics`, `shader`, `msu_path`, `language`) point *into* `g_config.memory_buffer`. Re-pointing them requires owned storage.
- **Keybindings** parse into derived structures with no reverse path / no rebuild:
  - keyboard `keymap_hash` via `KeyMapHash_Add` (`config.c:107`), keyed by 16-bit `key_with_mod` (`kKeyMod_ScanCode=0x200,_Alt=0x400,_Shift=0x800,_Ctrl=0x1000`; low 9 bits = keycode/scancode). Rejects duplicates. Lookup `KeyMapHash_Find` matches the exact key (bucket order irrelevant to results).
  - gamepad `joymap_ents` via `GamepadMap_Add` (`config.c:207`), keyed by button id + 32-bit held-button modifier mask; `GamepadMap_Add` inserts **most-modifiers-first** and `FindCmdForGamepadButton` returns the first match (order matters → re-derivation must reproduce the sort, which `GamepadMap_Add` does automatically).
- **Command set**: `kKeys_*` enum (`config.h:5-54`); `kKeyNameId[]` (`config.c:83`) gives each command its INI name + slot count (`Controls`=12, `Load/Save/Replay/LoadRef/ReplayRef`=20, rest=1). **`kKeyNameId` order is decoupled from enum order** (e.g. `VolumeUp/VolumeDown` precede `DisplayPerf/ToggleRenderer` in the name table but follow in the enum); `RegisterDefaultKeys` indexes `kDefaultKbdControls[k]` by `kKeyNameId[i].id`, so order is irrelevant for correctness — the entry only needs to **exist**. `kDefaultKbdControls[kKeys_Total]` (`config.c:47`) is FLAT, indexed by expanded command id, and its initializer is positionally enum-ordered. `kDefaultGamepadCmds[]` (`config.c:260`) is a **12-entry** table (NOT `kKeys_Total`-sized) binding 12 buttons to `kKeys_Controls+0..11`.
- **Runtime input**: `FindCmdForSdlKey` (`config.c:142`) is called from `HandleInput` with `event.key.keysym.mod` (`main.c`), and `FindCmdForGamepadButton` from `HandleGamepadInput` (`main.c:1834`), which maintains `g_gamepad_modifiers` by XOR-toggling on each button down/up. `HandleCommand_Locked` (`main.c:1706`) executes a command id; its `default:` is `assert(0)` (`main.c:1794`), but unconditional `case` labels with `#ifdef`-guarded bodies fall to `break` (the `RandoItemTrackerWindow` pattern, `main.c:1785-1793`).
- **`features0` runtime path**: `g_wanted_zelda_features` seeded once from `g_config.features0` (`main.c:1055`); **only inside the non-replay branch** of the frame driver (`zelda_rtl.c:~787-829`) is it mirrored into `g_ram+0x64c` (`enhanced_features0`) and recorded as a StateRecorder patch (`:813-816`). **During replay (`state_recorder.replay_mode`) the mirror does not run.** No in-tree code writes `g_wanted_zelda_features` after init (the in-game recommended-features panel writes only into slot *generation*, not live) — mid-run live-flip is therefore new behavior, valid while *recording* (the patch is logged for the emulated side too) but a no-op/desync during *replay*.
- **Feature consume sites read `enhanced_features0` live** (e.g. bomb count `?3:1` at spawn, `ExtendScreen64` as a live 0x40 coord offset in `ancilla.c`/`sprite.c`/`messaging.c`) — features are NOT used to size arrays at init, so a live bit flip changes behavior from the next read, not retroactively. **Exception:** `ExtendScreen64`/`WidescreenVisualFixes` assume the framebuffer was widened at init (`g_snes_width`/extraLeftRight, `main.c:1049-1050`); flipping them live shifts coordinates against a non-widened buffer → visual corruption. These two are restart-only.
- **The window** (`src/rando/rando_window/`): one ImGui context; top-level tab bar in `RandoWindow_BeginFrame` (`rando_window.cpp:~1030`); `RandoWindowBridge` for UI↔game-frame data. Created **hidden** at startup (`main.c:1143`); shown by `RandoWindow_OpenForNewSlot`, hidden by `RandoWindow_Hide`. `RandoWindow_WantsShown()` gates per-frame `BeginFrame`/`Render` (`main.c:1547`, which saves/restores GL contexts). Settings-window-`windowID` events route to `RandoWindow_ProcessEvent` (ImGui) and never reach the game (`main.c:1318-1327`); tracker windows consume their events first (`Z3RHost_ProcessEvent`, `main.c:1296`). Controller/joystick events are **global** (no windowID) → always game path. `RenderGenerateRow` already degrades gracefully when `target_slot_index < 0` (disabled button + tooltip), and `Rando_GenerateSlot` has its own `-1` guard.
- **Audit guard** (`assets/scripts/check_audit_guard.py`) rglobs `src/**/*.c` and excludes paths containing `"rando"` (so `src/rando/*.c` and `src/rando/rando_window/*.c` are skipped) — **but `src/config.c` and `src/main.c` ARE scanned.** It does not scan `.cpp`. The new C helpers in `config.c` write no tracked `g_ram` cell, so they pass; the guard is not bypassed, it simply has nothing to flag.

## Architecture overview

```
  ┌─────────────────────────── ImGui "Z3R Settings" window ────────────────────────────┐
  │ [Game Settings ▼]                                  [Randomizer tabs: General … Spoiler] │
  │   ├ Controls / Controller / Video / Audio / Gameplay (nested tab bar)                  │
  │ ───────────────────────────────────────────────────────────────────────────────────  │
  │ [Apply] [Revert] [Reset section]            ⟳ restart-required notice (after Apply)     │
  └──────────────────────────────────────────────────────────────────────────────────────┘
        edits ▼ (UI working copy)         Apply → bridge request ▼ (consumed on game frame)
  ┌──────────────────────────────┐                          ┌────────────────────────────┐
  │ s_cfg (scalars) + path bufs  │                          │ game frame:                 │
  │ s_kbd[] / s_pad[] bindings   │  RandoWindowBridge       │  commit → g_config + model  │
  │ s_dirty / s_restart_mask     │  .apply_config_requested │  Config_RebuildKeymap()     │
  └──────────────────────────────┘ ───────────────────────▶ │  Config_ApplyLive(prev,now) │
                                                             │  Config_WriteIniFile(path)  │
                                                             └────────────────────────────┘
```

The UI edits a **working copy**, never `g_config` live. `Apply` raises a bridge flag; the **game-frame consumer** (same place `generate_requested` is consumed, `main.c`) does the commit + live-apply + INI write **on the game thread with the game GL context current** — never mid-ImGui-render. `Revert` reloads the working copy from `g_config`.

---

## D1. Editable keybinding model (config.c refactor)

```c
// config.h
typedef struct PadBinding { int16 button; uint32 modifiers; } PadBinding; // button=-1 unbound
extern uint16     g_keybind_kbd[kKeys_Total];  // key_with_mod per expanded cmd; 0 = unbound
extern PadBinding g_keybind_pad[kKeys_Total];  // gamepad binding per expanded cmd
```

These are the **source of truth**, laid out flat like `kDefaultKbdControls` (indexed by expanded command id). The lookup hashes become a derived index.

- **Populate during parse:** `ParseKeyArray` (`config.c:156`) also writes `g_keybind_kbd[cmd]=key_with_mod`; `ParseGamepadArray` (`config.c:265`) writes `g_keybind_pad[cmd]={button,modifiers}`. **`g_keybind_pad` keeps the *last* binding parsed per command** (the runtime hash can hold alternative bindings per command; the editable model represents one). This matches the defaults and the "what button does X" UI model; the limitation is documented in-UI ("only one binding per action is editable here").
- **Defaults:** extend `RegisterDefaultKeys` (`config.c:292`) to fill `g_keybind_kbd`/`g_keybind_pad` from `kDefaultKbdControls`/`kDefaultGamepadCmds` for any command whose INI section was absent — mirroring exactly what it already does for the hash. After `ParseConfigFile` returns, both arrays fully describe the active bindings.
- **Keep the inline hash build** at parse time (startup behavior unchanged before any UI edit). `Config_RebuildKeymap()` is the post-edit re-derivation.

```c
void Config_RebuildKeymap(void); // reset keymap_hash + joymap, re-add from g_keybind_kbd/pad
```

Reset `keymap_hash_size=0` + `memset(keymap_hash_first,0,sizeof)`, `joymap_size=0` + `memset(joymap_first,0,sizeof)` (both file-static, `config.c:102,189`; reuse the realloc'd backing buffers — no free), then walk `kKeys_Total`: nonzero `g_keybind_kbd[i]`→`KeyMapHash_Add(v,i)`; `g_keybind_pad[i].button>=0`→`GamepadMap_Add(button,modifiers,i)`. The model is conflict-free by construction (§D5) so no `KeyMapHash_Add` rejection. `GamepadMap_Add` re-imposes the most-modifiers-first order, so **lookup results** are reproduced even though the raw array contents/`next` chaining differ from the parse-time build (this is *functional* equivalence — see §D8; the earlier "byte-identical" framing was wrong).

### Encode/decode/name helpers (single source of truth)

```c
uint16 Config_EncodeKeyEvent(SDL_Keycode code, SDL_Keymod mod); // identical bit-composition to FindCmdForSdlKey
bool   Config_DecodeKeyName(uint16 key_with_mod, char *out, size_t n); // "Ctrl+F1"; false if unrepresentable
const char *Config_GamepadButtonName(int button);                 // canonical: "L1","DpadUp","A" (never alias "Lb")
bool   Config_PadBindingName(PadBinding b, char *out, size_t n);   // "L1+Start" / "A" / "" (unbound)
```

- `Config_EncodeKeyEvent` is **extracted from** `FindCmdForSdlKey` (`config.c:142-153`, modifier-key-exclusion logic included); `FindCmdForSdlKey` becomes `KeyMapHash_Find(Config_EncodeKeyEvent(code,mod))`. **Capture passes the event's `keysym.mod`** (what the runtime lookup uses), never `SDL_GetModState()`, so a captured binding composes bit-for-bit with the runtime lookup.
- `Config_DecodeKeyName` reverses `REMAP_SDL_KEYCODE`: `keycode = (k & kKeyMod_ScanCode) ? ((k & 0x1FF) | SDLK_SCANCODE_MASK) : (k & 0x1FF)`; then `SDL_GetKeyName(keycode)`, prefixed `Ctrl+`/`Shift+`/`Alt+`. **If `SDL_GetKeyName` returns `""`** (layout-unnamed keycode), return `false` → the writer keeps the existing INI line for that command verbatim and the UI shows the binding as "(unnamed key)" rather than emitting an empty, drop-on-reload token. (`SDL_GetKeyName`→`SDL_GetKeyFromName` round-trips for all stock defaults — `"Up"`,`"Right Shift"`,`"Return"`,`"Tab"`,`"F1"`, letters; the empty-name path is the only failure mode and is handled.)

---

## D2. `Config_WriteIniFile` — comment-preserving in-place rewriter

```c
void Config_WriteIniFile(const char *path);   // in-place, atomic, preserves everything we don't manage
const char *Config_GetLoadedIniPath(void);    // the file ParseConfigFile chose at depth-0
```

`ParseConfigFile` records the resolved path into a static `g_loaded_ini_path` **in its own body** (the explicit filename, or `"zelda3.user.ini"`/`"zelda3.ini"` per the branch taken) — never inside the recursive `ParseOneConfigFile` (which would record an `!include`d child). If neither file existed (defaults), the path is `"zelda3.ini"` (created on first write). The UI writes to `Config_GetLoadedIniPath()`.

### Managed-key value renderers
A static table maps each (section, key) we own to a function producing its current string (from `g_config` + the keybind model, *after* Apply commits):

| Section | Keys |
|---|---|
| `[KeyMap]` | one line per `kKeyNameId` name: comma-joined `Config_DecodeKeyName` over the command's slot range (empty slots → empty fields) |
| `[GamepadMap]` | same names: comma-joined `Config_PadBindingName` over slots |
| `[Graphics]` | `WindowSize`(`Auto`/`WxH`), `Fullscreen`, `WindowScale`, `NewRenderer`, `EnhancedMode7`, `IgnoreAspectRatio`, `NoSpriteLimits`, `LinearFiltering`, `OutputMethod`(enum), `Shader`, `LinkGraphics`, `DimFlashes`(`features0` bit — stays in `[Graphics]` where the parser reads it, `config.c:478`) |
| `[Sound]` | `EnableAudio`, `AudioFreq`, `AudioChannels`, `AudioSamples`, `EnableMSU`(bitfield→`false`/`true`/`deluxe`/`opuz`/`deluxe-opuz`), `MSUPath`, `MSUVolume`, `ResumeMSU` |
| `[General]` | `Autosave`, `ExtendedAspectRatio`(composite), `DisplayPerfInTitle`, `DisableFrameDelay`, `Language` |
| `[Features]` | the 14 bool keys (`config.c:553-580`), each from its `features0` bit |

- **Composite `ExtendedAspectRatio`:** emit `<ratio>[,extend_y][,unchanged_sprites][,no_visual_fixes]`, ratio ∈ `{4:3,16:9,16:10,18:9}` kept as an explicit UI enum. On *load*, derive the enum by computing the four candidate `extended_aspect_ratio` values for the current `extend_y` height and matching; **0 ⇒ `4:3`**; no match (hand-set custom) ⇒ display "custom (4:3 shown)" and **leave the field non-editable** so Apply never rewrites a value the UI can't represent. `unchanged_sprites`/`no_visual_fixes` follow the parser's bit-derivation (`config.c:539-542`).
- **Value sanity:** a managed value that would contain a `#` (e.g. a path) cannot survive the parser (it truncates at `#`), so we **refuse to write** such a value (keep the prior line) and surface a UI warning. No value we manage legitimately contains `,` except the comma-list keymap lines, which are constructed, not user-free-text.

### Algorithm (preserves comments, order, line endings)
1. Read the file (absent → empty; step 4 appends all). **Detect the dominant line ending** (CRLF if the first `\n` is preceded by `\r`, else LF) and reuse it when serializing, so a CRLF `zelda3.ini` is not silently converted to LF (avoids whole-file git-diff churn on Windows).
2. **Line model:** split into records `{raw, section_id}`, tagging each line with its current section (track `[Section]` headers via `GetIniSection`; unknown sections → `-1`, never touched). A line is a comment iff its first non-space char is `#`. A managed `key=value` line may carry a **trailing `# comment`**, which is preserved (see step 3).
3. **Replace pass:** for each managed `(section,key,value)`, find the **last** non-comment `key=…` line across **all runs of that section** (matches the parser's last-wins). Overwrite it as `key = value`, **re-appending any trailing `# comment`** that was on the original line. Mark the entry emitted. Key compare is `StringEqualsNoCase` (parser-identical).
4. **Insert pass:** for each unemitted managed entry: if its section exists, insert `key = value` after the last record of that section's last run; else append `<eol>[Section]<eol>` + all that section's missing keys at EOF (one header per missing section).
5. **Serialize + atomic write:** reuse `Config_SaveRandoWindowIni`'s atomic block (`config.c:834-850`): `"<path>.tmp"` → `fflush` → Win32 `_commit(_fileno)`+`MoveFileExA(MOVEFILE_REPLACE_EXISTING)` / POSIX `fsync`+`rename`; `EnsureParentDir` first.

**Safety properties:** unowned sections (`[Randomizer]`, `[RandoAssetDecisions]`, future/unknown) are never matched/created → preserved verbatim. `!include` directives are not followed (we rewrite only the top-level file); if the loaded file contains any `!include`, the Controls/Video panels show: "This config uses !include; saved values are written to the main file and may be shadowed by included files." A **one-shot `<path>.bak`** of the original is written (overwrite-once-per-session) before the first successful rewrite, so a botched edit is recoverable.

---

## D3. UI layout, working copy, Apply path

Panels live in **`src/rando/rando_window/game_config_panels.cpp`** (new), exposing `GameConfig_RenderTab()` called from `rando_window.cpp`, plus capture accessors. Shared widget helpers (`EnumCombo`, `HelpTooltip`) move to **`game_config_widgets.h`** so both files use one implementation. Widget idioms copied from `rando_window.cpp` (combos `:124`, sliders `:273`, checkboxes `:325`, text input `:380`, tooltips `:142`, `SeparatorText`, `BeginDisabled`).

### Working copy
```c
static Config     s_cfg;                       // scalar working copy
static char       s_path_link[512], s_path_shader[512], s_path_msu[512], s_lang[32];
static uint16     s_kbd[kKeys_Total];
static PadBinding s_pad[kKeys_Total];
static int        s_aspect_enum;               // 0..3, or -1 = custom/non-editable
static bool       s_dirty;
static uint32     s_restart_mask;              // categories changed needing restart
```
`GameConfig_SyncFromLive()` copies `g_config` + model + path strings in (first open / `Revert` / after Apply). Path-field change detection is **`strcmp`-based** (value, not pointer) so a no-op Apply never re-interns/leaks.

### Panels
- **Controls (keyboard):** scrollable table, one row per command slot, grouped by `SeparatorText`/`TreeNode`: "Game controls" (12 `Controls`), "Save states" (`Load`/`Save`/`Replay`, 20 each, collapsed/lazy), "Reference replay" (advanced, collapsed), "System & window", "Cheats" (collapsed), "Randomizer" (tracker windows, reveal spoiler, **`OpenSettings`**). Row = label · binding text (`Config_DecodeKeyName`) · `[Rebind]` · `[Clear]`. A conflict badge marks any row duplicating another (live `s_kbd` scan).
- **Controller (gamepad):** same row model over the 12 `Controls` (+ any command the user binds). Row = label · `Config_PadBindingName` · `[Rebind]` (next button + held buttons = combo) · `[Clear]`. Note: "Hold modifier buttons while pressing the main button to bind a combo (e.g. L1+Start)." Note the one-binding-per-action limitation.
- **Video:** `WindowScale`(Slider 1–8, live), `Fullscreen`(combo Windowed/Desktop/Exclusive — Windowed↔Desktop live, Exclusive ⟳), `WindowSize`(Auto + W/H, ⟳), `OutputMethod`(combo, ⟳), `NewRenderer`(live), `EnhancedMode7`(⟳), `LinearFiltering`(⟳), `NoSpriteLimits`(live), `IgnoreAspectRatio`(⟳), `ExtendedAspectRatio` ratio combo + `extend_y`/`unchanged_sprites`/`no_visual_fixes`(⟳, custom-guard per §D2), `DimFlashes`(live), `Shader`(InputText, ⟳, disabled unless OutputMethod=OpenGL), `LinkGraphics`(InputText, ⟳), `DisplayPerfInTitle`(live).
- **Audio:** `EnableAudio`(⟳), `AudioFreq`(combo, ⟳), `AudioChannels`(combo, ⟳), `AudioSamples`(combo, ⟳), `EnableMSU`(combo, ⟳), `MSUPath`(⟳), `ResumeMSU`(live), `MSUVolume`(Slider 0–100, live).
- **Gameplay:** the user-facing `[Features]` bits as checkboxes with `HelpTooltip` from `features.h`, grouped Convenience / Bug fixes / Combat & items (all live). **The two geometry-derived bits (`ExtendScreen64`, `WidescreenVisualFixes`) are NOT exposed here** — they are driven only by the Video aspect combo and are restart-only; exposing them as raw live checkboxes would let a user shift sprite coords against a non-widened framebuffer. `[General]`: `Autosave`(live), `DisableFrameDelay`(live), `Language`(combo/InputText, ⟳).

### Bottom action row
`[Apply]`(disabled unless `s_dirty`), `[Revert]`, `[Reset section to defaults]`(active nested tab; confirm popup). Each ⟳ widget shows a "⟳" suffix; after an Apply that changed restart-only fields, a persistent yellow `TextWrapped` lists the affected categories. If `OpenSettings` is unbound, a hint shows how to reopen the window.

### Apply path (game-thread, via bridge)
`[Apply]` sets `g_rando_window_bridge.apply_config_requested = true` and snapshots the working copy into a bridge-carried staging area. The **game-frame consumer** (next to the generate-request consumer in `main.c`) runs, with the game GL context current:
1. Commit scalars `g_config = staged scalars`; for the 4 string fields, if `strcmp` differs, `g_config.field = Config_InternString(buf)` (process-lifetime arena in config.c; old pointer left in `memory_buffer`, never freed).
2. Commit bindings (`memcpy` into `g_keybind_*`), `Config_RebuildKeymap()`.
3. `Config_ApplyLive(&prev, &g_config)` (§D6) — runs the live window/renderer hooks here, in game-thread context.
4. `Config_WriteIniFile(Config_GetLoadedIniPath())`.
5. Signal the UI (a bridge status) to `GameConfig_SyncFromLive()` and clear `s_dirty`, set `s_restart_mask` from the returned bitmask.

Running on the game thread (not inside `RandoWindow_BeginFrame`) avoids touching `g_window`/renderer state while the settings GL context is current.

---

## D4. Interactive binding capture

```c
static int  s_capture_cmd = -1;    // expanded command id, or -1
static bool s_capture_pad = false; // false=keyboard, true=gamepad
static uint32 s_capture_deadline;  // SDL_GetTicks + 5000; 0 = none
```
`[Rebind]` arms capture and the row shows "Press a key…/button… (Esc cancels)". **Capture requires the settings window to have focus** (keydowns carry the focused window's `windowID`); the row text adds "click this window first" and capture auto-cancels on focus loss or the 5 s deadline so it can never wedge input.

- **Keyboard:** in `RandoWindow_ProcessEvent`, when `s_capture_cmd>=0 && !s_capture_pad` and the event is `SDL_KEYDOWN`: ignore pure modifier keys (L/R Shift/Ctrl/Alt) and `Escape` (=cancel); otherwise `v = Config_EncodeKeyEvent(e.key.keysym.sym, e.key.keysym.mod)` (the **event's** mod), apply to `s_kbd[s_capture_cmd]` via the resolver (§D5), exit capture, set `s_dirty`, and **consume only this keydown** (do not forward it to ImGui). All other events — modifier keydowns, every keyup (including the terminating key's keyup), text events — are forwarded to ImGui normally, so ImGui's internal key state never strands.
- **Gamepad:** controller events are global. Add `RandoWindow_IsCapturingGamepad()`; in `main.c`, before the controller-event game dispatch (`main.c:1331-1343`), if true and the event is `SDL_CONTROLLERBUTTONDOWN`, call `RandoWindow_CaptureGamepadButton(button)` and `continue`. Capture records that button as `PadBinding.button` and **snapshots the currently-held other buttons from `g_gamepad_modifiers`** as the combo `modifiers` (the same bitmask `FindCmdForGamepadButton` matches against), applies via the resolver, exits. Only the capturing **button-down** is intercepted: we never mutate `g_gamepad_modifiers`, so non-captured ups/downs keep it consistent; the captured button's later button-up reaches `HandleGamepadInput` with its bit clear and is correctly no-op'd (the press was "consumed" by rebinding). Buttons absent from `RemapSdlButton` (Guide/L2/R2/L3/R3) don't emit `SDL_CONTROLLERBUTTONDOWN` via the existing path and are not capturable in v1 (documented).

---

## D5. Conflict resolution (steal-with-warning)
The keymap hash forbids duplicates, so the model stays duplicate-free. When capture/Apply sets a binding equal to one held by a **different** command slot: scan the working array, clear the prior owner(s), and toast "Unbound <command> (was <key/button>)". This guarantees `Config_RebuildKeymap` never hits a rejection. Stealing a system key (e.g. `Tab` from `Turbo`) is allowed — the user is in control. `OpenSettings` can be rebound or stolen; if fully unbound, the window stays reachable via the rando kind-toggle and the UI shows a rebind hint.

---

## D6. Live-apply vs restart (`Config_ApplyLive`)
```c
uint32 Config_ApplyLive(const Config *prev, const Config *now); // applies live subset; returns restart-needed mask
```
Runs on the **game thread** (§D3). For window ops it calls small absolute hooks provided by `main.c` (NOT the relative/toggle paths):
- `MainHost_SetWindowScale(int scale)` — absolute (the existing `ChangeWindowScale` is relative and clamps to a screen-fit max; the hook reads `g_current_window_scale` and steps to the target, or is refactored to set absolutely; if the target exceeds the screen-fit max it clamps and reports so the UI can note it).
- `MainHost_SetFullscreen(uint8 mode)` — absolute set of windowed(0)/desktop(1); exclusive(2) is restart-only (not toggled live today; the existing `kKeys_Fullscreen` path is a blind XOR and must not be reused for an absolute set).
- `MainHost_SetNewRenderer(bool)` — set the `kPpuRenderFlags_NewRenderer` bit absolutely.

| Setting | Apply | Mechanism |
|---|---|---|
| keyboard/gamepad bindings | Live | `Config_RebuildKeymap()` |
| `features0` user-gameplay bits (the 12 non-geometry bits incl. `DimFlashes`) | Live* | `g_wanted_zelda_features = (g_wanted_zelda_features & GEOMETRY_MASK) \| (now->features0 & ~GEOMETRY_MASK)` — i.e. update only the live-safe bits; the two geometry bits keep their running value. Frame driver mirrors to `g_ram+0x64c`. |
| `ExtendScreen64`, `WidescreenVisualFixes` | Restart | framebuffer width fixed at init |
| `WindowScale` | Live | `MainHost_SetWindowScale` |
| `Fullscreen` Windowed↔Desktop | Live | `MainHost_SetFullscreen` |
| `Fullscreen` Exclusive | Restart | — |
| `NewRenderer` | Live | `MainHost_SetNewRenderer` |
| `NoSpriteLimits`, `DisplayPerfInTitle`, `DisableFrameDelay`, `Autosave`, `ResumeMSU` | Live | read live by loop/game; commit to `g_config` |
| `MSUVolume` | Live | `g_config.msuvolume` read by MSU player |
| `OutputMethod`, `LinearFiltering`, `EnhancedMode7`, `IgnoreAspectRatio`, `WindowSize`, `Shader`, `LinkGraphics` | Restart | renderer/asset chosen at init |
| Audio: `EnableAudio`, `AudioFreq`, `AudioChannels`, `AudioSamples`, `EnableMSU`, `MSUPath` | Restart | SDL audio device opened once |
| `Language` | Restart | strings/assets at init |

*\*Live-apply of `features0` and the keymap rebuild is **gated off while `state_recorder.replay_mode` is true** (the feature mirror doesn't run during replay, and changing input mapping mid-replay is meaningless): `Config_ApplyLive` skips the live feature/keymap step during replay, still writes the INI, and reports a "applied on next non-replay frame / restart" notice. (Persistence is always immediate; only the live step is deferred.)*

`GEOMETRY_MASK = kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes`. We deliberately do **not** re-open the SDL audio device or recreate the renderer live in v1 — highest-risk reinit, smallest win; honest "restart required."

---

## D7. Opening the window for configuration
- Add `kKeys_OpenSettings` **immediately before `kKeys_Total`** (`config.h`); add `S(OpenSettings)` to `kKeyNameId` (existence is what matters — order is irrelevant per Ground Truth); append its default to the `kDefaultKbdControls` initializer (`_(SDLK_BACKQUOTE)`) as the last element. No gamepad default.
- Add `_Static_assert(countof(kDefaultKbdControls) == kKeys_Total, …)` (none exists today) — the real guard against table desync. (`kDefaultGamepadCmds` is a 12-entry table, NOT `kKeys_Total`-sized; this assert does not apply to it.)
- `HandleCommand_Locked` (`main.c:1720`) gains an unconditional `case kKeys_OpenSettings:` with a `#ifdef Z3R_NATIVE_SETTINGS_WINDOW` body calling `RandoWindow_ToggleConfig()` and a `break` outside the `#ifdef` (the `RandoItemTrackerWindow` pattern → Switch falls to `break`, not `default: assert(0)`).
- `RandoWindow_ToggleConfig()`: if hidden, show+raise **without** setting `target_slot_index` (stays −1) or rolling a seed, default the top-level tab to "Game Settings"; if shown, hide. `RenderGenerateRow` is hidden when `target_slot_index < 0` (a UX choice — it already self-disables, so this is presentation, not a safety fix). The rando tabs still render (browsable); no slot-dependent path asserts because none runs without an explicit generate request.

---

## D8. Correctness self-checks (debug / `--rando-selftest` only)
- `Config_SelfCheckKeymap()`: snapshot **lookup results** for a representative key/button set from the parse-time hash, call `Config_RebuildKeymap()`, assert identical lookup results (functional equivalence — NOT raw-byte, since chaining order legitimately differs).
- Writer round-trip: `Config_WriteIniFile` to a temp path, re-parse into a scratch `Config` with a fresh parser instance, assert managed scalars + all bindings match. Guards the reverse-name/encode helpers and the writer. Not in release frame code.

---

## D9. Files & build
- **New:** `src/rando/rando_window/game_config_panels.cpp`, `game_config_widgets.h`.
- **Modified:** `src/config.{c,h}` (model, rebuild, writer, encode/decode/name, loaded-path, intern, apply-live, self-check), `src/rando/rando_window/rando_window.{cpp,h}` (Game Settings tab, `ToggleConfig`, generate-row gate, capture hooks, `apply_config_requested` plumbing), `src/rando/rando_window/rando_window_bridge.{c,h}` (apply-config request + staging), `src/main.c` (`kKeys_OpenSettings` case, gamepad-capture interception, apply-config consumer, `MainHost_Set*` hooks), `README.md`.
- **Makefile:** add the `.cpp` to `IMGUI_SRCS`. **`.vcxproj`:** add as C++17, `TreatWarningAsError=false`. **Switch:** unchanged (subdir not globbed; `kKeys_OpenSettings` body `#ifdef`-guarded). New C helpers are ImGui-free → Switch-safe and `-Werror`-clean.

---

## D10. Risks & mitigations
- **Parse refactor altering startup bindings** → keep inline hash build; model populated alongside; §D8 asserts rebuild-equivalence. Verify: stock INI, all default keys work before any UI use.
- **`Config_WriteIniFile` clobbering user content** (project's cardinal sin) → in-place preserves comments/order/line-endings/unknown sections; `#`-only comment grammar honored; trailing comments preserved; `!include` files never edited (+ UI warning); refuse values containing `#`; one-shot `.bak`; atomic write. **Round-trip test mandatory before ship.**
- **String ownership** → `Config_InternString` arena, `strcmp`-gated; `memory_buffer` never mutated. Bounded, documented small leak of replaced strings.
- **Live feature flip during replay / RAM-compare** → gated off during `replay_mode`; while recording, the existing patch-log path keeps the emulated side in sync (same as startup). Per-bit playtest required (§D11).
- **Gamepad capture input desync** → only the capturing button-down intercepted; `g_gamepad_modifiers` never mutated; later button-up self-no-ops; 5 s timeout + focus-loss cancel.
- **Apply mid-render context corruption** → Apply runs on the game thread via bridge consume, game GL context current — no window/renderer mutation while the settings context is current.
- **Table desync from new command** → `_Static_assert(countof(kDefaultKbdControls)==kKeys_Total)` + §D8 self-check.
- **Switch breakage** → new file under `rando_window/` (not globbed); only guarded window calls / pure-C helpers added to scanned files.
- **Audit guard** → `config.c`/`main.c` ARE scanned; the new helpers touch no tracked cell, so they pass (the guard is not bypassed). The `.cpp` panels are unscanned and write no `g_ram`.

## D11. Test plan
1. Build PC (MSBuild + `make`) and a Switch-config compile clean under `-Werror` (C side).
2. **Default-binding regression:** stock INI, exercise every default key/gamepad binding *before* opening the UI.
3. **Live rebind:** rebind a direction, Apply, works immediately, old key dead, conflict toast on steal.
4. **Gamepad rebind + L1-combo:** verify in-game.
5. **features0 live (per-bit playtest):** individually toggle each of the 12 live bits in actual gameplay and confirm no state/save corruption — explicitly incl. `MoreActiveBombs`, `CarryMoreRupees`, `MirrorToDarkworld`, `GameChangingBugFixes`, `CollectItemsWithSword`, `BreakPotsWithSword`, plus the display/economy bits. Any bit that misbehaves live is demoted to restart-only.
6. **Replay gate:** start a snapshot replay, change a feature → confirm it does NOT apply mid-replay, applies after, INI still written.
7. **Restart-only fields:** change `AudioFreq`+`OutputMethod`, Apply → ⟳ notice, INI updated, effect after relaunch.
8. **INI fidelity:** from a `zelda3.ini` with `#` comments (incl. a trailing comment on a managed key), `!include`, an unknown `[Custom]` section, CRLF endings, and `[Randomizer]`/`[RandoAssetDecisions]` content → Apply one change → diff: only the managed key changed, line endings preserved, trailing comment kept, `.bak` created.
9. **Round-trip self-check** green in `--rando-selftest`.
10. **user.ini precedence:** with `zelda3.user.ini` present, writer targets it.
11. **No-file bootstrap:** delete both INIs, defaults, Apply → fresh `zelda3.ini` created, re-parses.
12. **Rando untouched:** generate a seed after using config tabs; corpus/selftest unaffected.

## D12. Out of scope (v1)
Live audio-device re-open / live renderer recreation; editing `!include`d files; gamepad axis/deadzone config; non-`RemapSdlButton` buttons (Guide/L2/R2/L3/R3) capture; multiple bindings per action in the editable model; config presets/import-export; theming beyond the dark/light toggle.

## D15. Implementation notes (as-built deviations)
- **Apply plumbing:** rather than adding fields to `RandoWindowBridge`, the apply request is exposed as `GameConfig_HasPendingApply()` / `GameConfig_ApplyPending()` (C-linkage, in `game_config_panels.cpp`), consumed by the game-frame loop in `main.c` before the generate consumer. Behaviorally identical to the §D3 bridge design (game-thread commit), with less coupling.
- **Reset button:** the "Reset section to defaults" control was dropped from v1. The only safe way to repopulate defaults was re-`ParseConfigFile`, which re-points `memory_buffer` and resets rando defaults mid-run — too risky. **Revert** (reload from live config) ships instead.
- **Self-checks (§D8) implemented:** `Config_SelfCheckKeymap` (parse-time vs rebuilt-hash lookup equivalence) and `Config_SelfCheckIniWriter` (writer round-trip: managed key updates while comments / unknown / `[Randomizer]` survive), both wired into `--rando-selftest`. The keymap check caught a real ordering bug during development (the selftest path runs before `ParseConfigFile`, so the check now parses defaults first).
- **Aspect-ratio order-dependence:** `MatchAspect` matches both h=224 and h=240 candidate values, and the writer emits `extend_y` before the ratio token so the order-dependent parser reproduces the committed `extended_aspect_ratio` byte exactly (incl. the 4:3+extend_y edge).
- **Verification done:** clean MSVC build; `--rando-selftest` (incl. both new checks) exit 0; audit guard / determinism / codegen-wiring guards pass; 55-entry placement corpus green (determinism unaffected). End-to-end playtest of live rebinding and per-bit `features0` toggles (§D11.5) remains the user's loop — the automated nets do not cover gameplay/consume-site behavior.

## D14. Clarifications (fresh-eyes round 2)
- **Bridge consume order.** When both `generate_requested` and `apply_config_requested` are set in one frame, the game-frame consumer processes **apply first, then generate** (so a just-applied `features0`/seed is reflected if a generate immediately follows). They touch disjoint bridge fields (`pending`/`seed_u64` vs the config staging area) so the order is for determinism, not safety.
- **Window-scale persistence = achieved value.** If `MainHost_SetWindowScale` clamps the requested scale to a screen-fit max, Apply commits and persists the **achieved (clamped)** value to `g_config`/INI (and the slider snaps to it), so a later launch never re-clamps a value it can't honor.
- **`g_config` mirrors persisted (post-restart) intent.** After Apply, `g_config.features0` (and the aspect/geometry bits) hold what was *persisted* — i.e. the post-restart configuration. The *running* geometry bits are what `g_wanted_zelda_features` keeps via `GEOMETRY_MASK`. This is intentional: live reads go through `enhanced_features0` (still the masked running value), and a subsequent live Gameplay Apply recomputes `(running & MASK) | (g_config & ~MASK)`, so the running geometry bits are never disturbed. `g_config` describing post-restart state is the correct meaning of "what the INI says."
- **`[Features]` renderer omits the geometry bits.** The managed `[Features]` table contains only the 14 keys the parser exposes (`config.c:553-580` + `DimFlashes` in `[Graphics]`); `ExtendScreen64`/`WidescreenVisualFixes` have **no INI key** and are persisted solely via the composite `ExtendedAspectRatio`, so the writer can never emit an inconsistent raw bit.
- **`Config_GetLoadedIniPath` during replay.** It returns the path resolved by `ParseConfigFile` at startup, which is independent of any later snapshot-replay session, so the INI write target is never stale during replay.
- **Bootstrap line ending.** A brand-new file (no existing `\n` to sample) defaults to LF; harmless (the parser tolerates both) and noted so a Windows user isn't surprised by an LF `zelda3.ini` on first creation.

## D13. Implementation slicing (natural cut lines)
Independently shippable, in order — each builds + self-checks green on its own:
1. **INI writer + loaded-path + restart-only persistence** (the deferred `add-rando-native-settings-window` §D9 primitive). Lowest risk, high standalone value: panels can write every setting and apply on restart.
2. **Editable keybind model + `Config_RebuildKeymap` + live rebinding + capture.** The headline UX.
3. **Live-apply of `features0` + window hooks** (replay-gated, geometry-masked, per-bit playtested).
If schedule pressure appears, Slice 3 can defer to a follow-up without blocking 1–2; the design keeps them decoupled.
