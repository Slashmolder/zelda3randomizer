## Why

The `region_boss_hearts_in_pool` axis is **fully plumbed already** — settings struct
(`src/rando/rando_settings.h:91`), `Settings_SetDefaults` default of `1`
(`rando_settings.c:61`), canonical serialization at byte offset `[10]`
(`rando_settings.c:164`/`:222` → feeds `settings_hash`), CSV parse
(`rando_settings.c:889-892`), the placer branch (`rando_placement.c:1104-1106`),
and the runtime grant path (`Sprite_HeartContainer` → `Rando_GetBossHeartLocation`
→ `Rando_DispatchVanillaGrant`). Setting `region_boss_hearts_in_pool=0` via
`--generate-seed --settings=region_boss_hearts_in_pool=0` shuffles the 10
`<Dungeon> - Boss` heart-container drops into the general item pool **today**.

Three gaps remain:

1. **No UI control.** The PC ImGui settings window only *displays* the value
   read-only under "Locked settings" (`rando_window.cpp:360-361`). The player
   cannot toggle it.
2. **A naming footgun.** The field's value is inverted relative to its name:
   `region_boss_hearts_in_pool = 1` means **pinned / NOT shuffled** (the default);
   `= 0` means **shuffled into the pool**. Shipping the literal name to users
   ("boss hearts in pool: yes" while they are *not* in the general pool) is
   actively misleading. The Phase A placement spec scenario likewise describes
   "in pool = participate in shuffle", which is the natural reading the field
   value contradicts.
3. **Unverified logic safety.** Freeing the boss-heart slots into assumed-fill is
   only safe if every `<Dungeon> - Boss` Drop location's `can_reach` predicate
   actually gates on defeating that dungeon's boss — otherwise the placer could
   strand progression behind an unbeatable boss.

## What Changes

- **Add a real toggle** in the PC native settings window labeled
  **"Shuffle boss heart containers"**, in the existing "Toggles" panel alongside
  Prize/Medallion/Race/Hints. The control writes the settings field with the
  inversion applied at the UI layer: **checked → `region_boss_hearts_in_pool = 0`**
  (shuffled), **unchecked → `= 1`** (pinned, the default). Remove the stale
  read-only "Region boss hearts in pool" line from the "Locked settings" block.
- **Do NOT rename the C field or change the canonical byte / CSV keys.** Inverting
  at the UI layer is the minimal, baseline-safe option the field semantics already
  support. The external CSV keys (`region.bossHeartsInPool`,
  `region_boss_hearts_in_pool`) keep their existing meaning for headless/share-string
  compatibility.
- **Keep the default `1` (pinned/off).** Existing seeds and `settings_hash` values
  are unchanged; no corpus regen, no `kGeneratorVersion` bump.
- **Fix the stale comment** at `rando_placement.c:346` (it claims "when
  bossHeartsInPool is false (Phase A default)… identity-placed", which is
  backwards — the default is `1`, and `1` = identity-placed).
- **Document the verified logic-safety invariant**: each of the 10 boss Drop
  locations gates `can_reach` on the boss kill (verified across Standard +
  Inverted graphs — all 10 require a `CanKill<Boss>()` macro plus the items to
  reach/open the boss room).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-ui`: **ADDED** a requirement for the boss-heart-container shuffle
  toggle, pinning the user-facing label and the value-inversion mapping so the
  misleading raw field name is never shown.
- `randomizer-placement`: **ADDED** a requirement documenting the
  `region_boss_hearts_in_pool` value semantics (1 = pinned default, 0 = in pool)
  and the logic-safety invariant that every boss Drop's `can_reach` requires the
  boss kill.

## Impact

- **UI only**: `src/rando/rando_window/rando_window.cpp` — one checkbox added, one
  read-only line removed.
- **Comment fix**: `src/rando/rando_placement.c:346`.
- **No** change to `Settings_SetDefaults`, the canonical byte sequence,
  `kSettingsCanonicalLen`, `settings_hash`, the CSV parser, the placer branch
  logic, the runtime grant path, or `kGeneratorVersion`.
- **No corpus regeneration** required (default unchanged; byte already canonical;
  placer already branches deterministically on it).
- **Regression risk**: minimal. The only behavior change is a previously
  unreachable-via-UI axis becoming toggleable. The placer/runtime paths it drives
  already ship and are exercised by `--rando-selftest` and the headless corpus.
