# Audit — add-rando-inverted-world-state

This file records the apply-time provenance pre-flight and macro audit for the
Inverted world-state change. All ALTTPR facts below come from a `grep` against
the sibling checkout, never from memory.

## Inverted macro provenance

- **Upstream checkout**: `C:\src\alttp_vt_randomizer`
- **Pinned commit**: `219fcafd029dab597b8db400efafd8f56f8b4edb`
  (`git -C C:\src\alttp_vt_randomizer rev-parse HEAD`, captured 2026-05-29).
- **License**: MIT (per `CLAUDE.md` claim-grounding section; `LICENSE` + `composer.json`).

All line ranges below are relative to this pinned commit.

### §1.1 — RegionRemap pairing-table source

`app/World/Inverted.php` **exists** (53 lines). It is a thin `World` subclass
whose constructor populates `$this->regions` with the 24 Inverted region
classes (lines 28-55) plus the shared `Standard\Medallions` and
`Standard\Fountains` regions. It carries no per-region logic — it is the
authoritative **region-registry / pairing source** for the RegionRemap overlay
table (the named-key → `Region\Inverted\…` class mapping at lines 28-55).
Per-location predicates live in the 24 `app/Region/Inverted/**/*.php` files,
not here.

### §1.3 — File + line counts (actual vs expected)

| Metric | Expected | Actual | Match |
|---|---|---|---|
| `*.php` under `app/Region/Inverted` | 24 | 24 | ✓ |
| Total `wc -l` lines | 2977 | 2977 | ✓ |

Note: `wc -l` (newline count) yields 2977; `Measure-Object -Line` reports 2665
because several Inverted PHP files lack a trailing newline. The task's expected
figure is the `wc -l` figure, which matches exactly.

### §1.4 — Per-file predicate skim + effort note

Predicate-pattern counts per file (grep of `setRequirements` / `setFillRules` /
`setAlwaysAllow` / `can_enter` / `can_complete`):

| File | setRequirements | setFillRules | setAlwaysAllow | can_enter | can_complete |
|---|---|---|---|---|---|
| DarkWorld/DeathMountain/East.php | 4 | 0 | 0 | 1 | 0 |
| DarkWorld/DeathMountain/West.php | 1 | 0 | 0 | 1 | 0 |
| DarkWorld/Mire.php | 0 | 0 | 0 | 1 | 0 |
| DarkWorld/NorthEast.php | 6 | 0 | 0 | 1 | 0 |
| DarkWorld/NorthWest.php | 6 | 0 | 0 | 0 | 0 |
| DarkWorld/South.php | 9 | 0 | 0 | 0 | 0 |
| DesertPalace.php | 2 | 0 | 0 | 1 | 0 |
| EasternPalace.php | 4 | 0 | 0 | 1 | 0 |
| GanonsTower.php | 25 | 6 | 1 | 1 | 2 |
| HyruleCastleEscape.php | 8 | 2 | 0 | 1 | 0 |
| HyruleCastleTower.php | 0 | 0 | 0 | 1 | 0 |
| IcePalace.php | 0 | 0 | 0 | 1 | 0 |
| LightWorld/DeathMountain/East.php | 12 | 0 | 0 | 1 | 0 |
| LightWorld/DeathMountain/West.php | 1 | 0 | 0 | 1 | 0 |
| LightWorld/NorthEast.php | 10 | 0 | 0 | 1 | 0 |
| LightWorld/NorthWest.php | 23 | 0 | 0 | 1 | 0 |
| LightWorld/South.php | 24 | 0 | 0 | 1 | 0 |
| MiseryMire.php | 0 | 0 | 0 | 1 | 0 |
| PalaceOfDarkness.php | 0 | 0 | 0 | 1 | 0 |
| SkullWoods.php | 2 | 0 | 0 | 1 | 0 |
| SwampPalace.php | 9 | 3 | 2 | 1 | 0 |
| ThievesTown.php | 0 | 0 | 0 | 1 | 0 |
| TowerOfHera.php | 4 | 2 | 2 | 1 | 0 |
| TurtleRock.php | 13 | 3 | 2 | 1 | 2 |

Effort note: the bulk is concentrated in **GanonsTower.php** (495 lines, 25
`setRequirements` + 6 `setFillRules` — Agahnim 2 routing and the GT chest
chain), **LightWorld/South.php** (246 lines, 24), **LightWorld/NorthEast.php**
(232), **LightWorld/NorthWest.php** (223), **TurtleRock.php** (218, 13), and
**LightWorld/DeathMountain/East.php** (206, 12). Dungeons with zero
`setRequirements` (HyruleCastleTower, IcePalace, MiseryMire, PalaceOfDarkness,
ThievesTown) carry only a `can_enter` region gate — those translate trivially.
The translation work itself (§4.1-4.24) is already shipped per `tasks.md`; this
audit covers only the provenance + macro-coverage tasks.

## §2.1/2.2 — Inverted macro coverage cross-reference

Grep of `app/Region/Inverted/**/*.php` for `$items->can*` and
`$this->world->can*` method calls:

- `$this->world->can*` calls: **none** found.
- `$items->can*` calls (12 distinct ItemCollection macros):
  `canBlockLasers`, `canBombThings`, `canBunnyRevive`, `canExtendMagic`,
  `canFly`, `canKillMostThings`, `canLiftDarkRocks`, `canLiftRocks`,
  `canLightTorches`, `canMeltThings`, `canShootArrows`, `canSpinSpeed`.

(The broader `->can*` grep also surfaces `canEnter`, `canBeat`, `canAccess`,
`canReachTop/Middle/Bottom` — these are **region-local methods**, not
`ItemCollection` macros, confirmed by grepping
`app/Support/ItemCollection.php` for `function <name>`; they are not
translation targets for `macros.yaml`.)

Cross-reference against `assets/rando/macros.yaml`: **all 12 are already
present** (authored in Phase A, mirroring `app/Support/ItemCollection.php`):

| ALTTPR `$items->` call | macros.yaml entry |
|---|---|
| canBlockLasers | CanBlockLasers |
| canBombThings | CanBombThings |
| canBunnyRevive | CanBunnyRevive |
| canExtendMagic | CanExtendMagic |
| canFly | CanFly |
| canKillMostThings | CanKillMostThings |
| canLiftDarkRocks | CanLiftDarkRocks |
| canLiftRocks | CanLiftRocks |
| canLightTorches | CanLightTorches |
| canMeltThings | CanMeltThings |
| canShootArrows | CanShootArrows / CanShootArrowsL1 / CanShootSilvers |
| canSpinSpeed | CanSpinSpeed |

**Finding: no macro is missing.** Inverted uses no `ItemCollection` macro that
Phase A did not already author. Tasks §2.1/§2.2 close with **no change to
`assets/rando/macros.yaml`**. Consequently there is no `phase: B-inverted`
macro to tag, and no `kGeneratorVersion` bump is warranted on macro grounds.

## Verification results (apply-time)

- `python assets/rando_logic_gen.py` → exit 0; `warnings: 0, macro errors: 0`;
  generated 266 locations, 132 items, 31 regions/edges. Working tree stayed
  clean (byte-identical generated output — no macro change).
- `python assets/scripts/check_codegen_wiring.py` → exit 0; "6 generated
  file(s) wired across all build systems." Generated header **names**
  unchanged.
- `python assets/scripts/check_audit_guard.py` → exit 0; "no non-exempt
  writes (38 tracked offsets)." The `[advisory]` indirect-dispatch notes are
  pre-existing informational output, not failures.

## Out of scope for this work item

Full corpus/digest verification and end-to-end playtest require a build + dev
unit and are explicitly **out of scope** here. Because no macro (and no
logic.yaml / logic_parts content) changed in this work item, default-settings
corpus output is expected to be byte-identical and no `kGeneratorVersion` bump
is performed.
