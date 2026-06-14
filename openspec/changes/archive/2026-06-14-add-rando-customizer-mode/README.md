# add-rando-customizer-mode

**Phase D** — customizer mode: manual per-location placement override + custom item pool composition. The dispatcher API is unchanged from Phase A; customizer pins a subset of locations and the standard assumed-fill places the rest.

## Status

**Archived 2026-06-14.** Headless generation (`--generate-seed --customizer=`), the playable-slot path (`--generate-slot --customizer=` and native-window generation), and the PC native-window manifest UI are implemented. `customizer_active` is canonical byte `[26]` bit1, the slot persists the generated placement, and race mode is refused with customizer mode. Owner playtest of the in-window flow is complete as of 2026-06-14. The deferred `customizer_seed` share-string encoding is a follow-up share-string-format change; current customizer seeds fall back to the v1 identity string.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | Pin-over-assumed-fill pipeline + canonical `[26]` bit1 extension + race exclusion | ✅ reconciled to as-built |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | Native-window toggle + manifest field + inline validation | ✅ reconciled to as-built |
| [tasks.md](tasks.md) | Implementation checklist | ✅ complete |

## Verification (autonomous)

- WSL gcc `-Werror` + MSVC Release builds green; `--rando-selftest` green (incl. `Customizer_SelfCheck`, `Customizer_PlacementSelfCheck`, the `Settings_SelfCheck` `[26]` bit1 block).
- Corpus regenerated with customizer off: placement and sphere digests are byte-identical.
- Digest parity for (open/fast_ganon, seed 0x1, example manifest): `--generate-seed` == `--generate-slot` under MSVC (`26b667c3152b87f2…`).
- Isolated temp-slot smoke for an easy grant manifest (`Link's House: Hookshot`) generated and round-tripped (`26e16f55ea3c8ab8…`).
- Owner playtest confirmed the in-window flow: load manifest → generate → load slot → confirm a pinned item grants in-game.
- Negative paths: race+customizer refused (CLI + slot), empty manifest refused, no-manifest-with-flag refused.

## Dependencies

- Phase A archived. Benefits from #2 trackers (manual placement easier to verify with an in-game tracker overlay). No strict dependency.
