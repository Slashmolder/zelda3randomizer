# Design: Bonk-sanity

## Context

Grounded facts (research pass 2026-07-17, fork + ALTTPR + z3randomizer):

- **Sprite mechanism, not terrain.** Placed overworld sprites `0x79`
  (bee hive form) and `0xAC` (apple tree) are prepped DORMANT by
  `SpritePrep_OverworldBonkItem` (`sprite_main.c:8809`,
  `sprite_E[k]++`); `RepelDash()` → `Prepare_ApplyRumbleToSprites` →
  `Entity_ApplyRumbleToSprites` (`sprite.c:1663-1672`) clears
  `sprite_E[j]` for any deflect-bit-2 sprite overlapping Link's bonk box;
  the woken AI then spawns 12 bees (`Bee_DormantHive`,
  `sprite_main.c:24994`) or 3-6 apples (`Sprite_AC_Apple`,
  `sprite_main.c:26677`). No `Overworld_RevealSecret`, no
  `kOverworldSecrets`, no tile attrs. Infinitely refarmable (sprites
  respawn dormant per screen load; no persistence bit).
- **Fork prior art**: `Sprite_BonkKey` (`sprite_main.c:7513`) already
  turns dungeon bonk drops into checks via
  `Rando_DispatchVanillaGrant(loc, 0xffff, ...)` +
  `Rando_ReceiveOrConfirm`. The pot quiet-receive
  (`Rando_PotQuietReceive`) is the no-animation grant used by
  terrain/pots.
- **Upstream**: ALTTPR `setOverworldBonkPrizes` (`app/Rom.php:1060-1079`,
  62 addresses, one duplicate `0x4CBBF`) is DEAD CODE — never called.
  z3randomizer asm gates the bonk mechanic on Boots but shuffles nothing.
  Fork-original scope; the dead table describes the ABSORBABLE family and
  is not compared against (D1).
- **Existing checks to exclude**: Lumberjack Tree (id 170, Standing,
  `DefeatAgahnim AND PegasusBoots`, entrance-gating) and Pegasus Rocks
  (id 165, dash rocks). Neither is a placed `0x79`/`0xAC`.
- **Reusable machinery**: `TerrainShuffle` enum + junk-class placement
  discipline; `TerrainWorldGateLW/DW` bunny gates (inverted-aware);
  capacity 8192 with terrain ending at 7052; the capped nearest-N glint
  draw (collector is map16-pos-keyed and needs a sprite-coord variant);
  the 3-layer fail-closed registry guard + `Rando_StampSlotRegistries`;
  checked-location bitmap; snapshot registry TLV (format_version 2
  extension precedent).
- **Settings space**: byte [29] bits 5-6 free (grass 0-1, rock 2-3,
  shopsanity 4); `kSettingsCanonicalLen` 31 unchanged.

## Goals / Non-Goals

**Goals**
- Every placed overworld bonk-item sprite is an individually-checkable
  location under `all` (progression allowed), filler-only under `junk`;
  first bonk grants + marks; later bonks replay vanilla (bees/apples stay
  farmable). Off = byte-identical.
- Engine-dump enumeration (the generator is the spec), three-stage
  sprite-phase identity rule; fail-closed activation on registry absence.

**Non-Goals**
- The Lumberjack Tree and dash rocks stay their existing checks.
- No pull-tree / rupee-crab / fish prizes (different sprites; candidate
  follow-ups).
- No prize-table cosmetic shuffle (ALTTPR's dead-table semantics); checks
  only.

## Decisions

### D1. Enumeration: new `--dump-bonk-table` sweeping the SPRITE-PHASE axis

The terrain dump is map16-based and cannot see sprites — and its five
profiles are the WRONG axis for sprite data (plan-review HIGH-2): OW
sprite tables are selected per game phase by `GetOverworldSpritePtr`
(`overworld.c:315-319`, `sram_progress_indicator` else/==2/==3 → base
0/1/2), and the terrain profiles never visit base 1. The bonk dump
therefore sweeps **prog ∈ {1, 2, 3}** (all three sprite-phase tables;
inverted/event overlays are irrelevant to sprite tables) and emits
`(stage, screen, type, x, y)` for every placed `0x79`/`0xAC`.
`gen_bonk_tables.py` (committed) applies the grouping invariant:
**an object registers only if its `(screen, x, y, type)` is identical in
all three stages; otherwise it is EXCLUDED** (the missable-check vector
rule — a stage-variant object could become mid-game-uncollectable; the
fork's OW enemy checks solve the same axis with explicit stage keys, which
v1 deliberately avoids). Emits gitignored `assets/rando/bonk.gen.yaml`,
ids from 7168 (terrain max id is 7052; capacity 8192).

Count sanity (plan-review MED-3 rewrite): ALTTPR's dead
`setOverworldBonkPrizes` table is NOT this population — its 62 addresses
target the classic bonk-prize ABSORBABLES (types 0xD8-0xE3 via
`SpritePrep_Absorbable`, incl. the famous bonk fairies), a different
sprite family this feature deliberately EXCLUDES in v1 (see D8). The
generator's tripwire is therefore a plain bounds check (count in [1, 200],
loud report of the per-stage and grouped counts), not an ALTTPR
comparison. setup_worktree.py mirrors the new artifact (the
stale-artifact lesson).

### D2. Location identity = (screen, x, y, type)

Sprite slot indices are load-order-fragile; coordinates are stable in the
sprite data. The runtime hook resolves `(overworld_screen_index,
sprite x/y, sprite type)` → location id via a generated lookup (sorted,
round-trip selfchecked). Rejected: slot-index keys (shift under overlays).

### D3. Axis + placement

`bonk_shuffle` off/junk/all reuses `TerrainShuffle` values, byte [29]
bits 5-6. Placement integration is the terrain pattern verbatim: junk
tier = junk-pad-only class (excluded from progression fill), all = open
locations; skip-triple extension (open-loc loop, junk-pad target,
`Placement_SelfCheck` count); reachability-expansion inclusion via the
existing ws-filter helper (bonk rows carry real regions, so no
REGION_OPTIONAL involvement). A small dump-measured family — no capacity or
tracker-gating concerns.

### D4. Logic

`CanBonk()` macro = `HAS_ITEM(Boots)` (macros.yaml; the CanBootsClip
precedent shows Boots item id 37 usage). Row predicate:
`TerrainWorldGate{LW|DW}() AND CanBonk()` per object world, region from
the screen→region map + the terrain overrides file pattern (new
`bonk_logic_overrides.yaml`, committed, for split screens; generator
hard-fails unreviewed split screens — the terrain MED-2 lesson).
Standard-mode escape: one registered location's REGION
(HyruleCastleEscape, A1B) is logic-reachable pre-rescue while its sprite
spawns only in stages 1/2 (review F4) — harmless (rescue is item-free and
reachability monotone; Boots can't be used in the escape) but spheres may
list the check one phase before it physically spawns; no special casing.

### D5. Runtime: grant only on DASH-ORIGIN wakes, suppress by despawn

`Entity_ApplyRumbleToSprites` has TWO callers (plan-review HIGH-1): the
Boots dash (`RepelDash`, player.c:1453) and the **Quake medallion**
completion (`ancilla.c:2911`) — and the quake path also skips the
Link-overlap test entirely (`byte_7E0FC6 == 0xe`, sprite.c:1667, latched
by the quake's chr half-slot load), waking every dormant sprite on screen.
A Boots-less player with Quake must NOT collect Boots-gated checks
(logic/runtime desync class).

The hook therefore fires ONLY for a dash-origin wake: `RepelDash`'s call
is wrapped with a scoped flag (set before
`Prepare_ApplyRumbleToSprites`, cleared after), and the hook additionally
requires the normal overlap test to have passed (`byte_7E0FC6 != 0xe`)
and `!player_is_indoors` (plan-review MED-5 — `overworld_screen_index`
is stale indoors and dormant rumble-wakeable sprites exist there, e.g.
the Nice Bee). Then: rando active + axis on + `(screen,x,y,type)`
resolves + unchecked → quiet grant, mark checked, `sprite_state[j] = 0`
same frame (sound per review: both rumble sources run before the sprite
AI loop, and state 0 routes to the inactive handler — the 12-bee/apple
burst never spawns). Quake wakes, checked objects, axis-off, unresolved
sprites: vanilla behavior (no missable cost — sprites respawn dormant
every screen load, so the check is simply collectable on a later dash).
Verified single-wake surface: the only other `sprite_E` clear reachable
by these types is the lift path their AIs never invoke.

### D6. Fail-closed guard (the grass HIGH lesson, all three layers)

Registry absent/empty at codegen → `bonk_shuffle != off` seeds REFUSE at
generation (`BuildItemPool` fail-closed via
`settings_need_bonk_registry`); sidecar ext gains
`bonk_registry_digest/count` (ext version bump) checked at activation
(count==0 rejects); snapshot registry TLV extended to **format_version 4**
(v3 is ALREADY pot+terrain+enemy-check, rando_snapshot_tail.c:400-412 —
plan-review MED-4; count-stable append like the v2→v3 precedent); all
slot writers go through `Rando_StampSlotRegistries`.

### D7. Glint

Bonk registry rows carry sprite coords; a small sprite-coord collector
(cap shared with terrain's 20) feeds the existing overworld glint draw
(`Rando_DrawOverworldEnemyMarkerGlints` template). Cosmetic, draw-only.

### D8. Scope default (owner-visible)

v1 population = the placed `0x79`/`0xAC` sprites (bee hives + apple
trees) — every one yields a real vanilla wake (12-bee swarm / 3-6
apples; plan-review corrected the earlier "majority empty" framing, which
described a different family). The classic bonk-prize ABSORBABLES
(0xD8-0xE3 dormant via `SpritePrep_Absorbable` — bonk fairies, hearts,
rupees, the 0xD8→bomb transmute in the rumble path) are deliberately
EXCLUDED from v1: same wake surface, but a larger population with its own
transmute wrinkle. Including them is the owner's v2 call (Q1); the
morning report surfaces both counts.

## Risks / Trade-offs

- **[Sprite-table variance across stages]** handled by the three-stage
  identity rule (D1); any object not identical across the three
  sprite-phase tables is EXCLUDED (the missable-check vector rule).
- **[Wake-hook collateral]** `Entity_ApplyRumbleToSprites` fires for ALL
  deflect-bit-2 dormant sprites; the hook must act only when the
  (screen,x,y,type) lookup resolves — unknown wakes stay vanilla.
- **[Double-grant within one dash]** the rumble loop can touch a sprite
  once per frame; grant+despawn is same-frame atomic and the checked bit
  re-guards (pot precedent).
- **[Registry vs runtime drift]** digest guard + count agreement
  selfcheck (terrain template).
- **[Count surprises]** the generator's plain bounds tripwire (count in
  [1, 200] + per-stage/grouped counts reported, D1) catches an empty or
  runaway enumeration; there is deliberately NO ALTTPR comparison (its
  table is the absorbable family). The real count is unknown until the
  dump runs and is reported to the owner.

## Migration Plan

On `feature/rando-shopsanity` (owner-directed convergence branch), phased
like grass-rock but smaller: (1) dump + generator + counts report;
(2) registry + logic codegen + macros; (3) settings axis + placement +
fail-closed; (4) runtime hook + glint + UI + docs; (5) corpus rows + full
validation. Every phase buildable, selftest green.

## Open Questions

- Q1 (owner, morning): extend the axis to the dormant bonk-prize
  ABSORBABLES (bonk fairies / hearts / rupees, 0xD8-0xE3) in a v2? v1
  ships bee hives + apple trees only (D8).
- Q2 (owner): should checked objects show the glint until checked only
  (D7 default) — matches terrain — or also a subtle post-check marker?
