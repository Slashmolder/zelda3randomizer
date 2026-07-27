# Compose shopsanity with cave-entrance shuffle

## Why

`fix-rando-shopsanity-door-identity` normalized `shopsanity` **off** under
cave-entrance shuffle, because four shop interiors are cave-pool members whose
pool entries omit the shop slot ids. That closed a real soundness hole, but it
means the two axes cannot be enabled together. The owner wants both, at ALTTPR
parity: shops shuffled like any other cave, findable behind a random door.

The blocker is that the cave pool's identity unit is **one entry per interior
ROOM, looked up by entrance id**, while shops are per **overworld door**. Four
physically distinct Dark World shops share room `0x10F` and entrance id `0x60`;
two share room `0x112` and `0x58`. So the four Dark World shop doors move
through the shuffle as a single unit, and after a shuffle exactly one door
leads to room `0x10F` — it can present only one of its four shops however it is
keyed. That is why region-rebinding alone could not repair the composition.

ALTTPR does not have this problem because each shop is its own region with its
own entrance, so its ROM writer re-derives each shop's door byte from the
post-shuffle connection (`ALttPDoorRandomizer/BaseClasses.py` `Shop.get_bytes`:
`door_id = door_addresses[entrances[0].name][0] + 1`).

## What Changes

Make the cave pool's identity unit the **overworld door-row group** instead of
the interior room, then let the existing machinery do the rest.

- **Split the four shop-hosting interiors by door row.** Room `0x10F` becomes 4
  entries, `0x112` becomes 3 (two shops + the Dark Chapel door), `0x11F`
  becomes 2 (the shop door + entrance id `0x6B`). Room `0x110` already has a
  single door. Pool grows 40 -> 46. New entries are **appended**, never
  inserted, so every existing interior index keeps its meaning.
- **Key the source lookup by door row.** `Entrance_BuildDoorOverlay` currently
  resolves a door's pool entry with `interior_of_entrance(vanilla[d])`, which
  is ambiguous once entries share an entrance id. It becomes a door-row -> entry
  map. Every other entrance-id -> interior consumer is audited the same way.
- **Give each split entry its own region and locations.** Each shop entry
  carries its shop's three slot ids in `location_ids` and the vanilla logic
  region of *its own door* (the four `0x10F` doors sit in three different
  regions). `Entrance_ApplyRegionOverrides` then binds shop slots to the
  shuffled door's region with no further work — the option (a) that was
  impossible before splitting.
- **Derive shop identity from the layout, not a static door column.** A shop is
  the destination entry the player arrived at, so its door is whichever source
  door row the permutation assigned to that entry. This is ALTTPR's per-seed
  re-derivation, expressed against our own assignment instead of a rewritten
  ROM table.
- **Drop the forced-off normalization** added by the previous change.

## Impact

- **Every cave-entrance-shuffle seed's permutation changes** (the pool grew), so
  the whole cave-shuffle corpus moves and `kGeneratorVersion` bumps. Seeds
  without cave-entrance shuffle stay byte-identical.
- **No persistence format change.** The cave assignment is never stored — it is
  regenerated from `(seed, entrance_axes, entrance_attempt)` by
  `Entrance_ComputeLayout`. The sidecar keeps only the two axis/attempt bytes
  plus an `entrance_digest24` drift check, the share string carries nothing
  entrance-specific, and the discovery bitmaps are already 8 bytes (64 bits) so
  46 fits. Existing entrance-shuffle slots will fail the digest drift check,
  which is the correct outcome for a `kGeneratorVersion` bump rather than
  something to migrate.
- **`cave_arrival_baked.h` is positionally indexed by interior and hand-captured
  (`RandoCaveArrival g_cave_capture[]`, keyed today by
  `Entrance_InteriorOfEntranceId`).** Appending rather than inserting keeps
  entries 0-37 aligned. The six new entries have no captured arrival, which the
  loader already degrades safely (`valid == 0` -> coupled exit). Decoupled mode
  therefore emerges at the entered door for those six until an owner capture
  walkabout fills them in — a tracked follow-up, not a blocker, and never a
  softlock.
- Affected code: `src/rando/shuffle_entrance.{c,h}`,
  `assets/rando/entrance_registry.yaml` + `assets/scripts/gen_entrance_table.py`,
  `src/rando/rando.c` (shop resolution, arrival capture, discovery),
  `src/rando/rando_settings.{c,h}` (drop the normalization),
  `src/rando/rando_save.c`, `src/rando/rando_share.c`, `src/rando/rando_spoiler.c`.
- Player-visible: shops shuffle like any other cave; a random door can lead to a
  shop, and a shop door can lead anywhere.
