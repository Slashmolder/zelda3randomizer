# Inverted world-state logic (priming directory)

Empty YAML scaffold for Phase B `add-rando-inverted-world-state` translation work. Each file mirrors a PHP source in `../../../../alttp_vt_randomizer/app/Region/Inverted/`.

**This directory is NOT consumed by the codegen pipeline yet** — `assets/rando_logic_gen.py` reads from `assets/rando/logic_parts/*.yaml` (flat, no subdirs) per the Phase A convention. The Inverted YAML files will need to either:

1. Be flattened into the top-level `logic_parts/` directory with `inverted_` prefixes (e.g., `inverted_GanonsTower.yaml`), OR
2. Trigger a codegen pipeline change to scan subdirectories (cleaner long-term).

The decision belongs in `add-rando-inverted-world-state`'s `design.md` at apply-time. For now this scaffold preserves the upstream directory shape for translation-time reference.

## File mapping (PHP → YAML)

| PHP source (in `app/Region/Inverted/`) | YAML target (in this dir) |
|---|---|
| `DesertPalace.php` | `DesertPalace.yaml` |
| `EasternPalace.php` | `EasternPalace.yaml` |
| `GanonsTower.php` | `GanonsTower.yaml` |
| `HyruleCastleEscape.php` | `HyruleCastleEscape.yaml` |
| `HyruleCastleTower.php` | `HyruleCastleTower.yaml` |
| `IcePalace.php` | `IcePalace.yaml` |
| `MiseryMire.php` | `MiseryMire.yaml` |
| `PalaceOfDarkness.php` | `PalaceOfDarkness.yaml` |
| `SkullWoods.php` | `SkullWoods.yaml` |
| `SwampPalace.php` | `SwampPalace.yaml` |
| `ThievesTown.php` | `ThievesTown.yaml` |
| `TowerOfHera.php` | `TowerOfHera.yaml` |
| `TurtleRock.php` | `TurtleRock.yaml` |
| `DarkWorld/Mire.php` | `DarkWorld/Mire.yaml` |
| `DarkWorld/NorthEast.php` | `DarkWorld/NorthEast.yaml` |
| `DarkWorld/NorthWest.php` | `DarkWorld/NorthWest.yaml` |
| `DarkWorld/South.php` | `DarkWorld/South.yaml` |
| `DarkWorld/DeathMountain/East.php` | `DarkWorld/DeathMountain/East.yaml` |
| `DarkWorld/DeathMountain/West.php` | `DarkWorld/DeathMountain/West.yaml` |
| `LightWorld/NorthEast.php` | `LightWorld/NorthEast.yaml` |
| `LightWorld/NorthWest.php` | `LightWorld/NorthWest.yaml` |
| `LightWorld/South.php` | `LightWorld/South.yaml` |
| `LightWorld/DeathMountain/East.php` | `LightWorld/DeathMountain/East.yaml` |
| `LightWorld/DeathMountain/West.php` | `LightWorld/DeathMountain/West.yaml` |

Total: 24 files, mirroring the 24-file PHP source.

## Template

Each YAML file SHOULD use this template at authoring time:

```yaml
# Translated from: ../alttp_vt_randomizer/app/Region/Inverted/<file>.php
# Source lines: <range>
# Translator: <name>
# Translation date: <YYYY-MM-DD>

regions:
  - id: <RegionName>
    can_enter: <predicate>     # SOURCE: <Inverted/<file>.php:<line>>
    can_complete: <predicate>  # SOURCE: <Inverted/<file>.php:<line>>

locations:
  - id: <LocationName>
    region: <RegionName>       # required for reachability tagging (per Bug #2 fix)
    can_reach: <predicate>     # SOURCE: <file>.php:<setRequirements line>
    can_place: <predicate>     # SOURCE: <file>.php:<setFillRules line, if present>
    always_allow: <predicate>  # SOURCE: <file>.php:<setAlwaysAllow line, if present>
    world_state_filter:
      - inverted               # marks this location as Inverted-only (or universal/exclusion)

edges:
  - from: <RegionA>
    to: <RegionB>
    predicate: <predicate>    # SOURCE: <file>.php:<edge declaration line>
```

Authoring discipline per `audit.md §0.10`: every predicate references the source PHP line range. Don't paraphrase — translate mechanically.
