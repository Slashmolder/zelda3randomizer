# add-rando-enemy-shuffle — audit

## Fresh-eyes audit 2026-06-13

Scope: post-v3 enemy shuffle as shipped in source: type substitution, all-slot
sprite-group reshuffle machinery, generated Enemizer-sourced tables,
dungeon-overlord spawn-slot decode, graveyard slot-3 pin, and HP/contact-damage
randomization. Sheet widening is currently palette-gated off after F12 playtest.

### Findings

No new code changes required.

Spec/docs reconciliation completed:

- **Spec stale against as-built.** `specs/randomizer-shuffles/spec.md` still
  described the early slot-2/MVP model and omitted all-slot reshuffle,
  generated `kSheetNeed` / `kOverlordNeed` tables, the graveyard slot-3 pin, and
  HP/contact-damage randomization. Reconciled.
- **Unsupported water guarantee.** The old spec said water-only rooms SHALL draw
  only water-capable enemies, but the current picker never sets `require_water`.
  Reconciled the spec to the as-built conservative guarantee: all dungeon
  replacements are killable + key-capable, hard-excluded rooms pass through, and
  water stranding remains a playtest watch item / future classifier.
- **Source comments stale.** `shuffle_enemies.h`, `shuffle_enemies.c`, and
  `load_gfx.c` still had comments saying no subgroup reshuffle / no HP-damage /
  slot-2-only reshuffle. Updated comments only; runtime code unchanged.

### Verified Surfaces

- **GFX-sheet safety.** `EnemyShuffle_Pick*` reads the live
  `sprite_gfx_subset_0..3` values, not a static `kSpriteTilesets` row, so
  `0`-inheritance is handled at the loaded-sheet source. The self-check covers
  multi-slot enemies (`Walking Zora`, `Snapdragon`) so a one-sheet partial match
  cannot pass.
- **Sheet reshuffle safety.** `EnemyShuffle_ReshuffleCurrentRoomSheets` runs
  after subgroup ids resolve and before decompression. The generated widening
  path only reshuffles owned, unpinned slots; inherited/pinned slots restore the
  vanilla-resolved sheet from the per-slot shadow. Pool membership is generated
  and checked for per-slot disjointness by `gen_enemy_shuffle_tables.py`.
  Runtime widening is currently disabled because palette requirements are not
  modeled; all slots are forced to vanilla-resolved sheets.
- **Bit-packed coordinates.** Dungeon substitution touches only `ent[2]` before
  `Dungeon_LoadSingleSprite`; y/x bytes are not rewritten by enemy shuffle.
  Overworld substitution rewrites the stored `type + 1` value and skips
  `>= 0xf3` overlord/control types.
- **Boss/environment exclusion.** Bosses, GT mini-bosses, boss secondaries,
  Agahnim, and Ganon are excluded from substitution and pin all reshuffle slots.
  `EnemyShuffle_SelfCheck` asserts the must-exclude set.
- **Overlord decode.** Dungeon overlords use generated `kOverlordNeed` to pin
  only the spawned sprite's required slots. Unknown/no-spawn/boss-spawning
  overlords pin all slots; overworld overlords pin all slots.
- **Non-sprite sheet consumer.** Graveyard area `0x14` pins slot 3 for the
  pushable-grave ancilla; this is not visible in the room sprite list and was
  verified present in the blocked-slot computation.
- **Logic-free claim.** A sweep of logic YAML/macros/op registry still finds only
  boss/mini-boss kill predicates (`CanKillBoss`, direct `CanKill<Boss>` in GT)
  and general player-firepower macros (`CanKillMostThings`,
  `CanKillEscapeThings`). Enemy shuffle adds no predicate and remains placement
  orthogonal.
- **Stat scaling bounds.** HP scaling is deterministic per `(seed,type)`, leaves
  `base==0` and bosses unchanged, and clamps `[1,255]`. Damage scaling touches
  only plain classes `1..8`; flagged/special/boss values pass through.

### Residual Risk / Archive Gates

- Render/crash/softlock validation remains playtest-only. Stage all subgroup
  slots, overlord-freed spawner rooms, graveyard pushable graves, overworld
  areas, and water-adjacent enemy rooms.
- OAM footprint compatibility is still not modelled. The replacement type's
  `sprite_flags2` loads after substitution, which fixes init order, but
  scene-dependent OAM clobber still needs playtest.
- Water-only room classification is not implemented. Keep water stranding on the
  playtest checklist before archive.
- Backward-load warning for older enemy-shuffle-era slots remains a useful smoke,
  though the kGen bump + existing upgrade-warning path provide the behavior by
  construction.

## F12 palette follow-up 2026-06-13

Owner captured a second room `0xA9` issue after the stale-sheet fix. The dump
showed live sheets and snapshot both set to `1F,1E,23,52`, so tile safety was
working; the active Mini-Helmasaur had correct sheet coverage but bad colors.
Root cause is missing sprite-palette modeling: widening to sheet `1E` does not
also load the OBJ palette the Mini-Helmasaur expects.

Fix landed:

- `EnemyShuffle_ReshuffleCurrentRoomSheets` now forces all subgroup slots to the
  room's vanilla-resolved sheets until palette requirements are modeled.
- The resolved-sheet snapshot remains active, so type substitution still fails
  closed on stale/missing sheet state.
- Generated sheet pools and deterministic slot RNG remain in source as the
  dormant implementation for future palette-aware widening.

## F12 overworld Ball-and-Chain follow-up 2026-06-13

Owner captured a different render issue on the way back to room `0xA9`. The dump
was overworld area `0x2E` with live/snapshotted sheets `48,49,0C,46` (hex), and
an active Ball-and-Chain Trooper (`0x6A`). That means the snapshot guard was
working and the spawn was admitted normally because its current sheet
requirements (`46,49` hex) were loaded.

Initial fix landed:

- `0x6A` remained randomizable in dungeons but was flagged
  `ESF_NEVER_OVERWORLD`, matching this fork's dungeon-only sheet metadata and
  its dungeon-oriented flail draw/AI path. This manual flag was later removed
  once the vanilla-context allowlist became the behavior-level guard.
- `0x6B` Cannon Soldier is removed from the randomizable pool. Enemizer marks it
  with a verification TODO, and this fork's handler only behaves as a spawned
  cannonball path when `sprite_C != 0`.
- `EnemyShuffle_SelfCheck` asserts that `0x6B` stays non-randomizable and that
  `0x6A` is not overworld-eligible under the context guard.

## F12 overworld Assault Soldier follow-up 2026-06-13

Owner captured another garbled enemy below Link's house. The dump was overworld
area `0x2C` with live/snapshotted sheets `51,49,13,46` (hex) and an active
Assault Sword Soldier (`0x44`). Like the Ball-and-Chain case, the sheet snapshot
was valid; the missing invariant was context safety. `0x44` uses the special
`Sprite_44_BluesainBolt` / `PsychoTrooper_Draw` path in this fork and is unsafe
as a generic overworld replacement.

Initial fix landed:

- `0x44` remained randomizable in dungeons but was flagged
  `ESF_NEVER_OVERWORLD`. This manual flag was later removed once the
  vanilla-context allowlist became the behavior-level guard.
- `EnemyShuffle_SelfCheck` asserts that `0x44` is not overworld-eligible under
  the context guard.

## Vanilla-context guard 2026-06-13

The Ball-and-Chain and Assault Soldier captures showed the same class of bug:
sheet compatibility alone admits sprites whose draw/AI/palette assumptions are
not valid in the other loader. Rather than continue adding one-off overworld
bans for every playtest-caught case, the picker now derives a vanilla-context
allowlist by scanning the shipped sprite blobs:

- dungeon context from `kDungeonSprites` / `kDungeonSpriteOffs`
- overworld context from all stages of `kOverworldSprites` /
  `kOverworldSpriteOffs`

Candidate selection now requires the generated randomizable table, generated
`never_use_*` bans, the runtime vanilla-context allowlist, and loaded-sheet
coverage. `EnemyShuffle_SelfCheck` asserts known overworld and dungeon examples,
the caught overworld disallowances, and sampled picks staying context-eligible.
The temporary `0x44`/`0x6A` manual overworld bans were removed so future work can
expand context support in one place; the `0x6B` do-not-randomize override stays
because its handler only supports a spawned cannonball path.

## F12 overworld BombGuard palette follow-up 2026-06-13

Owner corrected the latest "cyclops" report to the two-eyed bomb soldier in
dark-world overworld. The newest F12 dump was overworld area `0x2B`, live and
snapshotted sheets `51,49,13,46` (hex), and active Red Bomb Soldier / BombGuard
sprites (`0x4A`). The sheet and vanilla-context guards were working: `0x4A`
requires sheets `46,49` and vanilla does use it in overworld. The remaining
missing invariant was overworld sprite palette compatibility.

Asset inspection showed vanilla `0x4A` overworld occurrences only in area `0x1B`
under sprite palette `3`; the captured area had
`overworld_sprite_palettes[0x2B] == 0`. A sheet-compatible `0x4A` is therefore
not safe in every overworld area.

Fix landed:

- The vanilla-context scan now also reads `kOverworldSpritePalettes` for each
  overworld sprite-list occurrence and records a per-type palette bitset.
- `EnemyShuffle_PickOverworld` reads the current area's `overworld_sprite_palettes`
  id from RAM — via `overworld_area_palette_index` (dark/special areas read the
  `[0x40..0x7F]` dark region; a plain `area & 63` dropped the dark-world `0x40` bit
  and gated every dark/special area against the light palette) — and only admits
  candidates whose type is vanilla-observed under that palette.
- `0x4A` remains randomizable and overworld-eligible where its palette matches;
  this is not a hard ban. `EnemyShuffle_SelfCheck` asserts `0x4A` allowed on
  palette `3` and rejected on palette `0`.

## F12 playtest follow-up 2026-06-13

Owner captured a garbled enemy in dungeon room `0xA9`; the dump showed current
sprite sheets `1F,2C,2E,52` and an active Mini-Helmasaur (`0x13`), which requires
sheet `1E`. That combination should be impossible under the in-sheet picker, so
the likely fault was stale transition sheet state being used for the pick.

Fix landed:

- `EnemyShuffle_ReshuffleCurrentRoomSheets` snapshots the resolved sheet set,
  room/area key, and dungeon/overworld context in reserved RAM `0x666..0x66c`.
- `EnemyShuffle_PickDungeon` / `PickOverworld` substitute only when that snapshot
  matches the current sprite-list key and still matches live `sprite_gfx_subset`.
- Missing/stale snapshots pass through vanilla instead of choosing from stale
  sheets.
- `EnemyShuffle_SelfCheck` now asserts missing and stale snapshot passthrough.

Verification: Release alternate build (`bin/x64-Release-codex/zelda3.exe`) passed
`--rando-selftest`. The normal Release link was blocked because the playtest
`bin/x64-Release/zelda3.exe` was still running.
