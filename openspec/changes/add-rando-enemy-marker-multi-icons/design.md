# Enemy Marker Multi-Icon Rendering - design

## D1 - Current constraint

The current live enemy marker path intentionally supports only one placed-item icon
at a time. Item markers reuse the receive-item tile bundle (`0x24` / `0x34`), so
two different live markers cannot both be correct: every OAM entry points at the
same tile data and whichever icon was loaded last wins. The current gold-glint
fallback avoids showing a misleading item.

This change replaces that single-slot behavior for enemy markers with a dedicated
bounded marker-icon pool. The receive-item slot remains owned by actual receive,
confirmation, direct-grant, and pickup presentation paths.

## D2 - Marker candidates and icon keys

Each frame or room-state refresh builds a side-effect-free list of active marker
candidates. The base candidate model covers the currently implemented enemy-check
domains:

- active unchecked forced `EnemyDrop` live carriers;
- active unchecked ordinary dungeon `Enemy` live carriers;
- active unchecked spawned forced-drop pickups;
- no inactive, checked, vanilla, or excluded sources.

Each candidate records the location id, room, source slot, marker kind
(`live_carrier` or `spawned_drop`), screen position, and an icon key. The icon key
includes every graphics-affecting attribute: tile bundle id, dimensions, OAM flags,
palette requirement, custom-art requirement, and any special draw policy such as
trap decoy handling. Identical icon keys share a marker tile slot.

The candidate model must be domain-extensible before it is reused by the future
all-enemy tier. For every emitted all-tier source that can have an in-world marker,
the registry must define domain-specific authored source identity, screen-coordinate
derivation, scroll/camera basis, sorted-OAM region, and checked-source suppression
behavior. If a domain intentionally cannot render in-world markers, that domain's
markers are suppressed while tracker/spoiler output remains present.

Candidates are sorted deterministically by priority, then by stable source identity.
Spawned drops have higher priority than live carriers. Within one priority class,
ordering uses location id and source slot so allocation does not depend on sprite
iteration accidents.

## D3 - Dedicated marker tile pool

Enemy item markers SHALL NOT DMA placed-item graphics into unowned OBJ cells. The
implementation first inventories sprite VRAM usage across the relevant dungeon
states, then uses only cells proven safe for randomizer overlays. Until a
marker-owned pool exists, the only safe exact-item path is the existing receive-item
slot, borrowed after `Sprite_Main` only when no current OAM entry already references
that slot.

The pool size is intentionally bounded. The current safe capacity is one distinct
receive-slot-backed icon key; additional distinct markers fall back to the gold
glint. A future marker-owned pool may raise this to two to four distinct icons, but
the implementation must choose the final capacity from measured VRAM availability
and backend behavior rather than from this proposal text.

Each marker icon slot reserves the complete OBJ tile footprint required by the icon,
including 8x8, 8x16, 16x16, and custom-art layouts. The implementation must document
the base charnum mapping used for top, bottom, and adjacent cells in each slot before
the allocator is wired to rendering. Current exact markers map to the receive-item
slot (`0x24` top, `0x34` bottom for 8x16 icons) only for the post-sprite overlay
frame that owns it. A future marker-owned pool must populate its cells without
leaving visible or cached state in the shared receive-item slot.
Any candidate whose icon cannot get a complete slot draws the gold glint instead.
Slot exhaustion is a normal fallback path, not a fatal condition.

## D4 - Palette and custom-art handling

Palette state is part of icon allocation. If two icons use the same tiles but require
different palette rows, they are distinct icon keys unless the art can be rendered
correctly with a shared palette. Custom randomizer icons such as magic upgrades,
Rupoors, Triforce pieces, and trap decoys must either acquire their required palette
state or fall back to the gold glint.

The renderer must not borrow a palette row that is already needed by Link, enemy
sprites, pot glints, direct-grant confirmation, or field-item sprites unless that row
is explicitly proven safe for the current frame.

Marker palette writes must be frame-scoped or tied to a proven marker-owned row.
Before a palette row can be reused by Link, enemies, pot glints, receipts, or field
items, the renderer must restore the non-marker contents or prove that the row was
never shared. Multiple custom icons that require incompatible contents in the same
palette row must not alternate, flicker, or recolor each other; lower-priority
candidates fall back to the gold glint or suppress cleanly.

## D5 - Marker rendering policy

`[Graphics] EnemyDropMarker=generic` remains unchanged: live enemy carriers draw
the neutral gold glint and do not reveal placement. Spawned forced-drop pickups may
still show the real dropped item when the existing spawned-drop policy can do so
safely; otherwise they draw the glint.

`[Graphics] EnemyDropMarker=item` uses the bounded allocator. Live carriers and
spawned forced-drop pickups draw their real placed item when their icon key has an
allocated safe slot. If a candidate cannot be represented safely, only that candidate
falls back to the gold glint unless a room-wide conflict makes per-candidate fallback
unsafe.

The renderer must never silently substitute a key, rupee, or other stand-in for the
real placed item in item mode. The acceptable outcomes are the exact placed item or
the neutral glint.

## D6 - Interaction with other overlay systems

Pot-sanity glints remain independent and may coexist with enemy item markers. The
enemy marker pool must not consume the pot-glint art or palette in a way that makes
pots disappear or recolor incorrectly.

Field-item sprites and item receipts keep their existing safety priority. If a
receipt animation, direct-grant confirmation, or field-item sprite path needs a
shared decompression, DMA, or palette resource that the enemy marker path cannot
isolate, enemy item markers fall back to the gold glint for that frame.

OAM allocation must stay within the existing sorted sprite regions. Before allocating
marker tiles or writing OAM, the renderer must reserve the full OAM footprint for the
candidate in the correct region. Multi-entry icons draw all required OAM entries or
none. Enemy markers must not clobber Link, the enemy body, spawned pickups, pot
glints, HUD/tracker overlays, or text boxes. If OAM is exhausted, lower-priority
enemy markers fall back to the glint; if the glint also cannot reserve its required
OAM/palette resources, the marker suppresses cleanly using deterministic priority.
No marker may draw partial garbage.

## D7 - Client-local behavior

This is visual-only. It does not change the generated world, item placement,
settings hash, share string, generator version, checked-location bitmap, sidecar
format, snapshot format, spoiler content, tracker reachability, or auto-tracker
messages. Two clients on the same seed may see different marker presentation based
on local graphics settings, but collection semantics are identical.

Enemy shuffle compatibility follows the owning enemy-check domain's authored source
identity. Dungeon rows use the preserved vanilla room/source slot; overworld,
boss/miniboss, and scripted-spawn rows use their generated authored identity tuples.
The icon key is resolved from the randomizer location's placed item, not from the
substituted enemy type.

## D8 - Implementation phases

1. Inventory sprite VRAM, palette, and OAM resources in the dungeon states that can
   host enemy markers. Pick a measured marker-icon capacity and document the
   rejected slots.
2. Add the marker candidate collector and deterministic icon-key allocator with
   selftests, but leave rendering on the current single-icon/glint path.
3. Add dedicated tile-slot loading for the allocated icon keys, guarded by receipt,
   palette, field-item, and OAM availability checks.
4. Wire `EnemyDropMarker=item` live carriers and spawned forced-drop pickups through
   the allocator. Keep the glint fallback active for every unsafe case.
5. Add targeted runtime dumps and playtests for dense rooms, spawned drops, custom
   icons, pot glints, direct grants, and enemy shuffle.

## D9 - Verification strategy

Required validation:

- `openspec validate add-rando-enemy-marker-multi-icons --strict`;
- allocator unit/selftests for identical-icon coalescing, distinct-icon allocation,
  deterministic priority, slot exhaustion, palette conflict, and receipt-active
  fallback;
- Release build and `--rando-selftest`;
- `git diff --check`;
- F12 dump verification for the Hyrule Castle room `0x72` case where the map guard
  and another enemy check can have different placed items;
- a high-density dungeon room with at least three active enemy markers, including
  one custom-art item and one ordinary vanilla receive-item bundle;
- a pot-sanity room that has both pot glints and enemy item markers;
- a spawned forced-drop pickup while another live enemy marker remains active;
- enemy shuffle enabled, confirming the marker follows the source-slot location and
  not the substituted enemy type.
