# All Enemy Checks - tasks

## Current implementation status

- [x] Foundation: reserve `enemy_drop_checks=all` as value `3`, remove the legacy
  text alias that made `all` mean `dungeon`, update canonical serialization,
  validation, CSV/share decode, generator version, docs, and corpus manifest.
- [x] Foundation: apply derived rules for vanilla keys, door shuffle, enemy
  shuffle, boss shuffle, entrance shuffle, and missing registry.
- [x] Foundation: fail generation before placement retries when pure effective
  `all` would require missing generated all-tier registry data.
- [x] Static-overworld phase: emit, place, grant, suppress, and expose generated
  ordinary overworld enemies under `enemy_drop_checks=all`.
- [x] Reviewed-underworld exception phase: emit audited rooms `0x03C`, `0x107`,
  `0x10C`, and `0x123` as all-tier-only indoor rows, with direct cave/interior
  access predicates, counted same-side pot kill routing where valid, and normal
  inventory combat where same-side throwables are not available.
- [x] Reviewed boss/miniboss and finite scripted-spawn domains ship with stable
  identities and runtime/logic support. Farmable/unbounded dynamic spawns are
  explicit audited exclusions, not pending gameplay checks.

## 1. Audit and scope

- [x] 1.1 Create a local enemy-source audit across dungeon rooms and overworld areas.
- [x] 1.2 Classify generated static dungeon/overworld sources as included or
  excluded with a stable reason.
- [x] 1.3 Add freshness guards for duplicate identities, stale source metadata, and
  location-capacity headroom in the static dungeon/overworld registry.
- [x] 1.4 Implement the concrete compatibility matrix for vanilla keys, door shuffle,
  enemy shuffle, boss shuffle, pot shuffle, cave entrance shuffle, and missing or
  stale registries.
- [x] 1.5 Treat unsupported finite killable authored sources as blockers or explicit
  effective downgrades/rejections, not as quiet exclusions from an active `all` tier.

## 2. Identity and runtime

- [x] 2.1 Add stable source identity for overworld enemy spawns.
- [x] 2.2 Add stable parent/child identity for finite scripted spawn groups.
- [x] 2.3 Add boss/miniboss death-event identity where a separate enemy check can
  coexist with existing boss prizes and heart/prize behavior.
- [x] 2.4 Implement death-time direct grant for all ordinary emitted dungeon and
  static overworld enemy rows.
- [x] 2.5 Suppress checked overworld sources on reload while preserving later source
  identities; add lazy block lookup so snapshot restore can still resolve visible
  restored sprites.
- [x] 2.6 Keep forced enemy-drop rows on the existing pickup-time path.
- [x] 2.7 Make an ordinary enemy check replace its normal prize-pack pickup while
  preserving Pikit-held items, forced drops, boss prizes/hearts, and scripted events.

## 3. Logic and placement

- [x] 3.1 Emit all-tier locations and grouping metadata for every included static
  dungeon/overworld source and reviewed underworld exception.
- [x] 3.2 Add per-source reachability predicates for static overworld sources.
- [x] 3.3 Add per-source kill-route predicates for dungeon rows and conservative
  overworld combat predicates for static overworld rows.
- [x] 3.4 Add counted thrown-pot routes only when enough reachable pots exist. The
  room `0x107` rats and room `0x03C` Blue Bari
  exceptions use this for same-side in-room pot routes; room `0x10C` disables the
  shared Fairy Cave pot because it is not on the reviewed Mimic Cave side. Rocks
  and broader non-pot throwable domains are outside the shipped model.
- [x] 3.5 Prevent thrown-pot branches from double-counting pot-sanity item checks.
  Until per-source consumption metadata exists, generated thrown-pot branches are
  gated on effective pot shuffle being off and active pot-sanity seeds fall back to
  the reviewed inventory-combat route.
- [x] 3.6 Normalize requested `all` under enemy shuffle to the highest lower tier
  allowed by existing derived rules until a future placement-affecting enemy-shuffle
  contract exists.
- [x] 3.7 Add door-shuffle non-key enemy bridges with digest/replay support before
  keeping `all` active under door shuffle; otherwise normalize requested `all` to
  `keys`.
- [x] 3.8 Normalize or exclude all-enemy boss rows under boss shuffle until assigned
  boss-room identity and reward interactions are modeled.

## 4. Capacity and persistence

- [x] 4.1 Measure worst-case all-enemy location count with pot sanity and other
  expansion features active.
- [x] 4.2 Raise location capacity and migrate placement/checked-state storage if the
  emitted registry exceeds current limits.
- [x] 4.3 Reuse the capacity-sized persisted checked bitmap; authored source maps
  are reconstructed from generated data and require no new per-source payload.
- [x] 4.4 Add fail-closed snapshot restore handling for missing or malformed
  all-enemy metadata.
- [x] 4.5 Include all-enemy door bridge rows, digest, and effective tier in
  DoorShuffleLayout activation and snapshot replay before supporting `all` with door
  shuffle.

## 5. UI, marker, and output

- [x] 5.1 Add `enemy_drop_checks=all` to CSV and share/settings decode as a distinct
  value; native UI and file-select expose it only when it can remain effective.
- [x] 5.2 Display effective downgrades or generation rejection reasons clearly.
- [x] 5.3 Attach stable dungeon room, overworld area/screen, boss arena, or scripted
  parent region metadata to spoiler, tracker, reachability, and autotracker rows.
  JSON placements remain a flat list of rows carrying that grouping metadata.
- [x] 5.4 Reuse enemy marker modes for dungeon rows and exact item markers for
  overworld rows; overworld carriers that cannot draw exact markers use the same
  post-sprite gold-glint fallback as dungeon carriers.
- [x] 5.5 Verify dense all-enemy marker screens do not corrupt OAM, palettes, pot
  glints, or item receipt graphics.
  <!-- owner confirmation 2026-07-11: dense enemy/pot marker and receipt
  interactions were previously playtested; exact marker charnum proof remains in
  add-rando-enemy-marker-multi-icons task 5.4. -->
- [x] 5.6 Add marker candidate metadata for every shipped all-tier domain that renders
  in-world markers, or explicitly mark that domain as marker-suppressed while
  keeping tracker/spoiler output complete.

## 6. Validation

- [x] 6.1 Run `openspec validate add-rando-all-enemy-checks --strict`.
- [x] 6.2 Run source-audit/codegen freshness checks.
- [x] 6.3 Run Release build and `--rando-selftest`.
- [x] 6.4 Run corpus rows for `all` and every supported/degraded interaction.
- [ ] 6.5 Runtime-test shipped dungeon, reviewed underworld, static overworld,
  boss/miniboss, and finite scripted-spawn checks through death, reload, save/load,
  snapshot, and transition cases.
  <!-- owner playtest 2026-07-11: Eastern room 0x0D8 Red Eyegore source slot 8
  showed no marker and dropped its vanilla 10-arrow bundle. F12 confirmed type
  0x84 had no generated check because the audit incorrectly treated enemy-shuffle
  eligibility as the definition of all-tier eligibility. Red Eyegores are now
  curated all-tier-only static checks with Bow-specific logic and Eyegore soul
  behavior; final owner retest remains. -->
  <!-- owner playtest 2026-07-12: ordinary enemies visibly produced both their
  direct-grant check item and a normal prize-pack pickup. The check now replaces
  only that ordinary pickup, including the frozen/hammer death path, while hidden
  prize-pack sequencing and all special/boss rewards remain intact. The client-local
  marker default is now the non-spoiler generic gold glint; placed-item markers remain
  available as an explicit preference. Final owner retest remains. -->
- [x] 6.6 Test thrown-pot kill logic with insufficient and sufficient pot counts
  while pot shuffle is off, plus the pot-sanity guard that disables those branches
  and requires the inventory-combat route while any effective pot tier is active.
- [x] 6.7 Test door-shuffle bridge digest drift, enemy-shuffle normalization, and
  boss-shuffle composition.
- [x] 6.8 Playtest dense screens with generic markers and item markers.
  <!-- owner confirmation 2026-07-11: enemy and pot marker combinations were
  previously covered. -->
