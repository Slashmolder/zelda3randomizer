# Key Rings and Skeleton Key - design

This design is grounded in the as-built code at `main` (`207269b0`): the small-key
pool in `rando_placement.c`, the predicate VM and door oracle in `rando_logic.c`,
the per-dungeon grant path in `rando.c`, the sole small-key-door consumption site
in `dungeon.c`, `dungeon_ids.h`, the current 30-byte canonical settings layout,
sidecar format 8, and the archived pot/enemy/door/chains contracts.

## Context and grounded facts

- Small-key registry IDs `53..65` are 13 contiguous dungeon families in
  randomizer order. The base shuffled pool has **29** copies; Eastern Palace's
  base count is zero.
- Pot and forced-enemy key locations contribute additional copies only when their
  check tier is active. Non-itemized key drops remain vanilla/free and are seeded
  into the Dungeon-key logic model where required.
- Runtime uses `link_keys_earned_per_dungeon[]` as the persisted per-dungeon
  counters and `link_num_keys` as the live current-dungeon counter. Hyrule Castle
  proper folds into the Escape/Sewers key slot through `Rando_KeySlotFrom*`.
- There is one small-key-door payment site (`dungeon.c`): after the door-shuffle
  already-open test, it checks `link_num_keys != 0`, decrements it, and writes
  through to `link_generic_keys` in Retro.
- Logic does not consume keys sequentially. `HAS_ITEM` / `HAS_AMOUNT` predicates
  and the door-shuffle oracle reason over collected counts and conservative
  thresholds. `OP_ITEM_IS` is also used by forced/forbidden key placement rules.
- Retro replaces all per-dungeon keys with `GenericKey` and collapses every
  per-dungeon requirement to the shared pool. A second per-dungeon collapse model
  would be ambiguous and is not needed.
- The item registry currently ends at ID 219, leaving 36 append-only IDs below the
  hard `ITEM__COUNT <= 256` boundary.

## D1 - Settings, defaults, and normalization

Add two settings:

- `key_rings`: `Off=0`, `Random=1`, `All=2` (default `Off`);
- `skeleton_key`: boolean (default `false`).

Append canonical byte `[30]`: bits 0-1 encode `key_rings`, bit 2 encodes
`skeleton_key`, and bits 3-7 are refused-undefined until claimed. This grows
`kSettingsCanonicalLen` from 30 to 31 and triggers the generator/share/sidecar /
suppressed-spoiler/corpus coupling cascade. Existing shorter canonical blobs
zero-extend the new byte, so both features decode off.

Canonical settings preserve the **requested** `key_rings` value.
`Settings_EffectiveKeyRings()` is the single non-destructive resolution authority:

| Condition | Effective rings | Skeleton Key |
|---|---:|---:|
| effective small keys = Vanilla | Off | requested value |
| Retro / `Settings_GenericKeysActive` | Off | requested value |
| effective small keys = Dungeon or Wild | requested Off/Random/All | requested value |
| door shuffle or dungeon chains | supported (those axes already force Dungeon keys) | supported |
| Open/Standard/Inverted, no generic keys | supported | supported |

Canonical hashing, v1/v2 shares, sidecars, snapshots, and the race-suppressed
settings payload retain the requested value. Generation, placement, logic, runtime,
and the selected-family mask consume the derived effective value. Native UI and
spoiler can therefore show both values and explain why a requested mode became Off,
including after race reveal. Skeleton Key never resolves off merely because another
shuffle is active.

## D2 - Eligible families and deterministic Random selection

Eligibility is based on the **pre-ring shuffled key multiset**, not a hard-coded
list. For each of the 13 small-key families, count copies that would enter the
pool from:

1. `kVanillaSmallKeyCounts` when effective keys are Dungeon/Wild;
2. every active key-pot location;
3. every active forced-enemy small-key location; and
4. future registered shuffled key-source contributors routed through the same
   centralized counter.

A family is eligible iff that count is nonzero. Thus Eastern Palace is normally
ineligible but automatically becomes eligible if an active itemized source adds an
Eastern key. This avoids both a useless Eastern ring and a future source-specific
exception.

`KeyRings_Select(settings, seed_u64)` returns a 13-bit randomizer-dungeon mask:

- Off: return zero.
- All: the complete eligible mask.
- Random: seed a dedicated RNG domain from `(seed_u64, "key-rings/v1")`; draw a
  bit for every eligible family and retry the salted selection until the result is
  neither empty nor the complete eligible mask. Use a deterministic alternating-
  bit fallback if a bounded retry cap is exhausted.

Effective Random requires at least two eligible families. If the resolved
source/settings combination still has effective Random and fewer families,
validation refuses generation with a diagnostic instead of silently changing
Random to Off or All. A requested Random mode that resolves to Off under
Vanilla/Retro is already Off and returns a zero selected mask without this refusal.

The selection must not consume the main assumed-fill RNG stream. The same helper is
used by pool construction, logic, spoiler, runtime activation, customizer
validation, and selfchecks. The selected mask is reproducible from persisted seed
+ requested canonical settings after deriving the effective mode, and does not need
its own authoritative save field.

## D3 - Append-only item model and presentation

Append 14 item IDs:

- IDs `220..232`: `KeyRing_<Dungeon>` in the same 13-family order as
  `SmallKey_HyruleCastleEscape..SmallKey_GanonsTower`;
- ID `233`: `SkeletonKey`.

Key Rings use the existing registry category `dungeon_item` with a new
`direct_key_ring` dispatch. Skeleton Key uses the existing category `absolute` with
a new `direct_skeleton_key` dispatch; C-side classification explicitly keeps it out
of progression despite that storage category. Add explicit
small-key↔ring↔rando-dungeon↔game-key-slot conversion helpers rather than spreading
range arithmetic beyond the append-only mapping boundary.

All rings share one custom ring receipt/field icon and palette; Skeleton Key uses
one distinct custom icon. Extend the generated custom-gfx blob/table, direct-grant
icon map, field-item rendering, item names, and confirmation path. Human-facing
names are `Hyrule Castle Key Ring`, `Palace of Darkness Key Ring`, etc., plus
`Skeleton Key`.

Build assertions pin:

- the 13 ring IDs remain contiguous and map bijectively to the 13 small-key IDs;
- `ITEM__COUNT <= 256`;
- every ring has a name, direct-grant handler, and icon;
- Skeleton Key is not inside any key/dungeon/progression range by accident.

## D4 - Pool collapse and one-check invariant

Pool construction first builds the ordinary key multiset from all active sources.
After the last registered small-key source is added and before junk padding:

1. compute the selected mask through D2;
2. for each selected family, require a nonzero copy count;
3. replace exactly one copy with that family's Key Ring;
4. remove every other copy of that family's small key; and
5. let the existing target-sized junk pad fill the released slots.

The result contains exactly one ring and zero ordinary shuffled keys for every
selected family. Unselected families remain byte-for-byte ordinary key copies.
Every active location still receives one item; rings reduce progression density,
not location count. Vanilla/free drops that are not active checks remain in the
world and may create harmless surplus keys.

Rings are progression and enter the dungeon-progression prefix. In Dungeon mode a
ring has the same confinement as its small key; in Wild mode it can be placed
worldwide. `OP_ITEM_IS(SmallKey_D)` treats `KeyRing_D` as the same candidate for
forced and forbidden placement rules, so Swamp's forced key and Turtle Rock's
always-allow rules remain sound.

Customizer rules preserve the one-check invariant:

- a selected ring may be pinned at most once or left for assumed fill;
- an unselected ring item is invalid;
- ordinary keys for a selected family and duplicate ring copies are invalid in
  pins; `pool_overrides` may not change the cardinality of any small key, Key
  Ring, or Skeleton Key at all (pins may relocate the generated item, not mint or
  remove one);
- Skeleton Key may appear exactly once only when enabled, and may be pinned like
  any other bonus item.

When `skeleton_key=true`, add one `SkeletonKey` before target padding. It therefore
replaces one junk item and never increases the placement count. It is classified
non-progression and may land anywhere an ordinary unrestricted bonus/junk item may
land, including outside beatable-only spheres. Pool construction SHALL remove a
deterministic junk copy if the pre-pad pool is already at its target, rather than
overflowing or dropping Skeleton Key.

## D5 - Logic semantics and placement predicates

Introduce one ring-aware effective-count helper and use it in every small-key
consumer. For small-key family D:

- Retro generic-key behavior remains the first mutually exclusive model;
- otherwise, if `counts[KeyRing_D] > 0`, the effective `SmallKey_D` count is a
  saturated value that exceeds every authored and door-shuffle threshold;
- otherwise, use the ordinary `SmallKey_D` count.

The helper must cover `HAS_ITEM`, `HAS_AMOUNT`, `HAS_ANY_OF`, `HAS_ANY_COUNT`,
door-shuffle `held_keys[]`, live reachability/tracker counts, final sphere walk,
goal verification, and any direct key-count fingerprint/cache input. This avoids
trying to shadow-increment and rewind two item IDs throughout assumed fill.

Generated/assumed inventories already contain ring item counts directly. The live
inventory fill path must additionally materialize every bit in the derived owned-
ring cache as `counts[KeyRing_D] = 1` before calling the same helper; numeric key
counters alone are not proof of ring ownership and must not synthesize a ring.

In placement context, `OP_ITEM_IS(SmallKey_D)` is true for `KeyRing_D` and vice
versa only when D is selected for the seed. Negation therefore rejects the ring
where it rejects the original key. No unrelated dungeon item aliases.

Skeleton Key is intentionally absent from all effective-count helpers and
predicates, is not progression, is not placed in the assumed inventory, and never
satisfies the door oracle. A seed generated with `skeleton_key=true` must have the
same logical reachability/goal result when that item is removed from collected
counts.

## D6 - Runtime grants and door payment

Generate one authoritative `key_ring_grant_count[13]` from the same dungeon table
and authored key-source metadata that define the complete vanilla key stock. The
count includes chest/placed keys plus authored pot/enemy/free-drop keys, is capped
below the `0xff` outside-dungeon sentinel, and is build-asserted to meet or exceed
every authored logic threshold and every supported door-shuffle worst-case
threshold.

Direct-granting `KeyRing_D`:

1. maps D to the correct game key slot (including HC proper → Escape/Sewers);
2. sets the persisted slot to `max(current, key_ring_grant_count[D])`;
3. if Link is currently in that key family, mirrors the value into
   `link_num_keys` immediately;
4. records derived ring ownership in the runtime cache;
5. refreshes HUD/reachability/tracker state and uses the normal direct-grant
   confirmation; and
6. returns `kRandoLttpSkip`.

Granting the complete stock after some doors were already opened can leave surplus
keys; this is intentional and safe. Door-open bits remain the authority for paid
doors, and a ring must never lower an existing counter.

Direct-granting `SkeletonKey` sets the derived owned cache, refreshes state, shows
the confirmation, and skips vanilla dispatch. At the sole small-key-door payment
site, after the door-shuffle already-open branch and before the ordinary
`link_num_keys` test/decrement:

- owned Skeleton Key → take `has_key_for_door` without decrementing any counter;
- otherwise keep the existing regular/GenericKey behavior unchanged.

The big-key-door branch remains earlier and unchanged. Skeleton Key does not set
`link_bigkey`, does not satisfy its message check, and does not open big-key doors.
When Skeleton Key and regular keys are both held, Skeleton Key wins and the regular
counter is preserved.

## D7 - Ownership reconstruction, saves, shares, and snapshots

Ring/Skeleton ownership is derived from authoritative existing state: scan the
installed placement table for ring/Skeleton item IDs and test those locations in
the checked bitmap. Cache the resulting 13-bit ring mask and Skeleton boolean for
hot runtime queries. Update the cache immediately on direct grant and rebuild it on:

- sidecar slot activation and checked-bitmap load;
- randomizer snapshot cold replay and checked-bitmap restore;
- placement table reinstall/reveal paths;
- customizer-generated slot activation; and
- slot deactivation (clear all derived state).

No new authoritative ownership TLV or SRAM field is needed. A missing/corrupt
placement or checked table follows the existing slot/snapshot refusal contract;
it must not guess ownership from counters.

Grow the sidecar to format version 9 so its settings section is 31 bytes. A v9
reader accepts v1..v8 files using their versioned shorter bodies and zero-extends
the missing key-item byte to Off/false. Pre-v9 binaries refuse v9 files under the
existing sequential-body compatibility rule. Update the snapshot RandoState
settings payload parser, race-mode suppressed-spoiler size/layout, fixed-settings
fixtures, and every `_Static_assert` coupled to `kSettingsCanonicalLen`.

V2 share strings continue to carry `settings_len`; current 31-byte settings make
the raw token 47 bytes and the unpadded base32 string 76 characters. Older shorter
v2 strings zero-extend; a newer longer string is refused. V1 identity-only share
strings retain their 50-character wire format and identity role, but their token
values intentionally change because they embed the bumped generator version and a
prefix of the canonical-settings hash.

## D8 - UI, spoiler, tracker, and autotracker

The native settings window adds:

- a `Key rings` selector (`Off`, `Random mix`, `All`), near small-key mode;
- a `Skeleton Key` checkbox with the text “Bonus only; logic never requires it”;
- disabled/effective text when rings normalize off under Vanilla small keys or
  Retro generic keys.

Spoiler JSON/text records requested/effective ring mode, eligible family mask,
selected family mask and names, selection salt version, Skeleton Key enabled state,
and ordinary placement rows for the actual ring/Skeleton items. This lets a seed be
audited without reimplementing selection.

The item tracker and autotracker expose each selected ring's owned state, Skeleton
Key owned state, and the existing remaining numeric key counters. Unselected ring
families are absent/disabled rather than displayed as missing progression. The
reach panel uses D5's effective counts, while the HUD keeps showing the real
remaining numeric counter. Hints may name a ring as progression and may hint the
Skeleton Key as a bonus; Skeleton Key remains excluded from progression-only hint
or accessibility calculations.

## D9 - Compatibility matrix

- **Pot / enemy key checks**: active key copies join D4 before collapse. Selected
  families have one ring regardless of how many sources were itemized.
- **Door shuffle**: ring-aware effective counts feed the existing key oracle and
  cache fingerprint. Runtime door-kind relocation needs no separate bypass.
- **Dungeon chains**: rings retain the original dungeon key family and Dungeon
  confinement; the chain boss-approach predicates see the same effective count.
- **Dungeon entrance shuffle**: rings travel with key-family logic, not physical
  entrance identity; current Dungeon/Wild confinement rules apply unchanged.
- **Inverted / Standard**: supported through their existing logic graphs; the HCE
  key-slot fold is explicit in D6.
- **Retro**: rings normalize Off because GenericKey is already the active collapse
  model. Skeleton Key composes and bypasses without spending the shared counter.
- **Traps**: rings are progression and never eligible for trap replacement.
  Skeleton Key is a real bonus item, not a junk-placement masquerade candidate.
- **Customizer / race mode**: D4 validation applies; spoilers continue to obey race
  suppression, and reveal regenerates the same selected mask.

## D10 - Determinism and validation strategy

The new default canonical byte is zero but changes SHA input length, so settings
hashes and v2 share strings intentionally change. With both features off, placement
table bytes must remain identical for the same seed because selection does not run
and no main fill RNG is consumed. Bump `kGeneratorVersion` and corpus
`generator_version` together, rebaseline provenance fields, and explicitly compare
pre/post Off placements before accepting the corpus update.

Automated coverage must include:

- settings validate/serialize/deserialize/CSV/share old-short/new-current cases;
- exact selection masks for fixed Random seeds, plus non-empty/non-total and All
  eligibility properties;
- per-family pool audits across base, pot, enemy, and combined key sources;
- `OP_ITEM_IS`, every HAS form, assumed-fill rewind/collection, final spheres, and
  door-oracle thresholds with a ring;
- runtime direct grants inside/outside the target dungeon, HC folding, no counter
  lowering, Skeleton Key no-spend, GenericKey no-spend, and big-key refusal;
- ownership cache rebuild across sidecar, snapshot, reveal, and deactivation;
- customizer rejection cases; and
- corpus rows for Off, Random Dungeon, Random Wild, All, pot keys, enemy keys,
  pot+enemy, door shuffle, dungeon chains, Inverted, Standard escape, Retro
  normalization, Skeleton-only, and rings+Skeleton.

Owner playtests remain load-bearing for item art/confirmation, mixed and all-ring
feel, key counters/HUD, small-key doors with and without ordinary keys, big-key
non-bypass, save/reload, snapshot replay, and one door-shuffle seed.
