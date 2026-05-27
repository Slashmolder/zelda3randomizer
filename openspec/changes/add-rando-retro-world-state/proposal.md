## Why

Phase A's Phase B+ roadmap (`docs/randomizer.md:286-289` original wording, now superseded by the chunked roadmap) and `tasks.md §14.2` deferred Retro world-state to Phase B. The settings-screen world-state picker is gated at Open + Standard per `select_file.c:2520` (Phase A §14.1b re-scope).

**Retro is the shortest Phase B world-state slice.** Per `../alttp_vt_randomizer/app/World/Retro.php` (44 lines, verified), Retro extends Open and forces 4 config flags:
- `rom.rupeeBow = true` (bow consumes rupees as ammo instead of arrows)
- `rom.genericKeys = true` (small keys are inter-dungeon transferable)
- `region.takeAnys = true` (the four "Take Any" shops have non-vanilla content)
- `region.wildKeys = true` (small keys can appear in the wild pool, not pinned to dungeons)

The shop subsystem upstream is in `app/Shop.php` (230 lines, verified) + `app/Support/ShopCollection.php` (64 lines, verified). Grep against `app/Region/Standard/**/*.php` enumerates **42 unique shop entities** across LightWorld + DarkWorld regions, broken down into three classes:
- **`Shop`** — standard shops with purchasable item inventory (Kakariko Shop, Lake Hylia Shop, the Dark World potion shop, Dark World Outcasts shop, the four "DM" shops, etc.).
- **`Shop\Upgrade`** — capacity upgrades (bomb/arrow capacity).
- **`Shop\TakeAny`** — the four "Take Any" caves that only appear in Retro (`20 Rupee Cave`, `50 Rupee Cave`, `Bonk Fairy (Light)`, `Bonk Fairy (Dark)`, `Desert Fairy`, etc. — these are visible in the upstream as `Shop\TakeAny` instances but `region.takeAnys` controls whether the player can enter them).

**Retro is NOT a separate logic graph** — it inherits Open's region structure and modifies item-pool composition + shop dispatch. The Phase A logic graph stays unchanged for Retro seeds; what changes is which locations are in `BuildItemPool` and which sprite-handler call sites route through the dispatcher.

This is the right slice to ship right after the warm-up changes — small, no logic translation, and exercises the world-state picker un-gate pattern that #4a Inverted will reuse.

## What Changes

### Item pool

- **Add shop-purchase locations** to `BuildItemPool` when `settings.world_state == Retro`. Roughly 30-40 shop-purchase slots across the 42 enumerated shop entities (most shops have 1-3 purchasable slots; Take-Any caves add 4-7 more).
- **Pin the 4 Retro config flags** in the seed's effective settings whenever `world_state == Retro`:
  - `rupeeBow = true`
  - `genericKeys = true`
  - `takeAnys = true`
  - `wildKeys = true`
  These flags are NOT exposed as separate settings axes in Phase B (decision recorded in design.md: pin per Retro defaults; expose individually only if Phase C surfaces a use case).
- **Junk-pool padding** adjusts to account for the increased location count — `BuildItemPool` already handles arbitrary `|locations|` via the junk-pad step (per Phase A's pool-construction step).

### Dispatch routing

- **Route shop purchases through the dispatcher** at `src/sprite_main.c:25308` (`ShopItem_HandleReceipt`) — per Phase A audit §0.1.4 enumeration ("Shop item dispatch via ShopItem_HandleReceipt"). The dispatcher resolves the shop slot to the placement-table entry; rupee cost stays under vanilla shop-pricing semantics (Phase B does not randomize shop prices).
- **Add per-slot LOC ids** to `assets/rando/location_registry.yaml` — one entry per shop purchasable slot. Existing 237-location registry grows; the append-only convention preserves Open/Standard digests.
- **Take-Any caves** require a separate dispatch site because Take-Any entry is gated on the `takeAnys=true` config, NOT a standard sprite-handler path. Apply-time discovery confirms whether the existing `ShopItem_HandleReceipt` covers Take-Any or whether a peer entry point is needed.

### Capacity upgrades

- **Bomb / arrow capacity upgrades** (`Shop\Upgrade` instances at `sprite_main.c:11483-11484` and `11520-11521`) are NOT in `app/Region/.../Location\\` form upstream — they're shop-subsystem-only. Phase B Retro keeps them outside the placement pool (their dispatch fires for uniformity but the upgrade is identity-placed; the player still buys the capacity upgrade for rupees as in vanilla). Recorded in design.md.

### Picker un-gate

- **Un-gate Retro** in the settings-screen picker at `src/select_file.c:2520` (per Phase A §14.1b re-scope). This is the parallel un-gate to #4a Inverted; both ship as **ADDED Requirements** in `randomizer-ui` to sidestep archive-sequencing conflict on the Phase A "Settings screen" Requirement.

### Corpus + version

- **`kGeneratorVersion` advances**; regression corpus regenerates via `assets/scripts/bump_rando_corpus.py`. Existing Open + Standard + Inverted (if shipped first) digests should remain byte-identical (Retro branch is gated on `world_state == Retro`).
- **Audit guard remains green**: the only new write sites are inside `ShopItem_HandleReceipt`-style dispatch paths, which are already classified as `grant` in `audit.md` §0.1.4.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-core`: MODIFIED Requirement on `BuildItemPool` (Retro branch + Retro flag pinning + junk-pad accommodation).
- `randomizer-placement`: ADDED Requirement for shop-handler dispatch routing.
- `randomizer-ui`: ADDED Requirement "World-state picker accepts Retro" — un-gates the picker at `select_file.c:2520` without modifying the Phase A "Settings screen" Requirement.

## Impact

- **Code**: `src/rando/rando_placement.c` (`BuildItemPool` Retro branch + flag pinning), `src/sprite_main.c:25308` (`ShopItem_HandleReceipt` dispatch routing), possibly a new Take-Any dispatch site, `src/select_file.c:2520` (picker un-gate).
- **Assets**: `assets/rando/location_registry.yaml` (~30-40 new shop-purchase location ids — append-only adds; existing locations unchanged). Per-shop-slot inventory maps to be authored in a new `assets/rando/retro_shops.yaml` or inline in the existing logic_parts.
- **Effort**: **1 week of focused work.** Small surface, no logic-translation overhead.
- **Regression risk**: `kGeneratorVersion` bumps; corpus regenerates. Open + Standard + Inverted (if #4a shipped first) digests should remain byte-identical (Retro branch is gated on `world_state == Retro`).
- **No dependency on #4a Inverted** — Retro extends Open, not Inverted. Either can ship first.
- **ALTTPR provenance**: 42 enumerated shop entities; full hand-translation discipline per `audit.md §0.10`. Per-shop source-line citations in `audit.md §"Retro shop provenance"` (new section).
