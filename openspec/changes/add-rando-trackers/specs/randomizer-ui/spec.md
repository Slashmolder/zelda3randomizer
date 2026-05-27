## MODIFIED Requirements

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

## ADDED Requirements

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
