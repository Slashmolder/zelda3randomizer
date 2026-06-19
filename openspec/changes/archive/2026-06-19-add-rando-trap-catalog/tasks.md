# Tasks — Randomizer Trap Catalog

Each slice is independently buildable + playtestable. Build = WSL `make -j zelda3` (`-Werror`)
**after `make clean`** when `rando.h`/`rando_settings.h` changed. Corpus = `bump_rando_corpus.py
--apply` + 3-way diff vs fresh `main`.

## 1. Scaffolding (no new effects — re-bucket Damage/Freeze, behavior-equivalent except the selector)

- [x] 1.1 `assets/rando/item_registry.yaml`: allocate a **contiguous** trap id block after
  132/133 reserving all planned effect ids (HAZARD/IMPAIR/DRAIN/SCARE/DISPLACE), each
  `category: trap`. Regenerate `item_ids.h`; mirror each `ID_Trap*` in `rando_placement.c` with a
  `_Static_assert(ID_TrapX == ITEM_TrapX)`.
- [x] 1.2 `rando_is_trap_item` → contiguous range check (`id >= ITEM_TrapFIRST && id <=
  ITEM_TrapLAST`). Confirm decoy masquerade + dispatch now cover every id in the block.
- [x] 1.3 `RandoSettings.trap_categories` (uint8): canonical byte [27] bits 2-6 in
  `Settings_CanonicalSerialize`/`Deserialize`; default 0 in `Settings_SetDefaults`; extend the
  deserializer defined-bit mask for [27]. Confirm `kExpectedCanonical`/`kExpectedHash` are
  **unchanged** (defaults stay zero) — if they move, the zero-sentinel wiring is wrong.
- [x] 1.4 Deterministic selector in `inject_traps_into_junk_placements`: replace `(injected & 1)`
  with `(seed, location_id)` splitmix64 → enabled category (zero-mask ⇒ all) → effect within
  category. Confirm it draws from the seed (not `RandoRng`) and runs after the last placer draw so
  non-trap placement is unperturbed.
- [x] 1.5 Table-driven dispatch skeleton in `rando.c`: `kRandoTrapEffect_*` per planned effect;
  `effect_for_id`/`reveal/sfx/duration/onset/sustain/teardown/context_ok/fallback/needs_world_mut`
  tables; rewrite `rando_trigger_trap` to **arm only** (set effect + timer + onset_pending) and
  `Rando_TickTrapEffects` to apply (gate on `{7,9,11}`, `submodule_index==0` for world-mutating
  effects, defer-not-drop, fallback-in-tick). Port Damage→HAZARD onset/sustain, Freeze→IMPAIR
  sustain. Extend `rando_clear_trap_effect` for the new state.
- [x] 1.6 Update the placement self-check: drop the Damage/Freeze **balance** assertion; assert
  total trap-slot count == `trap_count_for_frequency`. Add a `--rando-selftest` invariant that
  every id in the trap range resolves a non-green-rupee decoy.
- [x] 1.7 `kGeneratorVersion` bump; `make clean` + build + `--rando-selftest`. Corpus regen:
  expect **exactly the 3 traps-on digests move**, all else byte-identical. **Checkpoint** — the
  catalog is empty but the machinery is proven before any effect lands.

## 2. Cheap pure-trap effects (low softlock surface)

- [x] 2.1 DRAIN: Rupee (`link_rupees_goal -= chunk`, clamp 0), Magic (`link_magic_power=0;
  link_magic_filler=0; Hud_RefreshIcon()` — **assert no write to `link_magic_consumption`**),
  Ammo (zero `link_num_arrows` / **halve** `link_item_bombs` — **no `link_arrow_filler`**). All
  onset-only.
- [x] 2.2 IMPAIR: Reversed controls (stateless low-nibble remap of `joypad1H_last` +
  `filtered_joypad_H` in `sustain`), Disarmed (mask B/Y edges), Scrambled (input-drop). Verify
  none route through `rando_neutralize_trap_motion`.
- [x] 2.3 SCARE: Screen-shake (`AncillaAdd_DashTremor` + teardown zeroes `bg1_x/y_offset`),
  Fake-teleport (palette/INIDISP flash + restore), Fake low-health (beep + `countdown_for_blink`,
  HP untouched).
- [x] 2.4 Per-effect reveal sfx (`g_rando_trap_reveal_sfx`); optional per-category flavor text via
  reserved dialogue ids `0x0221+`.
- [x] 2.5 `--rando-selftest` arm/onset assertions per effect (save→arm→tick→assert state, mirroring
  the existing Damage/Freeze blocks). Build + corpus (no new movers beyond slice 1 — these are
  runtime; placement ids already reserved in 1.1).
- [x] 2.6 **Playtest**: each effect fires from a chest and a free-standing item; drains clamp
  correctly; reversed/disarmed restore cleanly on expiry and across a pause; shake/flash leave no
  stale camera/brightness.

## 3. Spawn / world effects (each needs a playtest cycle)

- [x] 3.1 Bomb (HAZARD): `AncillaAdd_Bomb(7,1)` in onset under `submodule_index==0`; capture
  `link_item_bombs`, temp-set to ≥1 if zero, **restore the exact original after the call**
  (handles the table-full no-spawn case — never steal/grant a bomb).
- [x] 3.2 Darkness (SCARE, dungeon-only): onset `dung_num_lit_torches=0` +
  `Dungeon_ApproachFixedColor_variable(31)` after snapshotting the prior color; sustain re-asserts;
  teardown restores from room state; **rando-active room load re-derives COLDATA** (D4). Fallback
  outdoors → Screen-shake.
- [x] 3.3 Cucco swarm (HAZARD, overworld-only): spawn-loop id `0x0B`, `sprite_C=1`,
  `Sprite_ApplySpeedTowardsLink`, screen-edge positions, slot-check each. Fallback indoors → Bomb.
- [x] 3.4 Ambush (HAZARD): spawn 2-4 from a **GFX-sheet-safe + land-safe whitelist** (reuse the
  enemy-shuffle `kSheetNeed` model to confirm sheets loaded); **exclude boss rooms**; OAM budget
  per spawned sprite. Highest-risk effect — land last.
- [x] 3.5 **Playtest** each: dungeon + overworld, both worlds, a boss room (spawns excluded),
  Darkness in a lit-torch room (no permanent torch loss) and a **save/Ctrl+F1 mid-darkness**
  (reloads lit), Cucco indoors (fallback), Bomb with 0 bombs in inventory (count stays 0).

## 4. Displace + variants

- [x] 4.1 Real Teleport (DISPLACE): reimplement `DoWarpStartPoint` in a production TU (not the
  `#ifndef`-gated debug `static`); onset under `submodule_index==0`; clear `follower_indicator`;
  **Inverted destination fix** (`savegame_is_darkworld=0x40` per `messaging.c:807`). Fixed
  Sanctuary destination v1.
- [x] 4.2 Scrambled re-roll permutation variant (optional). Knockback-Quake (optional IMPAIR:
  shake visual + Freeze stun) if desired.
- [x] 4.3 **Playtest** Teleport across **all world states** (Open/Standard/Inverted/Retro) and
  with entrance/door shuffle on; confirm the destination is always safe and never strands logic.
- [x] 4.4 Stretch (separate change if pursued): Mimic trap, Anti-trap charm, progress-scaling.

## 5. Audit, docs, hand off

- [x] 5.1 Fresh-eyes audit pass (independent reviewer) over the committed diff — softlock/leak
  paths, the proxy-byte exclusions (magic_consumption / arrow_filler), the masquerade range,
  the zero-sentinel round-trip through CLI `--settings` + share-string import.
- [x] 5.2 Reconcile the spec delta + design against as-built source before archive.
- [x] 5.3 `docs/randomizer.md`: trap catalog + `trap_categories` setting row + `kGeneratorVersion`
  version-history row. Update the `vanilla_sentinel_invariants` / trap memories.
- [x] 5.4 Owner end-to-end playtest sign-off (the load-bearing net for runtime dispatch).
