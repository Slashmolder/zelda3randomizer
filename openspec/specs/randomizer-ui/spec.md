# randomizer-ui Specification

## Purpose
TBD - created by archiving change add-randomizer-support. Update Purpose after archive.
## Requirements
### Requirement: Text-input infrastructure

The host SHALL provide a single-line text-input widget (`RandoTextField`) sourcing characters from SDL_TEXTINPUT events on PC and from libnx `swkbd` on Switch. The widget SHALL support cursor positioning, backspace, paste from clipboard, and a constrained character set (the base32 alphabet plus separators).

#### Scenario: PC keyboard typing
- **WHEN** the settings screen is active on a PC build and the player presses base32-alphabet keys
- **THEN** the characters appear in the share-string field and non-alphabet keys are silently ignored

#### Scenario: Switch swkbd is invoked
- **WHEN** the settings screen is active on the Switch build and the player activates the share-string field
- **THEN** libnx `swkbd` appears and its result is routed into the widget buffer

#### Scenario: Switch swkbd dismissed without confirming
- **WHEN** the player dismisses libnx `swkbd` without confirming (e.g., pressing B / Cancel)
- **THEN** the widget buffer is left untouched (no partial write), focus returns to the on-screen alphabet picker, and the settings hash is unchanged

#### Scenario: Paste from clipboard
- **WHEN** the player presses Ctrl+V (PC) or the controller paste shortcut and the clipboard contains a valid share string
- **THEN** the buffer is replaced with the clipboard content and the live settings hash updates

### Requirement: On-screen alphabet picker

The settings screen SHALL provide an on-screen alphabet picker as a controller-friendly alternative to keyboard typing. The picker SHALL be the default focus on the Switch undocked build.

#### Scenario: Picker operable with d-pad and two buttons
- **WHEN** the picker is focused
- **THEN** d-pad navigation moves the cursor, A confirms a character, B deletes the last character

#### Scenario: Default focus on handheld
- **WHEN** the settings screen opens on the Switch build in handheld mode
- **THEN** the picker is the focused widget

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

The settings screen SHALL allow the player to select world-state, goal, item-pool difficulty, dungeon-item mode, Triforce-Hunt piece counts (when goal is Triforce Hunt), and optional shuffle modules; SHALL accept seed entry via random generation or share-string paste; and SHALL display the current settings hash live.

#### Scenario: Preset application
- **WHEN** the player selects a built-in preset
- **THEN** all individual settings update and the settings hash refreshes

#### Scenario: Share-string paste populates fields
- **WHEN** the player pastes a valid share string
- **THEN** the settings panel updates to reflect the decoded settings and the seed field shows the decoded `seed_u64`

#### Scenario: Invalid share string surfaces inline error
- **WHEN** the player pastes a malformed or alttpr.com-format share string
- **THEN** an inline error appears and generation is blocked until the input is corrected or cleared

#### Scenario: Non-vanilla asset data triggers a one-time dialog per hash
- **WHEN** the player attempts to start generation while `g_assets_hash != kVanillaAssetsHash` and no persisted decision exists for this hash
- **THEN** the settings screen displays a dialog explaining the risk with three choices (Always allow / Allow once / Cancel); the chosen action is honored and "Always allow" persists keyed by hash

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

