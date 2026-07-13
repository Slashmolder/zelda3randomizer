## Why

`enemy_drop_checks=dungeon` is intentionally not "all enemies". It covers the
forced enemy-key checks plus a conservative set of ordinary dungeon enemy checks
whose identity, logic, and persistence have been reviewed. The shipped `all` tier
  extends that boundary across the reviewed finite authored source domains this
  change can model safely.

The `all` tier means every finite, authored enemy source that can be
killed and modeled safely. Non-killable actors such as thieves, NPC-like sprites,
hazards, projectiles, and unbounded farmable spawns are not valid checks, but every
killable finite enemy source needs either an emitted location or an explicit audited
reason why it cannot be modeled yet.

## What Changes

- Add `enemy_drop_checks=all` as a distinct shipping tier above `dungeon`.
- Generate a classified enemy-source audit across static dungeon and overworld
  sources, reviewed underworld exceptions, reviewed GT miniboss events, and finite
  scripted spawn groups.
- Emit randomizer locations for every in-scope audited finite killable enemy source whose
  reachability, kill route, death event, and duplicate-grant suppression can be
  proven.
- Keep non-killable and non-finite actors out of the emitted registry, but record
  each exclusion with a stable reason.
- Add stable runtime identity and persistence for overworld and scripted enemy
  sources, matching the source-slot identity model already used for dungeon enemies.
- Require per-source kill logic, including weapon routes and counted thrown-object
  routes when enough reachable pots exist. Thrown-pot routes are conservatively
  disabled while pot sanity is active so a shuffled pot cannot be counted twice.
- Expose `all` only when it really means all compatible emitted killable sources for
  the selected settings. In incompatible setting combinations, the effective value
  must visibly degrade or generation must reject the seed; it must never silently mean
  dungeon-only.

## Non-Goals

- Do not include non-killable actors, interactables, theft/NPC sprites, hazards,
  projectiles, or infinite/farmable dynamic enemies as checks unless they are
  converted into a finite source with stable identity and one-shot persistence.
- Do not add separate checks to dungeon bosses whose heart-container and dungeon-prize
  sequence is already their randomized reward.
- Do not include one-shot scripted children that can disappear without a repeatable
  collection opportunity.
- Do not use enemy type alone as location identity. Identity must be based on the
  authored source instance.
- Do not ship an `all` selector that omits reachable finite killable enemies without
  an explicit audit and user-visible effective downgrade/rejection.
- Do not require placed-item marker mode to reveal every item. The visual system may
  use gold glints or bounded multi-icon rendering; collection semantics are primary.

## Impact

- **Generated data**: complete all-enemy audit, all-enemy registry, source lookup
  tables, reachability metadata, kill-route metadata, and exclusion reports.
- **Runtime**: overworld/source identity tracking, death-time dispatch, checked-state
  grant/marker suppression with vanilla actor respawn, and save/reload and snapshot
  replay for all emitted enemy domains.
- **Logic and placement**: full location emission, kill predicates, setting
  compatibility rules, capacity checks, item-pool/trap/customizer policies, and
  corpus coverage.
- **UI and docs**: a distinct `all` option, effective-setting display, spoiler and
  tracker grouping by dungeon room or overworld area/screen, and clear downgrade or
  rejection messages.
