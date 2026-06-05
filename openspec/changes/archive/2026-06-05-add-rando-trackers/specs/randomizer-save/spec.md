## ADDED Requirements

### Requirement: Checked-location bitmap write path

The sidecar SHALL maintain a per-slot `checked_locations_bitmap` of size `(placement_table_size / 2 + 7) >> 3` bytes — the same size the Phase A read invariant (`randomizer-save / Checked-location bitmap read invariant`) already specs. **The write side** is added here.

The bit at position `k` (location_id k) SHALL be set to 1 when ANY of:

1. `Rando_OnLocationCheck(LOC_<...>, vanilla_item)` fires for that location and the dispatcher grants a substitute (the standard hot path).
2. An audit-exempt direct-write site enumerated in `audit.md` §"Reachability-affecting events" — Aga 1 defeat, every dungeon-boss-cleared flag, NPC-satisfied flags (sahasrahla, sick kid, magic-bat / mushroom→powder), pyramid-opened, master-sword-pulled, king's tomb item taken — fires and the site is the canonical pickup for a tracked location.
3. The §6.3 universal chest hook resolves a chest to a `LOC_<...>` and grants the placement-table substitute (covers every chest in the world, including ones not separately enumerated).

The bit SHALL persist to disk on the next sidecar write (per the existing `randomizer-save / Atomic-commit protocol`). Mid-session bit-set is in-memory only until the next save fires.

The bitmap SHALL NOT be cleared by the dispatcher under any circumstance during a session — once a location is checked, it stays checked until the slot is erased or the player explicitly invokes file-erase from the file-select screen.

When the in-session bitmap differs from the on-disk bitmap and the sidecar write fires, the writer SHALL include the updated bitmap; the Phase C TLV chain (per `randomizer-save / Slot region layout`) SHALL still be appended after the bitmap regardless of whether the bitmap content changed.

#### Scenario: Dispatcher fire sets the bit
- **WHEN** the dispatcher fires for `LOC_HyruleCastle_BoomerangChest` and grants a substitute
- **THEN** bit `LOC_HyruleCastle_BoomerangChest` in the in-memory bitmap is set to 1; on the next sidecar write, the on-disk bitmap reflects the change

#### Scenario: Audit-exempt event flag sets the bit when site is a tracked location
- **WHEN** the audit-exempt site for `RescuedZelda` fires (uncle's death / sanctuary escort completion) and the corresponding location is enumerated in `audit.md` §"Reachability-affecting events" with a `LOC_<...>` tag
- **THEN** that location's bit in the in-memory bitmap is set to 1

#### Scenario: No checkbacking — bit-set is monotonic during a session
- **WHEN** any logic path that already set bit k tries to set it again
- **THEN** the operation is a no-op; the bit stays at 1; no side effects

#### Scenario: Bitmap persists across save/load
- **WHEN** the player has 47 locations checked, saves, quits, and reloads
- **THEN** the bitmap on reload reflects all 47 checked bits; the location-tracker (`randomizer-ui / Optional in-game location tracker (Phase B)`) shows the same `*` glyphs that were visible before the save

#### Scenario: Phase C TLV reserve preserved
- **WHEN** the writer commits the slot with updated bitmap
- **THEN** the bitmap is at the same byte offset (slot header + placement table) and is `(placement_table_size / 2 + 7) >> 3` bytes long; any future Phase C TLV chain appended after it remains unaffected by bitmap-content changes

#### Scenario: File-erase clears the bitmap
- **WHEN** the player invokes file-erase on a slot from the file-select screen
- **THEN** the next write for that slot writes a fresh slot header with `slot_kind = Empty=0`, no placement table, and no bitmap; subsequent re-creation of the slot starts with a fresh zeroed bitmap
