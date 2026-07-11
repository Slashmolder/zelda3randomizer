# randomizer-ui Specification

## Purpose
TBD - created by archiving change add-randomizer-support. Update Purpose after archive.
## Requirements
### Requirement: Text-input infrastructure

On Switch builds, the host SHALL provide a single-line text-input widget (`RandoTextField`) sourcing characters from libnx `swkbd`. The widget SHALL support cursor positioning, backspace, paste from clipboard, and a constrained character set (the base32 alphabet plus separators).

On PC builds (Windows, Linux, macOS) where `Z3R_NATIVE_SETTINGS_WINDOW` is defined, this requirement does NOT apply: text input occurs in the native settings window via OS-standard ImGui text widgets (see `randomizer-native-window / Second OS window for randomizer settings on PC`), and `RandoTextField` is excluded from the build.

#### Scenario: Switch swkbd is invoked

- **WHEN** the settings screen is active on the Switch build and the player activates the share-string field
- **THEN** libnx `swkbd` appears and its result is routed into the widget buffer

#### Scenario: Switch swkbd dismissed without confirming

- **WHEN** the player dismisses libnx `swkbd` without confirming (e.g., pressing B / Cancel)
- **THEN** the widget buffer is left untouched (no partial write), focus returns to the on-screen alphabet picker, and the settings hash is unchanged

#### Scenario: Switch paste from clipboard

- **WHEN** the player triggers the Switch paste shortcut and the clipboard contains a valid share string
- **THEN** the buffer is replaced with the clipboard content and the live settings hash updates

#### Scenario: PC build excludes RandoTextField

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** `rando_textfield.{c,h}` is not compiled into the binary and no in-game text-input widget exists in the build

### Requirement: On-screen alphabet picker

On Switch builds, the settings screen SHALL provide an on-screen alphabet picker as a controller-friendly alternative to keyboard typing. The picker SHALL be the default focus on the Switch undocked build.

On PC builds where `Z3R_NATIVE_SETTINGS_WINDOW` is defined, this requirement does NOT apply: text entry occurs in the native settings window with OS keyboard input, and the on-screen alphabet picker is excluded from the build.

#### Scenario: Switch picker operable with d-pad and two buttons

- **WHEN** the picker is focused on a Switch build
- **THEN** d-pad navigation moves the cursor, A confirms a character, B deletes the last character

#### Scenario: Default focus on Switch handheld

- **WHEN** the settings screen opens on the Switch build in handheld mode
- **THEN** the picker is the focused widget

#### Scenario: PC build has no on-screen alphabet picker

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** no on-screen alphabet picker is reachable from any in-game UI path because the settings screen is excluded from the build

### Requirement: Three-slot file-select with kind-toggle (no 4th entry)

The file-select screen SHALL retain the existing 3-slot geometry (`kSelectFile_Draw_Y[3]`, OAM offsets, SRAM stride). It SHALL NOT add a 4th file-select entry. Each slot SHALL render according to its sidecar `slot_kind`:

- Empty → vanilla "NEW GAME" prompt; on confirm, the system asks the player to choose vanilla / new randomizer / load share string.
- Vanilla → existing vanilla banner (name, sword/shield/heart icons).
- Randomizer → rando banner (truncated 12-char share string, world-state abbrev, goal abbrev, "R" badge).

#### Scenario: Empty slot prompts for kind on new game
- **WHEN** the player selects an empty slot and chooses "NEW GAME"
- **THEN** a sub-prompt offers Vanilla / New Randomizer / Load Share String; on Vanilla, the existing new-game flow runs; on New Randomizer, the settings screen opens; on Load Share String, a share-string entry field opens

#### Scenario: Existing slot loads its kind
- **WHEN** the player selects an existing slot
- **THEN** the system loads it as vanilla or randomizer per the sidecar's `slot_kind` for that slot

#### Scenario: Copy refuses cross-kind
- **WHEN** the player attempts to copy a randomizer slot onto a vanilla slot (or vice versa)
- **THEN** the operation is refused with a clear error explaining the kind mismatch

#### Scenario: Erase resets kind to empty
- **WHEN** the player erases a slot
- **THEN** the slot's sidecar entry is removed (or marked empty), the `sram.dat` slot is cleared per vanilla behavior, and the slot displays "NEW GAME" on the next render

### Requirement: Settings screen

On Switch builds, the in-game settings screen SHALL allow the player to select world-state, goal, item-pool difficulty, dungeon-item mode, Triforce-Hunt piece counts (when goal is Triforce Hunt), trap frequency, and optional shuffle modules; SHALL accept seed entry via random generation or share-string paste; and SHALL display the current settings hash live.

On PC builds where `Z3R_NATIVE_SETTINGS_WINDOW` is defined, this requirement does NOT apply: the equivalent functionality is provided by the native settings window per `randomizer-native-window`, and the in-game settings screen module is excluded from the build. The kind-toggle call sites at `src/select_file.c:1664` and `:1891` (today invoking the file-static `SelectFile_Settings_Activate`) instead call `RandoWindow_OpenForNewSlot` under the same guard.

#### Scenario: Switch preset application

- **WHEN** the player selects a built-in preset on the Switch settings screen
- **THEN** all individual settings update and the settings hash refreshes

#### Scenario: Switch share-string paste populates fields

- **WHEN** the player pastes a valid share string on the Switch settings screen
- **THEN** the settings panel updates to reflect the decoded settings and the seed field shows the decoded `seed_u64`

#### Scenario: Switch invalid share string surfaces inline error

- **WHEN** the player pastes a malformed or alttpr.com-format share string on the Switch settings screen
- **THEN** an inline error appears and generation is blocked until the input is corrected or cleared

#### Scenario: Switch non-vanilla asset data triggers a one-time dialog per hash

- **WHEN** the player attempts to start generation on Switch while `g_assets_hash != kVanillaAssetsHash` and no persisted decision exists for this hash
- **THEN** the Switch settings screen displays a dialog explaining the risk with three choices (Always allow / Allow once / Cancel); the chosen action is honored and "Always allow" persists keyed by hash

#### Scenario: PC build excludes the in-game settings screen

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** no in-game settings-screen tile rendering, navigation, or input handling is compiled into the binary; the call sites at `src/select_file.c:1664` and `:1891` invoke `RandoWindow_OpenForNewSlot` instead of `SelectFile_Settings_Activate` and the in-game module is never reached

#### Scenario: PC build preserves Rando_RegisterAssetDecisionFromIni

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** the externally-visible function `Rando_RegisterAssetDecisionFromIni` (`src/select_file.c:2279`, called from `src/config.c:544`) remains compiled into the binary because the INI parser depends on it; it is NOT inside the compile-out region of the in-game settings screen

#### Scenario: PC build preserves SelectFile_Settings_Deactivate as a callable no-op or stub

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** `SelectFile_Settings_Deactivate` (called from `src/select_file.c:450, :3063, :3441` for file-select state reset, unrelated to opening the settings screen) remains callable — either as an empty stub under the guard or by leaving the function definition outside the compile-out region; removing it produces unresolved-reference link errors

### Requirement: 5-icon visual hash on slot banner and pause menu

Randomizer slots SHALL display a 5-icon visual hash derived deterministically from **`SHA-256(share_string_binary)`** — that is, the full seed identity (settings + seed_u64 + magic + checksum), NOT from `settings_hash` alone. Deriving from `settings_hash` would produce identical icons for every seed sharing settings, defeating the entire purpose of at-a-glance seed verification.

Icons SHALL be selected by computing `index_i = SHA-256(share_string_binary)[i] mod N` for `i ∈ {0..4}`, where `N = |kHashIconAtlas|` is pinned by the atlas YAML in `assets/rando/icon_atlas.yaml`. The atlas is **not** required to be 32 entries — its size is whatever the curated icon list yields. Bumping the atlas (adding/removing entries) changes the icon hash output for every existing share string and therefore advances `generator_version`.

The 5-icon strip SHALL render on the file-select slot banner AND on the in-game pause menu HUD.

#### Scenario: Icon hash distinguishes seeds with identical settings
- **WHEN** two players have identical settings but different `seed_u64`
- **THEN** their 5-icon hashes are different (because the input to the hash is the full share_string, which includes `seed_u64`)

#### Scenario: Icon hash matches for identical share strings
- **WHEN** two players have the same `share_string` (settings + seed_u64 match)
- **THEN** their 5-icon hashes are byte-identical OAM tile entries in the same order

#### Scenario: Pause-menu shows the same hash as the banner
- **WHEN** the player opens the in-game pause menu on a rando slot
- **THEN** the same 5-icon hash that appeared on the file-select banner appears on the pause menu HUD

#### Scenario: Atlas size is registry-pinned
- **WHEN** the atlas YAML defines N icon entries
- **THEN** the icon index for each of the 5 positions is computed as `SHA-256(share_string_binary)[i] mod N` for `N` whatever the YAML defines; no code path assumes `N == 32`

### Requirement: Slot banner with truncation

Randomizer slot banners SHALL contain the 5-icon visual hash, the truncated share string (first 12 base32 chars), world-state abbreviation, and goal abbreviation. A `!` marker SHALL appear on the banner when the slot was generated under forward-fill fallback (per `randomizer-core / Forward-fill fallback is surfaced prominently`). The banner SHALL fit within the slot's existing name-tile region without overflowing into adjacent OAM entries.

#### Scenario: Rando banner fits
- **WHEN** a randomizer slot renders
- **THEN** its banner fits in the OAM tiles previously used for the vanilla name plus a small "R" badge, with no overflow

#### Scenario: Vanilla slot unchanged
- **WHEN** a vanilla slot renders
- **THEN** its styling and content are identical to the pre-change build

### Requirement: Recommended-features opt-in panel

The settings screen SHALL include a "Recommended for randomizer" panel listing the **exact** Phase A `kFeatures0_*` toggles (no "etc." — the set is pinned to prevent scope creep and per-build divergence). Phase A panel set:

- `kFeatures0_SkipIntroOnKeypress` → recommended on
- `kFeatures0_ShowMaxItemsInYellow` → recommended on
- `kFeatures0_TurnWhileDashing` → recommended on
- `kFeatures0_CollectItemsWithSword` → recommended on
- `kFeatures0_BreakPotsWithSword` → recommended on
- `kFeatures0_DisableLowHealthBeep` → recommended on (off by default; on per racer convention)
- `kFeatures0_CarryMoreRupees` → recommended on
- `kFeatures0_MiscBugFixes` → recommended on
- `kFeatures0_GameChangingBugFixes` → recommended off (changes game behavior intentionally vs. vanilla; not the default rando expectation)
- `kFeatures0_DimFlashes` → recommended off (accessibility option; honors user preference)

Each toggle SHALL initialize to the user's *current* `zelda3.ini` value. The panel SHALL provide an "Apply recommendations" action that flips all toggles to the recommended values for this slot creation; the player opts in explicitly. Starting a rando slot SHALL NOT change the user's `kFeatures0_*` settings without an explicit toggle.

Adding or removing toggles from this list is **NOT** a `generator_version` bump trigger — the recommended set affects player experience but not placement determinism.

#### Scenario: Settings preserved without explicit opt-in
- **WHEN** the player creates a rando slot without touching the recommended panel
- **THEN** their `kFeatures0_*` settings are unchanged from the pre-rando state

#### Scenario: Apply-recommendations flips all to recommended
- **WHEN** the player clicks "Apply recommendations"
- **THEN** every toggle in the panel updates to the recommended value, and the live preview reflects the new settings before generation begins

### Requirement: Optional in-game item tracker (Phase B)

A toggleable item-grid overlay SHALL render the standard randomizer-tracker layout. The overlay SHALL re-render only when `reachability_state_counter` (bumped by the dispatcher and by audit-exempt direct write sites) differs from the last-rendered value; otherwise per-frame cost is limited to an OAM copy of the cached state.

The overlay layout SHALL be a fixed grid (rows × cols) anchored to a configurable screen corner via `[randomizer] tracker_anchor` in `zelda3.ini` (default: top-left). Tile cells display each item's "have / don't have / progressive level" state using the same HUD tiles used in vanilla. The grid SHALL NOT overlap text dialogue boxes when active — when a text box is open, the overlay SHALL hide for the duration of the box.

The overlay SHALL render on all five Phase A renderer backends (SDL software, SDL hardware-accelerated, OpenGL, OpenGL ES, Switch) with the same visible result. The OAM-cache invalidation rule is uniform across backends: invalidate exactly when `reachability_state_counter` advances.

#### Scenario: Toggle binding
- **WHEN** the player presses the configured tracker-toggle binding (`kKeys_RandoToggleItemTracker`)
- **THEN** the item tracker overlay appears or disappears without pausing gameplay; the toggle takes effect on the next frame, not the current one

#### Scenario: Re-render gated on inventory-change counter
- **WHEN** `reachability_state_counter` is unchanged between two consecutive frames
- **THEN** the tracker's draw path uses cached OAM data and does not recompute layout — per-frame cost is bounded to a fixed OAM-copy size irrespective of inventory complexity

#### Scenario: Dialogue box suppresses overlay
- **WHEN** the overlay is enabled and a text dialogue box opens (any vanilla text-box state)
- **THEN** the overlay is hidden until the dialogue closes; cached state is preserved (re-shown without recompute when the dialogue ends)

#### Scenario: Backend parity
- **WHEN** the same seed is loaded with the SDL-software backend and then with the OpenGL backend
- **THEN** the overlay's pixel composition is the same (modulo backend-native filter / scaling artifacts that affect the rest of the rendered frame uniformly)

#### Scenario: Switch backend renders the overlay
- **WHEN** the Switch build is running and the tracker is toggled on
- **THEN** the overlay renders at the same anchor as on desktop; per-frame cost on Switch hardware **targets** below 1ms under `Logic_ComputeReachability` benchmark conditions (target only; not CI-enforced because Switch is a manual-gated platform per `add-randomizer-support / tasks.md §12.3a`)

### Requirement: Optional in-game location tracker (Phase B)

A toggleable location-list overlay SHALL display locations grouped by region with reachability-colored entries (reachable / unreachable / checked). Reachability SHALL be recomputed only when `reachability_state_counter` increases.

The overlay SHALL show locations grouped by region name using the same region taxonomy as the spoiler text grouping (per `randomizer-core / Spoiler log emission` text-spoiler region grouping). Each location entry includes the location name and a 1-character status glyph: `?` unreachable, `.` reachable not yet checked, `*` checked. **Color encoding**: status uses both color and glyph so colorblind players still distinguish states.

The overlay SHALL render on all five Phase A renderer backends. **Text rendering** uses the existing in-game text engine (no new font infrastructure).

The "checked" state SHALL be sourced from the checked-location bitmap (see `randomizer-save / Checked-location bitmap write path`). Bitmap updates SHALL be reflected in the overlay on the next reachability recompute, not on a per-frame timer.

#### Scenario: Reachability updates on inventory change
- **WHEN** the player acquires an item that unlocks a previously unreachable region
- **THEN** all locations in that region transition to the reachable color within one frame after the inventory hash changes

#### Scenario: Checked location persists across save/load
- **WHEN** the player checks a location, saves, and reloads
- **THEN** the location remains marked as checked because the checked-location bitmap is persisted via the sidecar slot

#### Scenario: Colorblind-friendly status glyph
- **WHEN** a player without color-discrimination plays
- **THEN** the status glyph (`?`/`.`/`*`) provides the same information as the color encoding

#### Scenario: Region grouping mirrors spoiler text format
- **WHEN** the location tracker renders
- **THEN** the region names and ordering match the text-spoiler region grouping byte-for-byte

#### Scenario: Toggle binding distinct from item tracker
- **WHEN** the player presses `kKeys_RandoToggleLocationTracker`
- **THEN** the location overlay toggles independently of the item overlay; both can be on simultaneously

### Requirement: Cosmetic settings surface (client config)

The cosmetic-shuffle axes SHALL be configured through client-local `zelda3.ini` keys, parsed by `config.c`, and SHALL NOT appear in the playable-slot settings (they are presentation-only and never participate in generation, the `share_string`, or the `settings_hash`). This requirement is ADDED (not a modification of the existing rando settings-screen requirements) precisely because cosmetics are a separate client surface from the generation-driving settings UI.

The keys SHALL be:

- `[Graphics] CosmeticSeed` — unsigned 64-bit; `0` (default) resolves to the active slot's `seed_u64`.
- `[Graphics] PaletteShuffle` — `vanilla` (default) / `shuffled` / `grayscale` / `negative`.
- `[Graphics] SpriteShuffle` — `off` (default) or a folder path of `.zspr` files.
- `[Sound] MusicShuffle` — `off` (default) / `on`.

Unknown or hand-edited values in these keys SHALL NOT break INI round-trip (consistent with the existing foreign-section handling in `config.c`). A PC build MAY additionally surface these in the native settings window, but the INI keys are the normative source.

#### Scenario: Cosmetic keys parse and round-trip
- **WHEN** `zelda3.ini` sets `PaletteShuffle = grayscale` and `CosmeticSeed = 12345`
- **THEN** the values are read at startup and a write-back of the config preserves them without corrupting other sections

#### Scenario: Defaults are vanilla-safe
- **WHEN** none of the cosmetic keys are present in `zelda3.ini`
- **THEN** all axes default off / vanilla and rendering + audio match the unmodified game

#### Scenario: Cosmetic keys never enter the slot
- **WHEN** a playable slot is generated while cosmetic keys are set
- **THEN** the written sidecar slot and `share_string` are byte-identical to a generation with cosmetic keys unset

### Requirement: Boss-heart-container shuffle is not a UI axis

The settings UI SHALL NOT expose `region_boss_hearts_in_pool`. The legacy
canonical byte / CSV keys remain accepted for compatibility, but generation
canonicalizes the value to `0`: boss-heart drops are always shuffled and the
item-pool difficulty's boss-heart-container count always enters the item pool
(10 Easy/Normal, 6 Hard, 2 Expert).

#### Scenario: No boss-heart shuffle checkbox

- **WHEN** the settings UI is displayed
- **THEN** no control or text shows "Shuffle boss heart containers" or the raw
  `region_boss_hearts_in_pool` field name

### Requirement: Accessibility tier selection in the native settings window

The PC native (Dear ImGui) settings window SHALL expose all three accessibility
tiers as a single selector whose option order matches the enum values:

- index 0 → `items` (`kAccessibility_Items`)
- index 1 → `locations` (`kAccessibility_Locations`)
- index 2 → **`beatable only`** (`kAccessibility_None`)

The selector SHALL present a help affordance describing that all three tiers
guarantee a beatable seed and differ only in the extra reachability enforced
(`items` = every progression item; `locations` = every location; `beatable only`
= goal only).

When the goal is Completionist, the selector SHALL be forced to `locations` and
shown read-only (the existing Completionist lock); leaving Completionist SHALL
restore the user's previously-selected tier, including `beatable only`.

#### Scenario: Beatable-only is selectable on PC

- **WHEN** the goal is not Completionist
- **THEN** the accessibility selector offers `items`, `locations`, and
  `beatable only`, and selecting `beatable only` sets `accessibility` to
  `kAccessibility_None`

#### Scenario: Completionist locks the selector to locations

- **WHEN** the user selects goal = Completionist
- **THEN** the accessibility selector is forced to `locations` and is read-only

#### Scenario: Leaving Completionist restores the prior tier

- **WHEN** the user had `beatable only` selected, switched the goal to
  Completionist (forcing `locations`), then switched the goal back
- **THEN** the selector returns to `beatable only`

### Requirement: World-state picker accepts Inverted

The settings-screen world-state field SHALL accept `Inverted` as a selectable value, completing the world-state-axis picker (alongside Open and Standard from Phase A). Phase A pinned the picker to Open + Standard via the `select_file.c:2520` re-scope gate per `tasks.md §14.1b`; this change un-gates Inverted at that site.

This is an ADDED Requirement (not a modification of the Phase A "Settings screen" Requirement) so the Phase A scenarios (Preset application, Share-string paste, Invalid share string, Non-vanilla asset data dialog) are preserved verbatim and the un-gate doesn't conflict with the parallel Phase B `add-rando-retro-world-state` change.

> **Stub status**: implementation-side gate-removal detail (precise edit at `select_file.c:2520`) deferred to apply-time.

#### Scenario: World-state row cycles to Inverted
- **WHEN** the player cycles the world-state row left/right after this change has archived (and the Retro un-gate has not yet archived)
- **THEN** the field sequence is `Open → Standard → Inverted → Open`; cycling past Inverted wraps to Open (Retro is still gated until `add-rando-retro-world-state` archives)

#### Scenario: Inverted seeds generate cleanly
- **WHEN** the player selects Inverted and clicks Generate
- **THEN** generation runs against the Inverted region graph (see `randomizer-logic / Inverted world-state region graph`); the resulting `settings_hash` differs from an otherwise-identical Standard or Open seed

#### Scenario: Phase A "Settings screen" scenarios preserved
- **WHEN** any Phase A "Settings screen" Requirement scenario is exercised against an Inverted seed (preset application, share-string paste, invalid share string, non-vanilla assets dialog)
- **THEN** behavior matches the Phase A scenario verbatim — the Inverted un-gate adds capability without altering existing scenarios

### Requirement: World-state picker accepts Retro

The settings-screen world-state field SHALL accept `Retro` as a selectable value, completing the world-state-axis picker (alongside Open and Standard from Phase A). Phase A pinned the picker to Open + Standard via the `select_file.c:2520` re-scope gate per `tasks.md §14.1b`; this change un-gates Retro at that site.

This is an ADDED Requirement (not a modification of the Phase A "Settings screen" Requirement) so the Phase A scenarios (Preset application, Share-string paste, Invalid share string, Non-vanilla asset data dialog) are preserved verbatim and the un-gate doesn't conflict with the parallel Phase B `add-rando-inverted-world-state` change.

> **Stub status**: implementation-side gate-removal detail (precise edit at `select_file.c:2520`) deferred to apply-time.

#### Scenario: World-state row cycles to Retro
- **WHEN** the player cycles the world-state row left/right after this change has archived (and the Inverted un-gate has not yet archived)
- **THEN** the field sequence is `Open → Standard → Retro → Open`; cycling past Retro wraps to Open (Inverted is still gated until `add-rando-inverted-world-state` archives)

#### Scenario: Both un-gates archived
- **WHEN** both this change and `add-rando-inverted-world-state` have archived
- **THEN** the field sequence is the full Phase A axis: `Open → Standard → Inverted → Retro → Open`; cycle order matches `mode_state` enum ordering from `randomizer-core` settings-canonical-serialization (item #1)

#### Scenario: Retro seeds generate cleanly
- **WHEN** the player selects Retro and clicks Generate
- **THEN** generation runs against the Retro item pool (see `randomizer-core / Retro world-state item pool`) which inherits Open's region graph + adds shop locations; the resulting `settings_hash` differs from an otherwise-identical Open seed

#### Scenario: Phase A "Settings screen" scenarios preserved
- **WHEN** any Phase A "Settings screen" Requirement scenario is exercised against a Retro seed
- **THEN** behavior matches the Phase A scenario verbatim — the Retro un-gate adds capability without altering existing scenarios

### Requirement: Race-mode settings-screen toggle

The settings screen SHALL expose a user-toggleable Race-mode field. The toggle SHALL default to off. When race mode is off, the standard spoiler-emission contract applies; when on, the on-disk spoiler is suppressed per `randomizer-save` / `randomizer-core`.

Toggling race mode SHALL be permitted at any time before generation. After generation, the slot's `race_mode` is fixed by the stored `settings_hash`; the toggle on the settings screen has no effect on already-generated slots.

> **Carved scope**: the settings-screen *preview warning string* shown when race
> mode is enabled, and the file-select per-slot "Reveal Spoiler" action menu,
> were deferred to follow-up change `add-rando-race-mode-reveal-ui`. The reveal
> *action* itself shipped in this change and is reachable via the CLI
> `--reveal-spoiler` and the `RandoRevealSpoiler` keybind.

#### Scenario: Toggle default state
- **WHEN** the settings screen is opened for a fresh slot
- **THEN** the Race-mode field is unchecked (off)

#### Scenario: Toggle has no effect on existing slots
- **WHEN** the user toggles Race-mode on an already-generated slot from the file-select kind-edit menu
- **THEN** the toggle is non-interactive (display only); the existing slot's `race_mode` is whatever was stored in the slot's `settings_hash` at generation time

### Requirement: Tracker keybinding configuration

The randomizer SHALL declare two new key-binding command IDs in `src/config.c`'s `kKeys_*` enumeration: `kKeys_RandoToggleItemTracker` and `kKeys_RandoToggleLocationTracker`. Both default to unbound (the user must explicitly bind them in `zelda3.ini`). The `[KeyMap]` section of `zelda3.ini` SHALL accept these names; `README.md` SHALL document them in the existing key-binding table.

Per-slot tracker visibility state SHALL be in-memory only — trackers SHALL come back hidden on each fresh game launch. Persistence to sidecar is explicitly out of scope; player can re-toggle on each session.

#### Scenario: Binding loads from zelda3.ini
- **WHEN** the user adds `RandoToggleItemTracker = T` to the `[KeyMap]` section of `zelda3.ini`
- **THEN** pressing `T` toggles the item tracker; the binding survives across launches

#### Scenario: Unbound default does nothing
- **WHEN** the user has not bound `kKeys_RandoToggleItemTracker`
- **THEN** no key combination toggles the tracker; the overlay can still be enabled in code or via a future Phase B settings-screen toggle if added

#### Scenario: Visibility resets on launch
- **WHEN** the player toggles the item tracker on, plays, quits, and relaunches
- **THEN** the item tracker is hidden on the new launch's first frame; the player must press the toggle to re-show

### Requirement: Renderer-backend overlay compatibility

The tracker overlay SHALL produce visibly equivalent results across all five Phase A renderer backends: `SDL_SOFTWARE`, `SDL_HARDWARE`, `OPENGL`, `OPENGL_ES`, and the Switch build. "Visibly equivalent" means: the same anchor position, the same tile-set, the same on/off state for each location entry, and no backend-specific glitches (z-fighting, alpha-blending differences, palette mismatches).

The overlay implementation SHALL NOT introduce backend-specific code paths in `src/hud.c`. Backend differences SHALL be confined to the existing renderer-glue layer in `src/main.c` and `src/opengl.c`. The tracker's draw output SHALL be standard OAM data and BG-tile updates; if a backend cannot composite the overlay correctly, that's a renderer-glue bug, not a tracker bug.

#### Scenario: Renderer toggle preserves overlay
- **WHEN** the player has the tracker on and presses `R` to toggle the renderer (per `README.md` key map)
- **THEN** the overlay continues rendering on the new backend without flicker, OAM glitches, or color drift; the toggle takes effect on the next frame

#### Scenario: Per-frame cost is bounded
- **WHEN** the tracker is enabled and `reachability_state_counter` has not advanced since the last frame
- **THEN** the overlay's per-frame draw cost is dominated by an OAM-copy of fixed size (independent of inventory size, location count, or reachability state); benchmark **targets** < 0.5ms on desktop, < 2ms on Switch — desktop target is enforceable via the existing `Logic_ComputeReachability` 5ms gate per `tasks.md §1.0h`; Switch target is informational only

### Requirement: Race-mode reveal UI

The file-select screen's per-slot action menu SHALL include a "Reveal Spoiler" option, visible only when:
1. The active slot has `slot_kind == Randomizer`.
2. The slot's stored `race_mode` bit is 1.
3. The on-disk suppressed-spoiler file exists at `<spoiler_dir>/<share_string>.json`.

The action SHALL invoke `Rando_RevealSpoiler(slot_index)` (per `randomizer-core / Race-mode reveal action`). UI flow:
1. Confirmation dialog: "Reveal spoiler? This writes the placement to `<spoiler_dir>/<share_string>.json`." [Reveal / Cancel]
2. On confirm, run the action with a brief progress indicator (regeneration is bounded by the `randomizer-core / Generation performance budget` — 2s desktop / 5s Switch).
3. On success: dialog "Spoiler revealed at `<spoiler_dir>/<share_string>.json`. The race-mode bit remains set on the slot."
4. On failure: dialog with the specific failure reason (`StampMismatch`, `CrcMismatch`, `ShareStringMismatch`, `VersionMismatch`) and a recommendation (e.g., "VersionMismatch — use a binary built from generator_version N to reveal this slot").

The action SHALL be idempotent: invoking Reveal a second time on a slot with an already-revealed full spoiler returns success without rewriting (or with a confirmation to overwrite, at the implementer's discretion).

> **Stub status**: the underlying `Rando_RevealSpoiler` action, suppression, and
> stamp-verify shipped with `add-rando-race-mode-reveal` and are reachable today
> via the `RandoRevealSpoiler` keybind + the CLI `--reveal-spoiler`. This
> requirement covers only the **file-select per-slot action-menu surface**, which
> was deferred pending a per-slot action menu. Apply-time design TBD.

#### Scenario: Reveal entry visible only for race-mode slots
- **WHEN** the file-select cursor is on a Randomizer slot with `race_mode == 0`
- **THEN** the "Reveal Spoiler" action is not shown in the per-slot menu

#### Scenario: Reveal entry hidden when suppressed file missing
- **WHEN** the active slot is race-mode but `<spoiler_dir>/<share_string>.json` does not exist (deleted, wrong directory, etc.)
- **THEN** the "Reveal Spoiler" action is hidden; the player is advised to restore the file from backup, or the spoiler is permanently lost

#### Scenario: Reveal action completes within budget
- **WHEN** Reveal is invoked on a Phase A default-settings race-mode slot
- **THEN** the regeneration + stamp comparison completes within 2 seconds (desktop) / 5 seconds (Switch) — same budget as initial generation

#### Scenario: Failure preserves the suppressed file
- **WHEN** the action returns any failure code
- **THEN** the on-disk suppressed file is unchanged; the player sees the failure dialog; the slot remains usable (placement still loads from sidecar, only the spoiler is unrevealed)

### Requirement: Race-mode settings-screen warning

When the user enables Race mode in the settings UI, the settings-screen preview SHALL display a one-line warning: "Spoiler will be suppressed until Reveal is invoked." When race mode is off, no such warning is shown.

> **Stub status**: the Race-mode **toggle** itself shipped with
> `add-rando-race-mode-reveal` (native-window checkbox); only this preview
> **warning string** was deferred (it needs the settings-screen text-overlay
> surface). Cosmetic — it does not affect generation.

#### Scenario: Toggle preview surfaces the consequence
- **WHEN** the user enables Race mode in the settings UI
- **THEN** a one-line warning appears in the preview: "Spoiler will be suppressed until Reveal is invoked."

#### Scenario: Warning absent when race mode off
- **WHEN** race mode is toggled off in the settings UI
- **THEN** the suppression warning is not shown and the standard spoiler-emission contract applies

### Requirement: Auto-tracker server

The randomizer SHALL optionally expose a local-only TCP server that emits per-state-change JSON messages describing the player's current inventory, reachability state, and checked-location bitmap. External tracker clients (EmoTracker, PopTracker, custom OBS overlays) subscribe to the stream and render state without needing to peek at `g_ram` via emulator memory APIs.

**Lifecycle**:
- Disabled by default. Enabled via `[AutoTracker] enabled = true` in `zelda3.ini` or via CLI flag `--auto-tracker`.
- TCP listener binds to `127.0.0.1:<port>` (default 17400, configurable via INI). A malformed or out-of-range `Port` value (valid range 1..65535) SHALL be rejected with the standard config-parse warning, keeping the default.
- Remote bind (`0.0.0.0`) SHALL require explicit opt-in via INI `[AutoTracker] allow_remote = true`. Default localhost-only.
- Subscribe-only: external clients receive state; they CANNOT write back. No state-injection API.

**Emission triggers** (event-driven; not per-frame):
- `reachability_state_counter` advances (same trigger as the in-game tracker overlay refresh).
- Checked-location bitmap updates.
- Goal-completion state changes.

**Protocol**: newline-delimited JSON. Each message is a single line; the schema is documented in `docs/randomizer.md` Auto-tracker section.

> **Stub status**: exact JSON message schema, transport details (TCP-only vs. TCP+UDP option), and per-platform networking (Switch libnx integration) deferred to Phase D apply-time.

#### Scenario: Auto-tracker disabled by default
- **WHEN** a player launches the binary with default settings
- **THEN** no TCP socket is opened; no protocol overhead; the binary behavior matches pre-this-change exactly

#### Scenario: Localhost binding by default
- **WHEN** `[AutoTracker] enabled = true` but `allow_remote` is unset
- **THEN** the listener binds to `127.0.0.1:17400`; remote machines cannot connect

#### Scenario: Subscriber receives state on counter advance
- **WHEN** an external client is connected and the player picks up an item that advances `reachability_state_counter`
- **THEN** the server emits a JSON message describing the new state within one frame of the counter advance

#### Scenario: Determinism unaffected by auto-tracker
- **WHEN** the same seed is generated with auto-tracker on vs. off
- **THEN** the `placement_digest_hex` is byte-identical; auto-tracker is observation-only

#### Scenario: Auto-tracker disabled during self-test
- **WHEN** `--rando-selftest` is invoked
- **THEN** the auto-tracker server is not started (regardless of INI setting); self-tests run in an isolated environment

#### Scenario: Switch builds may omit auto-tracker
- **WHEN** the Switch build is compiled
- **THEN** the auto-tracker SHALL be optional (depending on libnx networking support availability); a Switch build that omits the server is a valid configuration

### Requirement: Share-string v2 on Switch and file-select surfaces is deferred

The Switch in-game share-string entry paths (the on-screen alphabet picker and `Share_PastePath`) SHALL remain v1-compatible and are NOT required to accept v2 strings: the 71-char v2 token exceeds the `kRandoTextFieldMaxLen` (64) entry cap, and Switch work is parked pending a development environment (v2 entry rides with the parked `add-rando-switch-swkbd` change, which owns the text-entry vehicle and its length cap). The file-select identity surfaces — the slot banner's truncated share-string prefix and the 5-icon visual hash — SHALL continue to derive from the stored v1 raw blob and are unchanged by the v2 format.

#### Scenario: Switch paste of a v1 string still works
- **WHEN** a player enters a valid v1 (50-char) share string through the Switch alphabet picker
- **THEN** `Share_PastePath` decodes it and the legacy seed-adoption flow proceeds exactly as before this change

#### Scenario: Banner identity is unchanged by v2
- **WHEN** a slot generated by a v2-capable binary is shown on file select
- **THEN** the banner prefix and 5-icon hash are byte-identical to what a pre-v2 binary renders for the same slot (both derive from the stored v1 raw blob)

### Requirement: Customizer-mode settings-screen entry

The PC native settings window SHALL provide a customizer-mode toggle (Randomizer → General → "Customizer") that, when enabled, exposes a manifest path field + "Load manifest" button (text-entry, matching the spoiler-save pattern — SDL2 has no portable native file dialog). The field SHALL accept a path to a manifest in the strict line-based YAML subset defined in `customizer.h`. When a manifest loads successfully, the panel SHALL display:

- The number of manual placements pinned (count of `placements:` entries).
- A pool-override summary when present (`pool +N/-M` add/remove counts).
- A capped per-pin preview (`location: item` bullets) under a collapsible node.
- The Generate button relabeled "Generate from manifest & start new slot".

A failed load SHALL display the parser's one-line error (with line number) inline and uninstall any previously-loaded manifest; the cross-field validation SHALL then block Generate ("no manifest is loaded") until a manifest loads or the toggle is turned off. Enabling race mode together with customizer mode SHALL be blocked by the same inline validation.

When customizer mode is disabled, the manifest reference SHALL be cleared (uninstalled) and the standard Generate flow proceeds unchanged. The manifest is SESSION state: a `customizer_active` bit restored from persisted window settings at startup SHALL be cleared (nothing re-installs the manifest, so a stale flag would block Generate with no visible cause).

#### Scenario: Customizer toggle exposes manifest picker
- **WHEN** the player enables Customizer mode in the settings window
- **THEN** the manifest path field + Load button appear; the Generate button is relabeled "Generate from manifest & start new slot"

#### Scenario: Invalid manifest surfaces inline error
- **WHEN** the user loads a manifest that references an unknown location name
- **THEN** the panel displays the parser error naming the line + unknown name; Generate is blocked until a valid manifest loads

#### Scenario: Customizer disabled returns to standard flow
- **WHEN** the player disables Customizer mode after loading a manifest
- **THEN** the manifest is uninstalled; the standard Generate flow runs

#### Scenario: Persisted customizer flag does not survive restart
- **WHEN** the window persisted settings with `customizer_active` set and the program restarts
- **THEN** the restored pending settings have `customizer_active` cleared; the user re-enables the toggle and reloads the manifest

### Requirement: Pot-shuffle tier selector and pot tracker presentation

The native settings window (`src/rando/rando_window/`) SHALL expose `pot_shuffle`
as a four-value selector — `Off` / `Keys` / `Contents` / `All` — defaulting to
`Off`, with a tooltip stating the durable player-facing fact (as shipped:
"Turns dungeon pots into randomizer checks. Keys = key pots only; Contents adds
loot pots; All adds the empty pots too."). On PC this is an ImGui
panel control (the in-game SNES settings screen is compiled out on PC per the
project layout; the Switch path uses the in-game screen). The un-checked-pot check
marker (`randomizer-pot-sanity / Un-checked in-scope pots are marked with a gold
check glint`) is a client-side cosmetic that does not affect placement; as built it
draws an **animated gold glint** (a sprite-layer overlay, always on) over
un-checked in-scope pots — there is no per-seed toggle, and the pot's background
tile / floor are not recolored. A 2-state variant (real item vs empty/junk) or a
"mark non-empty pots only" sub-toggle MAY be added later so it stays a useful
signal at the `All` tier (where marking every pot would otherwise read as noise),
but is not required for this change.

Because pot shuffle can add up to ~799 locations, **every location-listing surface**
SHALL present pots so non-pot checks stay legible: the native location tracker AND
reach panel group pots by room and/or gate them behind a "show pots" toggle; the
**SNES HUD location tracker (`src/hud.c`), which loops over all locations, SHALL
hide / page / summarize pots** (an 800+-row flat list is unusable there); and the
auto-tracker export SHALL define explicit pot metadata, with `ITEM_Nothing` pots
counted in the completion denominator. These surfaces also depend on the capacity
audit (`randomizer-placement`) raising their 1024-sized location-id tables
(`s_loc_*[1024]`, `kSpoilerMaxRows`) to the 2048 ceiling, or pots with id ≥ 1024 are
silently dropped from the view.

#### Scenario: Tier selector round-trips through settings
- **WHEN** the player sets `pot_shuffle = Contents` in the native window and
  generates a seed
- **THEN** the generated slot's canonical settings carry the `Contents` value and
  the seed shuffles exactly the contents-tier pots

#### Scenario: Tracker stays legible with pots enabled
- **WHEN** `pot_shuffle = All` and the location tracker is shown
- **THEN** pots are grouped (by room) or gated behind a show-pots toggle so the
  ~328 non-pot checks remain readable, not buried under ~799 pot rows

#### Scenario: SNES HUD location tracker handles pots
- **WHEN** `pot_shuffle = All` and the in-game SNES HUD location tracker
  (`src/hud.c`) is shown
- **THEN** pots are hidden/paged/summarized (not enumerated as 800+ flat rows), and
  no location with id ≥ 1024 is dropped by an un-raised tracker table

#### Scenario: Pot check marker is cosmetic and placement-neutral
- **WHEN** the gold check glint is drawn over un-checked in-scope pots
- **THEN** the seed's `placement_digest` and `settings_hash` are unaffected (the
  marker is client-side OAM + PPU-CGRAM only, like other cosmetic axes)

#### Scenario: Pot selector remains editable under door shuffle
- **WHEN** the native settings window has door shuffle enabled and cave-entrance shuffle
  is not effectively forcing pots off
- **THEN** the `pot_shuffle` selector remains editable and does not show a door-shuffle
  forced-Off note, because door+pot integration is part of the baseline behavior

#### Scenario: Pot selector disabled when cave-entrance shuffle forces pots off
- **WHEN** the native settings window has effective cave-entrance shuffle enabled
- **THEN** the `pot_shuffle` selector is greyed out with a one-line note, using the SAME
  `Settings_PotShuffleForcedOff` predicate generation uses to normalize `pot_shuffle` to
  Off, so the UI is honest about what will actually generate

#### Scenario: Tracker filter toggles persist across restarts
- **WHEN** the player toggles the Check Tracker's "Hide checked", "Only available",
  "Show pots", or "Show items (spoiler)" filters and restarts
- **THEN** all four are restored from `saves/rando_window.ini` (`g_rando_window_prefs`),
  EXCEPT "Show items (spoiler)" is force-off on load for a race seed so a replay cannot
  leak item names

#### Scenario: Spoiler reports cave-forced Off
- **WHEN** effective cave-entrance shuffle forces pots off but `pot_shuffle` was requested
- **THEN** the spoiler emits the EFFECTIVE value (0/Off via `Settings_PotShuffleForcedOff`),
  matching the canonical hash and the actually-generated pot-less seed, not the raw request

#### Scenario: Spoiler keeps requested pot tier under door shuffle
- **WHEN** door shuffle is enabled, cave-entrance shuffle is not effectively forcing pots
  off, and `pot_shuffle` was requested
- **THEN** the spoiler emits the effective requested tier rather than Off, matching the
  canonical hash and the door+pot placement that actually generated

### Requirement: Grass/rock shuffle tier selectors and terrain-check presentation

The native settings window SHALL expose two selectors in the Shuffles block — "Grass shuffle" and "Rock shuffle" — each an `EnumCombo` with lowercase labels `{"off", "junk", "all"}` matching the CLI grammar, defaulting to `off`, with tooltips limited to 1-2 durable player-facing facts (what the axis covers and what `junk` vs `all` means; no status or implementation detail). Neither selector is disable-coupled to any other axis (terrain composes freely, including under door shuffle). Presentation of terrain checks SHALL follow the pot-sanity pattern: the SNES HUD location-tracker grid skips `LOCTYPE_Grass`/`LOCTYPE_Rock` rows entirely; the native check tracker and reach panel gate terrain rows behind a session "Show terrain" toggle (default off, shown only when terrain locations exist in the slot) while ALL summary counts — global and per-region denominators — include terrain checks, with a per-region "+N terrain checks" note when rows are hidden that respects the active hide-checked/availability/search filters; the map-tracker pin hover tooltip summarizes terrain locations as a count line rather than enumerating them. The auto-tracker catalog SHALL tag the two new location types so external trackers can filter them. The spoiler surfaces (JSON, text, in-window) SHALL emit all terrain placements as ordinary rows (every active terrain location holds a real item — there is no empty-row class to filter), and the in-window row handling SHALL be verified against the enlarged location count.

#### Scenario: Selectors default off and read back
- **WHEN** the user opens the native settings window on a fresh profile
- **THEN** both selectors show `off`, and choosing `junk` or `all` round-trips through slot generation into the slot's stored canonical settings

#### Scenario: Tracker denominators count hidden terrain rows
- **WHEN** a grass-all seed is loaded and "Show terrain" is off
- **THEN** terrain rows are hidden, but the global and per-region completion counts include terrain checks, and regions with filtered-visible terrain checks show a "+N terrain checks" note

#### Scenario: HUD grid stays legible
- **WHEN** the SNES in-game location tracker renders for a seed with thousands of terrain checks
- **THEN** no terrain rows appear in the 8px grid; only the base location families render

### Requirement: In-world capped nearest-N terrain check glint

The runtime SHALL show an in-world "check" cue on unchecked, active overworld terrain objects so a player can identify remaining checks without re-cutting every bush and rock. Because the densest overworld screens hold more in-scope objects than the SNES sprite (OAM) budget can carry, the cue SHALL be capped: it marks only the nearest N (implementation constant, ~20) unchecked active terrain objects to Link on the current screen, and updates as Link moves. The cue SHALL reuse the established gold-sparkle glint (the same visual as the enemy-drop overworld marker), SHALL appear for every active tier (junk and all), SHALL disappear the frame after its object is checked, and SHALL be inert when the randomizer feature flag is off or no terrain axis is active. It is cosmetic only — it does not affect placement, logic, or the checked-location bitmap.

#### Scenario: Nearest unchecked terrain objects glint
- **WHEN** the player stands on an overworld screen that has unchecked active terrain checks with grass or rock shuffle enabled
- **THEN** the nearest few unchecked ones show the gold "check" glint, and objects already checked show no glint

#### Scenario: Glint never overruns the sprite budget
- **WHEN** the screen holds far more in-scope terrain objects than the OAM budget (e.g. a grass-dense lake/swamp screen)
- **THEN** only the nearest capped number glint; the game does not drop or corrupt other sprites, and the cue re-targets as Link walks toward other objects

#### Scenario: Glint is inert off-feature
- **WHEN** the seed has both terrain axes off, or the randomizer is inactive
- **THEN** no terrain glint is drawn and the overworld render is unaffected

### Requirement: Enemy-drop checks selector and tracker presentation

The settings UI SHALL expose enemy-drop checks as a selector separate from
`drop_shuffle`. It SHALL expose `Off`, `Enemy key drops`, and `Dungeon
enemies`. The UI SHALL reflect effective behavior: supported Wild/Retro and Dungeon
small-key modes can select `Keys`; vanilla-door Wild/Retro and Dungeon modes can
select `Dungeon`; door shuffle's forced Dungeon mode can select `Keys` but
downgrades a raw `Dungeon` request to `Keys`; vanilla keys show the effective
setting as `Off` or disable the selector. `enemy_shuffle` SHALL NOT disable the
selector.

Spoilers, the native location tracker, reach panel, and auto-tracker output SHALL
group active enemy-drop checks by dungeon and room. Inactive enemy-drop ids SHALL be
hidden from tracker denominators and spoiler placement rows. The spoiler SHALL record
the effective setting so a raw request normalized to `Off` does not imply phantom
enemy-drop checks.

#### Scenario: Selector is separate from drop shuffle
- **WHEN** the user opens the randomizer settings window
- **THEN** `drop_shuffle` remains the prize-pack shuffle control, and enemy-drop
  checks are controlled by a separate selector

#### Scenario: Dungeon tier is exposed
- **WHEN** the user opens the randomizer settings window or file-select settings
- **THEN** the enemy-drop-check selector offers the dungeon-enemy tier

#### Scenario: Trackers show only active enemy-drop checks
- **WHEN** a seed has effective `enemy_drop_checks = Keys`
- **THEN** tracker and spoiler surfaces show the generated enemy-drop locations
  grouped by dungeon/room; when the effective setting is `Off`, those ids are hidden

#### Scenario: Trackers include ordinary enemy checks for dungeon tier
- **WHEN** a seed has effective `enemy_drop_checks = Dungeon`
- **THEN** tracker and spoiler surfaces also show generated ordinary enemy locations
  grouped by dungeon/room

### Requirement: Dungeon-enemy UI is exposed after runtime and tracker support

The settings UI SHALL expose the dungeon-enemy tier now that dungeon runtime dispatch,
logic, persistence, and tracker grouping are implemented.

#### Scenario: Dungeon tier is available
- **WHEN** the native window or file-select settings present enemy-drop checks
- **AND** effective small keys support enemy-drop checks
- **AND** enemy shuffle is inactive (door shuffle is supported)
- **THEN** they offer `off`, `enemy key drops`, and `dungeon enemies`

#### Scenario: Effective value is downgraded
- **WHEN** raw settings request `dungeon` but derived rules normalize it to `off` or
  `keys`
- **THEN** user-facing effective labels reflect the lower active tier

