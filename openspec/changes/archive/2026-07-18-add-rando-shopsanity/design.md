# Design: Shopsanity

## Context

**As-built fork machinery (verified against worktree source, 2026-07-16):**

- **Locations exist.** `assets/rando/location_registry.yaml` defines 27 regular
  shop slots (ids 237–263, `type: Shop`, named `"<Shop Name> - <0|1|2>"`), 2
  Capacity Upgrade slots (264/265, `type: ShopUpgrade`), and 62 Take-Any slots
  (266–327, `type: TakeAny`) — all carrying `world_state_filter: [retro]`. The
  9 shops: DW Potion, DW Forest, DW Lumberjack Hut, DW Village of Outcasts,
  DW Lake Hylia, DW Death Mountain, LW Death Mountain, LW Kakariko, LW Lake
  Hylia. Generated enums live in `src/rando/location_ids.h`
  (`LOC_Dark_World_Potion_Shop_0` …); type enum `LOCTYPE_Shop=14` /
  `LOCTYPE_ShopUpgrade=15` / `LOCTYPE_TakeAny=16` in `src/rando/rando_logic.h`.
- **They are identity-pinned.** `location_is_prepinned()`
  (`src/rando/rando_placement.c`) returns true for `LOCTYPE_Shop |
  ShopUpgrade | Prize_* | Medallion` unconditionally; the pin pass writes
  `placement_at[k] = loc->vanilla_item_id`. Prepinned slots consume no pool
  item (shared accounting with `BuildItemPool`'s junk-pad target).
- **They are logic-invisible.** `Shop`/`ShopUpgrade`/`TakeAny` sit in
  `REGION_OPTIONAL_TYPES` (`assets/rando_logic_gen.py`), encode
  `region_id=0xFFFF`, and the reachability VM treats that as
  always-reachable. The registry's `region:` field on shop rows is
  descriptive only.
- **The purchase runtime exists.** `SpritePrep_Shopkeeper` (`src/sprite_main.c`)
  spawns slot sprites via `ShopKeeper_SpawnShopItem` (which stores
  `sprite_subtype = pos+1` under rando so the receipt can recover the slot);
  `Sprite_BB_Shopkeeper` dispatches per-kind handlers; `ShopItem_HandleCost`
  checks/deducts `link_rupees_goal` with **hard-coded vanilla price literals
  at each call site**; `ShopItem_HandleReceipt` substitutes the grant through
  `Rando_ShopDispatch(room, entrance, pos, vanilla)` →
  `Rando_DispatchVanillaGrant` → `Rando_ReceiveOrConfirm`. `shop_lookup`
  walks `kRandoShopSlots[9]` `{room, door, loc_base, room_only}`
  (`src/rando/rando.c`; LW Death Mountain is `room_only=true`).
- **Buy-once exists only for Take-Any.** `Rando_TakeAnyLiveSlot` gates spawn
  on `Rando_IsLocationChecked`; regular shop slots re-spawn on every room
  load with NO checked gate — a purchased Retro slot restocks its (vanilla)
  item indefinitely. That is correct for the Retro economy but is exactly
  the gap shopsanity must close for check semantics.
- **Reporting exists.** `src/rando/rando_spoiler.c` collects
  `SpoilerShopRow {loc, item, type, region_id}` and emits JSON `shops[]` +
  a grouped text `Shops:` section whenever shop-class placements exist
  (today: Retro only). `region` is emitted only when `region_id != 0xFFFF`.
- **Icon precedent exists.** Field-item sprites resolve placed items to
  drawable icons via the shared chain (`Rando_VanillaItemForRegistryId`
  primary, then `progressive_to_lttp`) — the draw-must-mirror-grant rule.
  `SpriteDraw_ShopItem` today draws only the 7 vanilla shop kinds from
  static `DrawMultiple` rows (3 price-digit tiles + item big-tiles); take-any
  kinds draw price-less icons; anything else falls back to the potion-bottle
  tile `0x02c0`.

**Upstream grounding (verified at pinned revisions):**
[`sporchia/alttp_vt_randomizer@219fcafd`](https://github.com/sporchia/alttp_vt_randomizer/tree/219fcafd029dab597b8db400efafd8f56f8b4edb)
and [`KatDevsGames/z3randomizer@dcb0a2b4`](https://github.com/KatDevsGames/z3randomizer/tree/dcb0a2b42d14445f7994a0e9e4d63cbecf4b98d3).
ALTTPR mainline has NO shopsanity — `Shop.php`
inventories are code-stocked (`Randomizer::setShops()`; Retro stocks 5 random
shops with `ShopArrow`@80 / `ShopKey`@100 / `TenBombs`@50) and serialized to
ROM tables at `0x184800`/`0x184900`; `Shop::getLocations()` fabricates
wrappers that feed *logic accessibility only* and never enter
`getEmptyLocations()`. The asm runtime (`shopkeeper.asm`) persists per-slot
purchases in SRAM `PurchaseCounts` (96 bytes @ `$7F64B8`) with per-slot
sold-out masks. There is therefore no upstream placement contract to be
faithful to — shopsanity is fork-original; where the fork's model differs
from the upstream *runtime* (we use the checked-location bitmap, not a
purchase-count SRAM block), that is deliberate reuse of shipped fork
persistence.

**Concurrent work:** `add-rando-key-rings-skeleton-key` (in flight on its
branch at authoring time) appends canonical byte `[30]`
(`kSettingsCanonicalLen` 30→31). This change is sequenced AFTER it.

## Goals / Non-Goals

**Goals:**

- The 27 regular shop slots become ordinary open fill locations — progression
  included — behind a default-off `shopsanity` axis, in all four world states.
- Deterministic, spoiler-visible, seed-derived prices; a purchased slot
  permanently reverts to its vanilla item at its vanilla price.
- Real region bindings for shops so reachability, accessibility tiers,
  spheres, hints, and the entrance-shuffle override seam see them truthfully.
- Off = byte-identical placement for Open/Standard/Inverted; Retro moves only
  by the intended region-binding correctness fix (proven scoped by the corpus
  3-way diff).

**Non-Goals:**

- Capacity Upgrade slots stay identity-placed (progressive max-7 purchase
  mechanic; out of scope for v1).
- Take-Any caves stay Retro-only with their existing per-seed selection.
- The Bomb Shop (scripted Big Bomb), the witch's powder trade (NPC check
  147), and Retro's genericKey 4th slot (unlimited economy purchase) are
  untouched.
- No shop-entrance shuffling (shop interiors stay out of the entrance pool,
  as today), no rupee-affordability modeling in logic, no multi-buy /
  replacement-item mechanics (upstream's `max`/replacement model), no new
  SRAM blocks.

## Decisions

### D1. Scope: exactly the 27 existing `LOCTYPE_Shop` slots

No new locations are minted; ids 237–263 are reused as-is, so trackers,
spoiler, customizer, hints, and the checked bitmap need no capacity work.
Alternative — also opening the 2 ShopUpgrade slots — rejected for v1: their
runtime is a vanilla capacity write marked `// rando-exempt: shop subsystem`
with a progressive purchase model that doesn't fit one-shot check semantics.

### D2. Axis: single boolean in reserved canonical byte [29] bit 4

`shopsanity` (`false`/`true`, default `false`; CSV key `shopsanity`, hard
parse error on unknown values, `MARK_SEEN` duplicate guard like every axis).
It packs into byte `[29]` bit 4 (grass bits 0-1, rock bits 2-3 are the only
occupants today; the deserializer's refused-undefined mask shrinks by one
bit). No `kSettingsCanonicalLen` change ⇒ default `settings_hash` unchanged
— softer than the terrain append. Alternatives rejected: a tier enum
(`off/junk/all`, grass-rock style) — with only 27 slots the `junk` tier is a
pure rupee tax with no routing interest, and a two-value axis keeps the UI a
checkbox; a new byte `[31]` — wasteful while [29] has four reserved bits,
and it would collide with keyrings' in-flight [30] append sequencing.

### D3. Activation = conditional world-state filter + conditional prepin

One predicate, `shop_slots_open(settings)` ≡ `shopsanity == true`, consulted
at BOTH existing gates so they cannot drift:

- the world-state filter loop (`place_assumed_fill_attempt` /
  `BuildItemPool`): a `LOCTYPE_Shop` row passes when `shop_slots_open()`,
  regardless of `world_state_filter`; otherwise the Retro-only filter applies
  as today.
- `location_is_prepinned()`: the `LOCTYPE_Shop` arm returns false when
  `shop_slots_open()` (ShopUpgrade stays unconditional).

The junk-pad target and `Placement_SelfCheck` expected counts extend by the
same predicate (the pot-sanity "skip-triple" discipline — open-location loop,
junk-pad target, self-check count move together). Alternative — editing
`world_state_filter` in the YAML to all states and gating only in the placer
— rejected: the filter is also read by spoiler/tracker consumers, and a
YAML-wide flip would activate Retro-only behaviors (e.g. slot emission)
everywhere even when the axis is off.

### D4. Regions: `Shop` leaves `REGION_OPTIONAL_TYPES`; bindings are static

Each of the 9 shops gets a `region:` binding + entry predicate in
`logic.yaml` (dark-world shops behind their DW region's existing
Pearl/world-state gates; LW Death Mountain behind the DM access predicates),
with `logic_parts/inverted/**` overrides where the world assignment flips.
Binding is STATIC (not conditional on the axis): under plain Retro with
shopsanity off, shop slots stop reading as always-reachable in
sphere/accessibility data — an intended correctness fix whose corpus blast
radius is Retro rows only (see Risks). `ShopUpgrade`/`TakeAny` remain
REGION_OPTIONAL. Two authoring traps apply: the `logic_parts` last-wins merge
(diff generated logic before/after; never duplicate a `logic.yaml` entry in a
part file), and the codegen's `region: 0xFFFF` warning allowlist must shrink
in the same commit that adds the bindings.

### D5. Prices: derived stream, never stored, uniform band

`ShopPrice(seed, loc_id)` = salted RNG (house pattern: `seed ^ ASCII salt`,
like `takeany_select`'s `"TakeAny"` salt) → uniform in **{10, 15, …, 250}**
(multiples of 5, 3 HUD digits max, well under vanilla's 500 ceiling).
Uniform across item classes on purpose: pricing progression higher than junk
would leak placement information through the price tag. Derived at
generation for the spoiler and re-derived at slot activation for the
runtime; determinism pinned by a `--rando-selftest` vector. Prices do NOT
enter logic (rupees are farmable — bushes, grass/rock junk, Retro economy;
matches upstream, where shop wrappers feed accessibility only) and do NOT
enter the settings hash/share string (fully derived from `seed`).
Worst-case seed cost ≈ 27 × 250 ≈ 6.7k rupees for a completionist sweep —
accepted as the mode's flavor. ShopUpgrade keeps vanilla 100; checked slots
revert to the vanilla price literal.

### D6. Runtime: checked-gate per frame, revert-to-vanilla, one dispatch path

> **As-built refinement.** The gate moved from the SPAWN site to a shared
> per-frame front-end on the seven kind handlers plus a draw branch (both on
> `Rando_ShopSlotCheckInfo`): no new spawn kind, no per-sprite state, and
> the unchecked check bypasses the vanilla refusal gates (shield-owned /
> bottle / ammo-full — keeping them would make checks missable, the
> location-guard class). `Rando_ShopDispatch` gained the checked-restock
> branch so a re-buy can never re-grant. Everything below this note is the
> original spawn-site sketch, superseded in mechanism, identical in
> behavior.

- **Spawn** (`SpritePrep_Shopkeeper` → `ShopKeeper_SpawnShopItem`): when
  shopsanity is active on the slot's location and `!Rando_IsLocationChecked`,
  spawn a new "check slot" kind carrying the placed item + derived price;
  otherwise spawn the vanilla kind (current behavior — this simultaneously
  covers axis-off, non-rando, and the post-purchase restock). The revert
  keeps red-potion/shield/bomb/arrow shopping alive after the check, exactly
  like grass/rock's revert-to-vanilla-drop.
- **Purchase** (new check-slot handler in `Sprite_BB_Shopkeeper`): A-press +
  `ShopItem_HandleCost(derived_price)` → route through the EXISTING chain
  (`Rando_ShopDispatch` → `Rando_DispatchVanillaGrant` →
  `Rando_ReceiveOrConfirm`) and return — every placement class goes through
  `Rando_ReceiveOrConfirm`, never a vanilla-consolation fallback (the
  Digging-Game bug class). Marking checked happens inside the dispatch as
  today; the slot sprite despawns for the rest of the room visit.
- **No new persistence**: checked state lives in the existing sidecar
  checked bitmap (the same bits Retro shop dispatch already sets — they
  merely become load-bearing at spawn time). Explicitly rejected: porting
  upstream's `PurchaseCounts` SRAM block — redundant with fork persistence.
- Retro + shopsanity: identical slot semantics; Retro's genericKey 4th slot
  and Take-Any redirect spawn paths are untouched (they short-circuit before
  the regular slot loop today and continue to).

### D7. Draw: shared resolver icons + dynamic price digits

Unchecked check-slots draw the placed item via the same resolution chain the
grant uses (registry-primary + `progressive_to_lttp` + the special remaps),
reusing the field-item icon atlas; items without a safe icon draw a generic
merchandise tile (rupee bag), never a vanilla-item lookalike. Price digits
become a dynamic 3-digit draw (the static per-kind `Dmd` rows only encode
the 7 vanilla kinds' fixed prices). OAM check: shop rooms draw ≤3 slot
sprites + the keeper; per-sprite OAM budgets are BYTES
(`((sprite_flags2&0x1f)+1)*4`) — the multi-tile icon + 3 digit tiles need an
explicit region allocation like the field-item draw already does.

### D8. Composition rules

- **Entrance shuffle**: composes. Shop interiors are not in the entrance
  pool (unchanged), so the static region bindings stay truthful; with real
  regions the `g_entrance_region_override` seam becomes *correct* for shops
  if they ever join the pool later.
- **Door shuffle / dungeon chains / pot shuffle / enemy checks**: no
  interaction (shops are surface cave interiors, not dungeon content).
- **Souls**: none of the 9 shopkeepers is in the NPC-soul roster (Witch
  gates only the powder check; BombShopDealer gates Bomb-Shop rows), and
  shopkeepers are not soul-suppressed sprites. No coupling.
- **Traps**: shop slots are excluded from the trap-masquerade eligible set
  in v1 (a bought trap is funny but needs trap-draw + refund semantics —
  Open Question Q2).
- **Accessibility**: with real regions, `items`/`locations` tiers treat shop
  slots like any location. Completionist counts them (27 more checks).
- **Customizer**: shop slots become pinnable like any open location once
  un-pinned; verify the manifest path accepts ids 237–263 when the axis is
  on and refuses them when off.
- **Race mode**: nothing new — prices are seed-derived, and the ZRSR
  suppressed spoiler already hides shop rows with everything else.
- **Hints**: shop locations become hintable named locations (they already
  have registry names); no new hint class.

### D9. Spoiler: price column, all world states

`SpoilerShopRow` gains `price`; JSON `shops[]` rows gain `"price"` (omitted
for identity-placed ShopUpgrade rows), the text `Shops:` section prints
`(NN rupees)`. The sections already emit iff shop-class placements exist, so
non-Retro shopsanity seeds start emitting them naturally; axis-off non-Retro
seeds stay byte-identical. Region emission begins populating automatically
once `region_id != 0xFFFF` (existing conditional).

### D10. UI: one checkbox, ordinary tracker rows

Native window Shuffles block gains a "Shopsanity" checkbox (tooltip: 1-2
durable player facts — slots are one-time checks at seed-random prices; a
bought slot restocks its normal item). No disable-coupling to any axis. The
27 rows are ordinary tracker/reach rows — no gating toggle (pot/terrain
toggles exist because of their thousands of rows; 27 is noise-free), and the
SNES HUD grid includes them as normal locations. Auto-tracker rows carry
their existing `Shop` type tag.

### D11. Versioning & validation

`kGeneratorVersion` +1 (ON-placement is new; Retro sphere data moves via
D4). Corpus: new entries open/standard/inverted/retro × shopsanity plus one
composition row (shopsanity + entrance shuffle) and one goal variant
(completionist + shopsanity for the locations tier); regen via
`bump_rando_corpus.py --apply` with the 3-way diff against fresh-built
unmodified main — expected movement: ONLY the new rows + existing Retro rows
(scoped by D4); any default Open/Standard/Inverted movement is a regression.
`--rando-selftest` gains: price determinism vector, pin/pool accounting with
the axis on (skip-triple), and a shop-slot open/closed count check per world
state. Playtest matrix (the only net for runtime): buy each slot class
(progression / junk / direct-grant), verify restock item+price after
purchase, save/reload mid-shop, Retro + shopsanity, Inverted DW shop, and a
snapshot cold-replay of a purchased-slot save.

### D12. Sequencing with keyrings

This change builds on the post-keyrings baseline (`kSettingsCanonicalLen`
31, byte [30] taken). The randomizer-core delta is authored against that
table; per the reconcile-before-archive discipline, re-diff the delta
against as-merged `main` before `openspec archive` (byte assignments,
`kGeneratorVersion` numbering, corpus manifest version).

## Risks / Trade-offs

- **[Retro digest movement from D4]** Region-binding shops changes Retro
  sphere/accessibility data with shopsanity off. → Intended and scoped;
  the corpus 3-way diff must show ONLY Retro rows moving; if placement
  digests (not just sphere digests) move for Retro, inspect before
  accepting — prepinned slots consume no pool item, so placement movement
  would indicate the reachability change altered assumed-fill routing,
  which is possible (accessibility pruning) but must be explained, not
  waved through.
- **[Restock re-grant surface]** The checked-gate lives at SPAWN; the
  purchase handler must also behave if a stale check sprite survives a
  mark (same-frame double-A-press). → Handler re-checks
  `Rando_IsLocationChecked` before dispatch (belt-and-braces, mirrors
  take-any).
- **[Draw-grant drift]** A shop icon resolved differently from the grant is
  the known field-item bug class. → Single shared resolver call (D7), no
  shop-local item→icon table.
- **[Price/UI legibility]** 3-digit dynamic prices must not collide with the
  item icon tiles on dense layouts. → Reuse the vanilla price-row y-offset
  (y16 rows in the existing `Dmd` layout); playtest all 9 shops.
- **[logic_parts clobber]** Adding 9 region bindings + inverted overrides is
  exactly the last-wins-merge shape that has fired twice before. → Diff
  generated `logic_data.c` before/after; codegen duplicate-id hard-fail
  already exists.
- **[Makefile header staleness]** `kGeneratorVersion` and settings-header
  edits ship stale TUs on partial WSL builds. → `make clean` after header
  edits (standing rule), MSBuild unaffected.
- **[Corpus tooling]** `bump_rando_corpus.py` regenerates only when manifest
  version < `kGeneratorVersion`, needs an absolute `--binary`, and rewrites
  the manifest LF. → Follow the standing gotcha list; restore CRLF.

## Migration Plan

Single feature branch off post-keyrings `main` (`feature/rando-shopsanity`),
phases sized so every phase builds MSVC + WSL gcc `-Werror` with the corpus
green:

1. Settings axis + canonical bit + CSV + UI checkbox (inert — nothing reads
   it yet; hash-neutral at default).
2. Regions: logic.yaml bindings + inverted overrides + REGION_OPTIONAL
   shrink; kGen bump; Retro corpus regen (the scoped movement lands here,
   isolated from placement changes for clean bisection).
3. Placement: conditional prepin/world-filter + skip-triple + price stream +
   spoiler price column; new corpus rows; selftest additions.
4. Runtime: spawn gate + check-slot kind + purchase handler + draw; playtest
   matrix; docs (`docs/randomizer.md` settings table + section).
5. Fresh-eyes audit (standing cadence), reconcile deltas vs as-built,
   archive on branch, squash-merge.

Rollback: the axis defaults off and every runtime branch gates on it; a
revert of phases 3-4 leaves an inert setting, and phase 2 alone is a pure
data change revertible with a second corpus regen.

## Open Questions

- **Q1 (owner)**: Uniform price band {10..250} step 5 — happy with the range,
  or prefer a wider top (vanilla max is 500) / cheaper floor?
- **Q2 (owner)**: Should shop slots be trap-masquerade eligible in a later
  pass (buying a trap at full price)? Excluded in v1.
- **Q3 (owner)**: Tier axis (`junk`/`all`) was rejected for a plain boolean
  (D2) — confirm, or ask for the tier form.
- **Q4 (owner)**: Capacity Upgrade slots as fill locations in a v2 (needs a
  one-shot model replacing the progressive max-7 purchase)?
- **Q5**: Exact region ids + entry predicates for the 9 shops get authored
  against the live region graph in phase 2 — LW Death Mountain in
  particular (which DM sub-region holds its door) needs a source check, not
  a guess.
