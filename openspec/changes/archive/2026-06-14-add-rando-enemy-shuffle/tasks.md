# Tasks — add-rando-enemy-shuffle

> **Implementation status (2026-06-08, first build-verifiable pass).** The
> build-verifiable module + plumbing + a CONSERVATIVE first-pass constraint
> table are landed: WSL `make -j zelda3` (-Werror) green, `--rando-selftest`
> (incl. `EnemyShuffle_SelfCheck` + the extended `Settings_SelfCheck`) green,
> corpus regen 0/112 digests changed (placement byte-identical — the spec's
> core claim verified). Subsequent owner playtesting caught and fixed the F12
> render issues documented below; the current context + overworld-palette gated
> build has been owner-playtested as solid. Remaining limits are explicit:
> OAM footprint and independent water-only room classification are not modeled
> (water-capable source sprites do stay water-capable). **Deviation:**
> kGeneratorVersion is **60→61**, not the brief's 58→59 — the worktree base
> already carried Phase D bumps to 60.

> **SHEET-GROUP RESHUFFLE (variety unlock) — BUILT, PALETTE-GATED, PLAYTEST-PENDING (§7.4).**
> Decided (owner): EXTEND this change (not a new one); ALWAYS-ON when
> `enemy_shuffle` (no new canonical axis). Phase 1 = subgroup **SLOT 2 only**
> (the "themed enemy" slot), dungeon + overworld, landed build-verified: WSL
> `-Werror` green, `EnemyShuffle_SelfCheck` extended + green, corpus regen
> **0/112 digests changed** (only `generator_version` moved — branch-built as
> 61→62, landed as **64→65** at the merge; placement byte-identical, the
> orthogonality claim holds). See **§7.4** for the as-built model.
> Eligibility (slot 2 free) + a safe slot-2 pool + a vanilla-inheritance shadow
> make it crash/softlock-safe BY CONSTRUCTION on paper — but **render/crash/
> softlock are playtest-only** (no headless net). Owner playtesting of the current
> palette/context-gated build is now green. Conservative by design (boss rooms +
> unknown-type rooms are skipped); the widening machinery has since landed
> (§7.4 as-built v3): all 4
> subgroup slots, dungeon-overlord decode, + per-seed HP/contact-damage
> randomization (kGen 64→65; bosses exempt). Runtime sheet widening is currently
> forced back to vanilla-resolved sheets after F12 caught missing sprite-palette
> modeling. Remaining deferred axes are killable-thief / bush enemies /
> absorbables / randomize-on-hit (§7.3).
>
> **Spec reconciliation + fresh-eyes audit (2026-06-13).** The stale
> `randomizer-shuffles` delta now matches the as-built v5 scope: all four
> subgroup slots with palette-gated runtime widening, generated Enemizer-sourced
> tables, dungeon-overlord spawned-slot decode, graveyard slot-3 pin, and
> HP/contact-damage randomization.
> The unsupported room-level water-only guarantee was narrowed in the normative
> delta: source sprites tagged `ESF_WATER` set `require_water`, but there is no
> independent water-room classifier yet. See `audit.md`.
>
> **F12 playtest finding (2026-06-13).** Room `0xA9` captured a garbled
> Mini-Helmasaur under live sheets `1F,2C,2E,52`; that replacement is only
> admissible if a stale sheet set containing `1E` leaked into the pick. Fixed by
> snapshotting the resolved sheet set with its room/area key at sheet-load time
> (`0x666..0x66c`) and making the picker pass through vanilla unless the snapshot
> matches the current sprite list and live subsets. Release build + extended
> `EnemyShuffle_SelfCheck` green; needs owner re-playtest of the caught room.
>
> **F12 context-safety follow-up (2026-06-13).** Subsequent overworld captures
> found sheet-compatible but context-wrong enemies (`0x6A` Ball-and-Chain in
> area `0x2E`, `0x44` Assault Sword Soldier below Link's house area `0x2C`).
> Fixed systemically: the picker now derives a vanilla dungeon/overworld context
> allowlist from the shipped sprite blobs and requires candidates to appear in
> the same loader context, in addition to sheet coverage and generated/manual
> hard bans. Temporary manual overworld bans for `0x44`/`0x6A` were removed once
> this behavior-level guard landed; `0x6B` stays do-not-randomize.
>
> **F12 overworld palette follow-up (2026-06-13).** Another overworld capture
> found Red Bomb Soldier / BombGuard (`0x4A`) in area `0x2B`. Its sheets were
> loaded, and vanilla does use `0x4A` in overworld, but only under sprite palette
> `3`; the captured area had `overworld_sprite_palettes[0x2B] == 0`. Fixed
> systemically: the vanilla-context scanner now also records per-type overworld
> sprite-palette ids from `kOverworldSpritePalettes`, and the picker rejects
> overworld candidates not vanilla-observed under the current area's palette.
> `0x4A` remains randomizable and eligible where the palette matches.

## 1. Constraint table (the correctness surface — do this first)

- [x] 1.1 Per-enemy-type constraint table authored in `src/rando/shuffle_enemies.c` (`kEnemyTable[256]`), cross-checked against Enemizer `SpriteRequirement.cs` (the Enemizer SpriteId IS the SNES type byte → identity mapping; `Sprite_HEX_*` ids in `sprite_main.h` confirm the symbol names). Flags: `ESF_RANDOMIZABLE`, `ESF_KILLABLE`, `ESF_CANNOT_KEY` (independent of killable), `ESF_WATER`, `ESF_NEVER_DUNGEON`/`ESF_NEVER_OVERWORLD`, `ESF_FLYING`, plus a required-`sheets[]` list. **CONSERVATIVE first pass:** only a SMALL, sound candidate pool (~30 unambiguous killable/common enemies + a few flying/water/key-banned) is `ESF_RANDOMIZABLE`; everything else (NPC/object/overlord/boss/mini-boss/absorbable/unknown) is `do_not_randomize` (the zero default). OAM footprint is NOT yet modelled (see §3.4 / Remaining). **Widen only with playtest** — the table is the SOLE beatability enforcer (logic models no kill-clear).
- [x] 1.2 Table form = inline named-initializer source table in `shuffle_enemies.c` (structural sprite-id/flag facts, no hex blob — passes the embedded-data guard), alongside the spirit of the `kSpriteInit_*` tables.
- [x] 1.3 Ported the per-room data: `kHardExcludeRooms` (Mimic Cave 268 + Agahnim-tower bridge 64 — never substitute), `kFlyingExcludeRooms` (210/268), and `kKillableRequiredRooms` (a ported subset of `DontUseImmovableSpritesRooms`). NOTE: the MVP currently enforces killable+key-capable for ALL dungeon rooms (see §1.4), so the immovable/shutter lists are advisory until the pool widens past killable-only.
- [x] 1.4 Key-room safety is over-approximated CONSERVATIVELY: EVERY dungeon replacement is `killable && !cannot_have_key` (so any key a shuffled placement drops in any room is obtainable, and every shutter/kill-clear door opens) — NOT keyed to the placement table per-room. This is the simplest SOUND model; it over-restricts dungeon variety. A future refinement can relax non-key rooms by consulting the placement table (see Remaining).
- [x] 1.5 GT mini-bosses `0x09`/`0x53`/`0x54` + all bosses + secondaries (`0xA3`/`0xCC`/`0xCD`) + Agahnim `0x7A` + Ganon `0xD6` are excluded (never `ESF_RANDOMIZABLE`); `EnemyShuffle_SelfCheck` asserts this against `kMustExcludeBossIds`.

## 2. Logic sweep (confirm enemy shuffle is logic-free — re-run on any logic edit)

- [x] 2.1 Swept `assets/rando/logic*.yaml` + `logic_parts/**` + `macros.yaml` + `op_registry.yaml`: every `CanKill<X>` macro is `CanKillBoss`, a per-boss macro (used only at dungeon boss rooms + the GT mini-boss gauntlet — all EXCLUDED sprites), or the player-firepower macros `CanKillMostThings`/`CanKillEscapeThings` (pure inventory tests, the `enemizer.enemyHealth='default'` kill-clear gate explicitly dropped). No non-boss kill-gated location exists. **Logic-free confirmed.**
- [x] 2.2 No pinning beyond the bosses/mini-bosses already excluded was needed. Enemy shuffle adds no predicate / no `OP_*`; beatability rests SOLELY on the §1 table.

## 3. Module + install (mirror boss shuffle)

- [x] 3.1 New `src/rando/shuffle_enemies.{c,h}`: `EnemyShuffle_Generate(settings, seed_u64)` (dedicated xoshiro256\*\* fork, salt `kEnemyShuffleSalt`) installs a module-global + per-seed RNG seed; `EnemyShuffle_Deactivate` tears it down. NOTE divergence from boss shuffle: no precomputed assignment table — the pick is computed per-room/per-area at LOAD time (the candidate pool depends on the LIVE-loaded sheets, unknown at gen time). Determinism comes from a per-(seed, room/area, slot) RNG.
- [x] 3.2 Installed in `Rando_ActivateSidecarSlot` on the `g_rando_active_settings_valid` path only; `EnemyShuffle_Deactivate()` on the fail-closed invalid path + in `Rando_DeactivateSlot`.
- [x] 3.3 Patched BOTH load paths in `src/sprite.c`:
  - **Dungeon** `Dungeon_LoadSprites`: substitutes `ent[2]` (the type byte) before `Dungeon_LoadSingleSprite` writes `sprite_type[k]` — pure type-byte swap, the bit-packed y/x untouched. Skips control `0xe4` / overlord `x>=0xe0`.
  - **Overworld** `Overworld_LoadSprites`: rewrites the stored value as `pick + 1` (keeps the `+1` bias). Skips count marker `src[2]==0xf4` / overlord `src[2]>=0xf3`.
  - Both: the pick is constrained to ACTUALLY-loaded sheets through the resolved-sheet snapshot recorded by `EnemyShuffle_ReshuffleCurrentRoomSheets` (snapshot `0x666..0x66c`, live subsets `0xC2FC..0xC2FF`). The picker refuses missing/stale snapshots, so transition-order leaks fail closed to vanilla.
- [x] 3.4 OAM footprint compatibility is explicitly deferred to the palette/OAM widening follow-up: the constraint table does not yet record each enemy's OAM byte footprint, so a multi-tile replacement could overrun `((sprite_flags2&0x1f)+1)*4` bytes. Mitigated for this shipped scope by the conservative pool and the current vanilla-sheet gate, but still a future playtest/modeling item. No init-order fix needed (`flags2` read at `SpritePrep_LoadProperties`, after the swap).

## 4. Settings axis + canonical layout

- [x] 4.1 `enemy_shuffle` (uint8) added to `RandoSettings`; CSV parse (`enemy_shuffle=true|false`) + default-off wired.
- [x] 4.2 Packed into canonical pad byte `[26]` bit0 (`kEnemyShuffleAxis_Enabled`) in `Settings_CanonicalSerialize`/`Deserialize`; `kSettingsCanonicalLen` stays 28 (no size-coupling cascade); `Settings_SelfCheck` asserts default byte-identity + that only [26] moves when on. Follows the entrance-axis `[25]` precedent.
- [x] 4.3 Bumped `kGeneratorVersion` **60 → 61** (NOT 58→59 — the worktree base already carried Phase D bumps to 60). The brief's intent (version-lock the new live axis) is preserved.
- [x] 4.4 The `randomizer-core` ADDED requirement (this change's spec delta) documents the new pad bit + carries the apply-time reconciliation note for the stale normative field list. The authoritative byte map now lives in the `rando_settings.{h,c}` layout comments (updated to include [26]=enemy_shuffle). Full normative-list reconciliation is an archive-time editorial pass (the note flags it) — not silently rewritten here.

## 5. UI

- [x] 5.1 Replaced the disabled enemy-shuffle "coming soon" placeholder with a live checkbox in `rando_window.cpp` (tooltip "Randomizes which enemies appear in each room."). Glitches remains the only disabled placeholder. Compiles under WSL g++ (`rando_window.o`) + the panel smoke check passes; full interactive MSBuild verify is pending (the WSL build does cover this TU, so the seam is exercised).

## 6. Validation

- [x] 6.1 Corpus regen via WSL `make zelda3` + `bump_rando_corpus.py --apply --binary <abs>`: **0/112 digests changed**; CRLF-normalized diff vs the v60 baseline shows ONLY `generator_version: 60→61`. `run_rando_corpus.py` re-verifies all 112 against the binary; `check_corpus_version_sync` green. (3-way diff vs a fresh unmodified-`main` build was not run separately — the in-tree before/after diff + the placement-orthogonality proof, 0 changes, is the stronger evidence; noted for completeness.)
- [x] 6.2 `EnemyShuffle_SelfCheck` registered in `Rando_RunAllSelfChecks` and passing under `--rando-selftest`: asserts off→passthrough, boss/mini-boss/marker exclusion, table integrity (every randomizable has a required sheet + a killable+key candidate exists), determinism, in-sheet picks, stale/missing sheet-snapshot passthrough, dungeon killable+key / overworld in-sheet invariants, generated reshuffle pool integrity, multi-slot-sheet completeness, overlord spawn-slot needs, and HP/contact-damage bounds.
- [x] 6.3 **Playtest — current F12 regression pass done.** Owner retested the palette/context-gated build after the room `0xA9` and overworld `0x2B`/`0x2C`/`0x2E` fixes and reported it solid. No headless validator covers render/crash/softlock, so keep OAM overruns, water strands, and HP/damage feel as residual watch items. Slot-0/1/2/3 widened-sheet rooms and dungeon-overlord-freed spawner rooms move to the future palette-aware widening pass.
- [x] 6.4 Backward-load disposition: not separately playtested for this change, but the kGen bump rides the existing one-time upgrade-warning path and requires no enemy-shuffle-specific save migration or sidecar field.

## 7. Follow-on axes

- [x] 7.1 Enemy HP randomization (clamp/scale `sprite_health[k]` / `kSpriteInit_Health[243]`) — built under the parent `enemy_shuffle` axis; deterministic per `(seed,type)`, bosses exempt, `base==0` passthrough.
- [x] 7.2 Enemy damage randomization (`kSpriteInit_BumpDamage[243]`) — built under the parent `enemy_shuffle` axis; plain classes 1..8 nudge by -1/0/+1, flagged/boss values passthrough.
- [x] 7.3 Killable thief, bush-enemy spawn, absorbables-in-place-of-enemies, and randomize-on-hit are explicitly deferred follow-on axes, not part of the shipped enemy-shuffle scope.
### 7.4 Sprite-group sheet reshuffle — the VARIETY UNLOCK (design D4) [BUILT — PALETTE-GATED]

> **As-built (phase 1, slot 2 only).** Decided: EXTEND this change; ALWAYS-ON
> when `enemy_shuffle` (no new axis). kGeneratorVersion **61→62**; corpus
> **0/112** changed. Hook `EnemyShuffle_ReshuffleCurrentRoomSheets(row)` in
> `Gfx_LoadSpritesInner` + `InitializeTilesets` (`src/load_gfx.c`), after the 4
> subgroup ids resolve, before decompress.
>
> **As-built v2 (slots 0,1,2 + widened table).** The reshuffle now covers subgroup
> slots **0, 1, 2** (the enemy slots), and the constraint tables are **GENERATED
> from the Enemizer (MIT) source** by `assets/scripts/gen_enemy_shuffle_tables.py`
> (parses `SpriteRequirement.cs`/`SpriteConstants.cs`) — replacing the hand tables
> that dropped Walking Zora's sheet 68 and `0x16`/`0xBC`. The generator emits the
> randomizable enemy table (ALL required slots per enemy; OR-within-slot → most-
> common vanilla sheet), `kSheetNeed[256]` (per-type pin mask, bit7 KNOWN | bits
> 3..0 needs slot 3..0), the boss set, and per-slot dungeon/overworld pools — and
> ASSERTS the disjointness invariant the position-unaware picker relies on. The
> randomizable enemy set widened **~31 → 48** (soldier/archer variants, Bari,
> Bush Hoarder, …) so EVERY pick has more types, not just the reshuffle. One room
> walk yields a blocked-slot bitmask; each owned, unpinned slot reshuffles from its
> pool (dungeon pools each self-contain a killable+key enemy → key rooms fillable),
> inheriting/pinned slots restore from a 3-byte per-slot vanilla shadow (g_ram
> `0x662..0x664`). Diag relocated to `0x665..0x667` (calls / cumulative slots-
> changed / last blocked-mask). Still runtime-only — corpus **0/112**, kGen stays
> **62**. The Zora "hybrid" bug taught the rule now enforced: **every enemy must
> list ALL its slots** (generator + selfcheck guard it).
>
> **As-built v3 (owner: "be complete" — slot 3 + overlord decode + HP/damage).**
> Three more axes, each its own revertable commit:
> 1. **Slot 3** (all 4 subgroup slots): `ES_RESHUFFLE_SLOTS` 3→4. Slot 3 is mostly
>    objects (statues/switches on 82/83 pin it via `kSheetNeed` bit3, so it
>    reshuffles rarely) but carries Armos/Tektite 16, Buzzblob/Bush Hoarder 17,
>    Pikit 27. Pools: dungeon `{16}`, overworld `{17,16,27}`. 4-byte vanilla
>    shadow now lives at `0x662..0x665`.
> 2. **Dungeon overlord decode**: instead of pinning ALL slots on any overlord,
>    decode `overlord_type = src[2]` (Enemizer id 0x100+type — verified against
>    `kOverlordFuncs`) and pin ONLY the slots its SPAWNED sprite needs (generated
>    `kOverlordNeed[32]`), freeing the rest of spawner/trap rooms. No-spawn-sheet
>    overlords (the boss-spawning ArmosCoordinator 0x19, MovingFloor, BombTrap,
>    unknown) stay pin-ALL. Overworld overlords still pin-all (future micro-opt).
> 3. **HP / contact-damage randomization** (`SpritePrep_LoadProperties` hook):
>    per-(seed,type) HP ×[0.5..2.0] clamped [1,255] (0-HP sprites untouched →
>    NPCs/objects unaffected; softlock-safe — never unkillable, logic-free); damage
>    is a flag-preserving ±1 *class* nudge on plain values 1..8 only (`bump_damage`
>    is `class | flags`). Always-on with `enemy_shuffle`; revert the single commit
>    to drop it. Still corpus **0/112**, kGen **62** (all runtime, placement-orthogonal).
>
> **As-built v4 (F12 stale-sheet guard).** The picker no longer trusts the live
> subset globals alone. The sheet resolver snapshots the resolved loaded sheets,
> room/area key, and context in `0x666..0x66c`; `EnemyShuffle_Pick*` substitutes
> only when that snapshot matches the current sprite list and still matches
> `sprite_gfx_subset_0..3`. Missing/stale snapshots pass through vanilla. This
> fixes the room-`0xA9` garbled Mini-Helmasaur capture (live sheets lacked `1E`).
>
> **As-built v5 (F12 palette guard).** A follow-up room-`0xA9` F12 capture had
> the snapshot and live sheets agreeing (`1F,1E,23,52`), so the stale-sheet bug
> was fixed; the Mini-Helmasaur tiles were now valid but its colors were wrong.
> Root cause: the runtime widened sprite sheets without also changing the room's
> OBJ sprite palettes. The runtime now forces every subgroup slot back to the
> vanilla-resolved sheet while still snapshotting the resolved set and allowing
> normal enemy type substitution inside that set. Palette-aware sheet widening is
> deferred.
>
> **As-built v6 (F12 overworld Ball-and-Chain guard).** A follow-up overworld
> area-`0x2E` capture had valid sheets/snapshot (`48,49,0C,46` hex) and an active
> Ball-and-Chain Trooper (`0x6A`). This was not stale state; the table had missed
> a context gate. The later vanilla-context allowlist now excludes `0x6A` from
> overworld without a manual flag. `0x6B` Cannon Soldier remains removed from the
> randomizable pool because this fork only handles it as a spawned cannonball
> path; the generator carries that fork-local override.
>
> **As-built v7 (F12 overworld Assault Soldier guard).** A below-Link's-house
> area-`0x2C` capture had valid sheets/snapshot (`51,49,13,46` hex) and an active
> Assault Sword Soldier (`0x44`). That sprite uses this fork's special
> `Sprite_44_BluesainBolt` / `PsychoTrooper_Draw` path. The later vanilla-context
> allowlist now excludes it from overworld without a manual flag.
>
> **As-built v8 (vanilla-context candidate gate).** The v6/v7 captures exposed a
> broader invariant: loaded sheets are necessary but not sufficient. The picker
> now scans `kDungeonSprites`/`kDungeonSpriteOffs` and all stages of
> `kOverworldSprites`/`kOverworldSpriteOffs` once to derive a per-type context
> bitset. A replacement candidate must be randomizable, pass directional/hard
> bans, appear in vanilla data for the current loader context, and have all
> required sheets loaded. `EnemyShuffle_SelfCheck` asserts known dungeon/overworld
> examples and sampled picks staying context-eligible.
>
> **As-built v9 (overworld sprite-palette candidate gate).** A later area-`0x2B`
> F12 capture had valid sheets/snapshot and an active Red Bomb Soldier/BombGuard
> (`0x4A`) with bad colors. Vanilla asset inspection showed `0x4A` appears in
> overworld only under sprite palette `3`, while the captured area had palette
> `0`. The vanilla-context scan now also records `kOverworldSpritePalettes` per
> sprite occurrence and builds a per-type overworld palette bitset. Overworld
> candidates must match the current area's `overworld_sprite_palettes` id (read
> with a dark-world-aware index — dark/special areas use the `[0x40..0x7F]` region)
> in addition to context, sheet coverage, and hard bans. `0x4A` stays
> randomizable and overworld-eligible where vanilla uses that palette.
>
> **MERGED to main** 2026-06-09 at kGen **65** (re-based from 62 — main advanced
> 61→62→63→64 via add-rando-major-glitch concurrently; corpus regen 0 digests
> changed). Default-off; placement byte-identical. The `ES_RESHUFFLE_DIAG`
> bring-up diagnostics were removed before push.
>
> ✅ **SPEC DELTAS RECONCILED 2026-06-13.** `specs/randomizer-shuffles` now
> describes the all-slot reshuffle machinery, palette-gated runtime sheet
> widening, generated Enemizer-sourced tables, dungeon-overlord spawned-slot
> decode, graveyard slot-3 pin, and HP/contact-damage randomization.
> `randomizer-core` remains accurate: the original one-bit `enemy_shuffle` axis
> owns both current enemy substitution and future palette-aware widening.
>
> OWNER PLAYTESTED current gates: overworld areas `0x2B` / `0x2C` / `0x2E` and
> room `0xA9` have been re-tested under the context + overworld-palette gates and
> reported solid. Remaining follow-up watch items are OAM footprint, room-level water strands, and
> HP-damage feel under broader play.
> Widened-sheet rooms wait for the palette-aware pass.

- [x] **Why / true-random.** The generated reshuffle tables and deterministic slot RNG remain built, but runtime sheet widening is currently palette-gated off: every subgroup slot resolves to the room's vanilla sheet so the picker draws only from the palette-safe, actually-loaded set. The vanilla-resolved sheet for each slot remains the forced outcome until palette requirements are modeled.
- [x] **Where (zelda3).** Reordered `Gfx_LoadSpritesInner` to resolve all 4 ids then hook then decompress (behavior-identical); `InitializeTilesets` hook inserted after its 4-id resolve. The hook may rewrite `sprite_gfx_subset_0..3` BEFORE decompress so VRAM + picker agree; cached-redecompress paths (mirror warp etc.) reproduce it for free. Room/area sprite TYPE list walked at load time via new `Dungeon_GetRoomSpritePtr` / existing `GetOverworldSpritePtr` (asset blob — list available before parse; key on `dungeon_room_index` / `overworld_area_index`).
- [x] **Enemizer port (`C:\src\Enemizer`).** Position whitelists from `SpriteGroupCollection` `PotentialSubsetN` (DISJOINT — a sheet id ⇒ one slot, so the picker's "sheet loaded" test stays sound with no position-aware change). Generated tables now cover all slots: `kEnemyTable`, `kSheetNeed`, per-slot pools, bosses, and `kOverlordNeed`. NOTE: the Walking Zora hybrid bug taught the invariant: every multi-slot enemy MUST list all required subgroup slots.
- [x] **Crash/softlock model (design D3).** (a) ELIGIBILITY: each subgroup slot originally reshuffled only when FREE — every present sprite is randomizable (substituted) or a known non-randomizable type that does not need that slot; this logic remains available but the palette guard currently pins all slots to vanilla. (b) ANTI-GARBAGE pool: each dungeon slot pool self-contains a killable+key enemy ⇒ the picker always finds a valid substitution and key/shutter rooms stay fillable when widening is re-enabled. (c) INHERITANCE leak: a snapshot-safe 4-byte shadow (`kRam_EnemyShuffleVanPos2` @ 0x662..0x665) restores vanilla-resolved sheets in inherited/pinned slots. (d) STALE-SHEET guard: the resolved sheet snapshot (`0x666..0x66c`) must match the current room/area key and live subsets, else the pick passes through vanilla. (e) Palette requirements and OAM footprint are still unmodelled (§3.4 / D4) — playtest watch-items, mitigated by forcing vanilla sheets plus the curated pool.
- [x] **Determinism + install.** Per-(seed, room/area, slot) RNG (`kEnemyShuffleSheetSalt`, distinct from the pick salts) remains in place for future palette-aware widening. Current runtime forces vanilla-resolved sheets, so it is race-deterministic and not visit-order-dependent. Rides the existing `EnemyShuffle_Generate` activation (shadow reset to 0 = "not established" there). No new canonical axis.
- [x] **Validation — current F12 regression pass done.** `EnemyShuffle_SelfCheck` covers the structural invariants, stale-sheet passthrough, overworld-palette gating, and stat bounds — green. Owner playtest reports the caught room/area cases solid; broader OAM/water/feel checks remain residual watch items.

## 8. Audit

- [x] 8.1 Fresh-eyes audit pass after the slice lands (`CLAUDE.md` cadence) — completed 2026-06-13, see `audit.md`. Result: no new code changes required; spec/source comments reconciled. Residual non-blockers are OAM footprint modeling, independent water-room classification, and HP/damage feel under broader play.
