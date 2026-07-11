# Proposal: dungeon-chains

## Why

The randomizer's existing structural shuffles (entrance, door, boss) all preserve the vanilla
invariant "one overworld door = one dungeon = one boss." Dungeon chains breaks that invariant
for a fundamentally new mode: each overworld dungeon entrance leads to a *chain* of zero or
more dungeons that terminates in a boss fight, so a door may open directly onto a boss room,
or onto a gauntlet of several full dungeons back-to-back. The pool is fixed — every pool
dungeon and every pool boss appears exactly once across all chains — so the degenerate
extremes are legal: eight doors that are each a bare boss fight, plus one door that chains
all nine pool dungeons in a shuffled order.

Two properties make this buildable now rather than later. First, bosses are always fought in
their **vanilla home rooms** — chains redirect *access to* rooms, never the rooms themselves —
which sidesteps the environment-boss problem (Blind's maiden trigger, Kholdstare's freeze
header, Trinexx's shell) that forced boss shuffle to pin three bosses. Second, the three
mature seams the feature composes from already exist and are playtest-proven: the
entrance-style dungeon load + coupled-exit machinery (entrance shuffle), the per-seed logic
edge/region override layer (entrance shuffle stages 2-4), and the boss beatability predicate
`OP_CAN_KILL_BOSS` with per-seed dungeon→boss assignment (boss shuffle).

## What Changes

- **New shuffle axis `dungeon_chains`** (default off, off = byte-identical to vanilla
  generation and runtime). When on, the generator partitions the chain-pool dungeons into
  ordered chains — one per participating overworld dungeon door — and assigns each chain a
  terminal boss, seeded-deterministically (xoshiro, pure in seed + attempt).
- **Chain pool (MVP)**: 9 prize dungeons (Eastern, Desert, Hera, PoD, Swamp, Thieves' Town,
  Ice, Mire, Turtle Rock) and their 9 vanilla bosses. Hyrule Castle (no boss, Standard-mode
  opening), Castle Tower/Agahnim (world-state trigger), Ganon's Tower/Agahnim 2 (crystal
  gate + Ganon flow), and Ganon stay vanilla. **Skull Woods is excluded** (with Mothula
  staying vanilla) for the same reasons entrance shuffle excludes it: many separate
  overworld entrances including drop-in holes that the door overlay cannot redirect, and a
  boss section behind its own overworld door. Two adjacencies are pinned: Blind terminates
  the chain segment ending with Thieves' Town (the maiden-escort trigger is TT-internal)
  and Moldorm terminates the segment ending with Tower of Hera (his room's fall-out holes
  land in ToH's interior); those two boss seams stay vanilla, the other 7 terminals shuffle
  freely. Castle Tower (Agahnim) and Ganon's Tower (Agahnim 2) are a **planned phase-2
  extension** (owner decision; entanglements catalogued in design.md), not v1.
- **Two runtime redirect seams**, both implemented as full entrance-style loads (not door
  transitions), so palettes/GFX/music/palace-index/key-HUD state stay correct per hop:
  1. Overworld dungeon door → first chain element (dungeon lobby, or boss room via a
     fork-added synthetic entrance record).
  2. The transition into a dungeon's vanilla boss room → that dungeon's chain successor.
- **Main-door exits return to the chain's origin door**: walking out a chained dungeon's
  main door and the post-boss pendant/crystal warp-out resolve to the overworld door the
  chain was entered from (reusing and extending the coupled-exit machinery); death +
  continue respawns at the current chain element. This is a correctness requirement, not
  polish — the vanilla post-boss warp can strand a player (e.g. dumped on the Misery Mire
  pad without flute or mirror). Exits through auxiliary doors (Desert Palace's ledge side
  doors, Turtle Rock's balconies) stay vanilla — they are re-enterable pockets, and
  origin-coupling them would sever vanilla-required routes (DP's back-door boss approach,
  TR's balcony route to Mimic Cave).
- **Placement logic models chains**: reaching chain hop *i+1* requires reaching hop *i*'s
  boss *door* (path + small/big key requirements, without the boss-kill predicate); a
  terminal boss's kill / prize / heart-container locations require reaching the chain end
  plus `CanKillBoss` for the assigned boss. Prizes attach to the boss location as today;
  prize shuffle composes unchanged.
- **Mutual exclusion (MVP)** with entrance shuffle, door shuffle, and boss shuffle (settings
  normalization coerces them off, same pattern as door↔entrance exclusion today). World
  states: Open and Standard only; Inverted and Retro deferred.
- **Full supporting surface**: settings canonical bit + UI toggle (native settings window),
  spoiler chain section, sidecar-slot persistence with regenerate-at-load, corpus entries,
  self-checks, `kGeneratorVersion` bump.

## Capabilities

### New Capabilities

- `randomizer-dungeon-chains`: the chain model (pool, partition, terminal-boss assignment,
  pins), both runtime redirect seams, synthetic boss-room entrance records, exit/death/warp
  coupling to the origin door, chain-aware reachability logic and the full-reachability
  generation gate, persistence/regeneration, and spoiler/self-check requirements.

### Modified Capabilities

- `randomizer-shuffles`: adds the `dungeon_chains` axis to the shuffle-axis surface and its
  mutual-exclusion/normalization rules against entrance shuffle, door shuffle, and boss
  shuffle (whichever is enabled first wins; chains defaults off).

## Impact

- **New code**: `src/rando/shuffle_chains.{c,h}` (permutation construction, seam tables,
  runtime redirect install/teardown), synthetic entrance records for the 9 pool boss rooms
  (generated offline from room/entrance assets, committed like the door tables).
- **Modified code**: `src/dungeon.c` (boss-seam transition hook, entrance-load path),
  `src/overworld.c` (exit coupling already seamed — extension only), `src/rando/rando_logic.c`
  (chain edge overrides), `src/rando/rando_generate.c` + `src/main.c` (generation phase in
  BOTH duplicated seams), `src/rando/rando_settings.{c,h}` (axis + normalization),
  `src/rando/rando_save.c` (header byte), spoiler writer, `rando_window` UI panel,
  `zelda3.vcxproj` (new sources).
- **Determinism surface**: `kGeneratorVersion` bump + corpus regen with new chains entries;
  `--rando-selftest` additions (chain partition validity, seam-table cross-check).
- **No asset-format change to existing tables** if synthetic entrance records ship as a new
  fork asset (precedent: custom item art); the alternative (extending the vanilla entrance
  tables in place) is a design.md decision.
- **Not covered by the corpus**: both runtime seams are gameplay-side — end-to-end playtest
  is the only reliable net for the redirect/exit/warp behaviors (project rule).
