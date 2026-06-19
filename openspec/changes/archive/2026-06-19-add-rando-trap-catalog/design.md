# Design — Randomizer Trap Catalog

All line references are anchors at authoring time; reference by symbol when implementing
(line refs rot). Grounded by four read-only engine research passes + one adversarial
verification pass (all nine load-bearing claims confirmed against source).

## D0. The existing framework (as-built, the thing we extend)

| Piece | Location | Behavior |
|---|---|---|
| Membership gate | `rando_is_trap_item` (`rando.c:426`) | `id==ITEM_TrapDamage \|\| id==ITEM_TrapFreeze`. **The hinge** — decoy masquerade + dispatch both gate on it. |
| Placement injection | `inject_traps_into_junk_placements` (`rando_placement.c:334`) | Replaces eligible junk slots (`trap_replacement_candidate`: rupees/magic/arrows/bombs/rupoor) post-fill. Count = `trap_count_for_frequency` (Low 4 / Med 8 / High 16). Type = positional `(injected & 1)`. Junk-only ⇒ never gates logic. |
| Trigger | `rando_trigger_trap` (`rando.c:590`) | Runs once on collection (via `Rando_DispatchVanillaGrant` → `rando_is_trap_item` → trigger → `return kRandoLttpSkip`, `rando.c:680`). Shows `kRandoTrapDialogueId`, sets effect/timers. |
| Tick | `Rando_TickTrapEffects` (`rando.c:566`) | Every frame from `zelda_rtl.c:267`, **before `Module_MainRouting`**. Effect state is **file-static** (`g_rando_trap_*`, `rando.c:436`). Gated to `main_module_index ∈ {7,9,11}` (`rando_trap_stun_can_tick`). |
| Masquerade | `rando_trap_decoy_icon` (`rando.c:2301`) | Deterministic `(seed, item, location)` → fake good-item icon. Drives both the field sprite and the pickup popup. |

Two parallel id spaces hold identical numbers: `ITEM_*` (codegen'd from
`assets/rando/item_registry.yaml` into gitignored `item_ids.h`) and `ID_*` (hand-mirror enum in
`rando_placement.c`). `TrapDamage=132`, `TrapFreeze=133`.

## D1. Catalog (5 categories, ~15 effects)

Harm model and lift verified per effect. **"Universal"** = safe to apply in any
`{7,9,11}`+submodule-0 context; context-gated effects declare a fallback.

### HAZARD — can damage / spawn threats
| Effect | Hook (verified) | Harm to Link | Notes / guards |
|---|---|---|---|
| **Damage**\* | direct `link_health_current` subtract + recoil shove | yes (1 heart, clamps to 1) | Universal. Existing. |
| **Bomb** | `AncillaAdd_Bomb(7,1)` (`ancilla.c:5952`) | yes — `kBomb_Dmg_ToLink[armor]` (`ancilla.c:1658`) | 2.6 s fuse. **No-ops at `link_item_bombs==0`** (`ancilla.c:5961`) and decrements on use — must restore the **exact original count** unconditionally after the call (the bomb may not spawn if the ancilla table is full ⇒ never steal/grant a real bomb). Universal. |
| **Ambush** | `Sprite_SpawnDynamically` (`sprite.c:4512`), template `ReleaseFairy` (`sprite.c:4574`) | yes — enemy AI contact (`Sprite_CheckDamageToLink`, `sprite.c:2684`) | Spawn 2-4 from a **GFX-sheet-safe + land-safe whitelist** (intersects the enemy-shuffle `kSheetNeed` crash surface — a sprite whose sheet isn't loaded renders garbage). **Exclude boss rooms.** Highest-lift hazard. |
| **Cucco swarm** | replicate `Cucco_SummonAvenger` setup (`sprite_main.c:10024`): spawn id `0x0B`, `sprite_C=1`, `Sprite_ApplySpeedTowardsLink` | yes — contact | **Overworld-only** (vanilla's own `player_is_indoors` early-return, `sprite_main.c:10026`). Fallback indoors → Bomb. Slot-check each spawn. |

### IMPAIR — control/status, no HP loss
| Effect | Hook (verified) | Notes / guards |
|---|---|---|
| **Freeze**\* | `rando_neutralize_trap_motion` (`rando.c:459`) | Universal. Existing. **This zeroes the joypad — it must become a per-effect handler, NOT the unconditional tick body (D4).** |
| **Reversed controls** | stateless low-nibble remap of `joypad1H_last` (0xF0) + `filtered_joypad_H` (0xF4) | Dir bits Up=8/Dn=4/Lf=2/Rt=1 (`zelda_rtl.h:104`). Tick runs before `Module_MainRouting`, so the remap is read this same frame. **No ownership/teardown** — pure per-frame transform; stop on expiry. Universal, zero softlock. |
| **Scrambled/"drunk"** | same bytes; per-frame directional **drop** (with prob.) or a re-rolled 4-dir permutation | Cheapest = input-drop. Same statelessness as Reversed. Universal. |
| **Disarmed** | mask out action edges: `filtered_joypad_H &= ~(B\|Y)`, `joypad1H_last &= ~B` | Movement intact, can't attack briefly. Stateless. **Safest** offensive-feeling trap. Universal. |

### DRAIN — resource loss (all one-shot in onset; no tick state)
| Effect | Hook (verified) | Guard |
|---|---|---|
| **Rupee** | `link_rupees_goal -= chunk` (clamp 0) | Reuses the Rupoor HUD ticker (goal<actual is the safe direction). No softlock. |
| **Magic** | `link_magic_power=0; link_magic_filler=0; Hud_RefreshIcon()` | **Never touch `link_magic_consumption`** (0xF37B = the Half/Quarter upgrade tier — a progress proxy; zeroing it permanently downgrades the upgrade). |
| **Ammo** | zero `link_num_arrows` / **halve** `link_item_bombs` | **Never touch `link_arrow_filler`** (per-frame drain counter, not a tier). Halve (not zero) bombs to avoid a bomb-wall-exit softlock. |

### SCARE — harmless visual/audio
| Effect | Hook (verified) | Guard |
|---|---|---|
| **Screen-shake** | `AncillaAdd_DashTremor` (`ancilla.c:6581`) — already used by Damage trap (`rando.c:544`) | Self-expiring, both worlds. Handler gated `submodule_index==0` (`ancilla.c:3074`) — a transition mid-shake freezes the BG offset until submodule returns to 0 ⇒ teardown must zero `bg1_x/y_offset` (D5). |
| **Darkness** | `dung_num_lit_torches=0` + `Dungeon_ApproachFixedColor_variable(31)` (`dungeon.c:6676`); ramp table `kLitTorchesColorPlus` (`zelda_rtl.c:53`) | **Dungeon-only** (overworld uses a different palette pipeline). Room-reload recomputes lighting. COLDATA is g_ram-backed and *not* re-asserted per frame ⇒ teardown + room-load re-derive required (D5). Fallback outdoors → Screen-shake. |
| **Fake-teleport** | palette/INIDISP flash + mirror sfx, **zero displacement** | The safe teleport stand-in. Restore brightness on expiry. Universal. |
| **Fake low-health** | low-HP beep + `countdown_for_blink` blink, **HP untouched** | Self-clearing. Low novelty (overlaps Damage's blink) — optional. |

### DISPLACE — movement
| Effect | Hook (verified) | Guard |
|---|---|---|
| **Teleport (real)** | `DoWarpStartPoint` machinery (`dbg_warp.cpp`): set start-point branch (`death_var4=1`), `follower_indicator=0`, hand to `main_module_index=6` | Destination = a baked `kStartingPoint` row (Link's House / Sanctuary / Mtn Cave) — the most battle-tested relocation in the game; the loader rebuilds the world (no leak). **Must require `submodule_index==0`** (not the cheat gate `Cheats_CanWarp`, which is off in race mode — use its *safety* subset). **Inverted-world destination fix**: LW start points strand an Inverted player; replicate the death-respawn world-fix (`messaging.c:807`, force `savegame_is_darkworld=0x40`). `dbg_warp.cpp` is `#ifndef`-gated `static` debug code — reimplement in a production TU. **Later slice.** |

**Rejected:** standalone **Quake** / **Bombos** — both damage *enemies only* via the sprite-only
`Medallion_CheckSpriteDamage` (`ancilla.c:501-508`), so as traps they buff the player. They also
force `link_player_handler_state=0` on completion (a mid-swim/fall hazard). The shake *visual* is
provided by `AncillaAdd_DashTremor` instead. A "Knockback Quake" (quake visual + a Freeze-style
stun) is a possible future IMPAIR effect, not a standalone.

## D2. Settings & selector

**Category enable mask.** `RandoSettings.trap_categories` — 5 bits in canonical byte [27]
bits 2-6 (previously reserved; `door_shuffle` owns bits 0-1). Bit order:
HAZARD=2, IMPAIR=3, DRAIN=4, SCARE=5, DISPLACE=6.

**Zero-sentinel.** When `traps>0` and `trap_categories==0`, **all categories are enabled.** This
keeps the default (traps off, mask 0) serializing all-zero ⇒ `kExpectedCanonical[27]`/`[26]`
unchanged, default `settings_hash` unchanged, byte-identical corpus. The native-window UI treats
**all-checkboxes-unchecked as the zero-sentinel "all"** — so "traps on, zero categories" is
simply unreachable (which is correct: "no categories" == "traps off"). No length cascade;
`kSettingsCanonicalLen` stays 28.

**Selector** (replaces positional `(injected & 1)` in `inject_traps_into_junk_placements`):
```
for each chosen trap slot (location_id):
    x = splitmix64(base_seed ^ location_id*K1 ^ DOMAIN_TRAP_SELECT)   // domain ≠ decoy mix
    cat = enabled_categories[ x % n_enabled ]                          // n_enabled from mask (0 ⇒ all)
    eff = category_effects[cat][ (x>>32) % count(cat) ]                // uniform within category
    placement_at[slot] = ID_for(eff)
```
Determinism: the selector is the **last consumer** of the placer RNG stream and draws from the
seed (not `RandoRng`), so it cannot perturb non-trap placement. `traps==0` short-circuits before
any selection (`rando_placement.c:339`) ⇒ traps-off seeds byte-identical. Uniform-within-category
for v1 (per-effect *weights* would need their own canonical bytes ⇒ length cascade ⇒ deferred).

**Contiguous trap id block.** Allocate the new effect ids contiguously after the existing
132/133. `rando_is_trap_item` becomes `id >= ITEM_TrapFIRST && id <= ITEM_TrapLAST` so every id in
the block auto-lights-up the decoy masquerade and the dispatch (resolves review finding B-1: the
masquerade is NOT free per-id unless the membership test covers the id). `_Static_assert(ID_TrapX
== ITEM_TrapX)` per id guards the hand-mirror enum.

## D3. Runtime dispatch — arm-in-trigger / apply-in-gated-tick

The trigger fires from **many** grant paths (chest, tablet, boss-prize, minigame, Flute spot,
cage, …) in **any** module/submodule. So `rando_trigger_trap` must NOT itself perform any
spawn/warp/PPU/state-mutating work — it only **arms**:

```
rando_trigger_trap(item_id):
    effect = effect_for_id(item_id)
    Sprite_ShowMessageUnconditional(reveal_id_for(effect))   // jumps to module 14
    g_rando_trap_effect      = effect
    g_rando_trap_stun_timer  = duration[effect]
    g_rando_trap_onset_pending = 1
    g_rando_trap_reveal_sfx  = sfx[effect]
```

`Rando_TickTrapEffects` applies, table-driven, after the dialogue dismisses and under the gate:

```
if stun_timer == 0: return
if (module 14 && showing trap dialogue): return            // existing: let the reveal play
if !(main_module ∈ {7,9,11}): return
if effect needs world-mutation (spawn/warp/PPU) && submodule_index != 0: return   // DEFER, don't drop
if onset_pending:
    if !context_ok(effect):   effect = fallback[effect]     // e.g. Darkness outdoors → Shake
    onset[effect]()                                          // subtract HP / drain / spawn / warp / darken
    onset_pending = 0
sustain[effect]()                                            // neutralize (freeze) / shove (damage) / remap (reversed) / re-assert dark
if (--stun_timer == 0): teardown[effect]()                  // restore PPU regs, clear forced state
```

This single change resolves three review findings: per-effect `sustain` means Freeze's
`rando_neutralize_trap_motion` is no longer the unconditional tick body, so Reversed/Disarmed get
**live** input instead of a zeroed one (B-3); the `submodule_index==0` gate + deferral makes
sprite-spawn / warp / PPU effects safe despite the trigger firing anywhere (B-4); and evaluating
`context_ok` + `fallback` **in the gated tick** (not at collection) means context is judged when
the effect actually runs (B-6). Traps are non-stacking by construction (single
`g_rando_trap_effect`) — a second pickup overwrites the first, which is safe.

## D4. Leak-safe teardown for PPU effects (review B-2)

Trap state is file-static (never serialized), so a save / Ctrl+F1 snapshot always reloads with
`stun_timer==0` — the *timer* never strands Link frozen. **But Darkness writes COLDATA and
Screen-shake writes `bg1_x/y_offset`, and those registers ARE in the saved g_ram image.** A save
captured mid-effect would reload a permanently dark room / frozen camera (until the next
transition). Therefore:

- Every PPU-writing effect snapshots the pre-effect register value at onset and **restores it at
  teardown** (not relying on the next room transition).
- A **rando-active dungeon room/area load re-derives** these registers from room state
  (`overworld_fixed_color_plusminus = kLitTorchesColorPlus[dung_want_lights_out ?
  dung_num_lit_torches : 3]`; `bg1_x/y_offset = 0`) so a save taken mid-effect normalizes on load.

Net rule: **no g_ram-backed PPU write may outlive the file-static timer.**

## D5. Determinism / corpus / version

- Bump `kGeneratorVersion` (currently **78**) → next.
- The three traps-on corpus seeds — `traps-low/medium/high-open-fast-ganon`
  (`tests/rando_corpus/manifest.yaml`, seeds `0x68/0x69/0x6A`) — get new `placement_digest`
  (the type at each trap slot changes from the 132/133 alternation to a `(seed,location)`-chosen
  spread). Their `sphere_digest` is unchanged (junk-only). **Every traps-off seed is
  byte-identical** (`traps==0` short-circuit + zero canonical mask).
- The placement self-check that today asserts a Damage/Freeze **balance** (`rando_placement.c`
  ~2517) must change to assert the total trap *count* only (the new selector emits >2 ids and no
  even split).
- Regen: build fresh (WSL `rm src/rando/logic_data.c && make -j zelda3` is fine — digests are
  cross-platform deterministic), `bump_rando_corpus.py --apply` (absolute `--binary`), 3-way diff
  vs `main` built fresh to confirm **only those three digests moved**. `make clean` after the
  `rando.h` edit; restore the manifest CRLF before commit.

## D6. Presentation

The reveal keeps the delayed bad-sfx fakeout (`g_rando_trap_bad_sfx_timer`). Per-effect (or
per-category) reveal sfx via `g_rando_trap_reveal_sfx` read at the delayed-fire site. Per-category
flavor text via reserved dialogue ids (`0x0221+`) switched in `Rando_RenderTrapMessage`; the
single generic "You are a fool!" is an acceptable v1 default. Optional polish (C-1): flash the
decoy graphic for a beat before the bad cue so the fakeout reads.

## D7. Phasing (each slice independently playtestable)

1. **Scaffolding** — `trap_categories` settings plumbing (field + canonical pack/unpack +
   defaults + UI), the deterministic selector, the contiguous id block + range-check
   `rando_is_trap_item`, and the table-driven arm/onset/sustain/teardown dispatch — re-bucketing
   the existing Damage/Freeze with no new effects. Corpus regen (3 movers). This is the
   foundation everything rides on; verify before adding effects.
2. **Cheap pure-trap effects** — DRAIN (rupee/magic/ammo), Reversed controls, Disarmed,
   Screen-shake, Fake-teleport, Fake low-health. All stateless or one-shot, low softlock surface,
   biggest catalog expansion per risk.
3. **Spawn / world effects** — Bomb (count-restore guard), Darkness (dungeon-only + teardown +
   fallback), Cucco (overworld-only + slot-check + fallback), Ambush (sheet-safe whitelist +
   boss-room exclusion + transition guard). Each needs its own playtest cycle.
4. **Displace + variants** — real Teleport (safe-start + inverted destination fix), Scrambled
   variants, and stretch ideas (below).

## D8. Stretch / future (not v1)

- **Mimic trap** — a trap whose decoy is a *fixed* high-value item (skip the decoy pool) to bait
  the player. Rides on `rando_trap_decoy_icon`; junk-only guarantee preserved.
- **Anti-trap charm ("Fool's Bell")** — a real *pool* item that disarms the next trap. First
  trap-adjacent item that is NOT junk-only ⇒ touches `BuildItemPool` + logic + a reserved
  counter; gate to appear only when `traps>0`.
- **Progress-scaling severity** — `rando_trigger_trap` reads `link_has_crystals` to lengthen the
  effect late-game. Runtime-only, placement-digest-stable; document the spoiler divergence.
- **Per-effect weights** — needs its own canonical bytes ⇒ a `kSettingsCanonicalLen` length-bump
  cascade; deferred until a concrete need appears.
- **Soft teleport** (within-room reposition) — a lower-risk DISPLACE entry that skips the
  start-point machinery entirely.

## D9. Open decisions resolved (for the record)

1. Per-category enable mask (not per-effect weights) — fits 9 free bits, no cascade. **Resolved.**
2. `kSettingsCanonicalLen` stays 28. **Resolved.**
3. Zero-mask = all categories; UI all-unchecked = sentinel. **Resolved.**
4. Selector keyed on `(seed, location_id)`, domain-separated. **Resolved.**
5. Real teleport ships in slice 4 with the inverted fix; Fake-teleport covers the "teleport"
   feel in slice 2. **Resolved.**
6. Quake/Bombos rejected as standalone. **Resolved.**

## D10. Bottleneck

The **owner playtest loop** — slices 3-4 (spawn/world/displace) carry the softlock surface and
are invisible to the corpus + `--rando-selftest` (generation-only); each needs a cross-context
playtest (both worlds, indoors/outdoors, boss rooms, a save/snapshot mid-effect). Slices 1-2 are
low-risk and largely verifiable by corpus + selftest. Corpus regen is mechanical (3 expected
movers).
