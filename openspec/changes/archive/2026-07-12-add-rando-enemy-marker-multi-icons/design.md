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

Each candidate records a typed source key: domain, location id, room or overworld
area, source slot or block, marker kind (`live_carrier` or `spawned_drop`), sprite
index, screen position, and sorted-OAM region. The icon key is visual-only and
includes every graphics-affecting attribute: tile bundle id, footprint, OAM flags,
palette policy, custom-art signature, and any special draw policy. Identical visual
icon keys share a marker tile slot even when they come from different source keys.

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

Enemy item markers SHALL NOT DMA placed-item graphics into unowned OBJ cells or the
shared receive-item slot. Exact enemy markers use the BORROWED OBJ range
`0xF0..0xFF` in objTileAdr2, which maps to VRAM `0x5f00..0x5fff`. That range is not
statically free: sprite subsets are `0x400` words each (64 tiles x 16 words), so
subset 3 spans `0x5c00..0x5fff` inclusive and chars `0xF0..0xFF` are the live
subset-3 sheet's 4th row (the vanilla cucco's bottom tiles render from chars
`0xFA/0xFB`, inside marker slots 2/3). Marker uploads therefore stamp the chars
they paint (`Rando_ObjScratchStampChars`); the NMI release pass restores any char
no overlay re-stamped that frame from the current subset-3 sheet
(`Gfx_RestoreSpriteSubset3Chars`), and a full subset reload clears the stamps.

The pool size is intentionally bounded at four distinct visual icon keys. The fixed
slots are `F0-F3`, `F4-F7`, `F8-FB`, and `FC-FF`, with each slot laid out as
top-left, top-right, bottom-left, bottom-right 8x8 tiles. Exact markers draw only
explicit small OAM pieces: 8x8 uses the top-left tile, 8x16 uses top-left plus
bottom-left, and 16x16/custom art uses all four tiles. The renderer never uses
large OAM and never addresses `base + 0x10`, so the final `FC-FF` slot cannot wrap
outside the scratch range.

The marker path never calls `Rando_EnsureRecvItemSlotGfx`, never mutates the
receive-slot staging buffers, and never touches `g_recv_item_slot_owner`.
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

Marker palette writes are owned by the unified overlay palette manager shared with
pot and enemy glints. NMI restores previous overlay rows from saved transformed PPU
CGRAM snapshots unless CGRAM was freshly rebuilt that NMI; on rebuild, the fresh
post-cosmetic CGRAM copy is the new baseline and stale snapshots are discarded.
Dynamic item marker palettes are transformed with the same cosmetic palette
semantics as normal CGRAM rows. Gold glint palettes are intentionally
cosmetic-exempt. Row 7 is never allocated for overlays.

## D5 - Marker rendering policy

The client-local default is `[Graphics] EnemyDropMarker=generic`, so a fresh or
key-absent configuration marks checks without revealing their contents. The
item-revealing mode remains available as an explicit preference and existing INI
values continue to parse unchanged.

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

Before drawing exact icons, the dungeon overlay reserves one one-entry glint for
every active pot/enemy marker. In sorted-sprite rooms, fallback glints allocate from
the normal floor region first and may spill into the ancilla region only by scanning
the finished frame's real OAM for physically hidden entries. The planner does not
trust allocator base counters because vanilla overflow does not advance them. Exact
icons may use only entries left after the baseline reservation. This keeps dense
scripted-spawn rooms stable as enemies enter or leave without overwriting ancilla OAM.

## D6 - Interaction with other overlay systems

Pot-sanity glints remain independent and may coexist with enemy item markers. The
enemy marker pool must not consume the pot-glint art or palette in a way that makes
pots disappear or recolor incorrectly.

Field-item sprites and item receipts keep their existing receive-slot ownership.
Enemy markers are isolated from the receive slot, so receipts and direct-grant
confirmations continue to own `0x24/0x34`. The legacy OAM tracker owns the
`0xF0..0xFF` scratch range when it will draw this frame; in that case exact enemy
markers have zero capacity and fall back to glints or suppress cleanly. PC rich
tracker windows do not consume the scratch OBJ range.

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

1. Add typed marker source keys and visual-only icon keys.
2. Add scratch-owner arbitration between legacy OAM tracker and enemy markers.
3. Add side-effect-free item icon planning and marker-owned `0xF0..0xFF` tile upload.
4. Replace pot-only palette injection with the unified overlay palette manager.
5. Wire dungeon and overworld post-`Sprite_Main` overlays through the exact-icon to
   glint fallback path.
6. Add targeted runtime dumps and playtests for dense rooms, spawned drops, custom
   icons, pot glints, direct grants, tracker overlays, and enemy shuffle.

## D9 - Verification strategy

Required validation:

- `openspec validate add-rando-enemy-marker-multi-icons --strict`;
- allocator unit/selftests for identical-icon coalescing, four distinct slots,
  deterministic priority, slot exhaustion, tracker-scratch conflict, palette
  conflict, and all-or-none OAM fallback;
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
