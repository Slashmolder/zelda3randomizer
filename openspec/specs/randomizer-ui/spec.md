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

On Switch builds, the in-game settings screen SHALL allow the player to select world-state, goal, item-pool difficulty, dungeon-item mode, Triforce-Hunt piece counts (when goal is Triforce Hunt), and optional shuffle modules; SHALL accept seed entry via random generation or share-string paste; and SHALL display the current settings hash live.

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

#### Scenario: Toggle binding
- **WHEN** the player presses the configured tracker-toggle binding
- **THEN** the item tracker overlay appears or disappears without pausing gameplay

#### Scenario: Re-render gated on inventory-change counter
- **WHEN** `reachability_state_counter` is unchanged between two consecutive frames
- **THEN** the tracker's draw path uses cached OAM data and does not recompute layout

### Requirement: Optional in-game location tracker (Phase B)

A toggleable location-list overlay SHALL display locations grouped by region with reachability-colored entries. Reachability SHALL be recomputed only when `reachability_state_counter` increases.

#### Scenario: Reachability updates on inventory change
- **WHEN** the player acquires an item that unlocks a previously unreachable region
- **THEN** all locations in that region transition to the reachable color within one frame after the inventory hash changes

#### Scenario: Checked location persists across save/load
- **WHEN** the player checks a location, saves, and reloads
- **THEN** the location remains marked as checked because the checked-location bitmap is persisted via the sidecar slot

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

### Requirement: Boss-heart-container shuffle toggle

The settings UI SHALL expose the `region_boss_hearts_in_pool` axis as a
player-toggleable control, and SHALL NOT display the raw field name or value to
the player, because the field value is inverted relative to its name (`1` = boss
hearts pinned to their boss slots / NOT in the general pool; `0` = boss hearts
shuffled into the general pool).

The control SHALL be labeled **"Shuffle boss heart containers"** and map the
inversion at the UI layer:

- **checked** ⇒ `region_boss_hearts_in_pool = 0` (the 10 boss heart containers
  join the general item pool; arbitrary items may land at boss kills).
- **unchecked** ⇒ `region_boss_hearts_in_pool = 1` (each dungeon boss grants its
  own heart container — the default).

The control SHALL initialize unchecked when the field is at its default (`1`) and
toggling it SHALL refresh the live settings hash like any other seed-defining axis.
The field SHALL NOT be renamed and the canonical byte / CSV keys SHALL keep their
existing meaning for headless and share-string compatibility.

#### Scenario: Toggle off (default) pins boss hearts

- **WHEN** the "Shuffle boss heart containers" control is unchecked (the default)
- **THEN** `region_boss_hearts_in_pool` is `1` and every dungeon boss kill grants
  that dungeon's boss heart container

#### Scenario: Toggle on shuffles boss hearts into the pool

- **WHEN** the player checks "Shuffle boss heart containers"
- **THEN** `region_boss_hearts_in_pool` is set to `0`, the live settings hash
  refreshes, and generation may place non-heart items at the boss slots while the
  10 boss heart containers are placed elsewhere

#### Scenario: Raw inverted field name is never shown

- **WHEN** the settings UI is displayed
- **THEN** no control or text shows the literal "region boss hearts in pool"
  field name or its raw 0/1 value; only the "Shuffle boss heart containers"
  label is shown

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

