# Zelda3 Randomizer

> **Status.** The randomizer is well past its Phase A foundation. Phase B
> work — Inverted/Retro world-states, in-game trackers, hints, race-mode
> reveal, boss/drop shuffles, the native settings window, and more — has
> landed incrementally. For current scope and per-change status, see the
> OpenSpec changes under `openspec/changes/` (start at its `README.md`
> index). `kGeneratorVersion` in `src/rando/rando.h` is the authoritative
> marker of the live placement/serialization format; it advances whenever a
> placement-affecting change ships.

This document covers user-facing operation of the in-binary randomizer, the
share-string format, save behavior, audit conventions for contributors, and
the generator-version bump policy. The authoritative source for behavior
decisions is the OpenSpec spec baseline at `openspec/specs/randomizer-*/` (Phase A
was archived 2026-05-29 to `openspec/changes/archive/2026-05-29-add-randomizer-support/`).

## Getting started

The randomizer lives inside the same `zelda3` executable as the vanilla port.

1. Extract assets per the top-level `README.md` (one-time `python assets/restool.py --extract-from-rom`).
2. Build per the standard instructions (`make` on Linux/macOS; `Zelda3.sln` on Windows).
3. The randomizer activates on a per-slot basis from the file-select screen.
   On PC, seeds are configured in the native settings window (press `` ` ``);
   the in-game settings screen serves the same role on Switch. Headless CLI
   generation is the automation entry point (and what the regression corpus
   drives):

   ```sh
   ./zelda3 --generate-seed \
     --settings=mode.state=open,goal=fast_ganon,crystals.ganon=7,crystals.tower=7 \
     --seed=0xDEADBEEFCAFEBABE \
     --out-spoiler=./spoilers/demo.json
   ```

   See `randomizer-core / CLI generation mode` (in `openspec/changes/.../specs/`)
   for the full grammar and exit codes.

### CLI flags

| Flag | Effect |
|---|---|
| `--generate-seed` | Headless generation mode. Required to enter the rando pipeline; otherwise the binary boots the normal game. |
| `--settings=k=v,...` | Comma-separated overrides for any settings axis (table below). |
| `--seed=0x...` | uint64 seed value. Required for single-seed mode. |
| `--out-spoiler=<path>` | JSON spoiler path. Also writes a sibling `.txt` text spoiler. |
| `--out-share-string=<path>` | Optional file for the base32 share string (single line, no trailing newline). Writes the **v2** exchange string — the distribute-to-players form; customizer seeds fall back to v1 (see [Share-string format](#share-string-format)). |
| `--budget-seconds=<n>` | Bounds the placement retry budget (default 0). Exhausted budget accepts the best-so-far attempt. |
| `--assets-must-be-vanilla` | Refuses non-vanilla `zelda3_assets.dat` (compares against `kVanillaAssetsHash` in `src/rando/vanilla_assets_hash.h`). |
| `--allow-broken-seed` | Bypass the goal-completability refusal — writes a spoiler even when `goal_completable=false`. Diagnostic use only. |
| `--customizer=<path>` | Customizer mode: load a manifest that PINS a subset of locations to chosen items; the assumed-fill placer completes the rest. See [Customizer mode](#customizer-mode). |
| `--print-assets-hash` | Print the SHA-256 of the loaded `zelda3_assets.dat` and exit. Useful for baking the vanilla hash. |
| `--rando-selftest` | Run subsystem self-tests (SHA-256 vectors, RNG, settings, logic, placement, prize/medallion shuffles, boss shuffle, drop shuffle, save, textfield, dispatch) and exit. CI invokes this on every Linux / macOS / Windows runner. |
| `--race-mode` | Generate a race-mode seed. Overrides `--settings=race_mode=false`. The spoiler is written as a 138-byte suppressed `ZRSR` binary file at the same path (instead of full JSON + .txt sibling); reveal via `--reveal-spoiler` to expand. |
| `--reveal-spoiler=<path>` | Read a suppressed `ZRSR` file at `<path>`, validate magic + CRC + version + stamp, regenerate the placement, and overwrite the file in place with the full JSON. Writes a sibling `.txt` text spoiler. Exits 0 on success; non-zero with a numeric `kRandoReveal_*` code on any failure (CrcMismatch, VersionMismatch, StampMismatch, ParseError, FileNotFound). Idempotent: a second invocation on an already-revealed file is a no-op success. |

Examples:
```sh
# Print the asset hash to bake vanilla_assets_hash.h
./zelda3 --print-assets-hash

# Run the regression-corpus self-tests
./zelda3 --rando-selftest

# Generate a Completionist seed at hard pool difficulty with a 30-second budget
./zelda3 --generate-seed \
  --settings=mode.state=open,goal=completionist,item_pool=hard \
  --seed=0x1234567890ABCDEF \
  --budget-seconds=30 \
  --out-spoiler=./spoilers/comp-hard.json
```

## Settings reference

Full per-axis documentation lives in `randomizer-core / Settings canonical
serialization order`. The **Setting key** column below is the literal token you
pass to `--settings=key=value` (parsed by `Settings_ParseCsv` in
`src/rando/rando_settings.c` — an unknown key is a hard parse error, not a
silent ignore). Note a few canonical *axis* names differ from their CLI key:
the `world_state` axis is set via `mode.state`, and the item-pool-difficulty
axis via `item_pool`.

| Setting key | Values | Default |
|---|---|---|
| `mode.state` | `open`, `standard`, `inverted`, `retro` | `open` |
| `goal` | `ganon`, `fast_ganon`, `dungeons`, `pedestal`, `triforce-hunt`, `ganonhunt`, `completionist` | `fast_ganon` |
| `crystals.ganon` | 0..7 | 7 |
| `crystals.tower` | 0..7 | 7 |
| `item_pool` (alias `item.pool`) | `easy`, `normal`, `hard`, `expert` | `normal` |
| `mode.weapons` | `randomized`, `assured`, `swordless` | `randomized` |
| `accessibility` | `items`, `locations`, `none` (alias `beatable`; UI label "beatable only") | `items` (auto-set to `locations` for Completionist) |
| `dungeon_items.{small_keys,big_keys,maps,compasses}` | `vanilla`, `dungeon`, `wild` | `vanilla` |
| `prize_shuffle` | `true`, `false` | `true` |
| `medallion_shuffle` | `true`, `false` | `true` |
| `boss_shuffle` | `true`, `false` | `false` (playable, experimental; render via the Enemizer redirect model; see [Boss & drop shuffle](#boss--drop-shuffle-experimental)) |
| `drop_shuffle` | `true`, `false` | `false` (experimental, playable) |
| `traps` (alias `trap_frequency`) | `off`, `low`, `medium`, `high` | `off` |
| `region_boss_hearts_in_pool` (alias `region.bossHeartsInPool`) | `true`, `false` | Legacy/no-op. Accepted for old CSV/share compatibility, but canonicalized to `false`; boss-heart drops are always shuffled and the item-pool difficulty's boss-heart-container count always enters the item pool (10 Easy/Normal, 6 Hard, 2 Expert). Pin boss hearts with Customizer if desired. |
| `race_mode` (alias `race`) | `true`, `false` | `false` (the `--race-mode` flag is the canonical way to set it; see [Race mode](#race-mode)) |
| `pieces_required`, `pieces_placed` | uint16 | (Triforce Hunt / Ganon Hunt only) |

**Accessibility tiers** (ALTTPR three-way; all three guarantee the seed is
beatable, differing only in how much *extra* reachability the generator
enforces):

- `items` — "100% Inventory": every progression item is reachable. Junk
  (rupees/arrows/bombs), maps, compasses, and heart pieces may be unreachable.
- `locations` — "100% Locations": every location is reachable (strictest).
  Auto-selected and locked when the goal is Completionist.
- `none` — UI label **"beatable only"** (ALTTPR's "Not Guaranteed"): only the
  goal must be reachable; some items or locations may be unreachable. CSV accepts
  `beatable` as an alias. This still produces a beatable seed — the old
  "possibly unwinnable" behavior is no longer on this axis (use the CLI
  `--allow-broken-seed` flag for diagnostic seeds).

### Tricks & glitch logic

Two logic-relaxation axes let the placer assume the player will perform certain
out-of-logic techniques, opening locations that vanilla logic leaves unreachable.
Both default OFF; default seeds are byte-identical with or without this feature
(every trick/glitch gate collapses to `false`).

| Axis | Values | Default |
|---|---|---|
| `tricks` | `none` \| comma/`+`-joined trick ids \| `0xNN` or decimal mask | `none` |
| `logic` | `NoGlitches`, `OverworldGlitches`, `MajorGlitches` (`HybridMG`/`NoLogic` → Phase D) | `NoGlitches` |

`--settings=tricks=pearl-bypass+boots-clip` enables those bits (CSV uses kebab ids;
unknown names are a hard error). The 8 trick bits:

| id | wired? | maps to ALTTPR |
|---|---|---|
| `boots-clip` | yes (38 gates) | `config('canBootsClip')` |
| `fake-flippers` | yes (6) | `config('canFakeFlipper')` |
| `bunny-revival` | yes (50) | `config('canBunnyRevive')` |
| `dark-room-nav` | yes (42) | `item.require.Lamp=0` |
| `pearl-bypass` | yes (72) | `config('canOWYBA')` |
| `bomb-jump` | no | *(fork placeholder — no ALTTPR flag)* |
| `hookshot-clip` | no | *(fork placeholder — no ALTTPR flag)* |
| `lobotomy` | no | *(fork placeholder — no ALTTPR flag)* |

The 3 placeholder bits exist in the settings field but no logic predicate reads
them; enabling one has no placement effect (and surfaces the unverified warning).

> **Glitch seeds auto-enable the JP-glitch runtime flag (`add-rando-major-glitch`
> D6).** The `fake-flippers` *placement* trick only tells the placer it MAY assume
> a flipperless swim; the runtime side is the `[Features] RestoreJpGlitches` toggle
> (`kFeatures0_RestoreJpGlitches`, the "Restore JP 1.0 glitches" checkbox). To
> avoid routing an item behind a glitch the build can't perform, any seed that
> **assumes a restored JP-1.0 glitch** — `logic >= OverworldGlitches`, OR the
> `fake-flippers` trick enabled — now **forces** that flag on at runtime: at
> generate time (via the slot's `recommended_features0`) and on every slot load
> (`Rando_ActivateSidecarSlot`, so reloads and imported share strings are covered).
> Only `fake-flippers` among the tricks couples (it maps 1:1 to a restored glitch);
> a plain `logic=NoGlitches`/non-glitch-trick seed never gets the flag forced.
> On PC, the Seed QoL tab also exposes a per-slot "Restore JP 1.0 glitches"
> toggle for NoGlitches seeds that want the gameplay glitches as an opt-in play
> preference. It is recommended off, does not affect the settings hash/share
> string, and is separate from the cosmetic JP overworld music feature.
> Note this guarantees the runtime can perform the **restored** subset (Fake
> Flippers + Superspeed); an OWG/HMG/MG seed may still route through an
> un-restored technique (boots-clip, mirror-clip, water-walk, one-frame-clip, …)
> that this US-1.0 build does not perform — those tiers remain playtest-pending.

### Tricks / glitch logic — ROM-version verification

ALTTPR targets the **Japanese 1.0** ROM; this fork targets **US 1.0**. A trick in
ALTTPR's logic graph may have JP/US timing differences, different mechanics, or be
absent on US 1.0. Each entry in `assets/rando/op_registry.yaml`'s `tricks:` and
`glitch_levels:` tables carries a `rom_version_status`:

| status | meaning |
|---|---|
| `untested-on-us10` | in the upstream graph; nobody has confirmed it on US 1.0 (default) |
| `verified-us10` | performed end-to-end on a real US 1.0 build (named contributor + date) |
| `cross-version` | pure player skill (e.g. `dark-room-nav`) or verified identical JP↔US |
| `jp10-only` | confirmed NOT to work on US 1.0 — codegen **rejects** it in any gate |
| `us10-different` | exists on both ROMs but with different timing/mechanics |

When a seed enables a trick (or reaches a glitch level) whose status is
`untested-on-us10`, `jp10-only`, or `us10-different`, the spoiler's
`fallback_warnings` gains an `unverified_tricks_enabled` entry naming the offenders
— informational, so race admins / seed validators can decide whether to accept.
`cross-version` and `verified-us10` never warn. The current baseline ships every
wired trick as `untested-on-us10` except `dark-room-nav` (`cross-version`) and
`fake-flippers` (`verified-us10` — Fake Flippers was restored + playtest-confirmed
by `add-jp-glitch-restoration`, and `add-rando-major-glitch` D6 force-enables the
runtime flag for any fake-flippers seed, so the assumed swim is always
executable). The glitch *levels* stay `untested-on-us10` — each bundles
techniques the JP-glitch restoration does not cover (`canSuperSpeed` is performable
but has no isolable registry entry; the rest are un-restored). Per-trick/-tier
US-1.0 verification is a follow-on playtest workstream.

**Upgrading a trick to `verified-us10`** (contributor guide): perform the trick
end-to-end on a real US 1.0 build at the gated location, record the date + your
handle, change `rom_version_status` in `op_registry.yaml`, regenerate
(`python assets/rando_logic_gen.py`), and note the evidence in the change's
`audit.md`. A `jp10-only` finding is more urgent — set the status (the codegen will
then fail any gate that references it, forcing the predicate to be removed).

### Swordless mode (`mode.weapons=swordless`)

No swords are placed in the pool; the hammer (and bug-net) stand in for the sword
everywhere the game normally requires it. Ported from ALTTPR's `setSwordlessMode`
(`Rom.php`), reimplemented in C rather than as ROM patches. Default seeds (any
non-swordless `mode.weapons`) are byte-identical — every swordless branch is gated
on `mode_weapons==swordless` / `Rando_IsSwordlessActive()`.

**Logic** (predicate op `OP_MODEWEAPONS_EQ`, all inert under the default):
- **Ganon**: `swordless ? (Hammer + silver arrows) : Master Sword`. Silver arrows
  are required *only* in swordless (ALTTPR `region.requireBetterBow`).
- **Agahnim 1 & 2**: Hammer or Bug-Catching Net (reflect his energy with either).
- **Medallion casts** (Ether→Misery Mire, Quake→Turtle Rock): no sword needed.
- **Bosses**: Kholdstare (melt + Hammer) and Trinexx (Fire+Ice Rod + Hammer) gained
  their swordless Hammer-kill path; every other boss already had a non-sword option.
- **Tablets** (Ether/Bombos): read with Book of Mudora + Hammer.
- **Pool**: swords removed, a Silver Arrow Upgrade guaranteed (Ganon needs silvers).

**Runtime** (C equivalents of the `setSwordlessMode` ROM writes):
- Hammer damages **Ganon** (the melee-immunity threshold rises so his phases take
  hammer hits; silver arrows still finish him).
- **Medallions** cast without a sword; **tablets** grant on Hammer ownership.
- The **Agahnim-Tower altar** + **Skull-Woods back-entry** curtains are pre-opened in
  the slot's SRAM at generation.
- The **Evil Barrier** at the Agahnim-Tower overworld entrance breaks with the Hammer
  (approach from the side — the passive proximity zap is unchanged from vanilla).

**Status:** validated end-to-end by playtest (a full swordless game was beaten,
2026-06-04) on US 1.0. The `b-swordless-*` corpus seeds pin the swordless placement.
Combining swordless with the (experimental, runtime-disabled) boss shuffle is not
supported — see the boss-shuffle note below.

### Boss & drop shuffle (experimental)

Two opt-in shuffle modules (`add-rando-shuffles-and-minigames`). Both default
**off** and are byte-identical to vanilla when off. Their per-seed assignment is
*not* stored in the slot; it is regenerated deterministically from
`(settings, seed)`. Determinism is pinned by `BossShuffle_SelfCheck` /
`DropShuffle_SelfCheck` (part of `--rando-selftest`).

- **Drop** shuffle is **orthogonal to item placement** — turning it on never
  changes `placement_digest` / `sphere_digest` (the corpus asserts a drop-on
  entry equals its drop-off twin).
- **Boss** shuffle WAS orthogonal, but as of **kGeneratorVersion 56** it affects
  placement: each dungeon's `- Boss`/`- Prize` now gates on the *shuffled* boss's
  kill predicate (see the boss note), so a `boss_shuffle=true` seed can move
  placement/spheres vs its boss-off twin. `boss_shuffle=false` (the default, and
  every non-boss corpus entry) stays byte-identical.

**Status:** drop shuffle is **playable**; boss shuffle is now **playable
(experimental)** — beatability logic + the runtime render (the
Enemizer redirect model) are both live and the UI toggle is enabled. A few bosses
in non-home rooms have known cosmetic/spawn quirks (see the boss note); playtest
feedback wanted.

- **`drop_shuffle`** (playable) — permutes the 56-entry enemy drop-prize table
  (`kPrizeItems`, 7 packs × 8 slots). A **heart-drop floor** guarantees pack 0
  (the vanilla heart-heavy pack that weak overworld enemies draw from) keeps at
  least one heart after the shuffle, so you are not HP-starved early. If the
  bounded re-roll budget is exhausted (≈1e-16 chance) the table falls back to the
  vanilla identity and the spoiler records a `drop_heart_floor_fallback` warning.
  The shuffled packs appear in the spoiler under `drop_tables` (resolved drop
  item ids; pack 0 first). Installed at slot load (`Rando_ActivateSidecarSlot`)
  and consumed at the sprite-drop site (`ForcePrizeDrop`). Drop sprites use the
  always-loaded common prize GFX, so a shuffled drop renders correctly.

  The enemy→pack binding is static (per sprite type), not sphere-indexed, so the
  heart floor is enforced structurally on pack 0 rather than against sphere data
  — the faithful realization of the spec's "a tier reachable in spheres 0-2 keeps
  a heart" in this fork's drop model.

- **`boss_shuffle`** (playable, experimental) — randomizes which boss guards each
  of the 10 shuffleable dungeon boss rooms (EP, DP, ToH, PoD, SP, SW, TT, IP, MM,
  TR; Agahnim 1/2 + Ganon pinned) and emits the assignment in the spoiler under
  `boss_assignments`. The dungeon->prize binding is unchanged. Current behavior:
  the logic graph tracks the shuffled boss (beatability) AND the runtime renders
  it (the Enemizer redirect model); both are live and the UI toggle is enabled.

  > ✅ **Beatability.** The "second gap" the earlier note
  > flagged is now closed in logic. Each dungeon's `"<Dungeon> - Boss"`/`- Prize`
  > location gates on a new predicate-VM op **`OP_CAN_KILL_BOSS(dungeon)`** (macro
  > `CanKillBoss`) that resolves the dungeon's *currently assigned* boss and
  > evaluates *that* boss's kill predicate — so a fire/ice-gated boss (Trinexx,
  > Kholdstare) shuffled into a fireless-reachable dungeon can no longer strand
  > its prize. With `boss_shuffle` off the op resolves to the vanilla boss, so
  > default placement is byte-identical. Headless-guarded by `Logic_SelfCheck`
  > (direct op resolution) + `Placement_SelfCheck` (boss_shuffle=1 seeds across
  > goals are completable with 0 unreachable). This corrects design.md D6.
  >
  > ✅ **Render (live, experimental — the Enemizer pointer-redirect model).** When
  > a shuffled boss room loads, `Rando_ActivateSidecarSlot` has installed the boss
  > assignment and `BossShuffle_RenderHomeRoom` redirects BOTH the room's
  > sprite-data list AND its sprite-graphics index to the assigned boss's vanilla
  > HOME boss room (`dungeon.c` / `sprite.c`). The home room already holds the
  > correct boss formation (right count, coords, trigger overlords) drawn with the
  > correct gfx, so the substituted boss renders + fights like its vanilla self.
  > (This replaced the earlier per-entry sprite-TYPE swap, which rendered garbage
  > and mis-spawned formation bosses.) Off / vanilla play is byte-identical (the
  > redirect returns a no-op). The render is PLAYTEST-ONLY validated — it could not
  > be confirmed headless. KNOWN RISKS. **Blind** is now PINNED to Thieves' Town, not
  > shuffled): TT has no Blind sprite — it spawns Blind via a maiden sequence + a
  > TT-only state bit (`dung_savegame_state_bits & 0x2000`), so a Blind shuffled
  > elsewhere never spawned (playtest-confirmed strand). Pinning is the fix;
  > making Blind shuffleable is enemizer-class (synthetic 0xCE + forcing the
  > 0x2000 gate + maiden suppression both directions) and is deferred.
  > **Trinexx / Kholdstare** in NON-home rooms miss their room-shell object-layer
  > setup (Enemizer special-cases these too) — may look wrong; killability TBD by
  > playtest (still in the pool pending confirmation). The other shuffleable
  > bosses use the redirect + spawn-coord alignment cleanly.
  >
  > **Swordless interaction** (review note): now that boss
  > shuffle is runtime-live, a remaining caveat — the per-seed boss-kill override
  > MUST be swordless-aware — ALTTPR forbids Kholdstare/Trinexx outside their home
  > dungeons under swordless (`canPlaceBoss`, e.g. `Region.php:98-105`,
  > `GanonsTower.php:151`) because their swordless kills (hammer + melt / fire+ice)
  > are tighter than a sword. Today this is **moot** — boss shuffle is a
  > runtime passthrough and is logic-decoupled, so a swordless slot always faces
  > vanilla bosses in their vanilla rooms, which the swordless logic + runtime
  > handle correctly. Exclude Kholdstare/Trinexx from the shuffle under swordless
  > (or gate them) at the same time the predicate override lands.

The drop-shuffle toggle is exposed in the PC native settings window under
"Shuffles (experimental)"; the boss-shuffle toggle is shown disabled there. The
shuffled-drops *visuals* are verified only by playtest — the headless checks
above cover determinism + the structural invariants.

### Door shuffle (experimental)

`door_shuffle=basic` (`add-rando-door-shuffle`) randomizes each dungeon's
**interior door-to-door connections** — walking through a door can land you in
a different room of the same dungeon than vanilla, and the small-key doors are
relocated onto the new connections (vanilla per-dungeon counts, validated by a
key-door softlock prover). Ported from ALttPDoorRandomizer's basic mode at
intensity 1 (normal doors + spiral staircases shuffle; holes, warps, straight
stairs and open edges stay vanilla).

MVP constraints (normalized automatically — the settings hash always matches
the actually-generated seed):

- Open/Standard world states + NoGlitches logic only (vanilla doors otherwise).
- Mutually exclusive with entrance shuffle (door shuffle yields).
- Forces in-dungeon small + big keys.
- **Hyrule Castle and Swamp Palace keep vanilla doors** (HC: the forced escape
  start + Zelda escort; Swamp: its water-level state machine).
- The dungeon map still shows room *positions*; with shuffled doors its implied
  adjacency no longer matches the connections.

The layout is **not stored in the save** — it regenerates from
`(seed, settings, door_attempt @76)` at slot activation, and a persisted
24-bit layout digest (@77-79) **hard-fails activation on mismatch** (a drifted
interior layout could make the certified-beatable placement unbeatable, so a
door-shuffle slot refuses to load on a build that regenerates a different
layout; entrance shuffle uses the same digest gate via the sidecar-v3
`entrance_digest24` — only pre-v3 entrance slots keep the old non-blocking
version-drift warning). The
spoiler gains a `door_shuffle` section listing every pairing + the relocated
key doors with their worst-case key thresholds. Logic-side, dungeon-interior
reachability is computed by the same crystal-barrier-aware explorer the
generator uses (single model — placer and generator cannot drift), seeded from
the entrance lobbies reachable under current logic.

`--door-selftest` runs the generation net headlessly (connectivity, prover
acceptance, determinism, oracle/stitcher agreement for every shuffleable
dungeon across many seeds).

## Customizer mode

Customizer mode hand-places specific items at specific locations. A manifest
file PINS a subset of locations; the assumed-fill placer then fills every other
location exactly as in a normal seed (faithful to ALTTPR's partial-manual +
random-fill customizer). The runtime dispatcher is unchanged — a customizer
placement is indistinguishable from an assumed-fill one.

**Headless generation** (the shipped slice):

```sh
./zelda3 --generate-seed \
  --settings=mode.state=open,goal=fast_ganon \
  --seed=0x1 --out-spoiler=./out.json \
  --customizer=./assets/rando/customizer.example.yaml
```

**Manifest format** — a strict line-based YAML subset:

```yaml
placements:
  Eastern Palace - Big Chest: Hookshot
  Link's Uncle: ProgressiveSword
  Desert Palace - Big Chest: ProgressiveBow
```

- **Location keys** accept either the canonical symbol form (`Eastern_Palace_Boss`)
  or the human form printed in the spoiler (`Eastern Palace - Boss`). Both resolve
  through a normalized (lowercase, alphanumeric-only) match, so punctuation and
  case don't matter.
- **Item values** use the item-registry names (`Hookshot`, `TitanMitt`,
  `ProgressiveSword`, …; see `assets/rando/item_registry.yaml` or any spoiler).
- A sample manifest ships at `assets/rando/customizer.example.yaml`.

An optional `pool_overrides:` section adjusts the item pool before fill:

```yaml
pool_overrides:
  add: [ProgressiveSword, ProgressiveSword]
  remove: [Rupoor]
```

- `remove` is best-effort — an item that isn't in the (settings-dependent) pool
  is a silent no-op. `add` inserts items into the correct tier; it cannot add
  prize/event items (no grant path). `remove` then `add` apply before pins, so a
  pin can consume an added item. Adding more items than open locations drops the
  excess junk at fill time; removing more than you add leaves some locations on
  their vanilla item.
- **Per-item grant caps** are enforced: the pool after overrides plus any
  out-of-pool pins may not exceed what the game can actually grant — 4
  `ProgressiveSword`, 3 `ProgressiveShield`, 2 `ProgressiveArmor`,
  2 `ProgressiveGlove`, 2 `ProgressiveBow`, and 4 bottles **total** across all
  bottle variants (Link has 4 bottle slots). An over-cap manifest is refused
  with an error naming the item and the cap; remove pool copies (e.g.
  `remove: [BottleEmpty]`) to make room for a pinned variant.

**What you can pin.** Ordinary item locations (chests, NPCs, freestanding items,
boss-heart drops, and dungeon item slots in a non-vanilla dungeon-item mode). The
generator REFUSES a pin on a computed-item location — dungeon prizes, the Misery
Mire / Turtle Rock medallion tablets, Retro shop / capacity-upgrade / take-any
slots — or on a slot the current settings already vanilla-place (a vanilla-mode
dungeon key). The error names the location and the reason.

**Completability.** The pinned seed runs the same goal/accessibility refusal gate
as a normal seed: an un-completable hand-placement is refused (the spoiler is not
written) unless you pass `--allow-broken-seed`. Generation is deterministic — the
same manifest + seed + settings always produce the same placement.

`customizer_active` participates in `settings_hash` (canonical byte `[26]` bit1);
`customizer_seed = sha256(manifest_bytes)[0..8]` identifies the manifest. With
customizer mode off, every byte of generation is unchanged (the regression corpus
is byte-identical).

**Playable slots + the native window.** The PC settings window (Randomizer →
General → "Customizer") has the same flow: enable the toggle, enter the manifest
path, "Load manifest" (inline error, or pin-count + pool summary on success),
then "Generate from manifest & start new slot". The slot persists the pinned
placement, so the manifest is needed only at generation time — reloading the
save never re-reads it. The headless slot seam mirrors the window:
`--generate-slot --customizer=<path>` produces the identical `placement_digest`
as `--generate-seed` for the same settings/seed/manifest.

> **Race mode is refused with customizer mode** (CLI and slots): the race
> reveal regenerates placement from seed + settings alone and cannot reproduce
> manifest pins. Share-string encoding of `customizer_seed`
> (reproduce-by-manifest across users) is still deferred.

## In-game item behavior

Several items that vanilla packs two-into-one byte are shuffled as independent
items by the randomizer, so their grant/selection behavior differs from vanilla:

- **Boomerang** and **magic upgrade** are *progressive*: the **1st** one you
  collect is always the lower tier (blue boomerang / ½ magic) and the **2nd** is
  always the higher tier (red boomerang / ¼ magic), regardless of which item the
  seed actually placed. You can never be downgraded, and no pickup is wasted.
- **Bow** keeps item identity but **never downgrades**: a wood bow grants wood, a
  silver-arrow upgrade grants silver, and collecting the lower one after the
  higher one does nothing (your silver bow stays silver). (Bow is distinct from
  boomerang here because wood-vs-silver is a real logic requirement.)
- **Sword / shield / gloves / mail** likewise never downgrade if a seed hands you
  a lower tier after a higher one.

**Item-menu swap (Press A):** when you own *both* tiers of a shared-slot item,
highlight that slot in the inventory menu and press **A** to swap which tier is
selected — **flute ↔ shovel**, **blue ↔ red boomerang**, and **wood ↔ silver
arrows** (the bow). The selected tier persists across save/reload. (Useful e.g.
to fire wood arrows instead of consuming silver, or to use the shovel after the
flute became your selected Y-item.)

## Share-string format

Share strings are single base32 tokens (uppercase, no padding) carrying a
CRC-16-CCITT-FALSE checksum and a 4-byte magic prefix unique to this port.
There are two wire formats; the decoder accepts both and dispatches on the
magic.

**v1 — magic `ZRSS`, exactly 50 characters.** Payload layout:
`magic[4] | generator_version[1] | settings_hash[16] | seed_u64[8] | crc16[2]`
(31 raw bytes before base32).
The settings hash is one-way, so pasting a v1 string restores **only the
seed** — if your current settings don't match the sharer's, you get a
settings-mismatch warning. v1 strings decode forever; strings minted by
earlier releases are never orphaned. v1 also remains the **stored identity**
form everywhere a share string is persisted or compared: the save-sidecar
slot (the 31-byte raw blob plus one pad byte), the suppressed `ZRSR` race file,
the spoiler filename and the spoiler JSON's `meta.share_string`, the 5-icon
visual hash, and the file-select banner prefix.

**v2 — magic `ZRS2`, exactly 71 characters.** Payload layout:
`magic | generator_version | settings_len | settings_canonical | seed_u64 | checksum`.
v2 embeds the **full canonical settings plus the seed**: pasting one restores
every setting AND the seed, and pins the seed so Generate reproduces the
sharer's placement instead of rolling a new one. The native settings window's
share-string display/copy and the CLI's distribution outputs
(`--out-share-string`, the `share_string_v2:` summary line) emit v2.

Version handling on paste:

- A v2 string from a **different generator version** still restores settings
  and seed, with a warning that Generate may produce a different placement
  than the sharer's.
- A v2 string whose settings payload is **larger** than this build
  understands is refused outright — it was made by a newer version; update to
  use it. (Honoring a prefix would silently generate a different seed.)

**Customizer seeds have no v2 form**: their placements depend on a local
manifest file that no share string can carry, so copy and CLI emission fall
back to the v1 string, and pasting a v2 string with the customizer bit set is
refused.

**Generate confirmation:** after a successful paste, editing any setting and
then pressing Generate opens a confirmation modal — settings no longer match
the pasted share string — instead of silently generating a different seed.

v2 is transport-only, which has one visible consequence: the window shows the
71-character v2 string while the file-select banner prefix comes from the
slot's stored v1 identity string, so the two differ. Banner-prefix matching
between friends still works — both slots store the same v1 string for the
same seed.

Both formats are distinct from alttpr.com's share format; **the two are not
cross-compatible in either direction** (a deliberate choice — different
generator, different placement output for the same notional "seed"). An
alttpr.com-style hash is rejected with an explicit format-mismatch error.

`Share_SelfCheck` round-trips both encodings and exercises the explicit-reject
paths (alttpr.com format, corrupted base32, wrong-length input, wrong magic
prefix, newer-version v2 payload).

## Save behavior

The randomizer's per-slot state lives in `saves/sram_rando.dat` — a sidecar
file alongside the existing `saves/sram.dat`. The vanilla save file is
**byte-untouched** by randomizer mode, so a vanilla-only binary sees those
slots as vanilla. Sidecar layout:

- 16-byte file header (magic `ZRSC`, format_version, slot_count, file_crc —
  a CRC-32 over the slot region, verified on load; `0` = legacy file, accepted
  without verification).
- 3 slots × {80-byte header + embedded placement table + checked-location bitmap
  + (format_version ≥ 2) a 28-byte canonical `RandoSettings` blob
  + (format_version ≥ 3) an 8-byte slot extension block}.
- No 4th slot anywhere.

Slot header records: `slot_kind`, `generator_version`, `settings_hash`,
`share_string` (the stored v1 identity blob: 31 raw bytes plus one pad byte),
`last_vanilla_write_version`, `sram_slot_checksum_at_last_write`,
`placement_table_size`, `flags`, `mushroom_held`, hints/`goal`/`world_state`/
`flute_shovel_owned` ext bytes, (`@70`) `settings_present`, (`@71`/`@72`)
entrance-shuffle axes/attempt, (`@73`/`@74`) `boomerang_owned`/`bow_owned`
(the progressive/swap ownership bitfields), (`@75`) `prize_attempt`, and
(`@76`-`@79`) the door-shuffle `door_attempt` + 24-bit layout digest. The
80-byte header is fully claimed, so further additive fields land in the
format_version ≥ 3 slot extension block.

**format_version 2** (added with the rich tracker windows): each slot appends a
`kSettingsCanonicalLen`-byte canonical `RandoSettings` blob after the checked
bitmap. This lets a reloaded slot reproduce the seed's full settings *and*
recompute the prize/medallion shuffle assignments (from the share string's seed,
in the exact placer order) — both of which the runtime reachability engine
needs. Older v1 slots have no blob and load unchanged via the version-aware
deserializer (`RandoSave_ReadFile` keys body layout on the file
`format_version`); on such slots `settings_present` is forced off and the
trackers **suppress** the reachability display rather than guess (a wrong
`prize_shuffle` flag would mis-seed the shuffle stream). The blob size is coupled
to `kSettingsCanonicalLen` by a `_Static_assert`; the round-trip + a v1-compat
case are covered by `RandoSave_SelfCheck` (`--rando-selftest`).

**format_version 3**: each slot appends a fixed 8-byte extension block after
the settings blob (the 80-byte slot header is fully claimed). It carries
`entrance_digest24` — the entrance-shuffle analogue of the door-shuffle
`door_digest24`: a 24-bit digest over everything the runtime entrance install
regenerates from (seed, axes, attempt). Activation recomputes it and **refuses
the slot on mismatch** (a drifted entrance layout can make the
certified-beatable placement unbeatable). Older v1/v2 slots have no block,
read digest 0, and keep the previous warn-only version-drift behavior; a
v2-compat load case is covered by `RandoSave_SelfCheck`.

Atomic-commit: write `<file>.tmp`, fflush, fsync (POSIX) / `_commit` (Windows),
rename atomically. Save order: sidecar first, then `sram.dat`.

Cross-version forward-compatibility (per `randomizer-save / Embedded placement
table — upgrade safety`): a slot written by `generator_version = N` loads on a
binary with version `N+1` and surfaces a one-time informational warning. The
embedded placement table is consulted; no regeneration is required. Exceptions:
door-shuffle and (format_version ≥ 3) entrance-shuffle slots hard-fail
activation when their regenerated layout digest no longer matches the stored
one — those layouts are regenerated, not embedded.

## Race mode

Race-mode seeds suppress the spoiler at generation time so tournament admins
can distribute the share string without leaking item placement. Reveal happens
later via `--reveal-spoiler` (the binary regenerates and overwrites the file
in place with the full JSON).

**Generation**: pass `--race-mode` (or set `race_mode=true` via `--settings`).
The spoiler path receives a 138-byte binary file with magic `ZRSR` instead
of the usual JSON + .txt pair. File layout (all multi-byte fields LE):

```
+0    4 bytes   magic "ZRSR"
+4    2 bytes   generator_version (u16 LE)
+6    32 bytes  spoiler_stamp[32] = SHA-256 of the canonical revealed JSON
                (with race_mode and wall-clock fields normalized to 0)
+38   4 bytes   share_string_len (u32 LE)
+42   64 bytes  share_string (zero-padded)
+106  28 bytes  settings_canonical (= kSettingsCanonicalLen)
+134  4 bytes   crc32 (IEEE 802.3 over bytes 0..133, LE on disk)
```

Total: 138 bytes. The settings are public on race sheets, so including the
canonical bytes does not leak the placement — they're needed at reveal time
to regenerate.

**Reveal**: `--reveal-spoiler=<path>` runs the full validation chain — magic,
CRC32, generator-version match, settings canonical-deserialize, share-string
decode, `Place_AssumedFill` regenerate, `Logic_ComputeSpheres`, compare stamp
against a re-hashed canonical revealed JSON; on match, rename the
`.reveal-tmp` over the original suppressed file and write a sibling `.txt`
spoiler. Exit codes (`kRandoReveal_*`):

| Code | Meaning |
|---|---|
| 0 | Ok — file overwritten with full JSON (or no-op success if already revealed) |
| 1 | FileNotFound |
| 2 | ParseError — file lacks `ZRSR` magic or has unexpected size |
| 3 | CrcMismatch — file is corrupt or tampered |
| 4 | ShareStringMismatch — caller-provided expected_share_string doesn't match |
| 5 | VersionMismatch — file was produced by a different `kGeneratorVersion` |
| 6 | SettingsCorrupt — `settings_canonical` failed deserialization |
| 7 | StampMismatch — regenerated placement does not produce the recorded SHA-256 (bug, file tampering, or undetected determinism drift) |
| 8 | PlacementFailed — `Place_AssumedFill` could not regenerate |
| 9 | WriteFailed — partial-write failure during the rename step |

The reveal action is **idempotent**: a second invocation on an already-revealed
file (first byte `{` instead of `Z`) returns Ok without rewriting. The
regression corpus exercises the round-trip via `run_rando_corpus.py`'s ZRSR
sub-path — the race-mode entries in `tests/rando_corpus/manifest.yaml` (labels
prefixed `b-race-`, plus `*-race` variants covering entrance shuffle and
Triforce-Hunt) are part of the determinism CI matrix. Because the stamp is over
the full canonical JSON, these entries also transitively assert that the
regenerated `hints[]` and `entrance_mapping` sections are byte-identical at
reveal — a regression in either fails the round-trip.

**Tamper detection**: any single-byte flip in the suppressed file produces a
CRC mismatch on read; reveal returns code 3 without touching the original.
Hand-crafting a file with a divergent `generator_version` + recomputed CRC
returns code 5 (`VersionMismatch`) — the gen-version check fires before
expensive placement regeneration.

## Hints

Hints are **on by default** (`hints=on`; binary on/off, part of the canonical
settings since `kGeneratorVersion` 14). They never affect placement or logic —
hint text is generated *after* placement from a sub-RNG seeded by the placement
digest, so the same `(settings, seed)` always yields byte-identical hints.

**Telepathic tiles (in-game).** Reading any of the 15 vanilla telepathic tiles
surfaces a generated item-location hint instead of its vanilla flavor text. The
messaging engine (`Text_LoadCharacterBuffer`) gives the hint system first refusal
on the 15 vanilla tele-tile message IDs via `Rando_RenderHintMessage`; the
substitution is gated on an active rando slot, so vanilla play (and the RAM
compare) is unchanged. `hints=off` leaves the vanilla tile text in place.

**Murahdahla.** On Triforce-Hunt / Ganon-Hunt goals an extra hint summarizes how
many Triforce pieces are spread across how many regions. It is **spoiler-only
today** — the in-game Murahdahla NPC is an ALTTPR asm-added sprite the fork has
not ported, so there is no in-game surface for it yet.

**Fork-extension NPCs** (Storyteller + the Kakariko / Dark-World Fortune Tellers)
route existing vanilla NPC dialogue through the hint generator for extra in-game
hint locations. These are a fork addition (not in ALTTPR); spoiler-JSON keys for
them are `fork_`-prefixed so the ALTTPR-equivalent core (15 tiles + Murahdahla)
stays diff-clean. The Lake-Hylia Fortune Teller is not wired — it shares its room
index with the Kakariko one, so it has no runtime discriminator.

**Spoiler + race mode.** The JSON/text spoiler carries a `hints[]` array; under
race mode it is suppressed inside the ZRSR until `RevealSpoiler`. Because the
reveal stamp is over the full canonical JSON, the race-mode corpus entries
(including a Triforce-Hunt variant that exercises the Murahdahla path) assert hint
determinism: a drift in regenerated hint text fails the reveal round-trip.

## Cosmetics

Cosmetic shuffles (palette / sprite / music) are **client-local presentation**
settings, deliberately kept *outside* the seed. They never touch the placement
table, share string, `settings_hash`, or any canonical serialization — so they
cause **no `kGeneratorVersion` bump and no corpus change**, and two players on the
same `share_string` with different cosmetics play a byte-identical game that merely
*looks* different (the tournament-decoupling property). Each axis defaults OFF, so
an unopted run — rando or vanilla — is byte-identical to the original game (the
RAM/PPU compare stays clean).

They are configured via `zelda3.ini` (the normative source; the PC native settings
window has a convenience "Cosmetics" tab that writes the same keys):

| Key | Section | Values | Meaning |
|---|---|---|---|
| `CosmeticSeed` | `[Graphics]` | u64 (accepts `0x…`); default `0` | Seeds the cosmetic RNG. **`0` ⇒ derive from the active slot's `seed_u64`** (a fresh user still gets a shuffled-but-reproducible look); any non-zero value overrides. |
| `PaletteShuffle` | `[Graphics]` | `vanilla` \| `shuffled` \| `grayscale` \| `negative` (default `vanilla`) | BGR555 CGRAM transform applied after the vanilla palette flush. `shuffled` = per-16-color-group channel permutation; `grayscale` = luma collapse; `negative` = per-channel complement. |
| `SpriteShuffle` | `[Graphics]` | `off` \| `<folder>` (default `off`) | Deterministically picks one `.zspr` from the folder (filenames sorted with a locale-independent ASCII comparator so the pick is identical on every platform) and feeds it to the existing ZSPR loader. Resolved at launch. |
| `MusicShuffle` | `[Sound]` | `off` \| `on` (default `off`) | Remaps area background songs (band `[0x01,0x0F]`) via a Fisher-Yates bijection — no song is silenced or duplicated; control codes and out-of-band ids pass through. Game-facing music state is unchanged; only the audible output differs. MSU-1 keys off the remapped id. |

The cosmetic RNG is a **separate stream** from the placement/fill RNG, so cosmetics
can never perturb generation. Determinism (same effective seed ⇒ identical look on
every platform) is asserted in CI by `Cosmetic_SelfCheck` (part of
`--rando-selftest`).

**Out of scope** (tracked elsewhere or deferred): gimmick palette modes
(dizzy/sick/puke/blackout — need per-frame animated transforms); heart color /
heart-beep / menu speed / quickswap (cosmetic *setters*, not shuffles — a separate
QoL change); enemy/enemizer shuffle (gameplay — `add-rando-shuffles-and-minigames`).

## Field item sprites

Under an active randomizer slot, free-standing item locations draw the **placed**
item's graphic instead of the vanilla sprite — a Piece-of-Heart spot that grants
the Bow shows a Bow, a Library book randomized to a red rupee shows a red rupee.
It's **on by default and has no UI** (a hidden `FieldItemSprites` key in
`[Graphics]`, default `1`, exists only as a dev/escape-hatch). Purely visual:
gated on `kFeatures1_RandomizerActive`, so vanilla play is byte-identical, and it
**never** touches placement / share string / `settings_hash` / `kGeneratorVersion`
/ the corpus (a draw-only consumer of the placement table).

**How it works.** The draw resolves the location's placed item with
`Placement_Lookup`, maps it to an LttP receive code via the same chain the grant
uses (`Rando_VanillaItemForRegistryId` + the progressive boomerang-colour remap,
then `progressive_to_lttp`), and renders the receive-animation graphic exactly as
`Ancilla_ReceiveItem_Draw` does — gfx/size/palette indexed by that code
(`kReceiveItemGfx` / `kReceiveItem_Tab1` / `kWishPond2_OamFlags`), chars
`0x24`/`0x34` out of the shared receive-item VRAM slot. The slot is loaded on
demand (cached by `g_recv_item_slot_owner`, invalidated by any
`DecodeAnimatedSpriteTile_variable` call) so it survives an item receipt or
direct-grant icon repainting it. 8×16 items reserve their own OAM block so the
bottom tile can't be clobbered by a busy scene.

**Custom art** (`add-rando-field-item-custom-art`): the ALTTPR items with no
vanilla receive bundle get dedicated art:

- **Triforce Piece** — a 16×16 triforce tile (the MIT-licensed z3randomizer
  custom sprite, shipped as the `kRandoCustomItemGfx` asset; committed source
  `assets/rando/custom_item_gfx.png`).
- **½ / ¼ Magic** — the ALTTPR magic-decanter sprites (a green jar with a white
  ½ or ¼ fraction). These were previously audio-only: no field
  sprite and no confirmation icon.
- **Rupoor** — the vanilla rupee tile re-coloured by ALTTPR's `off_black`
  palette: a flat dark-grey rupee, the recognizable "this drains you" cue
  (previously it reused the green-rupee bundle and looked like a normal rupee).

The PNG is regenerated by `assets/scripts/gen_custom_item_gfx_png.py` from local
palette-indexed pixel rows and an authored preview palette; the preview palette
is ignored by the asset compiler. Custom-art items load their 8-colour palette
at draw time, so the colour is stable in every area, and they use the same art
as their grant-confirmation icon. While one is on screen — and until the next
room/area palette reload after it
leaves — other sprites sharing SP3's upper half are tinted (the same trade-off
ALTTPR makes, which doesn't restore the slot either). Adding the asset means
**`zelda3_assets.dat` must be regenerated** when updating across this change.

**Covered sites:** all standing Pieces of Heart (Zora's Ledge, Pyramid, Lake
Hylia, Spectacle Rock, Sunken Treasure, the cave/hideout PoH, ...), the Book of
Mudora, the Mushroom, the Master Sword pedestal (the placed item rises through
the pendant ceremony), and the boss-reward Heart Container (visible under
item shuffle when a non-heart item lands on a boss drop).

**Out of scope / limitations:**
- **Chests** stay closed (ALTTPR doesn't reveal chest contents).
- **Medallion tablets** stay tablets — they render a stone slab you read, not a
  floating item; the location still grants the placed item.
- ~~Items with no receive graphic fall back to the vanilla sprite~~ — every
  placeable item now has art (Triforce Piece, ½/¼ Magic, and Rupoor were the
  last holdouts; they have custom art — see above).
- **One field item per screen** renders its real graphic: the receive-item VRAM
  slot holds a single item at a time, so two *different* field items sharing a
  screen would show the same (last-loaded) graphic. Standing items are effectively
  always solo per screen, so this is documented rather than fixed; a dedicated
  per-item slot is the path if a real two-item screen ever turns up.

## Tracker windows (PC)

On PC (behind `Z3R_NATIVE_SETTINGS_WINDOW`, the same gate as the native settings
window) the randomizer ships three rich, separate-OS-window trackers that
auto-update from live game state — no RAM-watcher heuristics or emulator/tool
desync, the headline advantage of a native port:

- **Item Tracker** — a live grid of the **real HUD item icons** (decoded from the
  game's 2bpp HUD graphics + palette into an RGBA atlas; dimmed when not owned,
  with level/count overlays), plus per-dungeon small/big-key/map/compass tracking,
  prizes, hearts, and magic. Consumables (mushroom/powder & flute/shovel) use the
  rando-aware shared-byte ownership.
- **Check Tracker** — every location grouped by region, tri-state
  **checked / available (reachable, unchecked) / locked**, with region counts, a
  summary + progress bar, filters (hide-checked, only-available, search), and an
  optional "show items" spoiler toggle (off by default, force-hidden for race
  seeds). This is the direct "what can I do right now given my items?" view.
- **Map Tracker** — the **real light- and dark-world overworld maps** (decoded
  from the in-game Mode-7 map graphic; Light/Dark toggle) with status-coloured
  region pins overlaid (hover for the region's check list); dungeon interiors are
  listed in a panel below. The decoders are verified by dumping PNGs via the
  `--dump-overworld-map` / `--dump-item-icons` dev flags.

### Architecture

- **`imgui_host.{h,cpp}`** — a small multi-window Dear ImGui host. One
  `ImGuiContext` per window; every entry point saves/restores the current ImGui
  + SDL-GL context so the single-context settings window (left untouched) keeps
  working. Tracker windows are created hidden at startup and driven once per game
  frame from `main.c` (event routing + `Z3RHost_RenderAll`).
- **`tracker_windows.{h,cpp}`** — the three window bodies; opened from the
  settings window's **Trackers** tab or via the optional hotkeys
  `RandoItemTrackerWindow` / `RandoCheckTrackerWindow` / `RandoMapTrackerWindow`
  (default unbound; bind in `zelda3.ini`). Windows opened at seed setup persist
  into gameplay. The Trackers tab also has an **Apply tiled layout** button that
  opens all three trackers and tiles them around the game window (Check left,
  game center, Map/Item stacked right), plus an **Apply at startup** checkbox
  persisted in `saves/rando_window.ini`. The layout preset applies in windowed
  mode; fullscreen game windows are left untouched.
- **Reachability bridge** (`rando.c`) — `Rando_BuildRuntimeCounts` maps the live
  `g_ram` inventory into the logical `RandoCounts` the predicate VM reads (the
  logic macros accept the progressive form, so progressive counts satisfy every
  tier); `Rando_GetLiveReachability` runs `Logic_ComputeReachability` memoized on
  the reachability-state counter and snapshots the result out of the shared
  buffer. Requires the format_version 2 settings blob (see *Save behavior*);
  absent → reachability suppressed.

On PC the legacy in-game OAM-overlay trackers (`hud.c`) are compiled out — the
ImGui windows are the single tracker system, and the legacy toggle keys
(`RandoToggleItemTracker` / `RandoToggleLocationTracker`) open the corresponding
window. Switch (no native-window support) keeps the OAM overlay. Open the windows
during play via `Ctrl+I` / `Ctrl+C` / `Ctrl+M` (default-bound) or from the
settings window's **Trackers** tab.

**Known limitations / follow-ups:** region pins are hand-placed (no per-location
geographic pin coordinates yet). Exact per-window custom geometry persistence is
not implemented; use the tiled layout preset for a repeatable tracker setup.
(Keysanity reachability and the dark-world map background — earlier follow-ups —
are now implemented.)

## Auto-tracker (external clients)

For external tracker tools (EmoTracker, PopTracker, custom OBS overlays) the
binary can run a small, **opt-in** TCP server that re-emits the same state the
in-game trackers render — inventory, live reachability, and the checked-location
bitmap — as **newline-delimited JSON**. Clients subscribe to the stream instead
of peeking at emulator memory via `usb2snes` / SNI. Lives in
`src/rando/auto_tracker.{c,h}`; wired into `main.c` (init / per-frame service /
shutdown) and `config.c` (the `[AutoTracker]` section).

**Off by default.** When disabled the module is a no-op — no socket is opened and
the binary behaves byte-identically to a build without it. It is **observation
only**: it never writes `g_ram` or any game state, and it is **subscribe-only** —
clients receive the stream and cannot inject state back (any bytes a client sends
are discarded). It does not affect placement, RNG, or determinism, and is never
started under `--rando-selftest` or the headless CLI paths.

### Enabling

`zelda3.ini`:

```ini
[AutoTracker]
Enabled = true       ; default false
Port = 17400         ; default 17400
AllowRemote = false  ; default false -> bind 127.0.0.1; true -> bind 0.0.0.0
```

Or force-enable from the command line without editing the INI:

```
zelda3 --auto-tracker
```

Or start/stop it **live from the settings window** (PC): the **Trackers** tab has an
*"Enable auto-tracker server"* toggle with a status line (bind address, port, and
connected-client count). The toggle controls the listener for the current session;
the boot default plus the port / remote-access settings still come from the INI
above. (The toggle is part of the PC native settings window; the INI / CLI options
remain the way to enable it on Switch or in headless setups.)

**Security.** The listener binds `127.0.0.1` (localhost-only) by default; remote
machines cannot connect. `AllowRemote = true` binds `0.0.0.0` and exposes the
stream to the local network — opt in only on a trusted LAN. There is no inbound
command channel regardless of bind address.

### Wire protocol

A TCP stream of UTF-8 JSON objects, **one object per line** (`\n`-terminated).
On connect a client receives a one-time `catalog` message, then a full `state`
snapshot; thereafter a fresh `state` snapshot is sent whenever the rando state
changes (the same `reachability_state_counter` advance that refreshes the in-game
trackers — item grants, location checks, slot activation — plus active-slot and
goal-completion transitions). Each message is self-contained (a full snapshot, not
a delta), so a client only needs the latest line.

`catalog` — sent once per connection; maps the numeric location ids used in every
later `state` message to human-readable names, so a client never has to hardcode
this fork's id space:

```json
{"type":"catalog","locations":[{"id":0,"name":"Sanctuary","region":"HyruleCastleEscape"}, ...]}
```

`state` — the live snapshot. When no randomizer slot is loaded the line is minimal
(`active:false`); once a slot is active it carries the full inventory /
reachability / checked view:

```json
{"type":"state","msg":7,"counter":42,"active":true,"game_completed":false,
 "settings":{"world_state":"open","goal":"ganon","crystals_ganon":7,"crystals_tower":7},
 "items":{"sword":2,"shield":0,"mail":0,"gloves":1,"bow":1,"boomerang":0,"bottles":1,
          "magic":0,"hearts":4,"heart_pieces":1,"crystals":0,"pendants":1,
          "crystal_mask":0,"pendant_mask":1,"hookshot":true,"firerod":false, ...},
 "dungeons":[{"name":"Eastern Palace","small_keys":0,"big_key":false,"map":true,"compass":false}, ...],
 "reachable_available":true,
 "checked":[10,23,47],
 "reachable":[1,2,5,88]}
```

Field reference (`state`):

- `msg` — monotonic message sequence number for this server session.
- `counter` — the `reachability_state_counter` value the snapshot reflects.
- `active` — a randomizer slot is loaded. When `false`, only `type`/`msg`/`counter`/
  `active` are present.
- `game_completed` — the game has reached the Ending/Credits (goal beaten).
- `settings` — `world_state`, `goal`, `crystals_ganon`/`crystals_tower`
  (+ `pieces_required` for the hunt goals); `null` when the slot's settings can't
  be recovered (snapshot-restore / legacy slot).
- `items` — the live inventory view (mirrors the in-game Item Tracker). Tiered
  items are levels (`sword` 0–4, `bow` 0–2, `magic` 0–2 = 1×/½×/¼×, …); the rest
  are booleans. `crystal_mask`/`pendant_mask` are bitfields per prize.
- `dungeons` — per-dungeon `small_keys` count + `big_key`/`map`/`compass` flags.
- `reachable_available` — whether logic-based reachability is available (false on
  legacy slots whose settings can't be recovered → `reachable` is empty).
- `checked` — location ids the player has checked.
- `reachable` — unchecked location ids reachable now under current logic.

**Spoiler-safe by construction.** The stream exposes only the player's own
inventory, the locations they have *checked*, and which unchecked locations are
*reachable* — never which item sits at an unchecked location. So it carries no
placement spoiler and is safe even for race seeds without any race-mode gate.

**Switch.** Networking on Switch (libnx) is deferred; `auto_tracker.c` compiles to
no-op stubs there, so the Switch build links cleanly with the server omitted.

## Audit comment convention (for contributors)

Per `audit.md` §0.9, every write to a tracked inventory cell (`link_item_*`,
`link_bottle_info[*]`, `link_has_crystals`, etc.) MUST either:

1. Flow through `Rando_OnLocationCheck` (the §6 dispatch path); OR
2. Carry an explicit `// rando-exempt: <reason>` comment immediately above
   the write line.

Valid exemption reasons (per `audit.md` §0.2 classification):

| Tag | When to use |
|---|---|
| `state-shuffle` | The write preserves existing state (e.g., bottle drink → empty), not a new grant. |
| `cosmetic` | HUD redraw / animation only; does not affect game state. |
| `consumption` | The inverse of a grant (bomb use, arrow use). |
| `progress` | Story event flag; relevant to logic graph but not a §6 dispatch target. |

Example:

```c
// rando-exempt: state-shuffle — drink consumes the bottle contents
link_bottle_info[btidx] = 2;
```

`assets/scripts/check_audit_guard.py` enforces this convention in CI; it now
runs in `--strict` mode (see `.github/workflows/rando_ci.yaml`), so an
undispatched, un-exempted inventory write fails the build.

## Source-level CI guards (for contributors)

The `rando-source-guards` job in `.github/workflows/rando_ci.yaml` runs a set of
pure-Python checks that need **no build, no ROM, and no assets** — they read the
source and YAML directly, so they're cheap and run on every push/PR. The full
set lives in `assets/scripts/check_*.py`; the ones most likely to catch a
regression while authoring logic or bumping the generator:

| Guard | What it catches |
|---|---|
| `check_audit_guard.py --strict` | An inventory-cell write that neither dispatches through `Rando_OnLocationCheck` nor carries a `// rando-exempt:` reason. |
| `check_no_embedded_data.py` | A long inline hex/data blob that belongs in a gitignored generated artifact. |
| `check_determinism.py` / `check_byte_order.py` | Non-deterministic calls (`rand()`, `time()`, float) or unpinned byte order in `src/rando/`. |
| `check_codegen_wiring.py` | A generated file referenced in one build system but not all three (Makefile / MSVC / Switch). |
| `check_generator_version.py` | A change to a bump-trigger path (see the policy below) that forgot to advance `kGeneratorVersion` (PR-gated). |
| `check_corpus_version_sync.py` | The corpus manifest's `generator_version` drifting out of sync with `kGeneratorVersion` — including a digest-neutral bump that forgot to re-stamp the manifest. |
| `check_logic_overrides.py` | A later base-level logic YAML **silently overriding** a location / macro / region declared in an earlier one with a *different* predicate — the King-Zora / Eastern-Palace "weaker predicate silently wins" regression class. Intentional overrides are allowlisted in the script with a reason. |

`rando_logic_gen.py --strict` also runs here (well-formedness, including the
`region: 0xFFFF` binding warning). Build-dependent guards (`check_init_order`,
`check_link_symbols`, the corpus with `--binary`, and the benchmark gate) run in
the separate build/determinism jobs.

## Generator version (`kGeneratorVersion`) bump policy

Per tasks.md §13.6, bump `kGeneratorVersion` (defined in `src/rando/rando.h`)
whenever a placement-affecting change lands. Triggers:

- `src/rando/rando_logic.c` — predicate VM changes
- `src/rando/rando_placement.c` — placement algorithm changes
- `src/rando/rando_rng.c` — RNG changes
- `assets/rando/logic.yaml` — logic graph changes
- `assets/rando/logic_parts/*.yaml` — per-region predicate authoring (merges into logic.yaml)
- `assets/rando/macros.yaml` — named-predicate macros (mirrors `app/Support/ItemCollection.php`)
- `assets/rando/item_registry.yaml` — item pool / registry changes
- `assets/rando/location_registry.yaml` — location registry changes (append-only adds advance the count, which is part of the determinism input)
- `assets/rando/op_registry.yaml` — op-code assignments
- `assets/rando/icon_atlas.yaml` — 5-icon hash output changes when atlas changes
- The `RandoSettings` struct or its canonical serialization order

Append-only location-registry additions advance the version and regenerate
the regression corpus for new seeds, but do NOT invalidate existing saves
(the embedded placement table preserves the older slot's interpretation).

### Serialization invariants

Two invariants underpin the bump policy and the byte layout pinned in
`openspec/specs/randomizer-core/spec.md`:

1. **The canonical settings serialization is append-only.** Byte positions,
   widths, and enum value assignments for existing fields never change. A new
   axis is appended at the end and bumps `kGeneratorVersion`; it never reorders
   or re-widths an existing field. This is what lets a slot written at version
   `N` load verbatim on an `N+k` binary via its embedded placement table.
2. **The enum space was fully declared up front.** Every enum value a setting
   can take (e.g. `world_state` open/standard/inverted/retro, `logic`
   NoGlitches..NoLogic, `mode_weapons` swordless, `accessibility` none) was
   enumerated at the canonical-layout stage. Later work *un-pins user input* to
   subsets of those already-declared values — it does not expand the enum
   space, so the byte representation of a given setting is stable across
   versions.

A corollary used throughout the corpus: the **default-settings placement
digest is preserved across bumps** — a bump that changes the default seed's
`placement_digest_hex` is a real placement change, not an inert one (see
below).

### Inert-change exception

A change in any of the listed paths that is provably **corpus
byte-identical under default settings** does not require a bump. The
proof shape: run the regression corpus against the modified binary
and confirm every entry passes without manifest changes. This
exception covers:

- Trick-predicate authoring that adds new disjuncts evaluating FALSE
  under `tricks=0`, `logic=NoGlitches` (i.e., the gates collapse to
  their Phase A shape on every existing corpus seed). Slice 4 §7
  batches landed without bumps via this exception.
- Comment / formatting changes in any of the listed YAML files.
- Renames of internal symbols whose canonical-byte representation is
  unchanged.

When in doubt, bump. The corpus regen via
`python assets/scripts/bump_rando_corpus.py --apply` is cheap (~5
seconds locally) and the bumper is idempotent.

### Bumping procedure

1. Edit `src/rando/rando.h:22` — increment the integer and update
   the comment with a brief reason.
2. Rebuild the binary.
3. Run `python assets/scripts/bump_rando_corpus.py --binary=./bin/x64-Release/zelda3.exe --apply`.
4. Verify with `python assets/scripts/run_rando_corpus.py --binary=./bin/x64-Release/zelda3.exe` (expect all entries OK at the new version).
5. Commit the `rando.h` bump, the manifest changes, and any sources
   in the same commit. The commit message states the new version
   and the reason.

### Save / snapshot compatibility across bumps

- **Sidecar slots** (`saves/sram_rando.dat`): the embedded placement
  table preserves the older slot's interpretation. Loading a v=N
  slot on a v=N+k binary surfaces a one-time informational warning
  (`Rando_DetectVersionDrift`) and uses the embedded data verbatim.
  The slot is NOT regenerated. (Exception: door-shuffle and sidecar-v3
  entrance-shuffle slots regenerate their *layouts* at activation and
  hard-fail on a layout-digest mismatch — see *Save behavior*.)
- **Snapshots** (`Shift+F1..F10` save / `Ctrl+F1..F10` replay): the
  TLV-tail format carries `generator_version` in the
  `TAIL_RANDO_STATE` payload. Replay on a different version uses the
  embedded settings + placement; the runtime treats them as
  authoritative.
- **Suppressed race-mode files** (`<spoiler>.json` ZRSR format):
  `Rando_RevealSpoiler` enforces version match — a v=N file
  produced against the runtime's current v=N+k binary returns
  `kRandoReveal_VersionMismatch` (code 5). The race admin must
  reveal on the same generator version the seed was produced
  against.

### Bump case studies (recent)

For maintainers: real-world examples of when to bump and what
shifts as a result.

Current `kGeneratorVersion` is in `src/rando/rando.h` (search for `#define kGeneratorVersion`); recent bumps including the post-phase-b-merge regen at v32 are tracked in the manifest commit history. The case studies below show the bump *pattern* — see `git log -- src/rando/rando.h tests/rando_corpus/manifest.yaml` for the canonical bump history.

| Version | Change | Corpus impact |
|---|---|---|
| 12→13 | Slice 2 Standard EP YAML promoted from inverted-only | 11 placement_digests + 13 sphere_digests changed (EP-region-only); 28 unchanged. See `27b52dd` |
| 13→14 | Slice 7+8 §66 — settings canonical layout 24→28 bytes (`hints`, `boss_shuffle`, `drop_shuffle` added) | 52/52 corpus entries unchanged — canonical layout grew but defaults are all 0, so placement digest doesn't move. Only seeds with non-default new settings would diverge. Sidecar `kRandoSuppressedSpoilerSettingsLen` static-asserts the coupling |
| 14→15 | Slice 3a #52 — 7 new item-registry IDs for Retro shop consumables | Pool composition unchanged at default settings; Retro entries shift if pool difficulty changes |
| 15→16 | Cluster-audit H1 fix — `PlacementTable_ComputeDigest` 256→512 entry cap | 3 Retro corpus entries get new digests (the truncation was silently dropping 9 slots from the hash) |
| 16→17 | Slice 3a #53 part 2 — `LOCTYPE_Shop` identity-pinned per ALTTPR `Randomizer.php:737-750` | Retro placement changes; 3 Retro entries regenerated |
| 17→32 | Phase-b merge cumulative — slice 4 trick predicates, slice 5 hints generator, slice 7+8 boss/drop algorithms, inverted parity translation, audit-fix passes | 55/55 corpus regenerated (`baa393b`); most defaults inert per the `kgenver_inert_change_exception` invariant but several intermediate bumps shifted Retro/Inverted digests. See `git log v17..v32 -- src/rando/ assets/rando/` |
| 46→47 | Fork-extension hint NPCs (Storyteller + Kakariko/Dark-World Fortune Tellers, ids 17-19) add 3 entries to the spoiler `hints[]` | **Placement/sphere digests unchanged** (hints are post-placement; `generator_version` is not an RNG input) — corpus 69/69 byte-identical, **not regenerated**. The bump exists only so a pre-fork v46 **race-mode** seed fails reveal with an honest `VersionMismatch` ("regenerate") instead of a misleading stamp `Tampered`, since the race stamp is a SHA over the full spoiler JSON incl. `hints[]`. A reveal-only reproducibility bump, not a placement bump. |
| 48→49 | **Drop shuffle** goes live in playable slots (installed at slot load; native-window toggle) with a heart floor; the spoiler emits `boss_assignments` / `drop_tables`. (Boss shuffle is generation-only — its runtime substitution is held back; see the Boss & drop shuffle section.) | **All 69 existing placement/sphere digests byte-identical** (boss/drop shuffle is orthogonal to item placement) — corpus regenerated reported 0 digest changes; 10 shuffle-on entries added that assert the orthogonality. Boss/drop *assignment* determinism is pinned by the new self-checks, not the corpus. The bump version-locks the now-live runtime drop algorithm + the shuffle-on race stamp (a shuffle-on v48 race seed would otherwise regenerate different drops/stamp). |
| 55→56 | **Boss-shuffle beatability logic** — each shuffleable dungeon's `- Boss`/`- Prize` gates on the new `OP_CAN_KILL_BOSS(dungeon)` op (the *shuffled* boss's kill predicate) instead of the inline vanilla `CanKill<Boss>` macro, so an item-gated boss can't strand its prize once boss shuffle is runtime-live (corrects design.md D6; boss shuffle is **no longer** placement-orthogonal). | **Only `boss_shuffle=true` entries move** — 6 of the 8 boss-on corpus entries (4 placement + 2 sphere); the other 2 boss-on + all 102 boss-off entries are **byte-identical** (with the assignment at the vanilla identity the op resolves to the vanilla boss-kill predicate). Boss assignment install added to `Place_AssumedFill` (base seed). settings_hash / canonical layout unchanged (boss_shuffle was already canonical field #23). The runtime *render* (Enemizer redirect model) landed separately as a runtime-only change — no further bump (corpus byte-identical). |
| 56→57 | **Pin Blind to Thieves' Town** in the boss-shuffle pool (10→9 shuffleable bosses). Blind has no boss sprite in its room data (TT-only maiden spawn + a `dung_savegame_state_bits & 0x2000` gate), so a Blind shuffled elsewhere never spawned (confirmed strand); pinning is the clean fix. | Changes the boss assignment for every `boss_shuffle=true` seed (9-perm + Blind pinned), so the boss-on placement/sphere digests move (7 entries); `boss_shuffle=false` stays byte-identical. settings_hash / canonical layout unchanged. Corpus regenerated. |
| 67→68 | **Retire dead Fountain placement slots** — Waterfall Bottle/Pyramid Bottle sparse slots are removed from the fillable registry/pool. | All corpus placement/sphere digests regenerated because the open-location count and junk padding changed; settings serialization is unchanged. |
| 68→69 | **Traps** — `traps=low|medium|high` replaces eligible final junk-filled placements with `TrapDamage` / `TrapFreeze`; pickup shows the generated fool message and skips vanilla item receipt. | Default `traps=off` keeps canonical byte `[26]` bits2-3 zero, so pre-existing v68 no-traps corpus seeds stay byte-identical; traps-on seeds intentionally change placement output and settings_hash. |
| 70→71 | **Retire boss-heart shuffle UI / always shuffle boss drops** — the legacy `region_boss_hearts_in_pool` byte canonicalizes to `0`, boss Drop slots are fillable locations, and the selected item-pool difficulty's BossHeartContainer copies always enter the pool. The obsolete Pyramid Fairy bow-upgrade setting is no longer shown in the native window. | Default settings hash changes (`[10]` 1→0) and placement/sphere digests move globally because 10 boss Drop locations join fill and BossHeartContainer items are no longer pinned to boss drops. |

The pattern: predicate changes that affect only one region (12→13's
EP gate) hit a subset of seeds; layout-only changes with default-zero
new fields (13→14) leave the corpus untouched; Retro-isolated rule
changes hit ~3 entries; a global predicate change (every
`CanKillEscapeThings`-gated location, every `CanBombThings()` caller,
etc.) would hit ~30+ Standard entries. Plan corpus regen time
accordingly when scoping a sprint.

## ALTTPR cross-compatibility (none)

The zelda3 randomizer is provenance-derived from ALTTPR (`alttp_vt_randomizer`)
in two ways:

- The 43 named macros in `assets/rando/macros.yaml` were hand-translated from
  `app/Support/ItemCollection.php` with per-method line-range citations.
- Location names, region grouping, prize/medallion conventions mirror
  `app/Region/{Standard,Open,Inverted}/*.php`.

ALTTPR's MIT license requires preserving its copyright in derivative works;
attribution appears in `NOTICE` (per task 13.9).

**The two share-string / spoiler formats are not interoperable.** An ALTTPR
share string fed to this binary is rejected with a "format mismatch" error
(per `Share_SelfCheck`); a zelda3-rando share string is not parseable by
alttpr.com. The settings semantics overlap but the canonical-byte order
differs, so `settings_hash` will not match between the two systems.

## Troubleshooting

### "BPS conflict" on extract

If `python assets/restool.py --extract-from-rom` fails complaining about an
asset hash mismatch and the rom path is correct, the extracted file likely
got patched by another tool. Delete `zelda3_assets.dat` and re-extract from
a clean US ROM (SHA-256 `66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`).

### Version drift warning on save load

Phase B feature. When a slot was written by an older `generator_version` than
the binary currently runs, a one-time informational warning surfaces on slot
load. The embedded placement table is honored; gameplay is unaffected.
(Door-shuffle and sidecar-v3 entrance-shuffle slots are stricter: their
regenerated layout is digest-checked and activation is refused on mismatch —
see *Save behavior*.)

### Sidecar atomicity

A crash during save leaves `sram_rando.dat.tmp` and the previous
`sram_rando.dat` intact (since the atomic rename only completes on full
flush). The next boot reads the previous-good `sram_rando.dat`.

## World-state notes

### Standard mode and the uncle's gift

In ALTTPR's Standard mode (and in this rando), the uncle's gift is **part of
the placement pool** — it can be any item allowed by the slot's `can_place`
restriction, not just the L1 Sword as in vanilla. Link starts swordless
(`link_sword_type = 0`) and the chosen item is collected from the uncle's
sprite handler at the standard receive path (`sprite_main.c:5733`, item id
0x00 in vanilla; the dispatcher rewrites the granted item).

The placement restriction on the uncle's slot rejects items that don't make
sense as a starting item: `MirrorShield`, `SilverArrowUpgrade`, `TitanMitt`,
`L4Sword`, `MagicMirror`, `MoonPearl`, `BookOfMudora` — see `02_uncle_standard_mode.yaml`
under `assets/rando/logic_examples/`. Other world-states (Open, Retro) place
no restriction on the uncle's slot — `WORLDSTATE_EQ(open)` short-circuits
the can_place predicate to true.

Standard mode also gates broader progression on the uncle pickup having
occurred; the virtual `RescuedZelda` item is granted when the uncle/sanctuary
escort completes, and dark-world / overworld access predicates reference it.

### Inverted world-state

Inverted flips the relationship between the light and dark worlds. Link
**starts in the dark world as a bunny** — the overworld the player wakes into
is the dark-world map, and progression runs **dark-world-first**: the dark
world is the early game, and the light world is opened up later (the reverse of
Open/Standard). This mirrors ALTTPR's `App\World\Inverted`, whose 24
`app/Region/Inverted/**/*.php` region files (2977 lines) re-route every region
gate for the inverted topology.

To make the bunny start survivable and the dark world traversable from the
first frame, Inverted seeds grant a fixed **starting inventory** of two items:

- **Moon Pearl** — keeps Link in human form in the dark world (without it he is
  stuck in the powerless bunny state and most actions are disabled).
- **Magic Mirror** — provides the dark→light return warp that the inverted
  routing depends on.

This is the only world-state with a non-empty starting inventory. Open, Retro,
and Standard grant nothing here (Standard's uncle's-gift is a separate
placement-pool mechanism, described above). The grant happens once at file-load
(`Rando_TryGrantStartingInventory`, gated on `kFeatures1_RandomizerActive` and
the `kRam_RandoStartingInventoryGranted` sentinel) and is idempotent across
save/reload — the sentinel persists in the Phase A `kRam_*` block so a mid-run
load does not re-grant.

Because the inverted topology relabels which overworld tiles a region occupies,
Inverted ships via a **static alternate edge table** (`kRandoEdges_Inverted[]`,
walked when `world_state == Inverted`) plus a per-screen visual tile overlay —
NOT a runtime region remap. The reachability seed starts from the inverted
counterpart of Link's House rather than the light-world spawn. (The Phase A
`RegionRemap` scaffold was dead identity code and was **retired** in the Phase C
entrance-shuffle work; see `archive/2026-06-05-add-rando-entrance-shuffle/design.md §1`.)

### Retro world-state

Retro **extends Open** (same region graph, same Open starting state) and turns
on ALTTPR's "retro" ruleset. Per `app/World/Retro.php` it forces four flags.
The fork does **not** store these as settings bytes — they are *computed* from
`world_state == Retro` at the point of use (no new serialized fields, canonical
length stays 28, default Open/Standard/Inverted placement digests unchanged).
The canonical runtime gate is `Rando_IsRetroActive()` (rando.c): true iff a
rando slot is active and its world-state is Retro; when false the vanilla code
path runs byte-identically.

The four flags and their as-built status:

- **`rupeeBow`** — *implemented, runtime.* Firing the bow spends **rupees**, not
  arrows: 10 rupees per wood-arrow shot, 50 per silver-arrow shot (matching
  ALTTPR `retro.asm` / `tables.asm` `ArrowMode*Cost`). The arrow counter is left
  untouched (in ALTTPR it is a 0/1 capability sentinel). If Link can't afford a
  shot the bow gives the empty-ammo beep. The branch lives in `LinkItem_Bow`
  (`player.c`) behind `Rando_IsRetroActive()`; the archery minigame keeps its
  vanilla arrow-refill path. *(Bow-fire rupee spend is runtime-only and is
  PLAYTEST-PENDING; the HUD still shows the arrow counter rather than a rupee
  gauge — a deferred cosmetic refinement.)*
- **`takeAnys`** — *implemented (shipped separately).* The 31 "Take Any" caves
  become enterable; per seed ~5 are activated and offer a free take-once item.
  Built by the archived `add-rando-retro-takeany` change: a per-seed
  overworld-door redirect into a take-any host room + a free-grant
  `ShopItem_TakeAny`. The runtime gates on exactly the same condition
  (`Rando_GetActiveWorldState() == kWorldState_Retro` + rando-active), and the
  generator only selects/places active caves' slots under Retro — so the flag is
  effectively pinned by `world_state == Retro`. See the spoiler "Shops" section
  for the active caves and their rewards.
- **`wildKeys`** — *implemented, generation.* Retro forces small keys out of
  their vanilla dungeon spots and into the general/wild pool. Implemented via
  `Settings_EffectiveSmallKeysMode()`, which pins `dungeon_small_keys_mode = Wild`
  whenever `world_state == Retro`. To keep the determinism contract intact the
  override is applied through that single helper in BOTH `apply_derived_rules`
  (so the canonical `settings_hash` reflects Wild) and at every placer +
  reachability-bridge read site (so placement and the tracker match the hash) —
  both key off `world_state`, so the hash and the placement can never desync. It
  reuses the fork's existing, corpus-tested Wild placement and the cross-dungeon
  key-credit runtime (a key for dungeon B found in dungeon A is credited to B's
  counter, `rando.c` key grant), so **no new runtime is needed** and keys keep
  their dungeon identity. This is a generation change: `kGeneratorVersion` 50→51,
  the 4 Retro corpus digests regenerated (non-Retro byte-identical). Verified
  headless: all Retro goals stay beatable with 30 small keys in the wild pool and
  0 unreachable placements. Because Retro forces this, the settings UI shows the
  **Small keys** control locked to "wild" (greyed out, with a "forced by Retro"
  tooltip) — your own small-keys pick is preserved and restored if you leave Retro.
- **`genericKeys`** — *implemented, generation + runtime.* Retro unifies the
  per-dungeon key counters into **one shared pool**, so **any** small key opens
  **any** locked door — matching ALTTPR. (Big keys, maps and compasses keep their
  dungeon identity; only small keys become fungible.) Three coupled pieces, all
  gated on `world_state == Retro` so non-Retro is byte-identical:
  - *Placement* — `BuildItemPool` substitutes every per-dungeon `SmallKey_<Dungeon>`
    with the fungible `GenericKey` (ROM `0xAF`), mirroring ALTTPR `Location::getItem`
    swapping each `Item\Key` for `KeyGK`. Same per-dungeon counts, so the pool size
    is unchanged; `wildKeys` (above) already places them wild.
  - *Logic* — the predicate VM collapses any per-dungeon small-key requirement onto
    "hold ≥1 `GenericKey`", a direct port of ALTTPR `ItemCollection::has()`'s ShopKey
    wildcard (`app/Support/ItemCollection.php`). This is intentionally permissive
    (one key satisfies every small-key door in logic); the assumed-fill + key
    abundance keep seeds beatable, exactly as upstream.
  - *Runtime* — a single SRAM-persisted shared counter (`link_generic_keys` =
    `link_keys_earned_per_dungeon[15]` = ALTTPR's `$7EF38B`) backs the live
    `link_num_keys` via dungeon enter-load / exit-save / death-save / door-consume
    write-through and the key grants, gated on `Rando_IsGenericKeysActive()`.
  This is a generation change: `kGeneratorVersion` 53→54, the Retro corpus digests
  regenerated (non-Retro byte-identical), `settings_hash` / canonical layout
  unchanged (genericKeys is computed from `world_state`, no new byte). Verified
  headless: `--rando-selftest` (incl. a cross-dungeon collapse check — one key
  opens a Palace-of-Darkness 5-key door AND a Turtle Rock 4-key door), corpus
  green, and every Retro goal beatable with 0 unreachable placements. Landed as
  `openspec/changes/archive/2026-06-05-add-rando-retro-generic-keys`.

What randomizes in Retro is the **shop economy**, not the shop inventory: the 9
regular shops keep their vanilla inventory (identity-pinned) but the player must
find the shops and pay rupees; the 2 Capacity Upgrade slots are identity-placed;
the active Take-Any caves carry placed items. See the placement/dispatch detail
in `openspec/changes/archive/2026-06-04-add-rando-retro-world-state/`.

Both spoiler formats surface the Retro shop placements: the `.txt` spoiler has a
grouped **Shops** section (shop-name headings, identity-placed Capacity Upgrade
slots flagged), and the `.json` spoiler carries a Retro-only **`shops[]`** array
(`location` / `name` / `item` / `type`, with `identity_placed: true` on the
Capacity Upgrade slots). Both are emitted only for seeds that actually place
shop-class locations, so non-Retro spoilers are unchanged.

## Phase B+ status

Most of the Phase B work below has **shipped and been archived** — the
per-row Status column is authoritative. The handful still in progress are
marked as such. The top-of-document banner and the
[`openspec/changes/` index](../openspec/changes/README.md) carry the live
picture; this table is a slice-level cross-reference.

### Phase B — chunked into 9 OpenSpec changes (2026-05-26)

The changes are authored at `openspec/changes/add-rando-*` (archived ones
under `openspec/changes/archive/`) and pass `openspec validate --changes`.

| # | Change | Slice | Scope | Status |
|---|---|---|---|---|
| 1 | [`add-rando-confirmation-icons`](../openspec/changes/archive/2026-06-04-add-rando-confirmation-icons/) | 9 | Visible per-item icon ancilla for §6.2 direct-grant placements | ✅ Archived 2026-06-04 |
| 1b | [`add-rando-fairy-chest-model`](../openspec/changes/archive/2026-06-04-add-rando-fairy-chest-model/) | 9 | Great-fairy ponds → two reach-only chest-model checks; retire Pyramid Sword/Bow | ✅ Archived 2026-06-04 |
| 2 | [`add-rando-trackers`](../openspec/changes/add-rando-trackers/) | 1 | In-game item + location tracker overlays + checked-bitmap r/w paths | Full |
| 3 | [`add-rando-race-mode-reveal`](../openspec/changes/archive/2026-06-05-add-rando-race-mode-reveal/) | 6 | Spoiler suppression + CLI `--reveal-spoiler` + `RandoRevealSpoiler` keybind + SHA-256 stamp verify (built scope; in-binary reveal-UI + settings warning carved to `add-rando-race-mode-reveal-ui`) | ✅ Archived 2026-06-05 |
| 4a | [`add-rando-inverted-world-state`](../openspec/changes/archive/2026-06-03-add-rando-inverted-world-state/) | 2 | Inverted region graph (2977 lines PHP) + Bug #12 starting-inventory wire | ✅ Archived 2026-06-03 |
| 4b | [`add-rando-retro-world-state`](../openspec/changes/archive/2026-06-04-add-rando-retro-world-state/) | 3 | Retro shop dispatch + rupeeBow/takeAnys/wildKeys pinned (genericKeys → #4b-i) | ✅ Archived 2026-06-04 |
| 4b-i | [`add-rando-retro-generic-keys`](../openspec/changes/archive/2026-06-05-add-rando-retro-generic-keys/) | 3 | Retro genericKeys — one shared key pool (any key opens any door); follow-up to #4b. Placement + logic-collapse + SRAM shared-counter runtime | ✅ Archived 2026-06-05 |
| 5 | [`add-rando-trick-logic-and-axes`](../openspec/changes/archive/2026-06-04-add-rando-trick-logic-and-axes/) | 4 + misc | Trick/glitch ops + §12.6 ROM-version scaffolding + `swordless` mode (end-to-end) + `accessibility=none` + Bug #7 per-item rewind (gated off) | ✅ Archived 2026-06-04 |
| 6 | [`add-rando-hints`](../openspec/changes/archive/2026-06-11-add-rando-hints/) | 5 | New `randomizer-hints` capability: 15 telepathic-tile hints + Storyteller/Fortune-Teller fork NPCs + Murahdahla (spoiler-only) + dialogue-ID injection | ✅ Archived 2026-06-11 (owner playtest-confirmed) |
| 7 | [`add-rando-shuffles-and-minigames`](../openspec/changes/add-rando-shuffles-and-minigames/) | 7 + 8 | Boss + drop-pool shuffles + §6.8 minigame dispatch (digging, hype-cave NPC, peg cave, treasure-chest minigame) | In-progress (drop-shuffle playable; boss-shuffle playable/experimental with beatability + Enemizer-redirect render) |
| 8 | [`add-rando-switch-swkbd`](../openspec/changes/add-rando-switch-swkbd/) | §9.1c | libnx `swkbdCreate` / `swkbdShow` / `swkbdInputText` wrapper routed into `RandoTextField` | Stub |

See the [`openspec/changes/` index](../openspec/changes/README.md) for the
per-slice scope detail (files-to-touch, ALTTPR references) and the
change-folder breakdown.

Items folded into the changes above:
- `swordless`, `accessibility=none`, and Phase A1 audit Bug #7 (per-item rewind) — all in **#5
  `add-rando-trick-logic-and-axes`**.
- §6.8 minigame dispatch — in **#7 `add-rando-shuffles-and-minigames`**.
- §9.1c Switch software-keyboard — **own change #8
  `add-rando-switch-swkbd`** (Switch-manual-gated; no PC code path).
- §7.6 follow-on visible confirmation icons — **#1
  `add-rando-confirmation-icons`** (warm-up).
- Inverted + Retro picker un-gates — split across **#4a Inverted** and
  **#4b Retro** (each as ADDED Requirements to sidestep archive
  sequencing).

### Phase C

| # | Change | Scope | Status |
|---|---|---|---|
| C1 | [`add-rando-entrance-shuffle`](../openspec/changes/archive/2026-06-05-add-rando-entrance-shuffle/) | Entrance shuffle, composable axes (caves / dungeons / coupled / crossed / decoupled); Simple/Restricted/Crossed as built presets. **Coupled cave + dungeon entrance shuffle + Crossed (cross-category) implemented** (Open/Standard), playtest-confirmed. ALL 38 cave interiors + **11 of 12 dungeons** shuffle (everything except Skull Woods; Ganon's Tower is an advanced opt-in, `shuffle_ganons_tower_entrance`). Caves use a per-seed region override, dungeons a per-seed edge overlay; both share the door overlay + coupled exit (capture source room at entry). The generation retry requires FULL reachability, so no entrance seed ships with stranded items. Save = regenerate π from (seed, packed axes, attempt) at slot load — entrance seeds are version-locked (sidecar-v3 slots persist a 24-bit entrance-layout digest and activation refuses on mismatch; pre-v3 slots fall back to a version-drift warning). `RegionRemap` scaffold retired (the archive `REMOVED` its stale baseline requirement). Insanity (full decoupled) is **built and shipped** as a live native-window preset — the cave-arrival table is baked (`src/rando/cave_arrival_baked.h`, preloaded so every door is one-way from launch); cave-decoupled is playtest-confirmed, the dungeon/cross-decoupled arms carry the usual built-but-playtest-pending caveat. Skull Woods + Link's House remain documented partial-coverage deferrals. | ✅ Archived 2026-06-05 |

### Phase D

| # | Change | Scope | Status |
|---|---|---|---|
| D1 | [`add-rando-cosmetic-shuffles`](../openspec/changes/archive/2026-06-02-add-rando-cosmetic-shuffles/) | Palette + sprite + music shuffles. Cosmetic only; `cosmetic_seed` separate from `settings_hash`. | ✅ Archived 2026-06-02 |
| D2 | [`add-rando-customizer-mode`](../openspec/changes/add-rando-customizer-mode/) | Manual per-location placement + custom pool composition. Dispatcher API unchanged. | Headless generation, playable-slot generation, and PC native-window manifest UI are built; share-string transport for the manifest identity remains deferred. See [Customizer mode](#customizer-mode). |
| D3 | [`add-rando-major-glitch`](../openspec/changes/add-rando-major-glitch/) | Major-glitch logic level: `HybridMajorGlitches` + `NoLogic` un-pin + NoLogic reachability short-circuit (logic graph merged to main). **Close-out pass** (D6 couples glitch seeds to `kFeatures0_RestoreJpGlitches`; F1/F3 reclassify the raw `major_glitches` thresholds to first-class `CanOneFrameClipOW`/`CanOneFrameClipUW` macros, closing canOneFrameClipOW at HMG; F2 authors the 9 missing technique macros; F4 flips `fake-flippers` → `verified-us10`; F1-followon surfaces the dropped OWG-group disjuncts (partial); F5 short-circuits `can_place` at NoLogic). kGen 64. | Applied; close-out playtest-pending |
| D4 | [`add-rando-auto-tracker`](../openspec/changes/archive/2026-06-05-add-rando-auto-tracker/) | Local TCP server emitting per-event inventory + reachability state for external tracker clients (NDJSON; see *Auto-tracker (external clients)* above). | ✅ Archived 2026-06-05 |

Phase C/D change folders are retained as the working record for unarchived features; individual README/task files describe their current built scope and remaining archive gates.

See `openspec/changes/archive/2026-05-29-add-randomizer-support/tasks.md` §7 and §14 for the
acceptance gates per phase.

## References

- Spec baseline (Phase A, archived 2026-05-29): `openspec/specs/randomizer-*/spec.md` — normative requirements per subsystem (the live source of truth).
- Archived Phase A change: `openspec/changes/archive/2026-05-29-add-randomizer-support/`
  - `proposal.md` — high-level scope
  - `design.md` — design decisions and trade-offs
  - `tasks.md` — implementation task list (119/139; the 20 deferrals are tracked in the active Phase B/C/D changes)
  - `audit.md` — Phase 0 audit deliverable (closes the §6 gate)
- Active follow-on changes: `openspec/changes/` (see its `README.md` index)
- Upstream provenance: `alttp_vt_randomizer` (MIT) — sibling checkout
  expected at `../alttp_vt_randomizer/` for translation work; not required
  to build or play.
