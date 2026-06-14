## Context

zelda3 stores each room/area's enemies as a list of `{y, x, type}` entries (`Dungeon_LoadSprites` `src/sprite.c:3752`; `Overworld_LoadSprites` `:3895`). `Dungeon_LoadSingleSprite` (`:3807`) decodes them: `type == 0xe4` is a control entry; `x >= 0xe0` is an overlord marker; the y/x bytes are **bit-packed** (bits 0-4 position, y bits 5-6 + x bits 5-7 subtype, y bit 7 floor) — verified, and the boss-shuffle coordinate shift at `:3791-3796` is the live demonstration of masking `& 0x1f` for position while preserving `& 0xe0`. The **type id is `src[2]`**, written verbatim to `sprite_type[k]` (`:3831`). Which enemy *tiles* a room can load is governed by `sprite_graphics_index` → `kSpriteTilesets[144][4]` (`src/load_gfx.c:59`, consumed `:623-642`): four subgroup sheet ids per index — the exact analog of Enemizer's per-group 4 subgroups. An enemy whose tiles aren't in a loaded sheet renders garbage / crashes.

Enemy shuffle therefore = **substitute the enemy type for another whose sheets are already loaded** — sidestepping the bit-packed-coord hazard (we change `type`, not `y`/`x`). But the two load paths store the type DIFFERENTLY (audit H3) and the spec must not conflate them:
- **Dungeon**: `Dungeon_LoadSingleSprite` writes `src[2]` to `sprite_type[k]` — a direct type-byte swap.
- **Overworld**: `Overworld_LoadSprites` (`:3895`) does NOT write `sprite_type`; it stores `sprite_where_in_overworld[...] = src[2] + 1` (`:3910`) and the spawn is lazy in `Overworld_LoadProximaSpriteIfAlive` (`:3973`). The swap must rewrite that map value as `pick + 1` (keep the `+1` bias). Overworld overlords are `src[2] >= 0xf3` and the count marker is `src[2] == 0xf4` (`:3903` in `Overworld_LoadSprites`; `:3985-3992` in `Overworld_LoadProximaSpriteIfAlive`) — NOT the dungeon `x >= 0xe0` / `0xe4` markers.

The hard part is the *constraint table* that says which swaps are safe (and, for beatability, is the *sole* enforcer — see D7).

## Goals / Non-Goals

**Goals**
- Deterministic per-seed enemy-type substitution, dungeon + overworld, installed at slot load, mirroring boss shuffle.
- GFX-sheet-safe by construction and conservative enough not to strand key/shutter
  rooms; render/crash/softlock validation remains playtest-only.
- `enemy_shuffle` as a canonical rando settings axis; all-off ⇒ byte-identical to vanilla; placement digest byte-identical for all seeds.

**Non-Goals**
- Killable thief, bush-enemy spawn, absorbables-in-place-of-enemies, and randomize-on-hit remain deferred follow-on axes. HP/contact-damage randomization shipped under `enemy_shuffle`.
- Boss shuffle (already shipped) and drop-pool shuffle (already shipped) — orthogonal; enemy shuffle does not touch either.
- Enemizer's "sprite group subgroup re-shuffle" (expanding which sheets load per group) — an optional later enhancement; the MVP picks only within the **already-loaded** set (smaller pool, zero crash risk). See D4.
- Sprite GFX/skin packs (cosmetic) — that is `add-rando-cosmetic-shuffles`.

## Decisions

### D1: Install model — copy boss shuffle

`EnemyShuffle_Generate(settings, seed_u64)` builds the substitution from a dedicated xoshiro256\*\* stream forked off the seed (per `randomizer-core / RNG family`), so the fill RNG is unperturbed and the result is cross-platform self-consistent. Installed module-global from `Rando_ActivateSidecarSlot` (alongside `BossShuffle_Generate`/`DropShuffle_Generate`, `src/rando/rando.c:1932-1947`); torn down in `Rando_DeactivateSlot`. Off / no-active-slot ⇒ the load path takes the vanilla branch.

**Fail-closed on the invalid-settings path (audit correction).** Boss/drop shuffle only install inside the `g_rando_active_settings_valid` branch; on the invalid path (a v1 slot, or a snapshot-restore that restored `g_ram` but not the C-static `RandoSettings`) they explicitly call `BossShuffle_Deactivate()` / `DropShuffle_Deactivate()` to avoid leaking a prior slot's assignment (`rando.c:1904-1960` — the `[[race_mode_null_settings_failopen]]` class). Enemy shuffle MUST mirror this: install only when settings are valid, and `EnemyShuffle_Deactivate()` on the invalid path. An unconditional install would leak a stale substitution into a snapshot-restored slot.

### D2: What gets substituted — the "randomizable enemy" predicate

For each loaded entry, substitute only when ALL hold (else leave vanilla):
- not a control entry (`type != 0xe4`) and not an overlord (`x < 0xe0`) — these are skipped before type read anyway;
- the type's constraint flags mark it a **randomizable enemy**: not `is_boss`, not `is_object`, not `is_npc`, not `do_not_randomize`.

Bosses and their secondary parts are excluded wholesale (boss shuffle owns those, and the room-environment-dependent ones — Blind/Kholdstare/Trinexx — must never be substituted *in*; see D5). NPCs, quest objects, switches, and statues are excluded so the room stays solvable and dialog/triggers stay intact.

### D3: The replacement pick — GFX-sheet + vanilla-context constrained

Given the room/area, the candidate pool is the set of randomizable enemies whose constraint-table sheet requirement is satisfied by the **actually-loaded** sheets and whose type appears in vanilla data for the same loader context (dungeon or overworld). **Caveat (audit M2): the loaded set is NOT purely `kSpriteTilesets[index][0..3]`.** `Gfx_LoadSpritesInner` (`load_gfx.c:627-640`) loads each subgroup only `if (p[N])` — a `0` entry leaves the previously-loaded `sprite_gfx_subset_N` in place, and many `kSpriteTilesets` rows have `0` slots (e.g. `[0,73,0,0]`). So a static check against `kSpriteTilesets[index]` is unsound for those rooms — a pick can pass it and still reference an unloaded sheet → garbage/crash. Resolve `0` entries against the live `sprite_gfx_subset_N`, snapshot that resolved set with the room/area key that produced it, and let the picker substitute only when the snapshot matches the sprite list currently being loaded. Missing/stale snapshots pass through vanilla. Separately, derive a compact vanilla-context allowlist by scanning `kDungeonSprites`/`kDungeonSpriteOffs` and all stages of `kOverworldSprites`/`kOverworldSpriteOffs`, then require the candidate to appear in the current loader context. During the overworld scan, also pair each sprite-list occurrence with the matching `kOverworldSpritePalettes` value and require the current area's `overworld_sprite_palettes` id — read with a **dark-world-aware index** (light areas index `[0x00..0x3F]`; dark *and* special areas read the dark region `[0x40..0x7F]`, matching both what vanilla uploads via `overworld_sprite_palettes[overworld_screen_index]` and how the per-type palette set was recorded) — to be one vanilla uses for that type. A plain `area & 63` here drops the dark-world `0x40` bit and compares every dark/special area against the *light* palette (which differs from the dark palette in all 64 slots). This prevents sheet-compatible but context-wrong or off-palette enemies from entering the pool. This is **non-negotiable**. (Enemizer enforces the loaded-sheet rule via `SpriteRequirement.SpriteInGroup`.)

Additional per-pick constraints (from Enemizer; the beatability invariants — audit M3/M4 expanded the flag set):
- **Killable AND key-capable are independent flags.** `killable` and `cannot_have_key` are separate (Enemizer `CannotHaveKey` ≠ `!Killable`: Keese/Buzzblob/Geldman are killable but key-banned). A key slot's replacement MUST be `killable && !cannot_have_key`; a shutter/kill-clear room's replacement MUST be `killable`. ("killable ⇒ key-capable" is wrong.)
- **Shutter/kill-clear rooms are a hand-maintained room-id list** (Enemizer `NeedKillable_doors` via `Room.IsShutterRoom`), not derivable from room data — port it.
- **Key-room detection must use the rando placement table, not the vanilla sprite list.** Enemizer's `HasAKey` scans the vanilla ROM for an adjacent key sprite (`DungeonSprite.cs:66`); under item shuffle the key may be absent or placed elsewhere, so that scan is unreliable. Define key-room safety against the placement table + `NeedKillable_doors`.
- **Context safety**: per-enemy `never_use_dungeon` / `never_use_overworld` flags (Enemizer `NeverUseDungeon`/`NeverUseOverworld`) constrain the pool by load context, and the runtime-derived vanilla-context allowlist further requires that vanilla uses the candidate in that same loader. Overworld candidates also require a vanilla-observed sprite-palette match for the current area. Some sprites are sheet-compatible in overworld but only safe in a narrower draw/AI/palette context.
- **Water capability is source-sprite constrained, not room-classified**:
  `ESF_WATER` is generated and a water-capable source sprite sets
  `require_water`, so those sources stay water-capable after substitution.
  There is still no independent "this room is water-only" classifier for
  non-water-tagged source sprites; treat broader water stranding as a playtest
  watch item / future classifier rather than a shipped room-level invariant.
- **Per-room excludes** (anti-softlock): the immovable-sprite room list (~60 rooms, `DontUseImmovableSpritesRooms`), the flying-sprite room list, per-sprite do-not-randomize room lists, and hard excludes (Mimic Cave, Agahnim-tower bridge) — port into a per-room constraint map (`SpriteRequirement.cs:897-956` + per-sprite `AddDontRandomizeRooms`).

### D4: Sheet widening is built, but currently palette-gated off

Enemizer first re-shuffles each group's 4 subgroup sheets to widen the enemy pool, then picks. The MVP **did not** — it picked only within the sheets a room already loads. This shrank variety but removed a whole class of crash risk and kept the change to a pure type substitution.

**As-built.** The reshuffle machinery is built under `enemy_shuffle` (no new
canonical axis) and covers subgroup slots **0,1,2,3**. It runs at sheet-load time
after the four `kSpriteTilesets` entries resolve and before decompression, so VRAM
and the type picker agree. A follow-up F12 capture proved tile safety alone is
insufficient: room `0xA9` legitimately loaded Mini-Helmasaur sheet `1E`, but the
room's OBJ palettes still matched the vanilla sheet set, producing bad colors.
Until sheet widening models and preserves sprite palette requirements, the runtime
forces every subgroup slot back to the room's vanilla-resolved sheet and only uses
the resolved-sheet snapshot for type substitution inside that set.

Key facts that keep the dormant widening path safe once palette requirements are
added:

- **Generated table source.** `assets/scripts/gen_enemy_shuffle_tables.py` parses
  Enemizer's MIT `SpriteRequirement.cs` / `SpriteConstants.cs` and emits the
  randomizable enemy table, complete per-type sheet requirements, `kSheetNeed`,
  per-slot dungeon/overworld pools, boss ids, and `kOverlordNeed`.
- **Disjoint position whitelists.** Enemizer's four `PotentialSubsetN` pools share
  no sheet id, so a pool sheet determines its subgroup slot. Loading a slot-N pool
  sheet into slot N keeps enemy tiles in their canonical VRAM region and the
  existing position-unaware "sheet loaded" test stays sound.
- **Per-room runtime, not global ROM patch.** Each room/area walks its sprite type
  list at sheet-load time and computes a blocked-slot mask. A slot may reshuffle
  only when the current row owns it and no present sprite/overlord/object pins it.
- **Conservative pinning.** Randomizable enemies do not pin because the picker will
  substitute them. Known non-randomizable sprites pin only the slots they need.
  Unknowns and bosses pin all slots. Dungeon overlords decode generated spawned
  sprite needs and pin only those slots; unknown/no-spawn/boss-spawning overlords
  and overworld overlords pin all slots. The graveyard screen pins slot 3 for the
  pushable-grave ancilla, which is not visible in the sprite list.
- **Safe pools + vanilla-inclusive choice.** Each dungeon slot pool self-contains a
  killable+key-capable candidate; overworld pools contain a randomizable candidate.
  The room's vanilla sheet is always a possible outcome and is currently the forced
  outcome.
- **Inheritance shadow.** A 4-byte g_ram shadow tracks the vanilla-resolved sheet
  per slot, so inherited/pinned slots restore vanilla and a prior reshuffle cannot
  leak through a `0` subgroup entry.
- **Resolved-sheet snapshot.** A separate g_ram snapshot (`0x666..0x66c`) records
  the resolved loaded sheet set plus dungeon/overworld context and room/area key.
  This caught and fixes the F12 room-`0xA9` case where a Mini-Helmasaur (requires
  sheet `1E`) was active under current sheets `1F,2C,2E,52`: stale/mismatched
  transition state now fails closed to the vanilla source sprite.
- **Palette guard.** A later F12 room-`0xA9` capture showed the snapshot/live sheet
  set agreeing while the Mini-Helmasaur still had bad colors. The guard now pins all
  subgroup slots to vanilla-resolved sheets until a palette model is added.

### D5: Room-environment-dependent sprites stay out

The same environment dependency that pins Blind/Kholdstare/Trinexx in boss shuffle (the room "effect" byte `dung_hdr_collision_2` = `$00AD` = `hdr[4]`, BG2 ice/lava objects, and room-state-gated spawns like Blind's maiden bit `dung_savegame_state_bits & 0x2000`) means: never substitute *into* a slot whose mechanics need that environment, and never put an environment-dependent enemy where its environment isn't present. The MVP achieves this simply by **excluding all bosses + their secondaries** (the only environment-coupled sprites) from both the source-replace set and the candidate pool.

### D6: OAM-budget compatibility

The per-sprite OAM reservation is `((sprite_flags2[k] & 0x1f) + 1) * 4` **bytes** (`Sprite_TimersAndOam` `src/sprite.c:1164`). Substituting a 1-entry enemy with a multi-tile one can overrun the reserved region (scene-dependent tile clobber). The current as-built table does **not** model OAM footprint; this remains a playtest watch item, mitigated by the curated pool and by `sprite_flags2` being loaded from the replacement type. **The init-order caveat in an earlier draft was spurious (audit L4):** `kSpriteInit_Flags2[j]` is read in `SpritePrep_LoadProperties` (`sprite.c:4144`), which runs at sprite prep/activation — strictly after the load-time type swap — so `sprite_flags2` already reflects the new type; no ordering fix is needed. The OAM-overrun concern is still real.

### D7: Logic — does the placer need to know? (verify, don't assume)

A pure enemy-type shuffle is **logic-free for our purposes** — the fresh-eyes sweep confirmed no predicate is gated on which *shuffled* enemy spawns. The graph's kill predicates are: `CanKillBoss(<dungeon>)` (→ `OP_CAN_KILL_BOSS`); the **11 per-boss `CanKill<Boss>` macros** (`CanKillLanmolas`/`CanKillMoldorm`/`CanKillArmosKnights`/… — used *directly* for the **GT mini-boss gauntlet** and the pinned Agahnim, e.g. `assets/rando/logic_parts/.../33_ganons_tower.yaml`); and the firepower macros `CanKillMostThings`/`CanKillEscapeThings` (`macros.yaml:318,337`). Every one is a **player-firepower** test (does the inventory let Link kill a thing of that class), NOT an "is this specific enemy present" test — and the only enemies they reference are **bosses / mini-bosses, which are excluded from the substitution pool**. So enemy shuffle adds no predicate and no `OP_*`, and `placement_digest_hex` is byte-identical. (Round-1's enumeration omitted the per-boss macros; the *conclusion* held but the list was wrong — audit round 2.)

**Exclusion must cover GT mini-bosses, not just boss rooms (audit HIGH).** The GT mini-boss gauntlet gates *non-boss chest* locations on `CanKill<Boss>`. So the excluded set is "all boss + mini-boss sprites," not "boss-room sprites": the GT mini-boss sprite ids (Armos/Lanmolas/Moldorm as GT mini-bosses — `0x09`/`0x53`/`0x54`, cf. `shuffle_boss.c:214-216`) MUST be flagged `is_boss` / do-not-randomize so substitution never replaces the enemy a `CanKill<Boss>` predicate assumes the player faces (and never breaks the boss fight itself).

**The crucial corollary**: ALTTPR/zelda3 logic deliberately models *no* per-room kill-clear (the `enemizer.enemyHealth='default'` gate was dropped — `macros.yaml:327,341`). So beatability is enforced **entirely by the runtime constraint table (D3), never by logic.** A table bug doesn't move the digest — it ships an unbeatable seed the corpus and `--rando-selftest` cannot catch. The sweep (Task 2) must re-confirm across `logic*.yaml` + `logic_parts/**` + `macros.yaml` + `op_registry.yaml` (not just `logic*.yaml`) on any future logic edit; if a non-boss kill predicate is ever added, pin that enemy.

### D8: Determinism + corpus

**`enemy_shuffle` does NOT grow the canonical layout (audit H1).** It packs into a reserved pad bit (e.g. byte `[26]` bit 0 — the deserializer's permissive trailing pad, the intended extension surface), exactly as the entrance-shuffle axes packed into pad byte `[25]` to avoid the size cascade. `kSettingsCanonicalLen` stays **28**; no size-coupling cascade (the ≥6 coupled sites + `_Static_assert` are untouched). `kGeneratorVersion` still bumps by one (60→61 as-built) — to version-lock a new *live runtime* axis and because an `enemy_shuffle=on` seed serializes a non-zero pad bit (changing *that* seed's `settings_hash`) — but **default-settings `settings_hash` AND all-seeds `placement_digest_hex` stay byte-identical**. The corpus regenerates only its manifest `generator_version`; digests are unchanged (no corpus seed enables the axis). Validate per `CLAUDE.md`: WSL `make zelda3`, `bump_rando_corpus.py --apply`, 3-way diff vs unmodified `main` — expect **zero** digest movement.

`EnemyShuffle_SelfCheck` asserts: an active shuffle never picks outside the resolved loaded-sheet set, never picks outside the vanilla loader-context or overworld-palette allowlists, never replaces an excluded type/marker, preserves the killable+key and context invariants for sampled rooms, verifies generated reshuffle pool integrity / multi-slot-sheet completeness / overlord needs, and pins stat-randomization bounds. It MUST be registered in `Rando_RunAllSelfChecks` or it won't run under `--rando-selftest` (audit L3).

**F12 stale-sheet fix (2026-06-13).** The selfcheck now also asserts that an
active picker with no matching sheet snapshot, or a snapshot for a different
room, leaves the source sprite vanilla. This is the headless guard for the
playtest-caught room `0xA9` Helmasaur render glitch.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| GFX-sheet mismatch → crash/garbage | D3 gates every pick on live `sprite_gfx_subset_0..3`; generated tables list every required slot; `EnemyShuffle_SelfCheck` asserts no out-of-sheet pick |
| Sheet-compatible but wrong loader context → bad draw/palette/AI | D3 derives a vanilla dungeon/overworld context allowlist from the shipped sprite blobs and requires it for every candidate |
| Sheet-compatible overworld candidate under the wrong sprite palette → bad colors | D3 derives a per-type overworld sprite-palette allowlist from vanilla sprite lists + `kOverworldSpritePalettes` and requires the current area palette id |
| Non-killable enemy in a key/shutter room → softlock | D3 killable/`can_drop_key` invariant; the constraint table is the correctness surface |
| Constraint table is wrong/incomplete for some sprite id | Start from Enemizer's `SpriteRequirement.cs` (battle-tested), conservatively mark unknowns `do_not_randomize`; widen later |
| Multi-tile substitution clobbers OAM | Not modelled yet; rely on curated pool + `sprite_flags2` from replacement type, and keep OAM overruns on the playtest checklist |
| Hidden non-boss kill-gated location desyncs logic | D7 sweep before declaring logic-free; pin the offending enemy if found |
| Playtest is the only validator (no headless render/crash test) | Stage playtest at slice start (`[[logic-vs-runtime-gap]]`): a small sheet-fixed pool across a few dungeons + the overworld before widening |

## Open Questions

1. Constraint table as a hand-authored source table (like `kSpriteInit_*`) or codegen'd from a YAML cross-checked against Enemizer? Decide at apply-time; lean source-table since it's structural (sprite ids + flags), not extracted ROM data.
2. Overworld substitution granularity — the *mechanism* is now known (rewrite `sprite_where_in_overworld = pick + 1`, exclude `>=0xf3`/`0xf4`; audit H3); the open part is whether to permute per area or per `sprite_where_in_overworld` block. Confirm against `Overworld_LoadProximaSpriteIfAlive` at apply-time.
3. Should the axis be a bool (on/off) or an enum (off / dungeon-only / overworld-only / both) for the MVP? Bool is simplest; an enum is append-compatible later (it would consume more than one pad bit — still within the pad bytes, no length growth).

## Audit notes (fresh-eyes pass, 2026-06-08)

Corrections folded in from an adversarial review (each re-verified against source):

- **Canonical layout (HIGH).** Do NOT grow `kSettingsCanonicalLen`; `[24]`=drop_shuffle, `[25]`=packed entrance byte, `[26]/[27]`=reserved pad. Pack `enemy_shuffle` into a pad bit (e.g. `[26]` bit 0); length stays 28; default `settings_hash` byte-identical. D8 + core delta rewritten.
- **Overworld mechanism (HIGH).** Not a `sprite_type` swap — it's `sprite_where_in_overworld = src[2]+1` with `>=0xf3`/`0xf4` markers. Context + shuffles delta corrected.
- **Native-window delta (HIGH).** ADDED leaves a permanent contradiction with the stale parity scenario (which is already wrong about boss/drop being inert). Made the supersession explicit + flagged the single reconciliation at archive.
- **GFX 0-inheritance (MED).** `kSpriteTilesets` `0` entries inherit the prior sheet; the loaded set isn't purely `[index][0..3]`. D3 + shuffles delta corrected.
- **Constraint flags (MED).** `killable` and `cannot_have_key` are independent; key-room safety must use the rando placement table (not the vanilla key-sprite scan); added directional bans + room exclude lists. D3 + shuffles delta expanded.
- **Fail-closed install (MED).** Mirror boss/drop's `*_Deactivate()` on the invalid-settings path. D1 corrected.
- **Logic-free confirmed (MED).** Sweep found only player-firepower kill macros; beatability rests *entirely* on the runtime table. D7 corrected; widen the sweep to `macros.yaml`/`op_registry.yaml`.
- **OAM ordering (LOW).** Spurious — `flags2` is read at `SpritePrep_LoadProperties` (`sprite.c:4144`) after the swap; only the overrun concern is real. D6 corrected.
- **Self-check registration (LOW).** Must register in `Rando_RunAllSelfChecks`. D8 corrected.

Confirmed sound by the audit: the boss-shuffle install precedent, `Sprite_HEX_*` symbol basis (178 symbols), and the core determinism bet (placement byte-identical) — the headline claim holds.

## Post-implementation reconciliation (2026-06-08)

Folded back from the implementation branch `claude/rando-enemy-shuffle` (build-verified + `--rando-selftest` green; not playtested):

- **kGen drift.** `main` advanced 58→60 (Phase D `add-rando-major-glitch`) while this proposal was open, so the as-built bump is **60→61**, not the 58→59 originally drafted. The branch + corpus regen used the correct v60 baseline and reported **0/112 digests changed**, proving the placement-byte-identical claim. The absolute pair tracks `main`'s value at merge.
- **GFX `0`-inheritance handled in code.** Picks read live `sprite_gfx_subset_0..3` directly rather than the static `kSpriteTilesets` row, so no `load_gfx.c` change was needed (D3's concern resolved at the read site).
- **First-pass table is intentionally small** (~30 unambiguous-safe enemies; everything uncertain `do_not_randomize`) — sound but low-variety; widening + OAM-footprint modeling + render/softlock validation are the playtest-gated remainder.

## Fresh-Eyes Audit Notes (2026-06-13)

See `audit.md` for the post-v3 audit. The audit reconciled the spec delta to the
all-slot reshuffle machinery + HP/contact-damage as-built behavior, removed the
unsupported water-room guarantee from the normative delta, and left
palette/OAM/render/softlock/water feel as playtest-only archive gates.

## F12 Playtest Fix (2026-06-13)

Owner playtest captured a garbled Mini-Helmasaur in dungeon room `0xA9`. The
dump showed live sheets `1F,2C,2E,52`; Mini-Helmasaur requires sheet `1E`, so it
could only have been admitted from stale sheet state. The runtime now records the
resolved sheet set with its room/area key at sheet-load time and requires that
snapshot to match before substitution. Release alternate build + full
`--rando-selftest` passed; the later context + overworld-palette gated build was
owner-playtested as solid for the caught F12 cases.

A follow-up capture in the same room showed the snapshot/live sheets agreeing on
`1F,1E,23,52`, but the Mini-Helmasaur still had bad colors. That confirms the
remaining failure was palette safety, not sheet safety. Runtime sheet widening is
therefore disabled until the implementation models which OBJ palettes each
replacement sheet needs.

A separate overworld capture showed Ball-and-Chain Trooper (`0x6A`) admitted in
area `0x2E` because its sheets were loaded. That exposed a context constraint
gap: the sprite is dungeon-only in this fork's sheet metadata and uses a
dungeon-oriented draw/AI path. The later vanilla-context allowlist now excludes
`0x6A` from overworld without a manual flag; adjacent `0x6B` Cannon Soldier is
still no longer randomizable because its handler only supports the
spawned-cannonball path.

Another overworld capture below Link's house showed Assault Sword Soldier
(`0x44`) admitted in area `0x2C` under valid sheets. That sprite uses this fork's
special `Sprite_44_BluesainBolt` / `PsychoTrooper_Draw` path and is also
dungeon-only for shuffle purposes. The later vanilla-context allowlist now
excludes it from overworld without a manual flag.

Those overworld captures exposed a broader rule, not just two bad IDs: "required
sheets are loaded" is necessary but not sufficient. The picker now derives a
vanilla-context allowlist from the shipped dungeon and overworld sprite blobs and
requires candidates to appear in the same loader context. Manual fork-local bans
still apply on top for non-context hazards like `0x6B`, but playtesting should no
longer need to discover every sheet-compatible dungeon-only sprite one at a time.

A later overworld capture in area `0x2B` showed Red Bomb Soldier / BombGuard
(`0x4A`) with valid sheets (`46,49` hex) but bad colors. Direct asset inspection
showed vanilla `0x4A` overworld occurrences only under sprite palette `3`, while
the captured area had `overworld_sprite_palettes[0x2B] == 0`. The context scanner
now records per-type overworld palette ids from `kOverworldSpritePalettes`, and
the picker rejects overworld candidates whose type is not vanilla-observed under
the current area's sprite palette. `0x4A` remains randomizable and overworld-
eligible in its vanilla palette context; this is not a spot ban.
