## ADDED Requirements

### Requirement: Direct-grant visible confirmation ancilla

When the dispatcher returns `kRandoLttpSkip` (direct-grant items: `HalfMagic`, `QuarterMagic`, `TriforcePiece`, prize bits, dungeon-item bits), the call site SHALL invoke `Rando_ShowDirectGrantConfirmation(item_id)` — extending the Phase A signature (`void`) to take the granted item id. The implementation SHALL spawn a per-item-type icon ancilla — drawn above Link in the same screen-position style as the vanilla item-receipt ancilla — for every direct-grant item that has a vanilla receive-sprite bundle. Items with no such bundle to borrow (the magic upgrades and the Triforce piece) SHALL keep the Phase A audio + HUD-only behavior via the `gfx == 0` audio-only sentinel. Audio + HUD refresh behavior from Phase A §7.6 SHALL be preserved; the visible ancilla is added alongside, not replacing.

Items SHALL be mapped to tile addresses via `assets/rando/direct_grant_icons.yaml`, which the codegen pipeline emits as `src/rando/direct_grant_icons.h` (a `static const DirectGrantIconEntry kDirectGrantIcons[]` table indexed by item id). Icons SHALL reference existing tiles in the bundled graphics blob (HUD/inventory tiles); no new sprite art SHALL be added.

#### Scenario: Prize grant pops the matching prize icon
- **WHEN** the player obtains a dungeon prize (a pendant or crystal) or a per-dungeon item (Big Key / Map / Compass) from a §6.x direct-grant site (e.g., a chest, tablet, or NPC grant placed there by the shuffler), excluding the boss-prize drop — which shows the recolored falling-prize sprite instead and deliberately suppresses this icon to avoid a duplicate visual
- **THEN** the standard item-receipt sound plays, the HUD refreshes, and an item-receipt ancilla draws the matching item icon above Link's head (for pendants, in the correct green / red / blue palette)

#### Scenario: Tablet pickup shows visible feedback
- **WHEN** the player strikes the Ether tablet (`player.c:594`) or Bombos tablet (`player.c:634`) and the dispatched item is a direct-grant item
- **THEN** in addition to the screen flash and magic consumption, a per-item icon ancilla draws above Link

#### Scenario: Pure UX — no kGeneratorVersion bump
- **WHEN** this change ships and the player generates a seed that previously generated cleanly
- **THEN** the placement output is byte-identical (`placement_digest_hex` unchanged) — the icon table affects only the visible animation path

#### Scenario: Icon lookup table is built into the binary
- **WHEN** the build runs
- **THEN** `assets/rando/direct_grant_icons.yaml` is consumed by `assets/rando_logic_gen.py`, producing `src/rando/direct_grant_icons.h`; the header is registered in `Makefile`, `Zelda3.vcxproj` pre-build, and `src/platform/switch/Makefile`; `assets/scripts/check_codegen_wiring.py` asserts the registration consistency across all three build systems

#### Scenario: Item with no receive-sprite bundle falls back to audio-only
- **WHEN** a direct-grant call site passes an item whose `kDirectGrantIcons` entry has `gfx == 0` — the audio-only sentinel, which covers both items absent from the table (zero-initialized) and items deliberately left audio-only because no vanilla receive-sprite bundle exists to borrow (`HalfMagic`, `QuarterMagic`, `TriforcePiece`)
- **THEN** the helper plays the audio + HUD-refresh portion only (Phase A behavior) and does NOT spawn a draw-tile ancilla

### Requirement: Direct-grant confirmation call sites enumerated

Every call site that invokes the dispatcher and may receive `kRandoLttpSkip` SHALL invoke `Rando_ShowDirectGrantConfirmation(item_id)` in the skip branch. The Phase A call-site set, with item ids passed at each site, SHALL be:

- `src/player.c:594` (Ether tablet) — passes the item id resolved from the tablet's placement-table entry.
- `src/player.c:634` (Bombos tablet) — passes the item id resolved from the tablet's placement-table entry.
- `src/player.c:3886` (generic direct-grant cue from the player module) — passes the granted item id from the call-site's local context.
- `src/sprite_main.c:1273` (Pyramid Fairy item drop, §6.7) — passes the placement-table-resolved item id at the active `LOC_Pyramid_Fairy_Sword` / `LOC_Pyramid_Fairy_Bow` slot.
- `src/sprite_main.c:18586` (generic direct-grant cue from a sprite handler) — passes the granted item id from the call-site's local context.

When this change lands, all 5 call sites SHALL be updated in the same commit; no partial migration is permitted. New call sites added by future changes (e.g., Slice 8 minigame dispatch) inherit the same contract.

#### Scenario: Adding a new direct-grant site without passing the item id fails CI
- **WHEN** a developer adds a `Rando_ShowDirectGrantConfirmation()` (zero-arg form) call after this change lands
- **THEN** the compile fails because the signature no longer accepts zero arguments
