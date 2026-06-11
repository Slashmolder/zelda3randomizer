# The 10 un-restored ALTTPR glitch techniques — US-1.0 performability writeup

Context: `add-rando-major-glitch` D6 established that of ALTTPR's 12 glitch
techniques (`config/logic.php`), the JP-glitch restoration makes exactly 2
performable — `canFakeFlipper`, `canSuperSpeed`. The other 10 are listed here.
Goal: classify each technique's true US-1.0 status (`rom_version_status`) with
ROM-disassembly + fork-source evidence, instead of the current bare
`untested-on-us10`.

## Top-level finding (JP-vs-US sweep, `_jpglitch_spike/romdiff.py`)

Swept all 925 routines in `player_addrs.txt` (201) + `dungeon_addrs.txt` (363) +
`overworld_addrs.txt` (108 located). Of those, the ONLY glitch-relevant JP↔US
**code** deltas are the ones already restored by `add-jp-glitch-restoration`:
- `PlayerHandler_00_Ground_3` ($8781A0) — the StartDash guard
  (Itemdash/Spindash/Superspeed). RESTORED.
- `PlayerHandler_04_Swimming` ($87963B) — the per-frame flipper recheck
  (Fake Flippers). RESTORED.
- (+ `LinkState_Pits` Death Hole and `DoSwordInteractionWithTiles_Mirror` Mirror
  Block Erase, found via targeted `careful_diff` in the JP spike. RESTORED.)

Every other delta in the sweep is either a **data-pointer relocation artifact**
(JP data tables live in different banks → immediate operands differ, e.g.
`#$7A`→`#$78` bank byte, `#$F1CD`→`#$EEAD` pointer) or a **disassembly
mis-alignment** (large `shift-NNN`, the differ compared unrelated code). None is a
behavioral patch.

**Implication:** none of the 10 un-restored techniques is JP-1.0-EXCLUSIVE — they
are **cross-version** mechanics present in BOTH ROMs. So on the original US 1.0
ROM they are performable. The open question for THIS fork is whether the C
reimplementation reproduces each mechanic's enabling quirk (collision resolution,
frame-race ordering, camera/transition math). That is a fork-source + playtest
question, NOT a ROM-patch question. **So the honest classification is
`untested-on-us10` (NOT `jp10-only`) — but now grounded: "cross-version ROM,
fork-reimpl-fidelity unverified," not "unknown."**

---

## The 10 (tier, mechanic, ALTTPR logic role, fork anchor, status hypothesis)

### OWG-group (5)

1. **canSuperBunny** (OWG; logic.php:6,15,25). In the Dark World without the Moon
   Pearl, Link is a bunny; "super bunny" routes keep acting through the bunny
   state to traverse DW screens/caves (e.g. Superbunny Cave, GT-via-DW-DM).
   Fork: `Link_HandleBunnyTransformation`/`LinkState_Bunny`/`PlayerHandler_17_Bunny`
   (`src/player.c`, ASM $878xxx). Sweep: bunny routines IDENTICAL JP↔US.
   Hypothesis: cross-version; fork-fidelity unverified (bunny-state action gating).

2. **canBootsClip** (OWG; :9,18,28). Pegasus-Boots dash into a diagonal
   wall/corner clips Link through one tile. Fork: dash collision in `src/player.c`
   (`LinkState_Dashing` $878F86) + `src/tile_detect.c`. Sweep: dash routines
   IDENTICAL (only the StartDash *guard* differs, unrelated to the clip).
   Hypothesis: cross-version; fork-fidelity unverified (corner collision is the
   most reimpl-sensitive — C `tile_detect` may resolve sub-pixel corners
   differently than the ASM).

3. **canMirrorClip** (OWG; :10,19,29). Use the Magic Mirror against terrain to
   clip into an adjacent area (distinct from the dungeon Mirror Block Erase).
   Fork: mirror dispatch in `src/player.c` (`LinkItem_Mirror` $87A91A) + overworld
   warp. Sweep: mirror routines IDENTICAL except the already-restored Mirror Block
   Erase delta. Hypothesis: cross-version; fork-fidelity unverified.

4. **canWaterWalk** (OWG; :11,20,30). A boots/position setup that lets Link walk
   onto water tiles without swimming. Fork: swim-entry/collision
   (`CheckAbilityToSwim` $81FFB6, `Link_SetToDeepWater` $878C44) + `tile_detect`.
   Sweep: swim-ENTRY routines IDENTICAL (only the swim HANDLER recheck differs =
   Fake Flippers). Hypothesis: cross-version; fork-fidelity unverified.

5. **canDungeonRevive** (OWG; :12,21,32). Bunny-revival used inside a dungeon to
   traverse (bottle-fairy / death-revive while routing). Fork: revival +
   `LinkState_Pits` ($8792D3, the Death-Hole-adjacent path) + bottle-fairy grant.
   Sweep: only the already-restored Death Hole pit delta. Hypothesis:
   cross-version; fork-fidelity unverified (interacts with the restored Death Hole
   gate — worth checking they compose).

### HMG+MG (1)

6. **canOneFrameClipUW** (HMG+MG; :22,35). A 1-frame-precise clip through an
   underworld (dungeon) wall during a specific sub-state. Fork: dungeon collision /
   intra-room movement (`src/dungeon.c`, `tile_detect.c`). Sweep: dungeon routines
   IDENTICAL (the deltas are data-pointer relocs). Hypothesis: cross-version;
   fork-fidelity HIGHLY uncertain (1-frame timing in fixed C control flow may not
   reproduce — the same class the JP spike flagged for input-race glitches).

### MG-exclusive (3) + canOWYBA

7. **canOneFrameClipOW** (MG; :34). The overworld 1-frame clip (the most common MG
   gate — now `CanOneFrameClipOW()`). Fork: overworld collision/movement. Same
   1-frame-timing concern as #6. Hypothesis: cross-version ROM; fork-fidelity
   uncertain.

8. **canMirrorWrap** (MG; :31). Mirror to wrap the camera/screen past its bound to
   reach OOB destinations. Fork: `MirrorWarp_FinalizeAndLoadDestination` ($82B260)
   + camera. Sweep: the routine shows a delta but `shift-218` = mis-alignment;
   needs a careful_diff to confirm. Hypothesis: likely cross-version; CONFIRM the
   mirror-warp routine is not actually patched.

9. **canTransitionWrapped** (MG; :36). A screen-transition that wraps to an
   unintended room/screen. Fork: `Dungeon_StartInterRoomTrans_*` /
   `Overworld_FinalizeEntryOntoScreen`. Sweep: deltas are mis-aligned/data.
   Hypothesis: cross-version ROM; fork-fidelity uncertain.

10. **canOWYBA** (MG; :33; already the `CanPearlBypass`/`pearl-bypass` macro).
    Overworld "YBA"-style bottle traversal — hold a bottle to keep acting in DW
    bunny state to bypass the Moon Pearl. Fork: bunny + bottle (`src/player.c`
    bunny state + `HasABottle`). Sweep: bunny IDENTICAL. Hypothesis: cross-version;
    fork-fidelity unverified.

## Classification framework (op_registry `rom_version_status`)

- `cross-version` — enabling ASM identical JP↔US AND the fork C reimpl
  demonstrably reproduces the quirk → does not warn. (Strongest bar; needs both.)
- `untested-on-us10` — cross-version ROM but fork-reimpl fidelity / frame-feasibility
  unverified (the expected landing spot for most of the 10, now grounded).
- `jp10-only` — US patched it out (sweep evidence says NONE of the 10 are this).
- `us10-different` — exists both ROMs but with a timing/mechanics delta.

**Headless can establish the ROM-delta prong rigorously and assess C-reimpl
plausibility; final "a human can perform it on this build" is playtest.**

---

## CONFIRMED verdicts (research agent, romdiff `careful_diff`/`align_diff` + fork source)

Differ re-validated against the known StartDash delta. Primary routines located
via forced-shift + distinctive-byte search.

| # | Technique | ROM JP-vs-US | Fork C reimpl | Verdict |
|---|---|---|---|---|
| 1 | canSuperBunny | identical (bunny routines, -6 reloc) | faithful bunny state machine | `untested-on-us10` |
| 2 | canBootsClip | identical (`LinkState_Dashing`, collision) | `tile_detect.c` sub-tile corner math bit-for-bit | `untested-on-us10` |
| 3 | canMirrorClip | identical (only the restored Block-Erase delta) | faithful | `untested-on-us10` |
| 4 | canWaterWalk | identical (`CheckAbilityToSwim`, `Link_SetToDeepWater`) | deep-water gate ported exactly | `untested-on-us10` |
| 5 | canDungeonRevive | identical (only the restored Death-Hole delta) | faithful; composes w/ Death Hole | `untested-on-us10` |
| 6 | canOneFrameClipUW | identical (`TileDetect_MainHandler`, dungeon collision) | velocity→pos→collision order preserved | `untested-on-us10` |
| 7 | canOneFrameClipOW | identical (OW collision) | same deterministic per-frame seq | `untested-on-us10` |
| 8 | canMirrorWrap | identical wrap/dest/position/camera. The $82B260 divergence is MUSIC-ONLY ($7F5B00=overworld_music[] WRAM; writes differ at $12C/$12D) — NOT a dest table (corrects an earlier us10-different misread) | faithful (US==JP for the wrap math) | `untested-on-us10` |
| 9 | canTransitionWrapped | identical position/screen-index/camera. The $82C242 divergence (US `LDA $7F5B00,x` vs JP `$130==#$F1`) is destination-screen MUSIC selection ONLY | faithful (US==JP for the wrap math) | `untested-on-us10` |
| 10 | canOWYBA | identical (bunny + bottle) | faithful | `untested-on-us10` |

**Bottom line:** NONE of the 10 is `jp10-only` (no behavioral US patch removes
any of them). **ALL 10 are cross-version ROM** with the fork faithfully
reproducing the substrate, but frame-precise feasibility (esp. the 1-frame clips
#6/#7) and human-performability on this build remain **playtest-only** → honest
`untested-on-us10`, no upgrade to `cross-version` (that bar needs a positive
playtest).

**#8/#9 correction (deeper romdiff, 2026-06-09):** an earlier pass flagged
canMirrorWrap/canTransitionWrapped as `us10-different` on the theory that the
`$7F5B00,x` lookup was a "special-area DESTINATION table." A line-by-line
disassembly of both routines disproved that: the warp/transition DESTINATION,
Link position (`link_x/y_coord`), screen index (`$8A`), and camera/scroll math
are **byte-identical JP↔US**. The ONLY JP↔US delta is the destination screen's
overworld **MUSIC + ambient-sound** selection — `$7F5B00` is `overworld_music[]`
(a WRAM scratch buffer, not a dest table); the differing writes are `$12C`
music_control / `$12D` sound_effect_ambient (fork C: the `music_unk1 == 0xf1`
gate in `Overworld_FinalizeEntryOntoScreen`, the `overworld_music[...]` read in
`MirrorWarp_FinalizeAndLoadDestination`). So **the wrap GAMEPLAY is identical on
both ROMs** — these two are cross-version like the rest, NOT us10-different.

**Op-registry impact:** rom_version_status values do NOT change (all 10 were
already `untested-on-us10`). The GROUNDING win: "cross-version ROM,
fork-fidelity/playtest-pending" replaces bare "untested" for all 10; the prior
us10-different mislabel for #8/#9 is corrected (no JP-vs-US wrap difference
exists; the sole delta is cosmetic post-warp/transition music).
