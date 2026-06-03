## Why

The randomizer's `accessibility` axis is meant to mirror ALTTPR's three tiers,
but only two are usable and they behave identically:

- The enum already defines all three values (`rando_settings.h:60-64`:
  `kAccessibility_Items=0`, `kAccessibility_Locations=1`, `kAccessibility_None=2`),
  and the CSV parser + canonical serialization already round-trip all three.
- But **`items` and `locations` behave identically** in the generator: nothing
  branched between them — both only gated on `Goal_IsCompletable` (goal
  reachable), so neither actually enforced "100% inventory" vs "100% locations".
- The third value, `none`, meant "no guarantee — the seed may be literally
  unwinnable" (`Goal_ShouldRefuse` short-circuited to *don't refuse*; the
  spoiler emitted an `accessibility_none_seed` "may be unwinnable" warning).
  That is **not** ALTTPR's third tier — ALTTPR's `none` ("Not Guaranteed") still
  produces a **beatable** seed (the assumed fill only skips access checks *after*
  the win condition is already satisfiable — `RandomAssumed.php:101`).
- The **PC native settings window** exposes only two options
  (`kAccessibilityLabels[] = {"items","locations"}`, `EnumCombo(... 2)`), so the
  third tier was unreachable from the primary PC UI.

The user asked for a real, ALTTPR-faithful three-way axis with a **"beatable
only"** third option.

## What Changes

- **Define one nested-strictness acceptance predicate**
  `Accessibility_SeedAcceptable(settings, placements)` (`rando_placement.c`).
  Every tier requires the goal be completable (the seed is beatable); the tiers
  add extra reachability:
  - `locations` (1) — "100% Locations": every placed location reachable
    (`unreachable_count == 0`).
  - `items` (0, default) — "100% Inventory": every **progression** item's
    location reachable (via the existing `is_progression_item` classifier — junk,
    maps, compasses, and heart pieces/containers may strand).
  - `none` (2) — UI label **"beatable only"**: goal completability is the whole
    bar.
  Strictness nests `locations ⊇ items ⊇ beatable`.
- **Route every acceptance gate through it**: `Goal_ShouldRefuse`
  (`rando_placement.c`), the CLI gate + entrance-retry accept (`main.c`), and
  **both** slot-generation paths in `rando_generate.c` (the non-entrance path
  was previously **ungated** — it shipped whatever the placer returned).
- **Retire "ship an unwinnable seed" from this axis.** `none` is now guaranteed
  beatable. The diagnostic "write a broken seed anyway" capability remains only
  behind the CLI `--allow-broken-seed` flag.
- **`Place_AssumedFill` is unchanged** — it is deterministic and does not branch
  on accessibility, so all three tiers receive the same best-effort placement;
  only the caller's gate differs. This keeps digest churn minimal (a
  fully-reachable placement is accepted by every tier).
- **Expose the third option in the PC native settings window** with the label
  **"beatable only"** (`EnumCombo(... 3)` + tooltip). The Completionist lock
  (forces `locations`) is unchanged. Relabel the Switch in-game screen `NONE` →
  `BEAT` and accept `beatable` as a CSV alias for `none`.
- **Spoiler**: delete the now-wrong `accessibility_none_seed` warning; the
  existing `unreachable_placements` warning already informs the reader for all
  tiers when something is unreachable by design.
- **Guard**: `check_rando_invariants.py` no longer treats `accessibility_none_seed`
  as a generation failure (its intent inverts — `none` is now a shippable tier).
- **Determinism**: bump `kGeneratorVersion` 45 → 46 and regenerate the corpus.
  The canonical byte layout, the `accessibility` offset `[8]`, and the default
  value (`items=0`) are unchanged, so `kSettingsCanonicalLen` stays 28 and the
  default **settings_hash** is unchanged; the version bump disambiguates old
  `none` share strings (which had different semantics).

Note: this supersedes the `accessibility=none` portion of the still-stub
`add-rando-trick-logic-and-axes` change, which described `none` as "no
accessibility guarantee" — the implemented meaning is ALTTPR's "beatable".

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-core`: **ADDED** a requirement defining the three-tier
  accessibility acceptance predicate (all tiers beatable; per-tier reachability)
  and that `none` ("beatable only") no longer ships unwinnable seeds.
- `randomizer-ui`: **ADDED** a requirement that the PC native settings window
  exposes all three accessibility tiers with the "beatable only" label and the
  Completionist→locations lock.

## Impact

- **Code**: `src/rando/rando_placement.{c,h}` (new predicate + gate rewire),
  `src/rando/rando_generate.c` (gate both paths), `src/main.c` (CLI gate +
  message), `src/rando/rando_spoiler.c` (delete warning),
  `src/rando/rando_window/rando_window.cpp` (3rd option + tooltip),
  `src/select_file.c` (label + comment), `src/rando/rando_settings.{c,h}` (enum
  comment, `beatable` CSV alias, selftests), `src/rando/rando.h` (genVer 46).
- **Tooling/docs**: `assets/scripts/check_rando_invariants.py`,
  `docs/randomizer.md`, `tests/rando_corpus/manifest.yaml` (regen + new
  per-tier entries).
- **Determinism**: `kGeneratorVersion` 45 → 46; corpus regenerated (placement
  **and** sphere digests). Default `settings_hash` unchanged; default placement
  digest unchanged when the seed is fully reachable (the common case).
- **Regression risk**: `items` (the default) becomes slightly stricter (rejects
  seeds with a stranded *progression* item — rare); `locations` becomes strict
  for an explicit non-Completionist selection (Completionist was already strict
  via its own goal predicate); `none` becomes safer (guaranteed beatable).
  Slot-path behavior is verified by playtest per `CLAUDE.md`.
