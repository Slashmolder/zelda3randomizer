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
- [x] 2.2 Define an icon key that includes tile bundle, dimensions, OAM flags,
  palette requirements, custom-art requirements, and special draw policy.
- [x] 2.3 Add a deterministic allocator that coalesces identical icon keys and assigns
  bounded marker tile slots by priority and stable source identity.
- [x] 2.4 Add selftests for coalescing, distinct allocation, priority order,
  capacity exhaustion, palette conflicts, and receipt-active fallback.
- [x] 2.5 Document complete OBJ tile footprints and base charnum mappings for every
  marker icon slot before rendering is enabled.

## 3. Rendering integration

- [x] 3.1 Load allocated marker icons only into proven safe OBJ cells. Current
  capacity is one receive-slot-backed exact icon after final OAM proves that slot is
  unused; additional distinct icons fall back to glints.
- [x] 3.2 Wire `[Graphics] EnemyDropMarker=item` live carrier markers through the
  allocator.
- [x] 3.3 Wire spawned forced-drop pickups through the allocator with higher priority
  than live carriers.
- [x] 3.4 Keep `[Graphics] EnemyDropMarker=generic` on the neutral gold glint for live
  carriers.
- [x] 3.5 Fall back to the gold glint for any marker whose exact item icon cannot be
  rendered safely.
- [x] 3.6 Reserve the full OAM footprint before tile/palette allocation; multi-entry
  item icons and glints must draw all required OAM entries or none.
- [x] 3.7 Keep custom marker palette writes frame-scoped or confined to marker-owned
  rows, with restoration before any shared row is reused.

## 4. Compatibility checks

- [x] 4.1 Verify pot-sanity glints and enemy item markers can coexist without palette,
  tile, or OAM corruption.
- [x] 4.2 Verify field-item sprites and item-receipt animations retain priority over
  enemy item markers.
- [x] 4.3 Verify enemy shuffle still resolves marker locations through vanilla
  room/source-slot identity.
- [x] 4.4 Verify custom icons for Triforce Piece, Rupoor, magic upgrades, and trap
  decoys either render exactly or fall back to the glint.

## 5. Validation

- [x] 5.1 Run `openspec validate add-rando-enemy-marker-multi-icons --strict`.
- [x] 5.2 Run allocator selftests and `--rando-selftest` in a Release build.
- [x] 5.3 Run `git diff --check`.
- [ ] 5.4 Capture F12 dumps for room `0x72` with two different enemy-marker placed
  items and confirm the lower-priority marker glints instead of showing the other's
  icon.
- [ ] 5.5 Playtest dense rooms with multiple live markers, spawned drops, pot glints,
  custom-art items, and receipt/direct-grant transitions.
