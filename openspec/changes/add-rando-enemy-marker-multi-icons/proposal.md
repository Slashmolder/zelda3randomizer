## Why

`EnemyDropMarker=item` currently has to fall back to the neutral gold glint when
more than one active enemy marker in a room resolves to a different item icon. That
fallback is intentional: the live-marker path reuses the single receive-item VRAM
bundle, so multiple OAM entries would all point at the same tile data and the last
loaded icon would win.

That keeps the current feature honest, but it leaves placed-item marker mode less
useful in rooms with multiple enemy checks. The improvement is to add a bounded
allocator that draws exact placed items when the runtime can prove the graphics
resources are safe, and falls back per marker when it cannot. A future marker-owned
OBJ pool can raise the exact-icon capacity beyond the current receive-slot-backed
single distinct icon.

## What Changes

- Add an enemy-marker item-icon allocator that represents exact placed-item icons
  without overwriting unrelated OBJ users. Current builds use one receive-slot-backed
  exact icon only after final OAM proves that slot is not visible.
- Precompute active enemy-marker icon requirements for the current room/frame,
  coalesce identical icons, reserve a bounded number of marker tile slots, and map
  each live carrier or spawned drop to the correct tile charnum.
- Keep `[Graphics] EnemyDropMarker=generic` as the non-spoiler gold-glint mode.
- Keep `[Graphics] EnemyDropMarker=item` as the placed-item mode, but upgrade it to
  draw exact real item icons when the allocator can do so safely.
- Preserve the gold glint as the required fallback for capacity exhaustion, missing
  art, custom-palette conflicts, receipt-animation conflicts, or OAM pressure.
- Treat spawned forced-drop pickups as higher-priority markers than live carriers,
  because once the enemy is killed the visible dropped item should be the real
  placed item whenever possible.

## Non-Goals

- Do not change placement, logic, share strings, canonical settings,
  `kGeneratorVersion`, save data, checked-location state, or spoiler output.
- Do not remove the gold-glint fallback; this change broadens when item mode can be
  exact, but unsafe cases still need an honest neutral marker.
- Do not raise location capacity or expand enemy-check eligibility.
- Do not redesign field-item sprite substitution or item-receipt graphics outside
  the narrow resource-sharing rules needed for enemy markers.

## Impact

- **Runtime graphics**: sprite marker draw path, item-icon graphics loading, palette
  selection, OAM allocation, and receipt/direct-grant conflict detection.
- **Configuration**: no new canonical setting. The existing client-local
  `[Graphics] EnemyDropMarker` values retain their names and meaning.
- **Compatibility**: seeds remain byte-identical across clients with different
  marker preferences; only the local presentation changes.
- **Verification**: OpenSpec validation, allocator selftests, Release
  `--rando-selftest`, and targeted F12/OAM/VRAM checks in rooms with conflicting
  enemy marker icons.
