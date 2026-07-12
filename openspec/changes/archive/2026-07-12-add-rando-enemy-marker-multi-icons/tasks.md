# Enemy Marker Multi-Icon Rendering - tasks

## 1. Resource audit

- [x] 1.1 Inventory sprite VRAM cells used by receive-item animations, field-item
  sprites, direct-grant confirmations, pot glints, enemy bodies, and spawned drops.
- [x] 1.2 Choose and document a fixed marker-icon tile capacity based on measured
  safe cells.
- [x] 1.3 Identify palette rows that can safely support item markers, including
  custom randomizer art and trap decoy variants.
- [x] 1.4 Document OAM budget and draw-order constraints for live carriers, spawned
  drops, pot glints, HUD/tracker overlays, and text boxes.
- [x] 1.5 Define marker metadata required for future all-enemy domains: stable
  identity, screen-coordinate derivation, scroll/camera basis, sorted-OAM region,
  and checked-source suppression behavior.

## 2. Allocator and candidate model

- [x] 2.1 Add a marker candidate collector for unchecked forced `EnemyDrop` carriers,
  ordinary `Enemy` carriers, and spawned forced-drop pickups.
- [x] 2.2 Define typed source keys and visual-only icon keys, so source identity
  does not affect tile coalescing.
- [x] 2.3 Add a deterministic allocator that coalesces identical icon keys and assigns
  bounded marker tile slots by priority and stable source identity.
- [x] 2.4 Add selftests for coalescing, four distinct slots, priority order,
  capacity exhaustion, palette conflicts, tracker-scratch fallback, and all-or-none
  OAM fallback.
- [x] 2.5 Document complete OBJ tile footprints and base charnum mappings for every
  marker icon slot before rendering is enabled.

## 3. Rendering integration

- [x] 3.1 Load allocated marker icons only into marker-owned `0xF0..0xFF` OBJ
  scratch cells, with four fixed exact icon slots and no receive-slot mutation.
- [x] 3.2 Wire `[Graphics] EnemyDropMarker=item` live carrier markers through the
  allocator.
- [x] 3.3 Wire spawned forced-drop pickups through the allocator with higher priority
  than live carriers.
- [x] 3.4 Keep `[Graphics] EnemyDropMarker=generic` on the neutral gold glint for live
  carriers.
- [x] 3.5 Fall back to the gold glint for any marker whose exact item icon cannot be
  rendered safely.
- [x] 3.6 Reserve the full OAM footprint before writing exact marker OAM; multi-entry
  item icons and glints must draw all required OAM entries or none.
- [x] 3.7 Keep custom marker palette writes frame-scoped through the unified overlay
  palette manager, with transformed-base restoration and CGRAM-rebuild invalidation.
- [x] 3.8 Give the legacy OAM tracker priority over the `0xF0..0xFF` scratch range.
- [x] 3.9 Default a missing `[Graphics] EnemyDropMarker` preference to the
  non-spoiler generic gold glint while retaining `item` as an explicit option.
- [x] 3.10 Reserve one fallback glint per active check before exact-icon upgrades
  and spill dense sorted-room glints into only verified-free ancilla OAM capacity.

## 4. Compatibility checks

- [x] 4.1 Verify pot-sanity glints and enemy item markers can coexist without palette,
  tile, or OAM corruption.
- [x] 4.2 Verify field-item sprites and item-receipt animations retain receive-slot
  ownership because enemy item markers no longer use `0x24/0x34`.
- [x] 4.3 Verify enemy shuffle still resolves marker locations through vanilla
  room/source-slot identity.
- [x] 4.4 Verify custom icons for Triforce Piece, Rupoor, magic upgrades, and trap
  decoys either render exactly or fall back to the glint.

## 5. Validation

- [x] 5.1 Run `openspec validate add-rando-enemy-marker-multi-icons --strict`.
- [x] 5.2 Run allocator/selftests and `--rando-selftest` in a Release build.
- [x] 5.3 Run `git diff --check`.
- [x] 5.4 Capture F12 dumps for a dense scripted room with multiple enemy-marker
  placed items and confirm marker charnums stay within `0xF0..0xFF` with no
  `0x24/0x34` exact-marker references.
  <!-- owner F12 2026-07-12: room 0x0A8 captured all four dynamic Red Stalfos
  alive (type 0xA7, runtime slots 9-12). PPU OAM exactly matched the WRAM OAM
  buffer and contained four complete marker footprints: 0x1F0/0x1F2,
  0x1F4-0x1F7, 0x1F8/0x1FA, and 0x1FC/0x1FE. No visible OAM entry referenced
  charnum 0x24 or 0x34. This supersedes the stale room-0x72 wording: that room
  has only one generated enemy check and cannot exercise a dense allocation. -->
- [x] 5.5 Playtest dense rooms with multiple live markers, spawned drops, pot glints,
  custom-art items, and receipt/direct-grant transitions.
  <!-- owner confirmation 2026-07-11: enemy and pot marker combinations were
  previously playtested. The separate F12 charnum proof in 5.4 remains. -->
  <!-- owner F12 2026-07-12: Eastern room 0x0A8's four scripted Red Stalfos were
  assigned checks correctly, but eight total Stalfos exhausted floor-region OAM;
  later carriers had no marker and Moon Pearl alternated exact/glint as capacity
  changed. The allocator now preserves fallback coverage before exact upgrades and
  uses only live-free ancilla-region overflow. The 2026-07-12 F12 retest in 5.4
  captured complete exact markers over all four live scripted carriers. -->
