# Design: Grass & Rock Drop Shuffle

## Context

Verified engine facts this design is grounded on (all read from current
source, 2026-07-06; re-verified after rebasing the branch onto main @
kGeneratorVersion 129, which merged the enemy/NPC souls feature between
authoring and rebase):

- **All overworld terrain-consume paths converge on one function.**
  `Overworld_RevealSecret(pos)` (`src/overworld.c`, `Overworld_RevealSecret`)
  is called by: the lift path (`Overworld_LiftingSmallObj`, reached from
  `Overworld_HandleLiftableTiles`), the big-rock-pile path
  (`SmashRockPile_fromLift`), the sword-cut / tool path
  (`Overworld_ToolAndTileInteraction`, which also handles shovel digs and
  thick grass), the bomb path (`Overworld_BombTile`), and the dash-smash path
  (`Overworld_SmashRockPile` → the same two helpers). It looks up
  `kOverworldSecrets` (asset) keyed by `(overworld_screen_index, pos)`,
  where `pos` is the area-relative map16 position. Data byte `< 0x80` = item
  code written to `dung_secrets_unk1` (spawned later by
  `Sprite_SpawnThrowableTerrain` → `Sprite_SpawnSecret`, or by the smashed-
  terrain spawn on the cut/bomb paths); `>= 0x80` = structural tile-below
  reveal (`kTileBelow`: pit 0xDCC / water 0x212 / big-rock stairs 0xFFFF /
  staircase 0xDB4). Screens `>= 0x80` bail out — no secrets possible.
  **Caveat (review finding H1): not every `Overworld_RevealSecret` call is a
  consume.** `Overworld_BombTile` also calls it SPECULATIVELY for blast tiles
  it does not consume (the `label_a` staircase probe — and for EVERY tile of
  the 3x3 blast when the super-bomb follower is active), and one bomb probes
  up to 9 positions (`Overworld_BombTiles32x32`). The runtime hook therefore
  lives at the consuming call sites, not inside `Overworld_RevealSecret`
  (D9).
- **The spawner substitutes a random secret when none was found.**
  `Sprite_SpawnThrowableTerrain_silently` (sprite.c:1365-1372) treats
  `dung_secrets_unk1 == 0xFF` as the engine's no-spawn sentinel (set by
  `Overworld_RevealSecret`'s structural path); but when `dung_secrets_unk1
  == 0` outdoors for the bush/grass throwable classes it calls
  `Overworld_SubstituteAlternateSecret()` — i.e. a zeroed secret byte
  TRIGGERS a random bonus item roughly half the time rather than suppressing
  one (review finding H2; pot sanity never hit this because
  `player_is_indoors` blocks substitution). Suppression must use the `0xFF`
  sentinel.
- **`overworld_tileattr[]` holds current map16 IDs** (despite the name); the
  behavior attribute is derived per map8 tile via
  `kMap8DataToTileAttr[GetMap16toMap8Table()[map16*4 + q] & 0x1ff]`. Liftable
  attribute classes are `0x50..0x56` (`0x57` is the dash-only bonk class,
  excluded). Glove requirements are ground-truthed (dump-verified) from
  `tile_detect.c`'s `kTile50data = {0x54,0x52,0x50,0x51,0x53,0x55,0x56}`
  (attr → interaction index) composed with `player.c`'s
  `kGetBestActionToPerformOnTile_a = {0,1,0,0,2,1,2}` (index → required
  glove level): `0x50`/`0x51` bushes = bare hands, `0x52` small rock =
  Power Glove, `0x53` small dark rock = Titan's Mitt, `0x54` sign = bare,
  `0x55` big pile = Glove, `0x56` big dark pile = Mitt; thick grass is attr
  `0x40` (cut-only). Map16 IDs matched by the
  reveal helpers: bushes `0x036` (LW) / `0x72a` (DW), thick grass `0x37e`,
  small rocks `0x20f` / `0x239`, sign `0x101`, big-pile quadrants
  `0x36d/0x36e/0x374/0x375`, `0x23b..0x23e`, and (dash path, via `dung_bg2`)
  `0x226..0x229`.
- **Magic powder overrides secrets**: `AdjustSecretForPowder()` forces
  `dung_secrets_unk1 = 4` (random item) whenever powder is in hand, on every
  reveal path including the no-secret fail path.
- **Sibling patterns to reuse** (pot sanity, merged; enemy-drop checks,
  merged): engine-dump enumeration (`--dump-pot-table` →
  `gen_pot_tables.py` → gitignored `pots.gen.yaml` + committed reviewed
  `pot_logic_overrides.yaml`), single runtime hook (`Rando_PotBreakHook` in
  `RevealPotItem`), quiet receive (`Rando_PotQuietReceive`), checked-location
  bitmap persistence, sidecar registry activation guard
  (`pot_registry_digest/count`, `Rando_PotRegistryMatches`, hard-fail at slot
  activation), tier-filtered placement with the **skip-triple** discipline
  (open-location loop + junk-pad target + `Placement_SelfCheck` expected
  count must agree), and the screen→region table `OVERWORLD_REGIONS` in
  `assets/scripts/gen_enemy_check_tables.py:511` with authored predicates for
  exceptional screens.
- **Capacity today**: `kRandoLocationCapacity = 4096` (`rando_logic.h`), with
  `_Static_assert(LOC__COUNT <= kRandoLocationCapacity)` in `rando.c`. Base
  registry ends at 327, pots occupy 328..~1162, enemy drops 1300..1313,
  enemy checks 1400..2633 (measure ceilings with a full-number grep —
  `id: [0-9]+` + numeric sort; a `1[0-9]*` pattern silently missed every id
  ≥ 2000 and briefly produced a colliding terrain base — corpus-caught, and
  the codegen now hard-fails duplicate ids).
  Canonical settings byte `[28]` is now fully committed: `enemy_drop_checks`
  bits 0-1, `souls_shuffle` bits 2-3, `npc_souls` bit 4 (add-enemy-souls /
  add-npc-souls), bits 5-7 refused-undefined — three free bits cannot hold
  two 2-bit axes, so the terrain axes APPEND canonical byte `[29]`
  (`kSettingsCanonicalLen` 29→30, the same append-only move that created
  [28]). Appending changes the SHA-256 input length, so every
  settings_hash — defaults included — changes once under this change's
  `kGeneratorVersion` bump; default PLACEMENT stays byte-identical (the
  corpus 3-way diff proves it). Sidecar file format_version = 7, slot ext
  block = 37 bytes.
- **Prior art**: ALTTPR randomizes only enemy prize packs + tree pull / crab
  / stun / fish prizes; bush/grass/rock drops are untouched there
  (`app/World.php:1463-1515`; `setOverworldBonkPrizes` at `app/Rom.php:1060`
  is dead code). This feature is fork-original.

Owner decisions (2026-07-06): grass scope = bushes + cuttable thick grass
(low tufts stay cosmetic); per-object checks; tiered axes (`off/junk/all`).

## Goals / Non-Goals

**Goals:**

- Two independent settings axes, `grass_shuffle` and `rock_shuffle`
  (`off / junk / all`), default `off`, hash- and corpus-neutral when off.
- Every in-scope overworld terrain object is an enumerable, individually
  checkable location with logic-sound predicates (rock strength modeled via
  the existing `CanLiftRocks()` / `CanLiftDarkRocks()` macros).
- One runtime hook covering every consume path (lift / cut / bomb / dash /
  powder), pot-style quiet grant, persistent checked bits, registry
  activation guard.
- Deterministic generation: off-tier seeds byte-identical; new corpus rows
  for on-tier seeds and compositions.

**Non-Goals:**

- Low walk-through grass tufts (no vanilla drop path; would need new engine
  mechanics).
- Signs (map16 `0x101`) and dash-only bonk rocks (attr `0x57`) — candidate
  future tiers (ALTTPR's dead `setOverworldBonkPrizes` is prior art for
  bonk prizes); excluded so the axes match the owner's ask exactly.
- Underworld terrain: dungeon/cave liftables route through `RevealPotItem`
  (pot sanity's domain). Phase 1 verifies via the dump that no underworld
  bush/rock secret exists outside the pot path; if any shows up it is
  explicitly out of scope for v1.
- Special overworld screens `>= 0x80` (Master Sword grove, under-bridge,
  Zora's Domain): `Overworld_RevealSecret` cannot fire there.
- Drop-TABLE randomization (shuffling the vanilla random-prize packs) — a
  different, cosmetic feature; deliberately not this change.
- A per-object glint on EVERY object (pot-sanity's model): the densest
  screens hold ~159 in-scope objects at once, over the SNES 128-sprite OAM
  budget — impossible and visual spam. INSTEAD the in-world cue is a CAPPED
  nearest-N glint (D15): the pot gold sparkle on only the ≤20 unchecked
  active terrain checks nearest Link, updating as Link moves. (A full
  per-object BG-palette tint was considered and rejected: the overworld BG
  renders by map16 ID not position — a checked and unchecked bush are the
  same tile — and lift/cut detection keys on those same IDs, so per-object
  BG state needs a scroll-reapplied second render pass plus per-theme palette
  tuning; that fragility isn't worth it when the capped sprite glint reuses
  the proven, OAM-safe enemy-glint path.)
- Structural secrets (stairs/holes/water/portal reveals) are never shuffled
  or suppressed.

## Decisions

### D1. Check model: per-object locations keyed exactly like the vanilla secrets table

Each in-scope object becomes one location keyed `(overworld_screen_index,
map16 pos)` — the key `Overworld_RevealSecret` already receives, so the
runtime lookup is a binary search over a sorted `(screen, pos) → loc_id`
table exactly like `Rando_GetPotLocation`'s `(room, pos4)` lookup. Two new
location types, `LOCTYPE_Grass` and `LOCTYPE_Rock` (next free ordinals),
because the two axes activate disjoint subsets and trackers/UI present them
separately. The pot-sanity type-migration checklist (logic schema,
`_location_type_id`, both LOCTYPE enum copies, placement type checks,
tracker/auto-tracker/spoiler type tables, customizer, hints) applies to both
new types.

Big rock piles (2x2 map16 quadrants) are ONE location each:
`SmashRockPile_fromLift` normalizes `pos` to the pile origin via
`kBigRockTab1` before calling `Overworld_RevealSecret`, so the origin pos is
the stable key.

### D2. Object classification comes from the engine dump, not hand-mapped IDs

`--dump-terrain-table` (new CLI mode mirroring `--dump-pot-table`) loads
assets, iterates overworld areas `0x00..0x7F`, and for each map16 cell
classifies via the same chain the engine uses (map16 → map8 →
`kMap8DataToTileAttr` → attr class `0x50..0x56` + glove level from
`kGetBestActionToPerformOnTile_a`), joining each object with its
`kOverworldSecrets` entry (secret code, if any). Emitted per object: screen,
pos, map16 id, class (bush / thick-grass / rock-light / rock-heavy /
pile-light / pile-heavy / sign / bonk), glove level, secret code.

The generator (`assets/scripts/gen_terrain_tables.py`) asserts its own
invariants (the "generator IS the spec" discipline): every map16 id the
reveal helpers match is classified; signs and bonk rocks are recognized and
excluded; unknown liftable attrs hard-fail. Axis membership: grass = bush +
thick-grass classes; rock = the four rock/pile classes.

### D3. Exclusions are structural, engine-grounded rules

Excluded from the registry (vanilla behavior always preserved):

- Objects whose secret data byte is `>= 0x80` (tile-below reveals: holes,
  water, staircases, the big-rock stairs `0xFFFF` path that also writes
  `save_ow_event_info |= 0x20`, and the Ice Palace portal rock `0x82`).
  These are terrain topology, not drops.
- Screen `0x5b`'s follower-gated secret (the `follower_indicator != 13`
  bail) — a scripted sequence, not a farmable drop.
- Any object on screens `>= 0x80` (unreachable by the secrets system).

Objects with a plain item secret (`data < 0x80`: rupees, bombs, hearts,
bee/good-bee, fairy, chicken, random) and objects with NO secret entry are
both in scope — vanilla-empty bushes are real checks per the owner's
per-object decision.

### D4. Enumeration pipeline mirrors pot sanity end-to-end

`--dump-terrain-table` → `assets/rando/terrain_dump.gen.txt` (gitignored) →
`gen_terrain_tables.py` → `assets/rando/terrain.gen.yaml` (gitignored,
mirrored by `setup_worktree.py` as a complete set with the dump, per the
pot-artifact rule) + committed reviewed
`assets/rando/terrain_logic_overrides.yaml`. `rando_logic_gen.py` injects
terrain locations into the compiled logic exactly as it does pots (mutating
the locations dict before codegen) and emits a sorted
`src/rando/terrain_lookup.h` binary-search table.

Game-phase variation: the dump runs per relevant persistent tile state
(game-phase overlays, per-screen `save_ow_event_info` states, the Inverted
map overrides). The dump profiles split into two WORLD-STATE GROUPS whose
within-seed latch states must each be internally consistent but which are
NOT required to agree with each other (Inverted rewrites the overworld — it
is a different map, not a within-seed latch of the same map):
- NON-INVERTED (Open/Standard/Retro): profiles {vanilla, +events, pre-rescue}
- INVERTED: profiles {inverted, inverted+events}
An object is registered **for a world-state group** iff it is PRESENT WITH
IDENTICAL CLASS IN EVERY profile OF THAT GROUP (the missable-check invariant,
applied within the group — presence-only-in-some-latch-states of a group is a
missable vector: the removing state latches and the check becomes permanently
uncollectable). It then carries a `world_state_filter`: universal (present-
and-equal in BOTH groups → no filter), `[open,standard,retro]` (non-inverted
only), or `[inverted]` (inverted only). Placement (`rando_placement.c`) AND
reachability (`rando_logic.c`) both honor the emitted mask, so a group-scoped
object is inert in the other world_state — no strand, no missable. Objects
absent-or-class-inconsistent WITHIN their group in every group are excluded
and flagged for override review, same as structural secrets. (Requiring
presence across the INVERTED boundary — the original all-5-profile
intersection — wrongly dropped ~30 non-inverted-only Light-World objects,
e.g. the castle-area bushes, from every Open/Standard/Retro seed; caught by
F12 playtest, fixed by the per-group rule above — review finding M5.) Event
overlays that add/remove single tiles (e.g. the bombable castle-wall
stairs) are structural and already excluded by D3.

IDs are assigned by `(screen, pos)` sort rank from a fixed base (D5) —
insertion renumbers, so any registry change is a `kGeneratorVersion` bump
(same contract as pots; the generator emits the registry digest the
activation guard checks).

### D5. Capacity: measure first, then bind — with the ABI hazard mitigated

Exact object counts are unknown until the dump runs (the 890 extracted
vanilla secret entries are a floor; plain bushes likely push the total into
the low thousands). Phase 1 produces the counts BEFORE any ID or capacity
decision is coded. Then:

- Base ID = the next free aligned block after the enemy-check block's REAL
  ceiling, measured with a full-number grep (`id: [0-9]+` + numeric sort —
  as-built: enemy checks end at 2633, base = 3072). The codegen hard-fails
  on duplicate location ids so a mis-measured base is a build break, not a
  silent id collision.
- If `base + terrain_count > kRandoLocationCapacity (4096)`, raise the
  capacity to 8192 — a single-define change by design, BUT: the Makefile has
  no header dependency tracking, and the enemy-drop review reproduced the
  worst case (mixed-ABI objects passing selftest, then 16.8 GB runaway
  spoiler). Mandatory mitigations: `make clean` on both WSL and MSVC after
  the bump, plus a cross-TU `sizeof`-agreement selfcheck (each TU that sizes
  arrays by the capacity contributes a compiled-in constant checked at
  `--rando-selftest` time) so a stale object fails loudly.
- Audit every literal-sized location-indexed array regardless (grep for
  `[1024]` / `[2048]` / `[4096]` in `src/rando/` including
  `rando_snapshot_tail.c` and the tracker/window TUs) — pot Phase 1 unified
  most of these onto `kRandoLocationCapacity`, but the audit is cheap and
  the failure mode (silent truncation) is not.

Downstream size effects accepted: checked bitmap grows with capacity (512 B
at 4096, 1 KB at 8192), sidecar slots and snapshot tails grow with the
placement table; the sidecar format already sizes both from
`placement_table_size` per slot.

### D6. Region binding: screen table + reviewed overrides; over-gate, never under-gate

Default binding: `OVERWORLD_REGIONS[screen]` (the enemy-check table). The
known failure mode is split screens — ledges, fences, river banks — where
part of a screen belongs to a different reachability region (Desert ledge,
DM ledges, Zora river banks, the race-game fenced strip, HCE secret-passage
grass, TR ledge, Bumper ledge...). Handling:

- A committed, reviewed `terrain_logic_overrides.yaml` maps `(screen, pos)`
  ranges or explicit positions to a different region and/or an extra
  predicate, seeded from the enemy-check ALL-tier authored predicates for
  the same screens and cross-checked against ALTTPR's region PHP.
- The generator hard-fails on screens listed as split-but-unreviewed (an
  explicit `needs_review` list compiled during Phase 1's manual screen
  sweep), mirroring pot D8's "hard-fail unresolved" discipline.
- Directional principle from pot sanity: when in doubt, OVER-gate (a
  too-strict predicate delays a check; a too-weak one lets the placer strand
  progression → softlock). Junk-tier locations tolerate binding error by
  construction (nothing required is ever behind them); the `all` tier is
  where binding must be right, and the playtest matrix targets exactly the
  override screens.

### D7. Predicates: existing macros + one new cutter macro + world-state-aware pearl gates

- Light rocks / light piles: `CanLiftRocks()` (macros.yaml:75).
- Heavy rocks / heavy piles: `CanLiftDarkRocks()` (macros.yaml:87).
- Bushes: no tool requirement (bare-handed lift).
- Thick grass: new macro `CanCutGrass()` — grounded in the engine's consume
  paths for `0x37e`: sword slash (`Overworld_ToolAndTileInteraction`) or
  bombs (`Overworld_BombTile`). The hammer branch of
  `Overworld_ToolAndTileInteraction` only handles pegs, and the powder
  branch skips the thick-grass case — so v1 sets
  `CanCutGrass() = HAS_ITEM(ProgressiveSword) OR CanBombThings()`, with an
  implementation task to re-verify the full cutter set against
  `Link_HandleSwordSlash`-side tile interaction before the macro lands
  (swordless seeds depend on it).
- World crossing / bunny: every location gets a world-side gate that is
  world-state aware in ONE entry (the macros system supports
  `WORLDSTATE_EQ`, cf. macros.yaml:137): DW objects require
  `(HAS_ITEM(MoonPearl) OR CanPearlBypass())` in Standard/Open/Retro and are
  home-side in Inverted; LW objects get the inverse in Inverted. This
  deliberately improves on pot sanity's known inverted over-restriction
  (acceptable for ~47 DW pots, not for ~half of all terrain locations).

### D8. Tier semantics: `junk` = junk-pad-only fill; `all` = full open locations; no ITEM_Nothing pinning

- `off`: locations inactive everywhere (the skip-triple sites all skip);
  placement, hash, spoiler, trackers byte-identical to today.
- `junk`: every in-scope object of the axis is an active location whose fill
  is restricted to junk: excluded from the assumed-fill progression
  placement set, filled by the junk pad. "Randomize the drops" without check
  pressure — no progression can hide there, logic never routes through
  them.
- `all`: ordinary open locations — progression can land under any bush/rock,
  junk-padded like every other surplus location.
- No `ITEM_Nothing` pinning (unlike pot sanity's vanilla-empty pots): every
  active object receives a real filler item, because "drops" are the point
  of the feature. Vanilla-empty bushes therefore drop one placed junk item
  once, then revert to vanilla (nothing / powder-random).
- The suppressed vanilla secret items (bush fairies, bees, rupees) are NOT
  added to the pool (pot precedent); after the check is collected the
  vanilla secret behavior returns, so fairy-bush farming survives — see D9.

### D9. Runtime: hook at the CONSUMING call sites, pot-style quiet grant

`Rando_TerrainRevealHook(overworld_screen_index, pos)` is called from the
four consuming contexts, immediately before their `Overworld_RevealSecret`
call: `Overworld_LiftingSmallObj` (lift + dash-bush), `SmashRockPile_fromLift`
(big piles, lift + dash), the `is_bush`/`isThickGrass` branch of
`Overworld_ToolAndTileInteraction` (sword-cut; also the powder-in-hand
route), and the bush/thick-grass branch of `Overworld_BombTile`. It is NOT
placed inside `Overworld_RevealSecret` itself: the bomb path's `label_a`
staircase probe (and the super-bomb follower path) calls that function
speculatively for tiles the blast does NOT consume — an interior hook would
grant a heavy-rock check to a bomb dropped next to it, with no Titan's Mitt
and no object consumed (review finding H1). Outcomes:

- **Not a registered location / axis off / rando inactive** → vanilla: the
  call site proceeds to `Overworld_RevealSecret` untouched.
- **Active + unchecked** → dispatch the placed item
  (`Rando_DispatchVanillaGrant(loc, 0xFFFF, kRandoLttpSkip-style routing)` →
  quiet receive), mark checked, and SUPPRESS the vanilla secret: the call
  site SKIPS `Overworld_RevealSecret` entirely (which also skips
  `AdjustSecretForPowder` — no powder bonus item on top of the grant) and
  sets `dung_secrets_unk1 = 0xFF`, the engine's own no-spawn sentinel, so
  the downstream spawner neither spawns a stale secret NOR rolls
  `Overworld_SubstituteAlternateSecret()` (which fires outdoors on a ZEROED
  secret byte for bush/grass classes — review finding H2; `0` would trigger
  a bonus random item, not suppress one). The call site then uses its
  default replacement tile — identical visuals to a no-secret object.
- **Active + checked** → vanilla behavior. Safe: vanilla bush secrets are
  already infinitely refarmable in vanilla (bushes respawn on re-entry) and
  contain no progression, so replay introduces no dupes and preserves
  fairy/bee farming spots after the check.

One bomb can consume up to 9 registered objects in a single blast
(`Overworld_BombTiles32x32` probes a 3x3 of positions), so the grant path
must survive up to 9 dispatch + quiet-receive rounds in one frame (the
receipt path's no-free-ancilla retry and last-icon-wins confirmation are
the existing mitigations; verified in the playtest matrix).

Grant delivery generalizes `Rando_PotQuietReceive` into a shared
terrain/pot quiet-receive (same routing: direct-grant items → confirmation
icon path; everything else → the `AncillaAdd_ItemReceipt` write with
immobilize-clear + ancilla-kill, including the no-free-slot retry fix and
the shared `rando_receive_item_icon` resolver so draw mirrors grant). The
hook must be inert when `!(enhanced_features1 & kFeatures1_RandomizerActive)`
— vanilla and RAM-compare runs untouched.

Structural secrets never reach the hook outcome path (excluded from the
registry, D3), so stairs/portals/holes always behave vanilla.

### D10. Activation guard + persistence mirror the pot registry guard

- Checked bits: the existing `g_rando_checked_bitmap` — no new persistence
  mechanism; the bitmap already sizes from `kRandoLocationCapacity` and the
  sidecar/snapshot already carry it.
- Slot ext grows (ext v8, `format_version` 8: 37→43 bytes) with
  `terrain_registry_digest` (u32) + `terrain_registry_count` (u16); v8 also
  widens the per-slot canonical settings blob to the new
  `kSettingsCanonicalLen` (30 bytes), mirroring how v4 widened it to 29 —
  older files' 29-byte blobs zero-extend on load, reading the terrain axes
  as off. Written on save and verified at slot activation exactly like
  `Rando_PotRegistryMatches` (rando.c:3892): a binary whose generated
  terrain registry is missing/empty or digest-mismatched REFUSES to
  activate a slot with either axis on, with a visible message — closing the
  chest_lookup silent-fail-open class for this family. The same digest is
  stamped by `--generate-seed` spoiler metadata for corpus traceability.
- `setup_worktree.py` mirrors the terrain artifacts as a complete set
  (dump + gen.yaml), same as the pot artifact set.

### D11. Placer integration: extend the skip-triple, add the junk-only class

- Activity filter `terrain_active(loc, settings)` keyed on the location
  type + the axis tier, applied at the three coupled sites (open-location
  loop, junk-pad target count, `Placement_SelfCheck` expected count) — the
  pot-sanity pattern, now four families deep (pots, enemy, grass, rock).
- New placer concept "junk-only location": for tier `junk`, locations enter
  the junk-pad fill but never the assumed-fill candidate set for pool
  (progression) items. `Placement_SelfCheck` verifies no pool item landed
  on a junk-only location.
- Accessibility: `items` (default) requires only progression reachability —
  junk on an unreachable ledge is permitted; `locations` requires all
  ACTIVE terrain locations reachable (expected to make `locations`-tier
  seeds harder to fill; honest refusals are correct behavior, as with hunt
  goals); `none` unchanged.
- Determinism: `--generate-seed` stays `budget_seconds = 0`; the added
  locations change RNG draw order for on-tier seeds only. Off-tier seeds
  must be byte-identical — validated by the corpus 3-way diff discipline
  (fresh build of unmodified main vs. branch, `rm src/rando/logic_data.c`
  first).

### D12. Composition rules

- **door_shuffle**: terrain is overworld-only and invisible to the door
  key-prover — both axes stay available, no coupling. (Correcting an earlier
  draft claim: as-built, `pot_shuffle` COMPOSES with door shuffle too —
  `Settings_DoorPotTier` — and is instead forced off under CAVE-ENTRANCE
  shuffle; see next bullet.)
- **cave-entrance shuffle**: pots are forced off here
  (`Settings_PotShuffleForcedOff` = effective cave shuffle,
  rando_settings.c:258-285) because cave/house pot location IDs sit above
  the per-location entrance region-override range, so an interior pot would
  evaluate from its VANILLA overworld region while the runtime routes
  through the shuffled entrance. Terrain does NOT inherit that problem:
  every terrain location is on the overworld SURFACE — its region binding
  never routes through an entrance, and `OP_REGION_REACHABLE` over OW
  regions is already resolved by the entrance-aware reachability. Both axes
  stay available under cave-entrance shuffle, with a corpus composition row
  to prove it.
- **dungeon-entrance / dungeon-chain shuffles**: same argument — OW-surface
  regions resolve through the entrance-aware graph; no special handling.
- **enemy_drop_checks**: disjoint key spaces and hooks; freely composable.
- **pot_shuffle**: disjoint (underworld vs overworld); freely composable.
- **Retro**: take-any/shop pinning unaffected; terrain junk inflates the
  junk pad which already accommodates Retro (existing requirement).
- **Inverted**: handled in-predicate (D7), no axis normalization.
- **Hunt goals**: Triforce pieces may land under terrain in `all` tier;
  the `accessibility=none` hunt convention is unchanged.
- **Customizer**: terrain locations are pinnable by location id (same rules
  as pots; no by-value exclusions since there is no ITEM_Nothing here).
- **Hints**: junk placements are already non-hintable (`item_is_junk`);
  terrain locations holding progression are hintable like any location.

### D13. UI & reporting

- Native settings window (Shuffles block): two `EnumCombo`s — "Grass
  shuffle" and "Rock shuffle", labels `{"off","junk","all"}` (lowercase =
  CLI grammar convention). Tooltips: 1-2 durable player-facts each (e.g.
  "Bushes and cuttable grass hide placed items. junk = filler only, all =
  anything can be there."). No disable-coupling needed (D12).
- SNES HUD location grid: skip `LOCTYPE_Grass`/`LOCTYPE_Rock` rows (the 8px
  grid can't carry thousands of cells) — same treatment as pots.
- Native check tracker / reach panel: a session "Show terrain" toggle
  (default OFF) gating rows, with counts INCLUDED in global and per-region
  denominators and a per-region "+N terrain checks" summary line —
  the as-built pot presentation, including the filter-aware hidden-count
  fix and the map-tracker hover-tooltip cap.
- Spoiler: all terrain placements emit (they are real items — no
  empty-row filtering needed); the in-window spoiler row cap and the JSON
  writers must be verified against the new row count (they size from
  capacity constants, audited in D5).
- Auto-tracker catalog: type metadata for the two new LOCTYPEs so external
  trackers can filter.

### D14. Validation strategy

Corpus additions (each with placement + sphere digests): grass-junk,
grass-all, rock-junk, rock-all, grass+rock-all, and compositions
(retro+terrain, inverted+terrain, door+terrain, enemy-drop+terrain,
pot+terrain, hunt-goal+terrain-all with accessibility=none). One
`kGeneratorVersion` bump for the whole change, taken when the first
placement-affecting piece lands; `bump_rando_corpus.py --apply` + 3-way
diff proves off-tier seeds byte-identical. `--rando-selftest` gains:
terrain lookup round-trip + sortedness, registry digest self-check,
junk-only invariant. The runtime grant path is corpus-blind (generation
only) — the END-TO-END PLAYTEST matrix is the load-bearing net (per
CLAUDE.md): per-tier grants on each object class (bush lift, bush cut,
bush bomb, thick-grass cut, small/big rock both strengths, dash-smash),
powder-on-bush, a clustered bomb consuming several bushes at once (up to 9
grants in one frame), a bomb dropped ADJACENT to a registered rock granting
NOTHING (the speculative-probe non-consume case), checked-object vanilla
replay (fairy bush), a structural rock (portal/stairs) staying vanilla,
swordless+grass-all, cave-entrance shuffle + terrain, inverted DW/LW
sides, and the D6 override screens.

### D15. In-world cue: capped nearest-N gold glint (owner-decided post-design)

A checked and an unchecked bush are the SAME map16 tile in the screen data,
so on screen reload there is no vanilla way to tell them apart — the player
needs an in-world checked-state cue to sweep efficiently. The pot-sanity
model (glint every in-scope object) is impossible here: the densest screens
hold ~159 objects at once against a 128-sprite OAM budget. So the cue is a
CAPPED nearest-N glint: `Rando_CollectTerrainGlints` returns the ≤20
(`kRandoTerrainGlintCap`) active + unchecked terrain objects nearest Link on
the current screen (area-map16 positions, nearest-first), and
`Rando_DrawTerrainGlints` (sprite.c, called from `Module09_Overworld` right
after `Rando_DrawOverworldEnemyMarkerGlints`, inside the BG-scroll-copy
window) draws the animated gold sparkle on each — reusing the proven
overworld enemy-glint machinery (free-sprite-palette-row scan avoiding
Link's row 7, the twinkle glyph set, the shared OAM allocator, and the NMI
gold-palette request). Object world coords come from the registry pos
(`world_x = (overworld_offset_base_x << 3) + col*16`, `world_y =
overworld_offset_base_y + row*16`, with `col = (pos & 0x7e) >> 1`, `row =
pos >> 7` — the inverse of the engine's `pos = row*128 + col*2`), then
`- BG2*OFS_copy2` to screen. It shows for BOTH active tiers (junk and all),
self-clears the frame after a check (the collector re-derives from the
checked bitmap), and is inert off-rando / for non-terrain seeds (a cheap
`Rando_SettingsNeedTerrainRegistry` early-out). The centering offsets, cap
count, and cull bounds are playtest-tunable (copied from the pot/enemy
glint). Not a placement or logic input — purely cosmetic.

## Risks / Trade-offs

- **[Scale unknown until the dump runs]** → Phase 1 is measurement-only and
  gates every ID/capacity decision; capacity bump path pre-planned with the
  ABI mitigations (D5). No code binds counts before the dump exists.
- **[Split-screen region binding errors]** → reviewed overrides + hard-fail
  on unreviewed splits + over-gate principle (D6); `all`-tier playtest
  targets override screens; junk tier is binding-error-tolerant by
  construction.
- **[Headless overworld area loading may fight the renderer]** (the pot dump
  needed `Dungeon_DrawRoomObjectsHeadless` because full room draws crash
  headless) → same technique: replay only the tilemap/attr build for each
  area, not the full frame path; validated by cross-checking dump counts
  against the extracted `Items:` secrets (890 entries must all be matched
  or explicitly excluded).
- **[Junk-pool inflation]** (potentially thousands of junk items) → pool
  builder and spoiler measured under grass+rock `all`; row caps audited
  (D5/D13). Refusal-rate check for `accessibility=locations` seeds goes in
  the corpus notes.
- **[Thick-grass cutter set wrong ⇒ swordless softlock or over-gate]** →
  cutter macro grounded + re-verified at implementation; swordless+grass
  playtest row (D14).
- **[Spawner-side vanilla leaks on the suppressed path]** (powder injection
  via `AdjustSecretForPowder`; random substitution via
  `Overworld_SubstituteAlternateSecret` on a zeroed secret byte) → suppress
  = skip `Overworld_RevealSecret` at the call site + set the `0xFF`
  no-spawn sentinel (D9); playtest rows cover powder-on-bush and grant
  cleanliness.
- **[Non-consume speculative reveals granting checks]** (bomb `label_a`
  probe, super-bomb follower) → hook lives at the consuming call sites
  only (D9); playtest row: bomb adjacent to a rock grants nothing.
- **[Multi-grant burst from one bomb]** (up to 9 consumed objects/frame) →
  quiet-receive retry + last-icon-wins; clustered-bomb playtest row.
- **[Spec rot found in adjacent requirements]** — randomizer-core's
  canonical table omits `dungeon_chains` in byte [25] bit 6 (as-built:
  `kEntranceAxis_DungeonChains = 1<<6`, rando_settings.h:297) and
  randomizer-save's "Sidecar slot contents" still describes format_version
  4 / 8-byte ext (as-built at rebase time: version 7 / 37-byte ext,
  rando_save.h — the souls merge bumped it again without updating that
  requirement).
  This change's core delta restates the canonical requirement to as-built
  truth in passing (flagged in the delta); the save delta ADDS a
  self-contained requirement instead of restating the rotted one, and the
  drift is reported to the owner for a separate reconcile.
- **[Two more LOCTYPEs multiply the type-enumeration sites]** → the pot D11
  checklist is re-run for both types in one pass; the generator asserts
  type-id agreement between YAML schema and the C enum.

## Migration Plan

Phased, each phase independently buildable (details in tasks.md):

1. **Measure**: dump tool + generator skeleton → real per-class counts; ID
   base + capacity decision locked; owner sees the numbers before code
   binds them.
2. **Registry + logic**: generator emits registry/overrides/lookup; logic
   injection; predicates + region overrides; codegen validation.
3. **Settings + placer**: axes, canonical bits, tier filter, junk-only
   class, selfchecks. `kGeneratorVersion` bump lands here.
4. **Runtime**: hook, grant, suppress, activation guard, sidecar ext v8.
5. **UI/reporting**: settings combos, trackers, spoiler, auto-tracker.
6. **Validation**: corpus entries + regen + 3-way diff, CI guards, selftest.
7. **Playtest matrix + independent fresh-eyes review** (per the audit
   cadence), then spec reconcile + archive on the branch, squash-merge.

Rollback: axes default off and every off-tier path is byte-identical, so
reverting = removing the axes; sidecar ext v8 readers tolerate shorter
(≤v7) files by format_version gating (existing mechanism).

## Open Questions

- Exact ID base and whether the capacity bump is needed — decided by Phase 1
  counts (owner sign-off on the numbers before Phase 2).
- Junk composition for the pad at terrain scale (flat vanilla junk
  distribution vs. weighting toward rupees/hearts for farm-feel) — owner
  preference, decidable at Phase 3 with no structural impact.
- Whether a findability cue (glint-like, or a screen-entry "N unchecked
  here" toast) is wanted after playtesting the tracker-only v1.
- Signs / bonk rocks as future micro-tiers (out of scope here).
