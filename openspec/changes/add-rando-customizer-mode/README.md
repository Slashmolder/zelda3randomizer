# add-rando-customizer-mode

**Phase D** — customizer mode: manual per-location placement override + custom item pool composition. The dispatcher API is unchanged from Phase A; customizer pins a subset of locations and the standard assumed-fill places the rest.

## Status

**Built; in-window playtest pending.** Headless generation (`--generate-seed --customizer=`), the playable-slot path (`--generate-slot --customizer=` and native-window generation), and the PC native-window manifest UI are implemented. `customizer_active` is canonical byte `[26]` bit1, the slot persists the generated placement, and race mode is refused with customizer mode. Remaining: owner playtest of the in-window flow (tasks §6.5) and the deferred `customizer_seed` share-string encoding (§6.4).

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | Pin-over-assumed-fill pipeline + canonical `[26]` bit1 extension + race exclusion | ✅ reconciled to as-built |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | Native-window toggle + manifest field + inline validation | ✅ reconciled to as-built |
| [tasks.md](tasks.md) | Implementation checklist | ✅ §1–6 built; §6.4 share-string + §6.5 playtest open |

## Verification (autonomous)

- WSL gcc `-Werror` + MSVC Release builds green; `--rando-selftest` green (incl. `Customizer_SelfCheck`, `Customizer_PlacementSelfCheck`, the `Settings_SelfCheck` `[26]` bit1 block).
- Corpus regenerated with customizer off: placement and sphere digests are byte-identical.
- Digest parity for (open/fast_ganon, seed 0x1, example manifest): `--generate-seed` == `--generate-slot` == MSVC == WSL (`a5067c3d46ca99ce…`).
- Negative paths: race+customizer refused (CLI + slot), empty manifest refused, no-manifest-with-flag refused.

## Dependencies

- Phase A archived. Benefits from #2 trackers (manual placement easier to verify with an in-game tracker overlay). No strict dependency.
