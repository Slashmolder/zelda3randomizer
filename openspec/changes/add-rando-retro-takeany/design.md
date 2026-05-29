# Slice 3b — Retro TakeAny: implementation design

**Status**: PLAN (authored 2026-05-28). Supersedes the parent change's `add-rando-retro-world-state/design.md` §5b scoping pass. Scope locked to **Full subsystem** (generator + runtime in one branch) per user decision 2026-05-28.

All mechanism claims below are grounded in a 2026-05-28 source dossier against three checkouts:
- ALTTPR PHP (placement/ROM-patch): `C:\src\alttp_vt_randomizer\` (MIT).
- z3randomizer ASM (the actual *runtime* take-any behaviour, patched into the ROM): `C:\src\z3randomizer\`.
- this fork: `C:\src\zelda3randomizer\`.

> **Claim-grounding note**: per CLAUDE.md, every ROM/asm/PHP fact in this doc carries a file:line. The single largest prior failure mode on this project is memory-based assertions about ROM mechanics. Two facts in the stub spec are already known-stale and corrected here: (1) the spec says **22** TakeAny shops; the PHP declares **31**; (2) the spec's `randomCollection(5)` regular-shop-extras requirement was *superseded* by Slice 3a's identity-placement simplification — see §6.

---

## 1. The take-any mechanism, end to end (grounded)

Take-any is a **randomizer-invented engine extension**, not a vanilla LttP primitive. In z3randomizer it is a flag on a unified custom-shopkeeper system; the fork has none of it.

End-to-end (ALTTPR build-time → z3randomizer runtime):

1. **Selection (PHP).** `app/Randomizer.php:716-735` picks, per seed, **4 of 31** `Shop\TakeAny` via `randomCollection(4)` → each gets `setActive(true)`, `setShopkeeper('old_man')`, inventory `BluePotion@0` + `BossHeartContainer@1` (both price 0). Then a **5th** distinct still-inactive take-any is chosen via `->random()` and gets a single item: `ProgressiveSword` by default, or `ThreeHundredRupees` when `mode.weapons ∈ {swordless, vanilla}`.
2. **Redirect patch (PHP→ROM).** Each active take-any's `writes` array (e.g. `[0xDBBED => [0x58]]`) writes a **destination entrance index** into the overworld-door→room redirect table at `0xDBB73 + (door_id − 1)` (`app/Shop.php:67-83`; verified `0xDBBED − 0xDBB73 = 0x7A = 0x7B − 1`). The three destination values are host-room entrances: `0x58`→room 0x112, `0x60`→room 0x10F, `0x46`→room 0x11F (general-store rooms).
3. **Shop record (PHP→ROM).** `app/Rom.php:1104-1144` writes an 8-byte ShopTable record `[id, roomID-lo, roomID-hi, door_id, 0, config, shopkeeper, sram_offset]` + inventory rows. A take-any reserves **exactly 1** `PurchaseCounts` SRAM slot (`Rom.php:1119-1121`), vs one-per-item for normal shops — the whole cave is a single "taken / not-taken" gate.
4. **Runtime entrance redirect (asm).** Vanilla `LDA $1BBB73,X : STA $010E` (X = OW door index) loads the (now-patched) destination entrance into `$010E` = `which_entrance`; player lands in the host room at the entrance-specific spawn (`doorframefixes.asm:8-12`, `hooks.asm:2081`).
5. **Runtime identification (asm).** `shopkeeper.asm` (`SpritePrep_ShopKeeper`) scans ShopTable for `roomID == RoomIndex` **and** `PreviousOverworldDoor == door_id` (door compare skipped when config bit `0x40` set). `PreviousOverworldDoor` is the **source** OW door (`X+1`), preserved independently of the redirect. **This (room, source-door) pair is how one host room serves many distinct take-anys.**
6. **Runtime suppression (asm).** On load, if `ShopType & $80` and `PurchaseCounts[ShopSRAMIndex] != 0`, draw nothing (`shopkeeper.asm:234-239`) — non-farmable.
7. **Runtime free grant + lock (asm).** Touching either item: skip rupee cost (`ShopType & $80`, `shopkeeper.asm:379,394`), `Link_ReceiveItem` (`:398`), then `ShopState |= $07` + `PurchaseCounts[ShopSRAMIndex] = 1` (`:411-413`).

### What the fork already has
- The 3 host rooms 0x10F/0x112/0x11F with multiple entrances and correct spawn points. **NOTE (review correction):** the per-room `dungeon-NNN.yaml` files are *extracted build artifacts* (produced from the ROM by `assets/extract_resources.py`), not committed source — they exist in a built tree but not a fresh clone/worktree. Ground room/entrance facts against `extract_resources.py` / the ROM entrance tables, not against fabricated `assets/dungeon/*.yaml` paths.
- `which_entrance` at `g_ram+0x10E` = ALTTPR `$010E` (`src/rando/rando.h:250`).
- The overworld cave-load site: `Overworld_UseEntrance` sets `which_entrance = kOverworld_Entrance_Id[lx]` at `src/overworld.c:3340`. **`lx` is a row index into the 129-entry parallel `kOverworld_Entrance_{Pos,Area,Id}` tables (from `LookupInOwEntranceTab2`), NOT the door id.** The door id is the table *value* `kOverworld_Entrance_Id[lx]` (== ALTTPR `PreviousOverworldDoor` = `X+1`); the fork already uses `kOverworld_Entrance_Id[lx] - 1` at `overworld.c:3333` for the tagalong gate, and the regular-shop dispatch keys on `which_entrance` (= `kOverworld_Entrance_Id[lx]`) per `rando.c:470-476`.
- A `(room, which_entrance, pos)` rando-dispatch pattern for the 9 regular Retro shops, already mirroring the ShopTable room+door match incl. the room-only `0x40` case (`src/rando/rando.c:470-523`).
- The rando vanilla-grant plumbing: `Rando_DispatchVanillaGrant` / `Rando_ReceiveOrConfirm` (`src/rando/rando.c:371-419,733-739`).

### What must be built (does not exist in the fork)
- **A1. Per-seed overworld entrance redirect** — re-point an active take-any cave's OW door to a host-room entrance, while preserving the source door for disambiguation.
- **A2. Take-any presentation** — in the host room, present the take-any reward(s) (an old-man / shopkeeper) instead of the vanilla general store, granting **free**.
- **A3. Persistent per-cave "taken" bit** — survive save/reload; non-farmable.
- **A4. Dispatch + disambiguation** — `(host_room, door_id) → LOC_id`, granting the placed item via the existing rando dispatch.
- **A5. Generator** — `LOCTYPE_TakeAny`, the 31 cave location entries, the deterministic `randomCollection` selection, kGeneratorVersion bump, corpus regen.

---

## 2. Architecture decisions (locked unless review overturns)

### D1 — Runtime redirect: in-place `which_entrance` override + captured source door (NOT asset-table mutation)
Hook `Overworld_UseEntrance` at `src/overworld.c:3340`. **The disambiguation key is the OW door row-index `lx`** (= ALTTPR's `X` in `LDA $1BBB73,X`), captured as `door_id = lx + 1` (= ALTTPR `PreviousOverworldDoor`). This is the bulletproof identity: verified against all 31 caves, `door_id − 1 == (write_addr − 0xDBB73) == X`, so `lx == door_id − 1`. We key on `lx` (the table *row*), **not** on `kOverworld_Entrance_Id[lx]` (the vanilla *destination* entrance — which may collide across caves that share a fairy-pond room, and is the wrong quantity for take-anys even though it happens to be the right key for the already-working regular-shop path). `lx` is in scope at the hook (`int lx = LookupInOwEntranceTab2(pos)`, `overworld.c:3329`).

```c
// at overworld.c:3340, lx already computed at :3329
which_entrance = kOverworld_Entrance_Id[lx];        // vanilla default (unchanged)
g_rando_takeany_door_id = 0;
if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
    Rando_GetActiveWorldState() == kWorldState_Retro) {
  uint8 host = Rando_TakeAnyHostByDoorIndex(lx);    // active take-any with (door_id-1)==lx? else 0
  if (host) {
    g_rando_takeany_door_id = (uint8)(lx + 1);      // = ALTTPR door_id; disambiguation key
    which_entrance = host;                           // land in host room
  }
}
```
The fall-hole path (`Overworld_GetPitDestination`, `overworld.c:3274`) is *not* a take-any source and needs no hook (verify in R1: no take-any cave is a pit). `g_rando_takeany_door_id` resets to 0 on every entrance so a stale value can't mis-key a later normal-shop visit (task R2.1). Host-room dispatch then matches on `(dungeon_room_index, g_rando_takeany_door_id)`.

> **Empirical gate (R1):** the chain `fork lx == ALTTPR X` rests on `LookupInOwEntranceTab2` returning the same row index ALTTPR's OW scan uses. Verified by arithmetic + faithful-port assumption; R1 forces one known cave (20 Rupee Cave, door 0x7B → lx 0x7A → host 0x58/room 0x112) and confirms in-game that the player lands in the host room and `door_id` round-trips before wiring all 31.

> **Rationale vs ALTTPR's asset-table patch.** ALTTPR mutates the redirect table at `0xDBB73+door-1`. We could equivalently mutate the loaded `kOverworld_Entrance_Id` asset copy at game start. We reject that: (a) it loses the source door (post-redirect `which_entrance` = host, so disambiguation would need a *separate* RAM byte the fork doesn't maintain); (b) in-place override at the single read site is smaller, reversible, and keeps all take-any logic in `src/rando/`. Capturing `lx+1` at the override site gives the disambiguation key for free.

### D1b — Host-room dispatch collision (review BLOCKER): the host rooms ARE randomized regular shops
Critical interaction the first draft under-specified: each host room is *itself* an active Retro regular-shop slot. After redirect `which_entrance` becomes the host entrance, e.g. `0x58` → room 0x112 = **Light World Lake Hylia Shop** (`kRandoShopSlots {0x12,0x58,261}`, `rando.c:498`); likewise `0x60`→0x10F (DW shops, low 0x0F) and `0x46`→0x11F (LW Kakariko Shop, slot 258). So a take-any visitor lands in the *same room+entrance* as a normal shopper, and:
- `SpritePrep_Shopkeeper` (`sprite_main.c:7927-7975`) spawns the 3 regular shop items keyed purely on room low-byte (0x0F→j0, 0x12→j5, 0x1F→j8).
- `ShopItem_HandleReceipt` → `Rando_ShopDispatch(room, which_entrance, pos)` (`sprite_main.c:25937`) would match the regular shop LOC.

**Therefore the take-any path must, when `g_rando_takeany_door_id != 0`:** (1) in `SpritePrep_Shopkeeper`, suppress the room-keyed regular-shop item spawns and instead spawn the take-any reward item(s) at price 0; (2) in `ShopItem_HandleReceipt`, short-circuit *before* `Rando_ShopDispatch` and route to `Rando_TakeAnyDispatch` instead. The disambiguation is `door_id`, never the (shared) host entrance. This collision is an explicit R1 acceptance criterion.

### D2 — Presentation: extend the host room's shopkeeper into a take-any mode
When `g_rando_takeany_door_id != 0` and `(dungeon_room_index, door_id)` maps to an active take-any LOC, the host room must show the take-any reward(s), free, and suppress the vanilla general-store inventory. The host rooms already carry a `BB-ShopMan` sprite. Two candidate implementations — decide in the runtime spike (task R1):
- **D2a (preferred): take-any branch on the existing shop-item path.** Reuse `Sprite_BB_Shopkeeper` / `ShopItem_*` + the existing `Rando_ShopDispatch` shape, adding a take-any sub-path that (i) skips the rupee cost, (ii) grants via `Rando_DispatchVanillaGrant((host_room, door_id, pos)→LOC)`, (iii) sets the taken bit, (iv) hides slots when already taken. Lowest new-surface; reuses dispatch + receipt plumbing.
- **D2b (fallback): dedicated take-any sprite/handler** modelled on `Sprite_BottleVendor` (0x75, the existing two-item-choice + receipt sprite, `sprite_main.c:6328-6396`) if the general-store sprite can't be cleanly suppressed/re-skinned.

The "old man" graphic (ALTTPR `shopkeeper='old_man'` → sprite 0xE2) is **cosmetic**; v1 may keep the general-store keeper graphic and still be functionally correct. Flagged as a polish item, not a correctness blocker.

### D3 — Take-any rewards are FIXED / identity-placed (NOT shuffled into the item pool)
ALTTPR sets take-any inventory via `addInventory(...)` writing fixed bytes to ShopContentsTable; the items are **not** added to the main shuffle pool `$this->items`. The cave *gives* BluePotion/BossHeart/sword-or-rupees; it is not a fill target for arbitrary items. This also matches Slice 3a's regular-shop choice (`vanilla_pin = true`, no pool add, `rando_placement.c:910-923`).

**Therefore: NO `BuildItemPool` additions for take-any** (this overturns the parent design.md §2(b), which added `BottleWithBluePotion×4 + BossHeartContainer×4 + sword/rupee` to the pool — that was an unverified outline, not shipped). The active take-any LOC slots are identity-placed to their fixed reward. **OQ1 RESOLVED (review, 2026-05-28):** confirmed against ALTTPR — take-any inventory is set via `addInventory` (`Randomizer.php:716-735`) and `Shop::getLocations()` pre-`setItem`s the fixed item (`Shop.php:144-162`); the items never enter the fill pool. Identity-place is correct.

### D4 — Generator encoding: Option B (active-only), with stable reserved IDs
Per parent design §5b recommendation. Register location IDs for all 31 caves (reserve a stable, contiguous block), each `world_state_filter:[retro]`. Only the **active** caves' slots emit into the placement table for a given seed; the ≤9 active slots (4×2 + 1×1) are identity-placed. Inactive caves produce **zero** placement entries (spec scenario "exactly 5 TakeAny entries… not 22/31"). This avoids dead slots / unreachable checks (the documented `fork_dispatch_gaps` / `logic_vs_runtime_gap` anti-pattern).

> **ID reservation.** Current max LOC id is 265 (3a; review-confirmed). Reserve **266–296** for 31 caves at **1 id per cave** (the cave's *first/only* reward slot), OR 266–327 for 31×2 if slot-1 (BossHeart) needs a distinct LOC. Decide in task G2 after confirming whether the spoiler/tracker needs per-slot granularity. Default: **1 LOC per cave**, with the cave's reward(s) attached to that single location's grant (simpler, and the runtime grants both items from one cave-visit anyway — the asm `ORA #$07` locks all slots at once, and `Rando_MarkLocationChecked` on that one LOC sets the taken state per D5). *(Open question OQ2.)*

### D5 — Persistent taken-bit: reuse the existing `g_rando_checked_bitmap` (review correction)
The taken state must persist across save/reload (the cave is empty on revisit). **Do NOT invent a new bitmap** (the first draft proposed a sidecar bitmap — over-engineering). The fork already has a persistent, serialized, **location_id-keyed** `g_rando_checked_bitmap[64]` (`rando.h:295-296`, `rando.c:630-645`) covering all 512 LOC ids and round-tripping through save/load (`rando_save.c:213,245-246`). "Take-any cave taken" is *exactly* "this take-any LOC id is checked" — a take-any is collected once and never re-collectible, identical semantics to every other location. So: set via `Rando_MarkLocationChecked(loc_id)` on grant; on host-room load, if `Rando_IsLocationChecked(loc_id)` present nothing. This consumes zero header bytes and dissolves the cave-index-vs-loc-id keying hazard. **OQ3 RESOLVED** — no new persistence needed.

### D6 — Deterministic `randomCollection` replication
ALTTPR's `randomCollection($n)` (`app/Support/Collection.php:71-78`) repeatedly `get_random_int(0, count-1)` + `array_splice` — i.e. pick-without-replacement, **not** shuffle-then-take. We are NOT bit-compatible with PHP's `random_int` RNG (the fork pins `xoshiro256**` per `randomizer-core`), so byte-identity with *ALTTPR* seeds is impossible and not required. The spec's determinism requirement is **self-consistency**: same `(settings, seed_u64)` → identical selection on all platforms. Implementation:
- Replicate the *algorithm* (pick-without-replacement over an ordered candidate list) using the fork's existing seeded RNG, consuming RNG state in a fixed order: **TakeAny selection (4 + 1) first, then any regular-shop selection** (§6).
- The candidate list order must be a fixed, documented enumeration (the 31 caves in a canonical order — propose: ALTTPR region-declaration order, recorded in the registry).
- This is the corpus-determinism-sensitive piece — task G4 includes a dedicated second-regen byte-identity check (`stamp_normalization_pattern` memory).

---

## 3. The 31 caves (authoritative table — to be generated, not hand-typed)

The 31 `Shop\TakeAny` declarations (Standard region tree; Inverted has 0) with `(name, door_id, host_room, host_entrance)` are enumerated in the 2026-05-28 dossier. **Task G1 will derive this table programmatically** from `app/Region/Standard/**` (parse the constructor args + `writes` map) rather than transcribing by hand, then commit it as a generated artifact with the PHP source line per row. Hand-transcription of a 31×4 numeric table is exactly the off-by-one risk class flagged in `canonical_size_coupling` / audit history.

Spot-checked rows (for review sanity only; not the source of truth):
| name | door_id | write addr | dest entrance | host room |
|---|---|---|---|---|
| 20 Rupee Cave | 0x7B | 0xDBBED | 0x58 | 0x112 |
| Light Hype Fairy | 0x6C | 0xDBBDE | 0x58 | 0x112 |
| Lake Hylia Fortune Teller | 0x73 | 0xDBBE5 | 0x46 | 0x11F |
| Dark Death Mountain Fairy | 0x70 | 0xDBBE2 | 0x58 | 0x112 |

---

## 4. Runtime data flow (target)

```
OW cave step
  └─ Overworld_UseEntrance (overworld.c:3340)
       Rando_TakeAnyHostByDoorIndex(lx):       // key on row-index lx, NOT the destination value
          active take-any this seed?  → host entrance + set g_rando_takeany_door_id = lx+1
       which_entrance := host
  └─ Dungeon load → host room (0x112 etc.), entrance-specific spawn
  └─ Shopkeeper prep in host room:
       if g_rando_takeany_door_id && taken-bit clear:
          present take-any reward(s) free (D2)
       else if taken-bit set: present nothing
       else: vanilla general store (non-take-any visitors)
  └─ Player touches reward:
       Rando_TakeAnyDispatch(host_room, door_id, pos):
          LOC = takeany_lookup(...)  →  Rando_DispatchVanillaGrant(LOC, …)
       free (no rupee charge); set taken-bit (D5)
```

---

## 5. Generator changes

1. **`LOCTYPE_TakeAny = 16`** — append-only in `assets/rando/logic.schema.yaml` + `assets/rando/rando_logic_gen.py` (mirrors how 3a added `LOCTYPE_Shop=14` / `ShopUpgrade=15`; the C side reads it in `rando_placement.c` near line 891-923).
2. **31 location entries** in `assets/rando/location_registry.yaml` (block 266–296 per D4/OQ2), `type: TakeAny`, `world_state_filter: [retro]`, region = the cave's ALTTPR region, predicate = the region's access rule (logic graph — most are trivially reachable; Good Bee Cave / Mire / DM caves need their region gates). Each row cites its PHP source line.
3. **`logic.yaml` / `logic_parts`** region bindings for the take-any locations (heed the CLAUDE.md "last-wins merge" trap — diff before adding; do not duplicate existing names).
4. **Selection in `BuildItemPool` / placement setup** (`rando_placement.c`): replicate `randomCollection(4)+1` over the 31-cave candidate list (D6); mark the chosen 5 caves active; emit only active caves' slots; identity-place rewards (D3). 5th reward = sword vs 300-rupees by `mode_weapons` (`kModeWeapons_Swordless || kModeWeapons_Vanilla → Rupee300` else `ProgressiveSword`).
5. **`kGeneratorVersion` 35→36**; corpus regen via the deterministic runner (no time/budget param); second-regen byte-identity check; stamp normalization (`stamp_normalization_pattern`).
6. **Non-Retro byte-identity**: all changes gated on `world_state == kWorldState_Retro`; Standard/Open/Inverted corpus digests must be unchanged. Verify with a pre/post diff of non-Retro digests.

---

## 6. OPEN DECISION — regular-shop `randomCollection(5)` reconciliation

The `randomizer-placement` spec for *this* change requires "5 of 9 regular shops gain ShopArrow/ShopKey/TenBombs extras via `randomCollection(5)`". **But Slice 3a already shipped regular shops as 27 fixed identity-placed slots with NO pool extras and NO per-seed selection** (`rando_placement.c:910-923`, "the randomization is that the player must find shops + pay rupees… NOT that shop inventory changes"). The two are in direct conflict.

> **Spec item-list is also inaccurate (review):** `Randomizer.php:743-749` adds `ShopArrow` **only if `rom.rupeeBow`** and `ShopKey` **only if `rom.genericKeys`** — both out of scope (design §8); only `TenBombs@2` is unconditional. So the spec's "ShopArrow+ShopKey+TenBombs triple / 15 entries" is wrong for base Retro. The amendment in option B must correct this too.

Three options (recommend **OQ4-B**; final call needs the reviewer + possibly the user):
- **A — Implement the spec literally.** Add `randomCollection(5)` selection + extra slots to 5 of 9 shops. Changes shipped 3a regular-shop corpus + location counts. Larger blast radius; re-litigates a closed slice.
- **B — Keep 3a's simplification; amend the 3b spec (recommended).** Treat regular-shop inventory as out-of-scope-already-shipped; this change does TakeAny only. Update `specs/randomizer-placement/spec.md` to record that the regular-shop extras were folded into 3a's identity-placement model, with the ALTTPR divergence documented. Smallest, honest, no corpus churn on regular shops.
- **C — Hybrid.** Keep regular shops identity-placed but additionally emit the 5×3 extra *locations* (still identity-placed to ShopArrow/ShopKey/TenBombs) without per-seed selection. Adds locations without RNG; partial spec compliance.

This must be resolved before G-tasks touch the regular-shop pool, because it determines whether the regular shops are touched at all.

---

## 7. Risks

- **R-keystone (entrance redirect / door identity):** D1 keys on the OW row-index `lx` and captures `door_id = lx+1`. The `door_id − 1 == write_offset == lx` relation is now verified across **all 31** caves (not just spot-checks). Task R1 is a 1–2 cave runtime spike confirming `fork lx == ALTTPR X` in-game (player lands in host room, `door_id` round-trips) before wiring all 31.
- **D1b host-room/regular-shop dispatch collision (BLOCKER):** each host room IS an active randomized regular shop (0x112=Lake Hylia 261, 0x10F=DW shops, 0x11F=Kakariko 258). The take-any path must suppress BOTH `SpritePrep_Shopkeeper`'s room-keyed spawns and `Rando_ShopDispatch`, gating on `g_rando_takeany_door_id != 0`. Disambiguation is the captured `door_id`, never the (shared) host entrance. R1.1 acceptance.
- **Normal-shop suppression:** the host rooms are real shops reachable by their *own* entrances (door_id==0). The take-any branch must fire ONLY when arriving via a take-any source door, never for a normal shopper.
- **Spec internal consistency:** the two ADDED spec files describe Fisher-Yates and an inaccurate regular-shop item list; V4 amends them to match the grounded mechanism (pick-without-replacement; TenBombs-only). Until then the change ships code contradicting its own spec.
- **Corpus determinism:** D6 RNG ordering must be fixed and documented; any reordering silently changes every Retro digest.
- **Playtest dependency:** A2/A3 are only fully validated in-game (the user's loop). Plan delivers a debug aid (force-activate a known cave) to make playtest deterministic.

---

## 8. Out of scope
- Bit-for-bit RNG parity with ALTTPR/PHP seeds (impossible; not required — §D6).
- The "old man" cosmetic sprite swap (polish; D2).
- Retro's other runtime flags (`rom.rupeeBow`, `rom.genericKeys`, `rom.wildKeys`) — parent design §4 Risk 8 / task #84, separate work.
- Inverted take-anys (ALTTPR declares 0).
