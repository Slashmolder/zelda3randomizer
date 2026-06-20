# Pot Sanity — design

Grounded in direct reads of the as-built engine and randomizer, plus a count
pulled from `zelda3_assets.dat`, and revised after a second source-cited review
round (findings R1–R13 below are folded into the decisions). Decision labels (D1…)
are referenced from `tasks.md`.

## Context & grounded facts

- **Pots are BG-layer tiles**, not sprites and not chests. A dungeon pot is a 2×2
  tilemap cluster drawn by `RoomDraw_SinglePot` (`src/dungeon.c:3651`, which stores
  the `0x2000` BG-half bit into the tile position) from room object opcodes (single
  `Pot` `:1887`, and the `Fake Pot [U-D]`/`[L-R]` runs `:1017`/`:1078` that expand
  to N pots). Lifting/smashing routes through **`RevealPotItem` (`src/dungeon.c:5850`)**,
  which resolves "what is under this pot" into `dung_secrets_unk1`, consumed by
  `Sprite_SpawnSecret` (`src/sprite.c:1071`) via `kSpawnSecretItems[22]`.
- **`RevealPotItem` has THREE callers** (verified): `Dungeon_LiftAndReplaceLiftable`
  (`:5791`, the lift path), `ThievesAttic_DrawLightenedHole` (`:5805`, a `0x2020`
  lightenable-hole — NOT a pot), and `HandleItemTileAction_Dungeon` (`:5832`, the
  sword-break path). The hook (D3) must be safe under all three.
- **Vanilla contents** live in asset `kDungeonSecrets` (`g_asset_ptrs[50]`): a
  320-room offset table + per-room `{pos_lo, pos_hi, contents}` records. Content
  byte: `1..22` = drop items, `0` = Nothing, `4` = Random, `0x80/0x82/0x84/0x86/0x88`
  = Hole/Warp/Staircase/Bombable/Switch (structural, not loot). Content `8` (small
  key) **skips** the transient `pots_revealed_in_room` mask (`:5871`) and the spawned
  0xe4 small-key sprite increments key counters (`src/sprite.c:1428`) with **no
  persistent per-pot "key taken" flag** — so vanilla key-pots are not safely
  re-enterable as a check by themselves (R3, D3).
- **Counts (PROVISIONAL — independent decode of asset 3 room objects + asset 50
  secrets; the authoritative numbers are produced AND self-asserted by
  `gen_pot_tables.py`, D2):**
  - **835** single `Pot` objects across 164 rooms (maximal pot-object set).
  - **643** secret records. **Loot (item) drops** — Heart 215, Magic 85, Bomb 83,
    BlueRupee 78, Arrow 62, GreenRupee 51, BigMagic 17 (591) **+ 19 small keys** =
    **~610 loot pots** (the `Contents` tier). **Excluded as non-loot:** Cucco ×3
    (a creature spawn, not an item — D2 content policy) and **30 structural** (1 Hole,
    29 Switch). (An earlier "~613" folded the 3 Cuccos into drops.)
  - Object-count vs record-count are decoded independently and not yet
    cross-referenced; "empty/non-loot" is approximate: ~192 pots have no record, plus
    the excluded creature/structural pots → roughly **~225 non-loot/empty pots**
    (`~835 − ~610`). The generator resolves the exact record↔position mapping and the
    per-tier counts; all figures here are provisional.
  - **16** `Fairy Pot` objects, **structural-secret pots**, and **creature-spawn pots**
    (Cucco etc.) are all excluded; the counts are generator-authoritative.
  - Overworld has **no true pots** (only bushes/rocks, 726 secrets) — out of scope.
- **Vanilla does not persist per-pot state.** `pots_revealed_in_room` (`$7EF580`) is
  wiped on every room entry (`:8853`); pots respawn. Pot Sanity supplies its own
  persistent checked bit via the rando checked-location bitmap.
- **Capacity walls (verified — there are TWO, 512 AND 1024).** Today: 328 locations.
  512-sized: `kRandoCheckedBitmapBytes`, the placer `[512]` arrays, `kDigestLocalCap`,
  `kReachabilityMaxLocations`. **1024-sized location-id consumers**: `auto_tracker.c`
  `s_loc_type[1024]` (`:331`), `customizer.c kCustomizerLocIdProbeMax 1024` (`:25`),
  `tracker_windows.cpp`/`rando_reach_panel.cpp` `s_loc_region[1024]`/`s_loc_type[1024]`,
  `rando_window.cpp kSpoilerMaxRows 1024` (`:1323`), `rando_snapshot_tail.c` `raw[1024]`
  + `> 1024` reject (`:331,:338`). `328 + 835 = 1163` exceeds **both** caps.

## D1 — Tiered scope; static full registry + active filter (R1)

`pot_shuffle` is an enum (`Off` 0 / `Keys` 1 / `Contents` 2 / `All` 3), default `Off`.
Every liftable, non-excluded pot gets a **stable, append-only LOC ID** (the maximal
~835 set). **Registry model (decided): the static registry and `kRandoLocationsCount`
GROW to ~1163; there is no "baseline count" claim.** The tier is a generation-time
filter realized as a **skip in the open-location/junk-pad/reachability loops**
(mirroring the inactive-Take-Any skip, `rando_placement.c:1332`). Off-determinism
(D9) comes from that loop skip, NOT from the count staying small. Every
location-id-keyed consumer (placement, reachability, UI/tracker, customizer,
spoiler, snapshot) must therefore be sized for and/or filter against the full
registry (D5). Out-of-scope pots get the `0xFFFF` sentinel and are pure vanilla.

Tier counts (provisional, generator-authoritative): `Keys` ~19, `Contents` ~613,
`All` ~835 (`keys ⊆ contents ⊆ all`). A mid-size **subset tier** (~25-50%) between
`Contents` and `All` is documented future work.

**Exclusions (generator maintains + asserts the list):** Fairy Pots;
structural-secret pots (Hole/Warp/Staircase/Bombable/Switch); the **Chris-Houlihan
room**; **boss arenas** and the pinned-boss environment rooms (Blind/Kholdstare/
Trinexx) and the Agahnim/Ganon arenas; other cutscene/triggered rooms whose pots are
decoupled from normal reachability. The generator hard-asserts no excluded room
emits a LOC.

## D2 — Build-time enumeration: `gen_pot_tables.py` (R11)

A committed generator (mirroring `gen_enemy_shuffle_tables.py`/`chest_table.gen.bin`)
emits, deterministically:
1. **Registry rows** for all in-scope pots (append-only IDs 328…), names
   `<Dungeon> <Room> Pot <n>`, a new `Pot` location type (D11 migration).
2. **`pot_table.gen.bin` → `src/rando/pot_lookup.h`**: a **sorted** `(dungeon_room,
   tile_position) → LOC` table for binary search.
3. **Per-pot classification**: tier membership + vanilla content.
4. **Logic entries** (D8).

It MUST **expand the fake-pot run opcodes (`0x95`/`0xBC`) into their final liftable
positions**, compute `tile_position` exactly as `RoomDraw_SinglePot` does (the full
16-bit `pos4` including the `0x2000` BG-half bit), and **verify each emitted pot
corresponds to a `0x1010`-class liftable replacement-tile entry** (so a mis-decoded
object can't create a phantom pot). It **asserts its own invariants** (R/`CLAUDE.md`
"generate, don't transcribe"): every pot has a region; `(room, tile_position)` is
unique across ALL pots incl. empties (not leaning on the secret table, which only
guarantees uniqueness for secret-bearing pots); tiers nested; classification
exhaustive; secret records cross-referenced against pot positions to COMPUTE each
tier's count; no excluded room emits a LOC. Identity = `(dungeon_room_index,
tile_position)` — fixed geometry, save-stable.

**Per-content-byte policy (the generator SHALL define one outcome for every value).**
`kDungeonSecrets` content `1..22` maps through `kSpriteDropToNameIdx` to a spawned
sprite, which is NOT always loot. Policy: rupees / bombs / arrows / hearts / magic /
big-magic / fairy / small-key → map to the item-registry id (the pot's vanilla item,
used for the checked-pot fall-back and the dispatch `vanilla_registry_id`);
**creature/enemy spawns (Cucco `14`, RockCrab `2`, Bee `3`, soldiers `15/17`, …) →
EXCLUDE as non-loot**; `0` → empty pot (`All` only); `4` (Random — nondeterministic
at spawn, `sprite.c:1077`) and `0x80+` structural → EXCLUDE. The generator SHALL
ASSERT that any content byte it cannot classify as loot/empty does not appear in an
in-scope pot, so a new/unmapped value fails the build rather than shipping a wrong
vanilla item. In the shipped US data the in-scope content bytes are only
`1,7,8,9,10,11,12,13` (loot) + `14` (Cucco, excluded); `4` does not appear.

## D3 — Runtime dispatch: precise contract, all callers, suppression (R3, R8, R9)

Hook at the **TOP of `RevealPotItem` (`:5850`), before its secret-table scan** — a
no-record pot returns at `:5859`, so a later hook misses the `All`-tier empty pots.
The hook fires for all three callers; for the `ThievesAttic_DrawLightenedHole`
(`0x2020`) caller the `(room, pos4)` lookup finds no registered pot and the hook is
**inert by construction** (lookup-gated). The exact sequence (matching existing grant
sites, e.g. `rando.c:978`):

Three **explicit** branches (the inactive/Off case MUST NOT run any suppression):
1. **Pure vanilla** — `loc = Dungeon_GetPotLocation(dungeon_room_index, pos4)` returns
   none, OR rando inactive, OR `loc` not in the active tier: return immediately and let
   the vanilla path run untouched. Out-of-scope / Off pots — INCLUDING vanilla key-pots
   — are byte-identical to vanilla; NO suppression.
2. **Active + checked** (`Rando_IsLocationChecked(loc)`): item-pots take the vanilla
   re-drop; key / one-shot pots are explicitly suppressed (below). No re-grant.
3. **Active + unchecked** — grant: `uint8 lttp = Rando_DispatchVanillaGrant(loc,
   pot_vanilla_registry_id, pot_vanilla_lttp_code)` (calls `Rando_OnLocationCheck`
   internally, marking checked BEFORE the lookup `rando.c:86`; direct-grant classes
   return `kRandoLttpSkip`) → `Rando_ReceiveOrConfirm(lttp, placed_item_id)` delivers it
   (direct-grant cue / `Link_ReceiveItem`). **`ITEM_Nothing` needs its OWN dispatch
   branch:** `Rando_DispatchVanillaGrant` returns `vanilla_lttp_code` only when `placed
   == vanilla_registry_id` (`rando.c:622`) and has no `ITEM_Nothing` case, so add
   `if (placed == ITEM_Nothing) return kRandoLttpSkip;` and have the caller show the
   "nothing" cue (keyed on `g_last_dispatched_item_id == ITEM_Nothing`) — otherwise an
   empty pot mis-dispatches the pot's vanilla code. Then suppress the vanilla secret
   across every break path and RETURN — never fall through.

**Suppression contract (R9):** the hook SHALL reset its one-lift "granted" flag at
the **top of every `RevealPotItem` call** and the flag SHALL be consumed only by the
matching sword-break block (`:5836-5838`, which OR's `dung_secrets_unk1 |= 0x80` and
spawns smashed-terrain AFTER `RevealPotItem` returns). Zeroing `dung_secrets_unk1`
alone is insufficient on the sword path.

**Checked-pot behavior (R3 — corrected):** a checked pot is NOT a naive "fall back to
vanilla." Branch on the pot's known vanilla content:
- **Item-pot** (heart/rupee/magic/…): vanilla re-drop is allowed (repeatable, exactly
  like vanilla) — this is the user's "vanilla item under it after checked."
- **Key-pot (and any one-shot content): the hook EXPLICITLY suppresses the vanilla
  spawn (reveals nothing).** Vanilla has no persistent per-pot "key taken" flag and
  content `8` bypasses the room mask, so relying on vanilla would **duplicate keys**
  on room re-entry. A regression playtest (collect a randomized pot-key, leave,
  re-enter, re-break) is mandatory (§6).

## D4 — Un-checked pot recolor (palette swap, code-only)

While a pot is in-scope and un-checked, `RoomDraw_SinglePot` **masks out the palette
bits (10-12) of the four tilemap words and sets** the selected alternate CGRAM
sub-palette row (vanilla uses row 3 — a plain OR would corrupt it, so it is
clear-then-set, not OR); checked/out-of-scope pots draw vanilla. Gated on rando + tier + `!IsLocationChecked`; code-only; non-rando
path byte-identical. The alternate row must be loaded across dungeon themes and
visibly distinct, **verified by offline render against `zelda3_assets.dat`**, not
guessed. At `All` the recolor risks becoming wallpaper (D12 offers a 2-state tint /
non-empty-only sub-toggle).

## D5 — Capacity: typed audit of ALL location-id arrays/constants (R6)

`328 + 835 = 1163` exceeds **512 AND 1024** caps. The fix is **NOT a `512` grep** — it
is a **typed audit of every location-id-keyed array, constant, and guard**, raised to
a single `kRandoLocationCapacity` (2048) with a per-site `_Static_assert` tying it to
`LOC__COUNT` (≥) so overflow/truncation is a build break, not a silent fail-open.
Known sites (re-audit at implementation, do not treat as exhaustive):
- **512:** `kRandoCheckedBitmapBytes`; the placer arrays
  (`pool`/`open_loc_idx`/`placement_at`/`eligible`/`junk`/`candidates`/`junk_consumed`/
  `junk_filled`/`best_entries`/`trap_entries`) + `BuildItemPool(..., 512)`;
  `kDigestLocalCap` + `buf[512*4]` (digest TRUNCATES at 512 → corpus blind);
  `kReachabilityMaxLocations` + `location_bitset` + the fail-closed `id >= …` guard;
  `RandoSpheres.sphere_index_by_placement`; `kRando_SessionPlacementCapacity` +
  `g_session_placements`; in-memory `RandoSidecarSlot.checked_bitmap`/`placements`.
- **1024 (silent-DROP — locations ≥1024 invisible):** `auto_tracker.c s_loc_type[1024]`;
  `customizer.c kCustomizerLocIdProbeMax`; `tracker_windows.cpp` +
  `rando_reach_panel.cpp` `s_loc_region`/`s_loc_type[1024]`; `kSpoilerMaxRows` (1024);
  `rando_snapshot_tail.c` `raw[1024]` + the `> 1024` reject.

On-disk save format is sized by `placement_table_size` and is unchanged; only
in-memory caps grow.

## D6 — ITEM_Nothing as a placement-class rule + dedicated fill phase (R5)

`ITEM_Nothing` ("Literally Nothing") is **not** just an item id dropped into
`kJunkRotation` (which fills any open slot). The placer SHALL fill empty-pot
locations with `ITEM_Nothing` in a **dedicated pre-pass** (like vanilla-dungeon-item
pre-placement): empty-pot LOCs are filled with `ITEM_Nothing` and removed from the
open set **before** assumed-fill and junk padding, so neither `ITEM_Nothing` can land
on a real location nor a real item on an empty pot. Cross-cutting behavior, all
specified: logic no-op (never progression, never satisfies a predicate); excluded
from the `items` "100% inventory" accessibility tier but **counted in the tracker
completion denominator** (so `All` can reach 100%); emitted in the spoiler only as a
grouped/omittable line (D12); a sphere-0 / always-reachable placement for sphere
math; **trap shuffle never replaces `ITEM_Nothing`** and never targets empty-pot
slots; **customizer cannot pin `ITEM_Nothing` nor pin items onto empty-pot slots**
(empty pots are non-customizable like prize/shop), while active NON-empty pot
locations ARE customizable (the user may pin an item there, like a chest) and
out-of-tier / inactive pot locations are rejected by the customizer (not in the
pool); hints never source an `ITEM_Nothing` pot.

## D7 — Key-pots, key economy, and door shuffle (R4 — decided: restrict in v1)

Pot-key locations are **dungeon small-key locations** and follow the dungeon's
`dungeon_small_keys_mode` exactly like any vanilla key location. The dungeon's
small-key COUNT (vanilla per-dungeon) is FIXED and a pot-key is one of those existing
keys, NEVER an extra:
- **Vanilla key mode (the default):** the pot-key location is IDENTITY-PINNED to its
  pot. The existing vanilla key pre-seed/pin path (`rando_placement.c:228/280/501`,
  which today knows only vanilla *chest* key locations and does not add keys to the
  pool in vanilla mode) MUST be extended to include pot-key locations. The pot is a
  check that grants its pinned vanilla key; the key is NOT added to any shuffled pool.
- **Shuffled key modes:** the pot-key location is an open location and the dungeon's
  fixed key count is placed across all its key-eligible locations incl. pots — no
  extra key created.
- **Under door shuffle** (`door_shuffle != vanilla`, which forces Dungeon key mode →
  the full vanilla key count enters the shuffled pool): key-pots are EXCLUDED from the
  active pot checks AND pinned as fixed vanilla keys, AND the shuffled key-pool count
  for that dungeon is **REDUCED by the pinned pot-key count**. Saying only "the pot
  keeps its vanilla key" is INSUFFICIENT — without the subtraction the key exists
  twice (once pinned, once in the pool). Integrating pot-key locations into the door
  prover (`door_keylogic.c`) is the correct long-term fix and is **deferred**.

The mode-dependent pinning + the door-shuffle subtraction are enforced in
`apply_derived_rules` / the key pre-seed path and surfaced in the UI. (Owner may
override this scoping — see the summary.)

## D8 — Logic auto-binding: uniform-room inheritance + reviewed gates (R10)

`can_reach` defaults to `TRUE()`; reachability is region membership (`rando_logic.c:1027`).
Every pot MUST carry a `region:` (missing → silent sphere-0, a generator hard-error).
For gates, **same-room does NOT imply same reachability** (split rooms, water states,
crystal switches, one-way drops, bomb/key/big-key doors, dark rooms, dungeon-state
variants). So:
- **Uniform room** (all authored locations in the room share one region + predicate):
  a pot inherits that predicate. Safe and automatic.
- **Non-uniform room** (authored locations differ, or the room is on a known
  multi-subregion list): the generator REQUIRES an explicit, reviewed per-pot (or
  per-subregion) gate in `pot_logic_overrides.yaml` and **fails the build** until one
  is supplied. No silent inheritance for non-uniform rooms.

This makes a falsely-`TRUE()` pot impossible to ship without review. Room→region
mapping is derived from the existing region definitions. Named playtest surface:
Swamp flood, Ice/Mire gates, Hera/Eastern dark rooms (§6).

## D9 — Off-determinism: loop skip, verified by corpus diff (R1)

The digest hashes only the `t->count` placed entries (no trailing sentinels), so
this is NOT a digest-filtering problem. Out-of-scope pots are **skipped in the
open-location collection loop (`rando_placement.c:1325`) and the junk-pad target
loop** (mirroring the inactive-Take-Any `continue`), so they draw no RNG and never
enter the placement table or digest. Without the skip, registry growth alone makes
every pot an open location and changes every existing seed's digest. The
`pot_shuffle` canonical bits default 0 (`kSettingsCanonicalLen` unchanged), so
existing `settings_hash` is unchanged. **Validated by a corpus regen + 3-way diff
against `main`**, never by a digest-neutral claim.

## D10 — Performance + sorted-table invariants (R12)

`Placement_Lookup` becomes a binary search over a **sorted** placement table — but
the sort invariant SHALL hold at EVERY install boundary (assumed-fill output, sidecar
deserialization, snapshot-tail reinstall, customizer, race/spoiler reveal, tests),
enforced by a `--rando-selftest` sortedness check and a sort-on-install where a
producer can't guarantee order. Reachability runs a ≤64-pass fixed point, each
O(locations); pot nodes are cheap `TRUE()` predicates but the count ~3.5×'s, so the
budget (`randomizer-logic`, today <5 ms desktop / <20 ms Switch for the baseline) is
restated with a concrete pot-graph ceiling — provisionally **<30 ms desktop / <120 ms
Switch** for the ~1163 `All` graph, to be measured. `--generate-seed` keeps
`budget_seconds = 0`.

## D11 — "Pot" location type migration checklist (R7)

Adding a `Pot` type is cross-cutting; the change SHALL update ALL of: the YAML schema
enum (`logic.schema.yaml`, currently ends at TakeAny); the codegen type map
(`rando_logic_gen.py`, which maps unknown types to 0 — a silent trap); the shared C
enum (`rando_logic.h`, ends at TakeAny); placement type checks (pre-place skips,
`can_place`, junk eligibility); the tier filter; the auto-tracker + native tracker +
reach panel type tables; spoiler grouping; customizer non-customizable-type rejection;
and hint-source eligibility. A `--rando-selftest` assertion confirms the `Pot` type
round-trips through codegen (not silently mapped to 0).

## D12 — Tracker, spoiler, hints, recolor (R13)

- **Capacity first:** raise the spoiler/tracker/reach/auto-tracker 1024 caps (D5) or
  pots ≥1024 silently vanish from those views.
- **SNES HUD location tracker** (`src/hud.c:1863`) loops over locations and is
  unusable with 800+ extra rows — pots SHALL be hidden/paged/summarized there (it is
  not just the native window).
- **Spoiler**: pots in a grouped/collapsible section; `ITEM_Nothing` pots omitted.
- **Native/auto tracker**: group pots by room and/or a "show pots" toggle; the
  completion denominator counts `ITEM_Nothing`; auto-tracker export formats define
  explicit pot metadata behavior.
- **Hints**: junk/empty pots excluded as sources; a pot holding a progression item
  (key/major) is hint-eligible. No `randomizer-hints` spec change (pots are simply
  added or not to the existing candidate set).
- **Recolor cosmetic**: 2-state tint (real item vs empty/junk) OR a "recolor
  non-empty only" sub-toggle; client-side, placement-neutral, default on with
  pot-shuffle.

## D13 — Save compatibility is one-directional (R2 — corrected)

The append-only ID rule lets a **new binary read an OLD slot** (older slots are a
valid prefix of the larger registry; new locations default unchecked). The reverse
is **NOT supported**: an OLD binary reading a pot-expanded slot has a larger
`placement_table_size` than its fixed 512-era buffers and **rejects the slot**
(`rando_save.c` size/bounds validation refuses it non-destructively; the snapshot
tail rejects `> 1024` likewise). The spec SHALL state new-reads-old works and
old-reads-new is unsupported (refused, file untouched) — there is no
forward-compatibility for old binaries without an added compat layer (out of scope).

## Risks (load-bearing; mostly playtest-only)

1. **Runtime grant hook** (D3) — empty-pot grant fires (top-of-`RevealPotItem`),
   exactly-once, re-entry safety, all-classes routing, sword-break `0x80`
   reset/consume, checked **key-pot suppression** (dup risk). Corpus/selftest blind.
2. **`Off` determinism** (D9) — loop-skip filter; corpus 3-way diff is the only proof.
3. **Capacity** (D5) — typed audit must catch BOTH 512 and 1024 sites; per-site
   `_Static_assert`.
4. **Falsely-in-logic pots** (D8) — non-uniform rooms force reviewed gates.
5. **Excluded-room completeness** (D1) — allowlist + assertion.
6. **Door shuffle × key-pots** (D7) — v1 restricts; integration deferred.
7. **ITEM_Nothing placement** (D6) — dedicated pre-fill phase, class-constrained.
8. **Pot type migration** (D11) — the unknown-type→0 codegen trap; selfcheck.
9. **Sorted-table invariant** (D10) — every install boundary; selfcheck.
10. **Recolor palette + signal** (D4/D12) — offline-render verify; wallpaper at `All`.
11. **Generation time** (D10) — ~3.5× locations; measure; keep `budget=0`.
