# Fix shopsanity shop identity and its entrance-shuffle composition

## Why

Shopsanity (`shopsanity=True`) opens the 27 regular shop slots (`LOCTYPE_Shop`,
location ids 237-263) as ordinary fill locations. Two independent
generation-soundness defects ship today.

**1. The runtime identifies a shop by the wrong quantity (HIGH).**
`shop_lookup` (`src/rando/rando.c`) resolves a shop from
`(room_low_byte, which_entrance)`. Its door column was copied verbatim from
ALTTPR's `new Shop(..., $room, $door, ...)` constructor args, on the stated
assumption that "door values equal the vanilla overworld entrance ids
(`kOverworld_Entrance_Id`), which is exactly what `which_entrance` holds while
the player stands in the cave." That assumption is false. ALTTPR's door value is
`PreviousOverworldDoor`, which `z3randomizer/doorframefixes.asm`
(`StoreLastOverworldDoorID`: `TXA : INC : STA.l PreviousOverworldDoor`) defines
as the overworld door **row index + 1** — a different quantity from the entrance
id that same routine loads separately into `EntranceIndex`.

The two coincide for only 3 of the 9 shops. Four physically distinct Dark World
shops share entrance id `0x60` (room `0x10F`) and two share `0x58` (room
`0x112`), so `which_entrance` cannot tell them apart. Measured against the real
vanilla tables with the live resolver (`--rando-shop-doorwalk`, added here):
**15 of the 27 shop slots are unreachable from any overworld door, and 6 more
are reached from several doors at once** (slots 246-248 answer to all four
room-`0x10F` doors; 261-263 to both room-`0x112` doors). The placer treats all
27 as fillable, so progression placed in the 15 dead slots is unobtainable.

The shipped `--rando-shop-probe` dump cannot see this: it feeds each table row
its own door id back, validating the table only against itself.

**2. Shopsanity has no derived-rule coupling against cave-entrance shuffle.**
The archived spec asserts the axis composes with entrance shuffle because "shop
interiors remain outside the entrance pool; static region bindings stay
truthful". Both halves are false. Rooms `0x10F`/`0x110`/`0x112`/`0x11F` are
`kCaveInteriors` entries 23/20/21/32, and those interiors' `location_ids` lists
omit the shop slot ids, so `Entrance_ApplyRegionOverrides` never rebinds them. A
`shopsanity=True,shuffle_cave_entrances=True` seed therefore evaluates each shop
slot from its **vanilla** overworld region while the runtime reaches that
interior through a different, shuffled door — the same failure that motivated
`Settings_PotShuffleForcedOff`.

## What Changes

- **Key shops by the overworld door, matching ALTTPR.** Capture the entered
  door's ALTTPR id (`lx + 1`) at the overworld entry hook and resolve shops from
  `(room_low_byte, door_id)`. Persist it in the reserved `g_ram` compat block so
  a snapshot replay-restore keeps it. This makes all 9 shops distinct and all 27
  slots reachable in every non-entrance-shuffled seed.
- **Room-only matching for unambiguous shop rooms.** Rooms hosting exactly one
  shop (`0x0FF`, `0x110`, `0x11F`) match on room alone, mirroring ALTTPR's
  `shop_config & 0x40` fallback.
- **Normalize `shopsanity` off under effective cave-entrance shuffle**, via a
  `Settings_ShopsanityForcedOff` predicate consulted by canonical serialization,
  placement, runtime, and the spoiler alike — the `Settings_PotShuffleForcedOff`
  convention, so the `settings_hash` equals the actually-generated seed.
- **Add `--rando-shop-doorwalk`**, an asset-backed audit that walks every real
  overworld door row through the live resolver and fails on any unreachable or
  colliding shop slot. This is the guard that would have caught defect 1.
- **Correct the archived spec's false composition claim** and bump
  `kGeneratorVersion` + regenerate the corpus (shopsanity seeds move).

## Impact

- Affected specs: `randomizer-shopsanity` (composition rules, shop identity),
  `randomizer-core` (settings normalization).
- Affected code: `src/rando/rando.c` (shop table + lookup), `src/overworld.c`
  (door capture), `src/sprite_main.c` (shop call sites), `src/features.h`
  (reserved RAM byte), `src/rando/rando_settings.{c,h}`, `src/main.c`
  (door-walk audit).
- Placement moves for `shopsanity=True` seeds only; `shopsanity=False` seeds
  stay byte-identical. Requires a `kGeneratorVersion` bump and corpus regen.
- Player-visible: shopsanity seeds that combine with cave-entrance shuffle now
  generate without the shop axis rather than certifying unreachable progression.
