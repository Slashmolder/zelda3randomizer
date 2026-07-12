# All Enemy Checks - design

## D1 - Definition of "all"

As built in the first shipped `all` phase, the `all` tier means every generated
compatible ordinary enemy source in the local static registry: the `keys` tier,
the `dungeon` tier, static overworld ordinary enemies whose runtime identity is
stable, and reviewed underworld cave/interior exceptions whose access can be
modeled directly. The longer-term definition remains every finite, authored,
killable enemy source that can be assigned a stable one-shot location identity. It
includes:

- forced enemy-drop checks from the `keys` tier;
- ordinary dungeon enemies from the `dungeon` tier;
- static overworld enemy sources with stable authored `(stage, area, source slot,
  block)` identity;
- reviewed underworld cave/interior enemy sources with stable
  `(dungeon_room_index, sprite_N source slot)` identity and direct access
  predicates, such as the Kakariko Storage Shed room `0x107` rats gated by bombs,
  Hookshot Cave-side room `0x03C` Blue Bari, Mimic Cave room `0x10C`
  Goriya/Mimic sources, and Mini Moldorm Cave room `0x123` Mini-Moldorms. Same-side
  in-room pot routes are used only when the reviewed access side actually reaches
  enough pots; the shared Fairy Cave pot in room `0x10C` is not counted for Mimic
  Cave enemies;
- future ordinary dungeon audit-only sources once their room reachability is
  modeled;
- future miniboss and boss combat sources when their death event and existing
  prize/heart behavior can coexist with a separate enemy check;
- future finite scripted spawn groups when each emitted child can be assigned a
  stable bounded source identity.

For ordinary enemy sources, the randomized check is the kill reward and replaces
the probabilistic prize-pack pickup. This prevents one kill from visibly producing
both a placed check item and an unrelated vanilla/shuffled consumable. The hidden
prize-pack event still advances its normal cursor/luck state so later non-check
drops remain deterministic. Forced key-drop pickups, items swallowed by Pikits,
boss prizes/hearts, and scripted progression retain their dedicated paths.
Non-check enemies continue to use the prize-pack table.

The tier excludes actors that are not valid one-shot kill checks:

- non-killable sprites such as thieves and NPC-like actors;
- hazards, traps, projectiles, decorative sprites, and overworld effects that do not
  have a killable enemy death event;
- unbounded or farmable dynamic spawns unless the design converts the parent into a
  finite one-shot source with stable persistence;
- sources whose death cannot be detected, dispatched, and guarded without
  duplicate grants or source-slot drift.

The generated `all` registry is complete for the currently shipped static
dungeon+overworld domains plus the reviewed underworld exception list only if every
emitted source has stable identity, logic, death dispatch, and checked-source
grant suppression. Bosses, minibosses, finite scripted spawns, and unbounded/farmable
spawns are not quiet exclusions from a completed future full-all domain; they
remain explicit future domains until their audits can classify each source as
included or excluded with a stable reason.

## D2 - Setting semantics

Add `enemy_drop_checks=all` as a tier above `dungeon`:

- `off`: no enemy checks;
- `keys`: forced enemy key-drop checks plus the one-shot forced big-key source;
- `dungeon`: `keys` plus reviewed ordinary dungeon enemy checks;
- `all`: `dungeon` plus every compatible generated static overworld ordinary enemy
  source and every reviewed all-tier underworld exception in the current modeled
  domains.

The enum values are pinned: `Off` (0), `Keys` (1), `Dungeon` (2), and `All` (3).
`All` is accepted by settings validation, CSV parsing, share strings, file-select,
and the native settings window as a distinct requested value. Selectors may hide or
disable `All` when derived rules immediately lower it, but they must not show `all`
as an alias for the dungeon-only tier. The current parser alias that treats text
`all` as `Dungeon` must be removed or made legacy-version-scoped so new settings
cannot make `all` mean dungeon-only. Adding the new value changes generation
semantics and therefore requires the normal generator-version, hash, fixed-settings,
corpus, share decode, UI persistence, and selfcheck updates.

The effective value must be honest. Derived rules normalize requested `All` through
the same effective-setting path used by lower tiers, and the normalized effective
value feeds the settings hash, share strings, placement, logic, UI, spoiler, and
runtime. UI/spoiler output may also show the raw request with a downgrade reason.
The initial compatibility table is:

- vanilla effective small keys: normalize requested `All` to `Off`;
- Wild/Retro/Dungeon small keys with a fresh all-tier registry: keep `All`;
- missing, stale, or capacity-overflowing all-tier registry: reject;
- door shuffle: keep `All`; generated door x ordinary enemy-check bridge rows,
  source predicates, and bridge digest/replay support make the checks provable;
- enemy shuffle: normalize requested `All` to the highest lower tier allowed by
  existing derived rules, normally `Keys` but `Off` when the keys tier is unsupported,
  until a future change makes enemy shuffle placement-affecting for all-enemy logic
  and updates its digest/corpus contract;
- boss shuffle: keep `All`; the emitted GT-miniboss checks are outside the
  shuffleable dungeon-boss room set, unless another rule lowers the effective tier
  further;
- pot shuffle: compose conservatively; generated thrown-pot routes are active only
  when effective pot shuffle is off, while pot-sanity seeds use the reviewed
  inventory-combat route until per-source throwable consumption is modeled;
- entrance shuffle, including cave entrance shuffle: normalize requested `All` to
  `Dungeon` until all-enemy overworld/domain reachability is modeled against the
  entrance graph, unless another rule lowers the effective tier further; existing
  cave-entrance pot/key derived rules still apply before this normalization.

It must never silently treat `all` as `dungeon`.

## D3 - Source audit and identity

The all-enemy generator builds one source audit from local ROM assets and curated
runtime tables. The current shipped generator emits ordinary dungeon, reviewed
all-tier underworld, static overworld, GT-miniboss event, and finite
scripted-spawn rows. Each emitted source row records at minimum:

The enemy-shuffle constraint table is only the default safety oracle, not the
definition of "all." Curated finite static sources may be check-only even when
they are not safe generic shuffle replacements. Red Eyegores (`0x84`) use this
path: every authored dungeon source is an all-tier-only check with stable
room/source-slot identity and a Bow-specific kill predicate.

- domain (`dungeon`, `overworld`, `boss`, `scripted_spawn`);
- stable source identity fields for that domain;
- vanilla source type and coordinates;
- enemy-shuffle source category and safety flags;
- killability status and exclusion reason if not emitted;
- reachability region and source predicate;
- death hook and checked-state suppression policy;
- visual-marker lookup data, or an explicit marker-suppressed policy for domains
  that cannot render in-world markers safely.

Dungeon identity and reviewed underworld exception identity continue to use
`(dungeon_room_index, sprite_N source slot)`. Reviewed underworld rows carry an
`all_tier_only` activation bit so the runtime and placement can keep them out of
the `dungeon` tier. Overworld identity uses the authored active sprite-list tuple
`(stage, area, source_slot, block)` plus the runtime `sprite_N_word` block. A lazy
block lookup fallback re-resolves `(current area, current stage, block)` so snapshot
restore does not depend on the process-static load-time map being rebuilt first.
GT-miniboss identity uses the destination dungeon event plus room. Dungeon bosses
remain excluded because their heart-container and dungeon-prize sequence is already
the boss reward. Scripted-spawn identity uses the authored parent
`(room, source slot, overlord type)` plus child index and child type. Unbounded
children are excluded.

Enemy shuffle must not change location identity. The first all-enemy implementation
normalizes requested `All` to the highest lower tier allowed by existing derived
rules while enemy shuffle is active, normally `Keys` but `Off` when the keys tier is
unsupported, because the archived enemy-shuffle contract is runtime-only and does
not affect placement digests. A future change may allow `All` with enemy shuffle
only by explicitly changing that contract so placement and logic consume substituted
per-source type, HP, damage, and killability, with new digest and corpus
expectations.

## D4 - Runtime collection and persistence

Ordinary all-enemy checks use death-time dispatch. The death hook resolves the
source identity, looks up the generated location id, guards already-checked rows,
dispatches the placed item, marks the location checked, and then allows the normal
death cleanup that is safe for that source.

Checked sources must not re-grant on room/area reload, save/reload, snapshot replay,
screen transition, mirror transition, or world transition. The runtime suppresses
the checked source's randomized grant and marker, not the actor. Ordinary checked
enemies keep their authored source identity and follow vanilla spawn, room-history,
death, and prize-drop behavior. Forced key checks remain distinct: their carrier may
respawn normally, but its collected one-time forced-key behavior stays suppressed.

Boss and miniboss rows may coexist with existing boss prizes, dungeon prizes, heart
containers, and scripted progression only if the death hook can keep those existing
checks intact and add exactly one separate enemy check. If that cannot be proven for
a boss source, the source is excluded with a specific audit reason until modeled.

Forced enemy-drop rows keep their pickup-time path from the `keys` tier. Ordinary
enemy rows, including overworld rows, do not spawn abandonnable item pickups for
their check; they direct-grant at death to avoid stranding checked state.

## D5 - Logic and kill routes

Every emitted all-enemy location requires both reachability and a kill route.
Reachability uses the appropriate dungeon room, overworld area, boss arena, or
scripted-spawn parent predicate.

Static overworld rows include the generated logic region, the active overworld
sprite-list stage gate, and a conservative kill route. Stage-2 overworld rows are
post-Agahnim sources; they remain real checks, but placement rejects item classes
that can be required to reach or clear Agahnim's Tower. This prevents the
progressive-copy cycle where every sword/key/combat alternative lands behind the
post-Aga sprite-stage gate while still allowing other progression on all-tier
checks.

Kill routes are source-specific:

- inventory routes use the effective source type and HP, including enemy-shuffle
  substitutions when supported;
- thrown-object routes are emitted only when engine damage tables show the object can
  damage that source and the reachable area contains at least the number of
  liftable/throwable objects required to kill it;
- multi-hit thrown-object routes must count required hits exactly, so an enemy that
  needs two pots is logical only when at least two reachable pots or equivalent
  throwables are available;
- generated thrown-pot routes require effective pot shuffle to be off. This avoids
  treating one shuffled pot as both a prerequisite item check and a later weapon;
  active pot-sanity seeds retain the inventory-combat branch instead;
- special vulnerabilities, invulnerability phases, armor, environmental constraints,
  and weapon immunities require explicit curated predicates.

If a kill route cannot be modeled conservatively for a finite authored killable
source, effective `All` is not allowed for that source's domain; the request must
downgrade visibly or reject until a reviewed predicate exists.

## D6 - Capacity, save data, and snapshots

The all-enemy registry may exceed the existing location capacity. The implementation
must measure the emitted count with pot sanity, enemy-drop keys, dungeon enemies,
boss/prize checks, and other active location-expansion features enabled. If the count
does not fit, `all` must not ship until location capacity, placement tables,
checked-location bitmaps, sidecar payloads, snapshot payloads, spoilers, trackers,
and selftests are migrated together.

Save and snapshot compatibility must be explicit:

- old slots without `all` state load unchanged;
- new all-enemy slots persist every emitted checked bit;
- snapshot restore fails closed or deactivates randomizer state if required all-enemy
  source-identity metadata is missing or malformed;
- grant/marker suppression and vanilla actor respawn after restore match normal
  reload behavior.

## D7 - Door shuffle, pot shuffle, enemy shuffle, and entrance shuffle

Door shuffle supports `all` through generated door x ordinary enemy-check bridge
rows and source predicates. The bridge rows, effective all-enemy tier, and digest
are included in `DoorShuffleLayout` generation, the accepted door layout digest,
sidecar activation, and snapshot type-5 replay validation so bridge drift fails
closed like pot and forced enemy-drop bridge drift.

Pot shuffle composes with `all` conservatively. Generated thrown-pot kill routes are
guarded by the shared effective-pots-off predicate. When any effective pot-sanity
tier is active, those routes are disabled and enemy checks use their reviewed
inventory-combat branch. This prevents a shuffled pot from being counted both as an
already-required item check and as a future thrown weapon without introducing a
per-source consumption model.

Enemy shuffle composes with forced-key `Keys` only in the first all-enemy change.
Requested `All` normalizes to the highest lower tier allowed by existing derived
rules while enemy shuffle is active, normally `Keys` but `Off` when the keys tier is
unsupported. Boss shuffle composes with `All` because dungeon bosses are excluded
and the emitted GT-miniboss checks are outside the shuffleable room set, unless
another rule lowers the effective tier further. Entrance shuffle normalizes requested `All` to
`Dungeon` until all-enemy overworld/domain reachability is modeled against the
entrance graph, unless another rule lowers the effective tier further.

## D8 - Visuals, tracker, and spoiler output

All-enemy checks reuse the existing enemy marker preference:

Dungeon all-tier rows use the existing enemy marker path. Static overworld
all-tier rows currently support exact placed-item markers in item mode when the
renderer can show them safely. Overworld carriers that cannot draw an exact item
marker use a post-sprite gold-glint fallback so dense screens still show every
unchecked check without drawing an item stand-in.

Because `all` can place markers on dense screens, the renderer may prioritize visible
markers and fall back or suppress under OAM pressure, but tracker/spoiler state must
still expose every emitted location. Spoilers and trackers group checks by dungeon
room, overworld area/screen, boss arena, or scripted parent source. Names must include
enough source identity to distinguish duplicate enemy types in the same area.

For every all-tier domain that renders in-world markers, the generated data must
provide the marker candidate metadata required by the renderer: stable authored
identity, screen-coordinate derivation, scroll/camera basis, sorted-OAM region, and
checked-marker suppression behavior. A domain that lacks safe marker metadata may
suppress in-world markers only if tracker/spoiler output still exposes every emitted
location and the UI does not imply an in-world marker is guaranteed.

## D9 - Verification strategy

Required validation includes:

- complete source-audit freshness checks: every source classified, no duplicate
  identity, no unclassified finite killable source;
- codegen checks for location capacity, lookup uniqueness, source identity, and
  stale audit data;
- logic selfchecks for reachability plus kill-route predicates;
- Release build and `--rando-selftest`;
- corpus rows for `all` under Wild/Retro/Dungeon keys, pot shuffle, enemy shuffle
  normalization, door shuffle normalization, boss shuffle normalization, cave
  entrance interaction, and dense marker rooms;
- runtime tests for shipped dungeon, reviewed underworld, static overworld,
  GT-miniboss, and finite scripted-spawn death grants;
- leave/re-enter, save/reload, snapshot before death, snapshot after death, mirror or
  world transition, checked grant/marker suppression, and vanilla actor respawn;
- targeted thrown-pot tests where one pot is insufficient and two pots are logical;
- F12/OAM/VRAM marker checks for dense all-enemy screens.
