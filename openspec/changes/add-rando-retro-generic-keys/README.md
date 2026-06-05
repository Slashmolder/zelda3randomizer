# add-rando-retro-generic-keys

Follow-up to `add-rando-retro-world-state`. Lands ALTTPR Retro's fourth flag,
`rom.genericKeys` — one shared small-key pool, any key opens any locked door —
which the parent change deliberately deferred (it is the dominant-bug-class
landmine: per-dungeon key *logic* must be rewritten in lockstep or dungeons
soft-lock, with no headless validation).

## Status

**Scaffolded 2026-06-04** (proposal + design + tasks + spec deltas). Not yet
implemented. Prereq: the parent change archived (wildKeys + the
`Settings_EffectiveSmallKeysMode` seam in the baseline).

## Read these in order

| File | Purpose |
|---|---|
| [proposal.md](proposal.md) | Why, what changes (placement + logic + runtime), impact |
| [design.md](design.md) | Grounded upstream model + fork sites + the 3 coupled pieces + risks + acceptance |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | ADDED: Retro generic small-key pool (keys → GenericKey) |
| [specs/randomizer-logic/spec.md](specs/randomizer-logic/spec.md) | ADDED: Generic small-key door reachability (shared-pool logic) |
| [tasks.md](tasks.md) | Implementation checklist (placement / logic / runtime / corpus / playtest) |

## The hard part

The fork models small keys in *logic* per dungeon (`HAS_ITEM(SmallKey_<Dungeon>)`).
Collapsing to one pool is **not** a blind runtime intercept — the reachability
logic must change so the assumed-fill never strands the player behind a door they
can't open. That is the whole reason this is its own change with a playtest gate.

## Key upstream references

- `../alttp_vt_randomizer/app/Location.php:201,268` — `Item\Key` → `KeyGK` under `rom.genericKeys`
- `../alttp_vt_randomizer/app/Filler/RandomAssumed.php:102` — wildKeys placement relaxation
- `../../z3randomizer/inventory.asm` (LoadKeys/SaveKeys) + `$7EF38B` `CurrentGenericKeys` — shared-counter runtime
- Fork key sites: `src/dungeon.c:5238/6586/8003/8075`, `src/rando/rando.c` grant, `src/sprite.c:1408`, `src/sprite_main.c:7394`
