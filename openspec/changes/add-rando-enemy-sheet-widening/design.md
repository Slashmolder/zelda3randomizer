## Context

Enemy shuffle (`src/rando/shuffle_enemies.c`) substitutes enemy *types* within a room's
loaded GFX sheets. **Widening** additionally rewrites the room's owned, unpinned subgroup
slots (`sprite_gfx_subset_0..3` at `g_ram[0xC2FC..0xC2FF]`) to load *other* sheets,
growing the substitution pool. It is implemented but force-disabled
(`ES_ENABLE_SHEET_WIDENING 0`); with it off, `EnemyShuffle_ReshuffleCurrentRoomSheets`
forces every slot to its vanilla-resolved sheet and only type substitution runs.

The substitution **picker** already has the right shape. `candidate_allowed_in_context`
gates each candidate on an empirically-built allowlist:
- `g_enemy_vanilla_context[type]` — does this enemy appear in a vanilla room of this
  loader context (dungeon vs overworld)?
- `g_enemy_overworld_palette[type]` — a 256-bit set of `overworld_sprite_palettes` ids the
  enemy is observed with in vanilla. The palette branch is **overworld-only** today
  (`if (!is_dungeon && …)`, `shuffle_enemies.c`).

Both tables are built once at activation by `build_vanilla_context_table`, scanning the
shipped vanilla sprite blobs (`kDungeonSprites`/`kOverworldSprites` via their offset
tables). Overworld records each list's palette id during the scan.

Dungeon sprite palettes are **per-room-header**, not per-area: `Dungeon_LoadHeader`
(`src/dungeon.c`) reads `hdr_ptr = GetRoomHeaderPtr(room)` (= `kDungeonRoomHeaders +
kDungeonRoomHeadersOffs[room]`, assets 6/7), then sets
`palette_sp0l/sp5l/sp6l = kDungPalinfos[hdr_ptr[1]].{pal1,pal2,pal3}`. So a room's sprite
palette is fully determined by its palinfo index `hdr[1]` (0..40), and the color a sprite
draws with depends on which aux row (`sp0l`/`sp5l`/`sp6l`) its OAM palette field selects.

## Goals / Non-Goals

**Goals:**
- Re-enable widening with a dungeon sprite-palette model so widened enemies render with
  correct colors.
- Guarantee — by construction — that widening can never strand a forced substitution
  (no garbage render, no key/shutter softlock), the way the existing anti-garbage sheet
  pools guarantee fillability for the no-palette case.
- Keep placement byte-identical (corpus unchanged); keep the widening-off path
  byte-identical to today's playtested behavior.
- Provide an offline verification path so render correctness is validated without
  consuming the playtest loop.

**Non-Goals:**
- Per-sprite OAM-palette-row modeling (knowing exactly which of `sp0l`/`sp5l`/`sp6l` each
  enemy uses). The empirical seen-on-signature approach is intentionally conservative; a
  finer model is a future refinement, not required for safety.
- Setting/overwriting a room's `palette_sp*l` to follow a widened sheet. Widening only
  selects sheets that are already palette-correct for the room; it never repaints the room
  (that would mis-color pinned NPCs/objects).
- Any change to placement, logic predicates, the canonical settings layout, or
  `gen_enemy_shuffle_tables.py`.

## Decisions

### D1 — Dungeon palette signature = the `(sp0l, sp5l, sp6l)` triple

A room's palette signature is the sprite-palette triple `kDungPalinfos[hdr[1]].(pal1,
pal2, pal3)`. Two palinfo indices that resolve to the same triple (there are duplicates,
e.g. indices 22 and 35 both `{20,0,4,4}`) are treated as one signature, so an enemy seen
under one is allowed under the other.

*Alternative considered:* key on the palinfo **index** `hdr[1]` directly (simpler, no
dedup). Rejected: it would treat identical-palette rooms as distinct, needlessly shrinking
variety — and variety is the whole point. The triple is also directly readable at runtime
(`palette_sp0l/sp5l/sp6l` are live by pick time), but we derive it from the room header at
both build and pick time so the two never drift and there is no ordering dependence on
when the palette was loaded.

**Why the triple is the *complete* per-room signature (grounded in the engine).** A dungeon
sprite's color is set by `Dungeon_LoadPalettes` (`src/load_gfx.c`), which loads, per room:
`Sp0L`/`Sp5L`/`Sp6L` (the aux rows = the `kDungPalinfos` triple, **captured**), the common
rows 1–4 (`Palette_Load_SpriteMain`), and `sp6r` (`Palette_Load_SpriteEnvironment_Dungeon`).
The latter two are **constant across dungeon rooms**, not per-room variables:
- Common rows 1–4 read `kPalette_MainSpr + (overworld_screen_index & 0x40 ? 60 : 0)`, and
  dungeon entry forces `overworld_screen_index = 0` (`src/dungeon.c:8641`) → always the same
  light common palette in every dungeon room.
- `sp6r` reads `kPalette_MiscSprite + palette_sp6r_indoors*7`, and every dungeon room load
  pins `palette_sp6r_indoors = 10` right before `Dungeon_LoadPalettes` (`src/dungeon.c:6732`,
  `:8179`).
So an enemy that renders correctly in a vanilla room of signature S renders identically in
*any* room of signature S — a code-level proof of the model, not just an empirical hope.
**Residual:** `Graphics_LoadChrHalfSlot` (`src/load_gfx.c:912`) can set `palette_sp6r_indoors`
to a non-10 value for the minority of rooms that stream an extra CHR half-slot, so an enemy
using OAM-palette-6 *high* colors could mis-tint there. This is a minor cosmetic gap (never a
crash/softlock — verify-then-commit is palette-independent), recorded as a playtest watch-item
per the "widen once playtested" philosophy; a follow-up can fold `sp6r` into the signature.

### D2 — Empirical "seen-on-signature" allowlist (mirror the overworld mechanism)

Add `g_enemy_dungeon_palette[ES_TABLE_LEN]` as a bitset over distinct dungeon signatures
(≤41 → a `uint64`). Distinct signatures are enumerated from `kDungPalinfos` (deduped) into
a small table with stable ids. During the existing dungeon scan, for each room: derive its
signature id from `GetRoomHeaderPtr(room)[1]`, then mark every enemy type in the room's
sprite list with that id. This is the exact analog of `mark_overworld_palette_type`.

The elegance carries over: an enemy that only uses common palettes (OAM 1–4) appears in
vanilla across many rooms/signatures, so it accumulates a wide allowlist and passes nearly
everywhere; a palette-sensitive enemy (using an aux row) appears only in rooms sharing its
needed aux value, so its allowlist is exactly the compatible signatures — **without us
having to model which OAM row it uses.**

To read room headers from `shuffle_enemies.c` (kept free of heavy headers) without copying
the static `kDungPalinfos`, add a tiny pure accessor in `dungeon.c`:
`uint32 Dungeon_GetSpritePaletteSig(int room)` returning the packed `(pal1<<16|pal2<<8|
pal3)` (or a sentinel for out-of-range), forward-declared in `shuffle_enemies.c` like
`Dungeon_GetRoomSpritePtr`.

### D3 — Dungeon palette gate is active only when widening is on

Extend `candidate_allowed_in_context` to gate dungeon candidates on
`g_enemy_dungeon_palette`, but **only when `ES_ENABLE_SHEET_WIDENING`**. Rationale: with
widening off, `live[]` is the room's vanilla sheets and adding a dungeon palette gate would
reject some currently-allowed substitutions, changing today's playtested behavior. Gating
the dungeon check behind the same flag keeps "widening off" a precise, byte-identical
safety valve / bisection point.

### D4 — Verify-then-commit the widened sheet choice (the fillability guarantee)

This is the load-bearing safety decision. Widening slot `s` from vanilla `V` to chosen `X`
removes `V` from VRAM, so every randomizable enemy the room has on slot `s` is *forced* to
substitute, and `sheets_loaded` will reject the original type (its sheet `V` is gone). If
no valid substitute exists on the new live set, `pick_replacement` returns the vanilla type
→ it spawns with `V` unloaded → garbage. The dungeon picker requires
`killable && !cannot_key` for *all* dungeon substitutions, so the new live set must carry
at least one such enemy that is also palette-compatible and has all its sheets loaded
(plus a water-capable one when the room has a water source — `ESF_WATER`).

`EnemyShuffle_ReshuffleCurrentRoomSheets` therefore commits a widened set only after a
verify pass over the *resulting* live 4-slot set: it confirms the room retains a valid
forced-substitution target under the same constraints the picker will apply; if not, it
reverts the offending slot(s) to the vanilla-resolved sheet (which always restores
fillability, since the vanilla configuration is by definition fillable). The verify reuses
the candidate-filter logic in `pick_replacement`. Determinism is preserved: the choice and
the verify are pure functions of `(seed, room/area, slot)` and the shipped tables.

*Alternative considered:* gate only the picker (D3) and rely on the existing anti-garbage
pools. Rejected: the pools guarantee a killable+key enemy exists on each pooled sheet, but
the palette gate can reject exactly that enemy in a given room, breaking the guarantee.
The verify closes the gap the pools alone cannot.

### D5 — Overworld widening is symmetric

Overworld widening activates with the same flag and has the same forced-substitution
shape, minus the killable/key requirement. The overworld palette gate already exists, so
overworld needs only the verify-then-commit pass (does the widened set retain ≥1
palette-compatible substitute for the area's forced substitutions, water-respecting). No
new overworld palette table is needed.

### D6 — `kGeneratorVersion` bump; no canonical or placement change

Widening rides the existing `enemy_shuffle` axis (always-on when set) — no new canonical
setting, no `kSettingsCanonicalLen` change, no `RandoSettings` struct change (so no
`make clean` ABI trap beyond the `rando.h` header edit itself). It draws no fill RNG and
adds no predicate, so `placement_digest_hex` is byte-identical and the corpus regenerates
byte-identical. The `kGeneratorVersion` bump version-locks the now-live runtime behavior
(the same rationale as the original `61→62` widening version-lock).

### D7 — Verification net: analytical grounding + pipeline selfcheck (not a pixel renderer)

The original plan was an offline `zelda3_assets.dat` renderer that re-runs the sprite-palette
pipeline so a widened enemy could be compared pixel-for-pixel under a target palette. The
palette-completeness grounding under D1 (the dungeon sprite palette is *fully* determined by
`(sp0l,sp5l,sp6l)` + engine constants) makes that correctness argument **directly from the
engine source**, which is stronger and far cheaper than a pixel renderer that is "itself a
hypothesis until it reproduces a known-good render." So the verification net is:
- The D1 code-level proof that the signature is complete (common rows + `sp6r` constant).
- A `--rando-selftest` invariant that drives the **whole build pipeline** with *synthetic
  room headers* (header → `Dungeon_GetSpritePaletteSig` → intern → scan → mark → gate),
  asserting an enemy is marked on its room's signature and gated off others — validating the
  exact code that builds the allowlist, with controlled data and no real assets.
- Verify-then-commit selfchecks (fillability) + corpus byte-identical (placement).
- A fresh-eyes audit and one owner playtest as the final nets.

A pixel renderer remains available as a future belt-and-suspenders if a palette regression is
ever suspected, but is not required to ship given the grounding above.

## Risks / Trade-offs

- **[Conservative variety]** Triple-keyed seen-on-signature is stricter than true
  OAM-row compatibility: an enemy that would render fine under a signature it never
  appeared on in vanilla is excluded. → Accepted; safety-first matches the enemy-shuffle
  MVP philosophy ("widen once playtested"). Per-OAM-row modeling can loosen it later.
- **[Verify pass cost]** The verify runs at room/area sheet-load time. → It is O(table ×
  4 slots) over ≤256 entries, once per load, off the per-frame path — negligible.
- **[Boss-room palette override]** `Dungeon_LoadHeader` overrides `sp0l/5l/6l` for shuffled
  boss rooms. → Out of scope: boss rooms are never reshuffled (pinned) and bosses are not
  randomizable, so the dungeon picker never substitutes there. The header-derived
  signature (not the live triple) is used for build/scan, sidestepping the override.
- **[Offline renderer is a hypothesis]** A wrong palette pipeline can render a broken
  result as "clean." → Mitigation: cross-validate by re-rendering a known-good
  vanilla room and matching pixel-for-pixel before trusting any widened render.
- **[Selftest blind spot]** `--rando-selftest`/corpus cannot see runtime render or
  fillability. → The new selfcheck invariants assert the verify-then-commit guarantee
  statically (every reachable widened set retains a valid forced-substitution target); the
  offline renderer covers palette; one playtest confirms end-to-end.

## Migration Plan

1. Land the palette-signature build + dungeon gate + verify-then-commit + selfchecks with
   `ES_ENABLE_SHEET_WIDENING` still `0` (foundation; corpus + selftest stay green,
   behavior unchanged).
2. Validate offline (renderer spot-checks) and via the new selfcheck invariants.
3. Flip `ES_ENABLE_SHEET_WIDENING → 1`, bump `kGeneratorVersion`, `make clean`, regenerate
   the corpus (must be byte-identical), re-run selftest.
4. Owner playtest for final render confirmation.

**Rollback:** flip `ES_ENABLE_SHEET_WIDENING → 0` (and revert the `kGeneratorVersion`
bump) — the dungeon gate and verify pass go inert, restoring today's behavior exactly.

## Open Questions

- Should the dungeon verify also special-case `forbid_flying` rooms (require the retained
  substitute be non-flying)? Leaning yes for completeness, since the picker forbids flying
  there; cheap to include in the verify constraints.
- Is a per-(signature, slot) precomputed "compatible sheets" table worth it over the
  per-load verify? The verify is simpler and already O(small); precompute is a possible
  optimization only if profiling ever flags the load path.
