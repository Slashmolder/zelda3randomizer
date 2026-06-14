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
- Boss shuffle and drop-pool shuffle are orthogonal; enemy shuffle does not touch either.
- Sprite GFX/skin packs (cosmetic) are owned by `add-rando-cosmetic-shuffles`.

## Decisions

### D1: Install model — copy boss shuffle

`EnemyShuffle_Generate(settings, seed_u64)` builds the substitution from a dedicated xoshiro256\*\* stream forked off the seed (per `randomizer-core / RNG family`), so the fill RNG is unperturbed and the result is cross-platform self-consistent. Installed module-global from `Rando_ActivateSidecarSlot` (alongside `BossShuffle_Generate`/`DropShuffle_Generate`, `src/rando/rando.c:1932-1947`); torn down in `Rando_DeactivateSlot`. Off / no-active-slot ⇒ the load path takes the vanilla branch.

**Fail-closed on the invalid-settings path (audit correction).** Boss/drop shuffle only install inside the `g_rando_active_settings_valid` branch; on the invalid path (a v1 slot, or a snapshot-restore that restored `g_ram` but not the C-static `RandoSettings`) they explicitly call `BossShuffle_Deactivate()` / `DropShuffle_Deactivate()` to avoid leaking a prior slot's assignment (`rando.c:1904-1960` — the `[[race_mode_null_settings_failopen]]` class). Enemy shuffle MUST mirror this: install only when settings are valid, and `EnemyShuffle_Deactivate()` on the invalid path. An unconditional install would leak a stale substitution into a snapshot-restored slot.

### D2: What gets substituted — the "randomizable enemy" predicate

For each loaded entry, substitute only when ALL hold (else leave vanilla):
- not a control entry (`type != 0xe4`) and not an overlord (`x < 0xe0`) — these are skipped before type read anyway;
- the type's constraint flags mark it a **randomizable enemy**: not `is_boss`, not `is_object`, not `is_npc`, not `do_not_randomize`.

Bosses and their secondary parts are excluded wholesale (boss shuffle owns those, and the room-environment-dependent ones — Blind/Kholdstare/Trinexx — must never be substituted *in*; see D5). NPCs, quest objects, switches, and statues are excluded so the room stays solvable and dialog/triggers stay intact.

### D3: The replacement pick — GFX-sheet-constrained (the anti-crash invariant)

Given the room/area, the candidate pool is the set of randomizable enemies whose constraint-table sheet requirement is satisfied by the **actually-loaded** sheets. **Caveat (audit M2): the loaded set is NOT purely `kSpriteTilesets[index][0..3]`.** `Gfx_LoadSpritesInner` (`load_gfx.c:627-640`) loads each subgroup only `if (p[N])` — a `0` entry leaves the previously-loaded `sprite_gfx_subset_N` in place, and many `kSpriteTilesets` rows have `0` slots (e.g. `[0,73,0,0]`). So a static check against `kSpriteTilesets[index]` is unsound for those rooms — a pick can pass it and still reference an unloaded sheet → garbage/crash. Resolve `0` entries against the live `sprite_gfx_subset_N`, or conservatively treat a `0` slot as unknown and exclude any enemy needing a sheet not in the non-zero entries. Pick deterministically from the resolved pool. This is **non-negotiable**. (Enemizer enforces the loaded-sheet rule via `SpriteRequirement.SpriteInGroup`.)

Additional per-pick constraints (from Enemizer; the beatability invariants — audit M3/M4 expanded the flag set):
- **Killable AND key-capable are independent flags.** `killable` and `cannot_have_key` are separate (Enemizer `CannotHaveKey` ≠ `!Killable`: Keese/Buzzblob/Geldman are killable but key-banned). A key slot's replacement MUST be `killable && !cannot_have_key`; a shutter/kill-clear room's replacement MUST be `killable`. ("killable ⇒ key-capable" is wrong.)
- **Shutter/kill-clear rooms are a hand-maintained room-id list** (Enemizer `NeedKillable_doors` via `Room.IsShutterRoom`), not derivable from room data — port it.
- **Key-room detection must use the rando placement table, not the vanilla sprite list.** Enemizer's `HasAKey` scans the vanilla ROM for an adjacent key sprite (`DungeonSprite.cs:66`); under item shuffle the key may be absent or placed elsewhere, so that scan is unreliable. Define key-room safety against the placement table + `NeedKillable_doors`.
- **Directional bans**: per-enemy `never_use_dungeon` / `never_use_overworld` (Enemizer `NeverUseDungeon`/`NeverUseOverworld`) constrain the pool by load context. (Some such sprites — e.g. Wallmaster, Flopping Fish — are additionally fully `do_not_randomize` in Enemizer; the directional flag covers the partially-restricted remainder.)
- **Water capability is tabled but not enforced yet**: `ESF_WATER` is generated,
  but the current picker never sets `require_water`. Treat water stranding as a
  playtest watch item / future classifier rather than a shipped invariant.
- **Per-room excludes** (anti-softlock): the immovable-sprite room list (~60 rooms, `DontUseImmovableSpritesRooms`), the flying-sprite room list, per-sprite do-not-randomize room lists, and hard excludes (Mimic Cave, Agahnim-tower bridge) — port into a per-room constraint map (`SpriteRequirement.cs:897-956` + per-sprite `AddDontRandomizeRooms`).

### D4: MVP kept sheets fixed — SUPERSEDED by all-slot runtime reshuffle

Enemizer first re-shuffles each group's 4 subgroup sheets to widen the enemy pool, then picks. The MVP **did not** — it picked only within the sheets a room already loads. This shrank variety but removed a whole class of crash risk and kept the change to a pure type substitution.

**As-built.** The reshuffle is now always-on under `enemy_shuffle` (no new
canonical axis) and covers subgroup slots **0,1,2,3**. It runs at sheet-load time
after the four `kSpriteTilesets` entries resolve and before decompression, so VRAM
and the type picker agree. Key facts that keep it safe without Enemizer's global
group-table rewrite:

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
  The room's vanilla sheet is always a possible outcome.
- **Inheritance shadow.** A 4-byte g_ram shadow tracks the vanilla-resolved sheet
  per slot, so inherited/pinned slots restore vanilla and a prior reshuffle cannot
  leak through a `0` subgroup entry.

### D5: Room-environment-dependent sprites stay out

The same environment dependency that pins Blind/Kholdstare/Trinexx in boss shuffle (the room "effect" byte `dung_hdr_collision_2` = `$00AD` = `hdr[4]`, BG2 ice/lava objects, and room-state-gated spawns like Blind's maiden bit `dung_savegame_state_bits & 0x2000`) means: never substitute *into* a slot whose mechanics need that environment, and never put an environment-dependent enemy where its environment isn't present. The MVP achieves this simply by **excluding all bosses + their secondaries** (the only environment-coupled sprites) from both the source-replace set and the candidate pool.

### D6: OAM-budget compatibility

The per-sprite OAM reservation is `((sprite_flags2[k] & 0x1f) + 1) * 4` **bytes** (`Sprite_TimersAndOam` `src/sprite.c:1164`). Substituting a 1-entry enemy with a multi-tile one can overrun the reserved region (scene-dependent tile clobber). The current as-built table does **not** model OAM footprint; this remains a playtest watch item, mitigated by the curated pool and by `sprite_flags2` being loaded from the replacement type. **The init-order caveat in an earlier draft was spurious (audit L4):** `kSpriteInit_Flags2[j]` is read in `SpritePrep_LoadProperties` (`sprite.c:4144`), which runs at sprite prep/activation — strictly after the load-time type swap — so `sprite_flags2` already reflects the new type; no ordering fix is needed. The OAM-overrun concern is still real.

### D7: Logic — does the placer need to know? (verify, don't assume)

A pure enemy-type shuffle is **logic-free for our purposes** — the logic sweep confirmed no predicate is gated on which *shuffled* enemy spawns. The graph's kill predicates are: `CanKillBoss(<dungeon>)` (→ `OP_CAN_KILL_BOSS`); the **11 per-boss `CanKill<Boss>` macros** (`CanKillLanmolas`/`CanKillMoldorm`/`CanKillArmosKnights`/… — used *directly* for the **GT mini-boss gauntlet** and the pinned Agahnim, e.g. `assets/rando/logic_parts/.../33_ganons_tower.yaml`); and the firepower macros `CanKillMostThings`/`CanKillEscapeThings` (`macros.yaml:318,337`). Every one is a **player-firepower** test (does the inventory let Link kill a thing of that class), NOT an "is this specific enemy present" test — and the only enemies they reference are **bosses / mini-bosses, which are excluded from the substitution pool**. So enemy shuffle adds no predicate and no `OP_*`, and `placement_digest_hex` is byte-identical.

**Exclusion must cover GT mini-bosses, not just boss rooms (audit HIGH).** The GT mini-boss gauntlet gates *non-boss chest* locations on `CanKill<Boss>`. So the excluded set is "all boss + mini-boss sprites," not "boss-room sprites": the GT mini-boss sprite ids (Armos/Lanmolas/Moldorm as GT mini-bosses — `0x09`/`0x53`/`0x54`, cf. `shuffle_boss.c:214-216`) MUST be flagged `is_boss` / do-not-randomize so substitution never replaces the enemy a `CanKill<Boss>` predicate assumes the player faces (and never breaks the boss fight itself).

**The crucial corollary**: ALTTPR/zelda3 logic deliberately models *no* per-room kill-clear (the `enemizer.enemyHealth='default'` gate was dropped — `macros.yaml:327,341`). So beatability is enforced **entirely by the runtime constraint table (D3), never by logic.** A table bug doesn't move the digest — it ships an unbeatable seed the corpus and `--rando-selftest` cannot catch. The sweep (Task 2) must re-confirm across `logic*.yaml` + `logic_parts/**` + `macros.yaml` + `op_registry.yaml` (not just `logic*.yaml`) on any future logic edit; if a non-boss kill predicate is ever added, pin that enemy.

### D8: Determinism + corpus

**`enemy_shuffle` does NOT grow the canonical layout.** It packs into canonical byte `[26]` bit0, exactly as the entrance-shuffle axes packed into byte `[25]` to avoid the size cascade. `kSettingsCanonicalLen` stays **28**; no size-coupling cascade is triggered. `kGeneratorVersion` still bumps to version-lock a new *live runtime* axis and because an `enemy_shuffle=on` seed serializes a non-zero pad bit (changing *that* seed's `settings_hash`) — but **default-settings `settings_hash` AND all-seeds `placement_digest_hex` stay byte-identical**. The corpus regenerates only its manifest `generator_version`; digests are unchanged when no corpus seed enables the axis.

`EnemyShuffle_SelfCheck` asserts: an active shuffle never picks outside the resolved loaded-sheet set, never replaces an excluded type/marker, preserves the killable+key and directional invariants for sampled rooms, verifies generated reshuffle pool integrity / multi-slot-sheet completeness / overlord needs, and pins stat-randomization bounds. It MUST be registered in `Rando_RunAllSelfChecks` or it won't run under `--rando-selftest` (audit L3).

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| GFX-sheet mismatch → crash/garbage | D3 gates every pick on live `sprite_gfx_subset_0..3`; generated tables list every required slot; `EnemyShuffle_SelfCheck` asserts no out-of-sheet pick |
| Non-killable enemy in a key/shutter room → softlock | D3 killable/`can_drop_key` invariant; the constraint table is the correctness surface |
| Constraint table is wrong/incomplete for some sprite id | Start from Enemizer's `SpriteRequirement.cs` (battle-tested), conservatively mark unknowns `do_not_randomize`; widen later |
| Multi-tile substitution clobbers OAM | Not modelled yet; rely on curated pool + `sprite_flags2` from replacement type, and keep OAM overruns on the playtest checklist |
| Hidden non-boss kill-gated location desyncs logic | D7 sweep before declaring logic-free; pin the offending enemy if found |
| Playtest is the only validator (no headless render/crash test) | Stage playtest at slice start (`[[logic-vs-runtime-gap]]`): a small sheet-fixed pool across a few dungeons + the overworld before widening |
