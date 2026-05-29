# add-randomizer-support — change folder index

OpenSpec change adding a built-in ALTTPR-style randomizer to this port. Ten rounds of expert critical review applied; ready for implementation via `/opsx:apply`.

## Read these in order

| File | Purpose | Length |
|---|---|---|
| [proposal.md](proposal.md) | Why this change, what changes, capabilities, impact | ~250 lines |
| [design.md](design.md) | Architectural decisions (17 of them: D1-D17), risks, migration plan | ~330 lines |
| [specs/](specs/) | Six capability specs with normative SHALL requirements + WHEN/THEN scenarios | ~6 files |
| [tasks.md](tasks.md) | Implementation checklist, 14 sections, with explicit Phase A0/A0.5/A1/A2 gating | ~250 lines |
| [lessons.md](lessons.md) | What this conversation taught us about working with the spec and external upstreams | ~80 lines |

## The six capabilities

| Spec | Concerns |
|---|---|
| [randomizer-core](specs/randomizer-core/spec.md) | Generator, RNG, share string, item pool, simulated inventory, spheres, spoiler, CLI |
| [randomizer-logic](specs/randomizer-logic/spec.md) | Predicate VM, 15 ops, named macros, can_reach/can_place/always_allow, RegionRemap, well-formedness |
| [randomizer-placement](specs/randomizer-placement/spec.md) | Dispatcher, grant-site instrumentation, receivable items, asset-hash warn-dialog |
| [randomizer-shuffles](specs/randomizer-shuffles/spec.md) | Prize / medallion / boss / entrance / drop-pool / cosmetic shuffles, dungeon-item modes |
| [randomizer-ui](specs/randomizer-ui/spec.md) | File-select kind-toggle, settings screen, 5-icon hash, text-input infrastructure, trackers |
| [randomizer-save](specs/randomizer-save/spec.md) | Sidecar file, atomic-commit protocol, snapshot tail-TLV, orphan handling |

## The 17 design decisions (decision summary)

D1. Placement layer as runtime override, not asset rewrite • D2. Bytecode predicate VM with 15 Phase A ops + RegionRemap • D3. Determinism constraints (no rand/time/uninit/FP) • D4. xoshiro256\*\* RNG, fundamentally different model from ALTTPR's database-storage reproducibility • D5. Regression corpus, not parity corpus • D6. Sidecar `sram_rando.dat` with embedded placement table for upgrade safety • D7. `kFeatures1_*` gating with strict init order • D8. Kind-toggle on 3 file-select slots (NOT a 4th entry — no SRAM room) • D8a. Asset-data warn-dialog, not hard-block • D9. Tracker overlays gated on `reachability_state_counter` • D10. SDL_TEXTINPUT + libnx swkbd infrastructure • D11. Snapshot tail-TLV with ordering invariant • D12. Atomic-commit protocol (write-temp + fsync + rename) • D13. Reachability counter bumps on dispatcher + audit-exempt writes + event flags • D14. Phase A0/A0.5/A1/A2 sub-phasing • D15. CLI / headless generation mode with manifest support • D16. 5-icon visual hash from `SHA-256(share_string_binary)` (NOT from settings_hash — common pitfall) • D17. QoL features as opt-in panel, not auto-enable

## Pre-implementation gates (per tasks.md §0)

Before any §6 grant-site PR is accepted:
- [ ] **0.10**: Logic-source decision (recommended: hand-translate ALTTPR PHP — MIT-derivative, mechanical mapping)
- [ ] **0.11**: Owner assignment per workstream + merge-order rule documented in `audit.md`
- [ ] **0.1-0.7**: Phase 0 audit deliverable lands with all 0.8a-e checks ticked

## Implementation entry point

```
/opsx:apply
```

Walks through `tasks.md` task-by-task. Start with §0 (audit), then §1 (foundation + CI scaffolding), then §2 (RNG/share-string), etc. Phase A0.5 vertical-slice work happens on a feature branch (`phase-a0.5-vertical-slice`), not master.

## External upstream this change references

`../alttp_vt_randomizer/` — community ALTTPR PHP implementation, MIT-licensed. The spec maps directly to:
- `app/Region/{Standard,Open,Inverted}/*.php` → our `assets/rando/logic.yaml`
- `app/Support/ItemCollection.php` (43 macros) → our named macro set
- `app/Filler/RandomAssumed.php` → our `src/rando/rando_placement.c` assumed-fill algorithm
- `app/Randomizer.php` config keys → our `RandoSettings` canonical-serialization order

**Discipline note**: when extending this spec, claims about ALTTPR's behavior MUST be grounded in the actual `../alttp_vt_randomizer/` source. See [lessons.md](lessons.md) §"Claim-grounding for upstream projects" for the failure modes from this conversation's history.
