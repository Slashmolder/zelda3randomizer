## Why

This project is a reimplementation of the **US 1.0** ALTTP ROM (`README.md` — required ROM is `zelda3.sfc`, SHA256 `66871d66…`, US region). Several speed-running glitches that exist in the **Japanese (JP) 1.0** ROM were patched by Nintendo before the US release, so they are absent from this port. A segment of the community (glitch runners, practice-ROM users) wants them back. The ask: **one settings checkbox that re-introduces the JP-1.0-exclusive glitches.**

There is **no runtime version/region switch anywhere in `src/` today** (grep for `JP`/`japan`/`version`/`region`/`rev`/`NTSC` finds only unrelated rando `region_id` grouping and the emulator's PAL/NTSC *timing* in `snes/`). This change introduces the project's first JP-vs-US behavior toggle.

### The catalog (the "find what's patched" half of the ask)

The ALTTP community's canonical list of JP-1.0-exclusive *minor* glitches is **six**. The **JP-vs-US 65816 ROM diff (disassembly spike, 2026-06-08)** ground-truthed each: **3 are IMPLEMENTABLE** (US 1.0 added an exact instruction that gating restores) and **3 are NEEDS-MORE-WORK** (no JP↔US code delta in the anchored routines — state-leak / input-race, likely not reproducible in the fixed C control flow). Source: the ROM diff (both ROMs verified, headerless 1 MB LoROM) + <https://alttp-wiki.net/index.php/Version_Differences>.

| Glitch | Effect | JP↔US delta (ROM diff) | zelda3 anchor | Verdict |
|---|---|---|---|---|
| **Fake Flippers** | Swim with no flippers (via ledge/recoil entry only) | US adds a per-frame flipper recheck in the swim *handler*; JP omits it. The entry ejects (`CheckAbilityToSwim`, `LinkState_CrossingWorlds`) are byte-identical JP↔US — NOT gated. **No grace counter exists.** | `PlayerHandler_04_Swimming` `src/player.c:1721` (single site) | **IMPLEMENTED (faithful)** |
| **Death Hole** | Die + fall in a pit same frame → phantom transition + fairy-revive | US adds `link_disable_sprite_damage++` per pit-fall frame; JP omits it | `LinkState_Pits` `src/player.c:1576` | **IMPLEMENTED** (faithful; full-warp playtest-pending) |
| **Itemdash** | Y+A same frame to use an item while dashing | US adds a StartDash guard skipping `Link_HandleYItem` on the dash frame; JP calls it unconditionally | `PlayerHandler_00_Ground_3` `src/player.c:249` | **IMPLEMENTED** (faithful; payloads playtest-pending) |
| **Superspeed** | 4 px/frame after dash + ladder/stairs touch | **No code delta** — all `link_speed_setting` resets byte-identical | `LinkState_Dashing` + reset sites | **NEEDS-MORE-WORK** (state-leak) |
| **Spindash** | Charge-release + A same frame arms a glitched state | **No code delta** — spin routines byte-identical | `LinkState_SpinAttack` / `Link_ActivateSpinAttack` | **NEEDS-MORE-WORK** (input-race) |
| **Mirror Block Erase** | Mirror mid block-push deletes the block | **No code delta** — block + mirror routines byte-identical | `Dungeon_PushBlock_Handler` / `LinkItem_Mirror` | **NEEDS-MORE-WORK** (state-interaction) |

**Honest framing for the five UNVERIFIED rows**: the wiki authors themselves did not document the US patch mechanism for these. Worse, several are *input-arbitration* glitches that depend on how the 65816 frame loop raced two button reads — the C reimplementation sequences those reads in fixed C control flow and may not reproduce the glitch at all without deliberate reconstruction. **Each of the five requires a JP-vs-US 65816 disassembly-diff spike before it can be claimed feasible.** This proposal does not assert their causes; it schedules the spike.

**The Fake Flippers fix is a single-site gate** (ROM-confirmed by the spike). The swim-ENTRY ejects — `CheckAbilityToSwim` (`:129-136`) and the `LinkState_CrossingWorlds` deep-water guard (`:3192-3213`) — are **byte-identical JP↔US**: a flipperless Link who walks onto deep water is ejected in *both* versions, so they are NOT gated. The *only* JP↔US difference is the per-frame flipper recheck US added to `PlayerHandler_04_Swimming` (`:1721`); JP omits it. Gating exactly that one site makes the C handler byte-equivalent to JP. The glitch is then reached only via the un-flipper-checked ledge/recoil entry sites (`:710`, `:918`). There is **no 8-frame grace counter** in the ROM — that was a runner-technique description, not JP code. A "free-swim" bypass that *also* gates the entry sites (so Link can walk straight into water) is the **rejected** always-swim bug, not the glitch.

## What Changes

- **New feature flag**: `kFeatures0_RestoreJpGlitches` (bit 18 = `262144`) in `src/features.h`. Bits ≤ `kFeatures0_EasyMinigames` (bit 17) are taken; bit 18 is the next free bit in the `Features0` uint32 (no new `kRam_*` byte needed). **Default off.**
- **One master checkbox** "Restore JP 1.0 glitches" in the native game-config window's gameplay-feature panel (`src/rando/rando_window/game_config_panels.cpp`, the `FeatureCheckbox(label, mask, help)` helper). Persisted as a **new named boolean key in `[Features]`** — `src/config.c` parses each `features0` bit from its own key via `ParseBoolBit` and round-trips it through the aligned `kFeatKeys[]`/`kFeatMasks[]` tables (`config.c:615-646,1193-1208`); there is **no** `[Features] features0` integer mask. Read at point-of-use via `enhanced_features0 & kFeatures0_RestoreJpGlitches`; live-applies per the existing `features0` path.
- **MVP behavior — Fake Flippers** (IMPLEMENTED, faithful): gate the single per-frame recheck at `PlayerHandler_04_Swimming` (`src/player.c:1721`) under the flag; leave the byte-identical entry ejects untouched. Flipperless swim then works only via the ledge/recoil entry, exactly as JP 1.0 — walking into water still ejects. (Also IMPLEMENTED faithful from the spike's exact deltas: **Death Hole** — gate the US-added `link_disable_sprite_damage++` per pit-fall frame; **Itemdash** — relax the US-added StartDash guard so `Link_HandleYItem` fires on the dash frame.)
- **Discovery spike** (first task): the JP-vs-US disassembly diff for the other five glitches, recording per-glitch the exact US instruction(s) changed and whether the reimplementation even reproduces the relevant code path. Each glitch that survives the spike is implemented behind the **same** master flag in a follow-on; the master toggle's contract is "enable every JP-1.0 glitch this build has verified-and-implemented."
- **RAM-compare safety**: every restored glitch diverges from the original US ROM. Primary safety is **default-off** — the same discipline `kFeatures0_EasyMinigames` relies on (its point-of-use gates at `dungeon.c`/`player.c` are `RandomizerActive || EasyMinigames` with *no* emulator check; side-by-side cleanliness comes purely from the flag being off in vanilla). Because these glitches diverge *every frame* (far more than EasyMinigames), each gate ALSO checks `!ZeldaIsEmulatorAttached()` (`src/zelda_rtl.c:469`) — a **new, stronger** suppression than EasyMinigames uses, owned as new here, not a copy of an existing pattern.

## Capabilities

### New Capabilities

- `jp-glitch-restoration`: the master-flag contract, the per-glitch verification gate (no glitch ships until its JP behavior is grounded in a disassembly diff or, for Fake Flippers, the documented per-frame check), the default-off + side-by-side-off RAM-compare invariant, and the Fake Flippers MVP scenario.

### Modified Capabilities

- `game-config-ui`: ADDED Requirement for the "Restore JP 1.0 glitches" checkbox in the gameplay-feature panel — live-applies as a `features0` bit, persists to the INI, and is excluded on Switch with the rest of the PC panels.

## Impact

- **Code**: `src/features.h` (new bit), `src/config.c` (named-key parse + `kFeatKeys`/`kFeatMasks` entries), `src/rando/rando_window/game_config_panels.cpp` (checkbox), `src/player.c` (Fake Flippers gate set: `:3193` walk-in guard, `CheckAbilityToSwim` `:129-136`, handler eject `:1721`, ledge/jump-in `:710`/`:918`). Follow-on glitches touch `player.c` / `dungeon.c` per the catalog anchors.
- **No rando impact**: this is a general game feature, not a randomizer axis. It does NOT touch `RandoSettings`, the canonical serialization, `kGeneratorVersion`, or the corpus (per `game-config-ui / Determinism and randomizer isolation`). Related-but-separate: `add-rando-trick-logic-and-axes` / `add-rando-major-glitch` make the *placer assume* glitches; this flag makes glitches *executable*. Auto-coupling them (a glitched-logic seed auto-enabling this flag) is a **non-goal** here. **But there is a real hazard worth flagging**: the rando trick bitmask already includes a `fake-flippers` *placement* trick (`randomizer-core / Settings canonical serialization`, field `tricks`; `randomizer-logic / OP_TRICK`). A seed that enables that trick while this runtime flag is OFF lets the placer assume a swim the runtime can't perform → unreachable item / soft-softlock. The spec marks `fake-flippers` trick + flag-off as an **unsupported combination** (see the spec delta).
- **Effort**: Fake Flippers is a small, self-contained slice. The disassembly spike + the five follow-on glitches are the bulk and are individually scoped (and some may resolve to INFEASIBLE/N-A — that is an acceptable, honest outcome).
- **Regression risk**: default-off ⇒ side-by-side RAM compare stays clean; vanilla and rando play are byte-identical unless the player opts in.

## Status

**Implementation in progress** on branch `claude/jp-glitch-restoration`. Authored 2026-06-07; the JP-vs-US disassembly spike (2026-06-08) ground-truthed all six glitches against the ROMs. **3 IMPLEMENTED faithful** — Fake Flippers, Death Hole, Itemdash (each an exact gated JP↔US delta; WSL `-Werror` build green; owner playtest-pending). **3 NEEDS-MORE-WORK** — Superspeed / Spindash / Mirror Block Erase (no JP↔US code delta; not shipped). The Fake Flippers free-swim approximation was rejected on owner playtest and replaced with the faithful single-site gate. See [design.md](design.md) Post-implementation reconciliation + per-glitch mapping, and [README.md](README.md).
