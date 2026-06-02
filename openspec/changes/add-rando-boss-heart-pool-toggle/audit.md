# Audit log — add-rando-boss-heart-pool-toggle

## Fresh-eyes audit 2026-06-01 (archive-readiness)

Reviewed: proposal.md, the UI toggle in
`src/rando/rando_window/rando_window.cpp:370-381`, the placer pin branch
`src/rando/rando_placement.c:1104-1114`, the fixed comment at
`rando_placement.c:344-353`, and the `can_reach` predicate for all 10
`<Dungeon> - Boss` Drop locations across logic.yaml + logic_parts.

Verified clean:
- UI inversion is correct: `v = (region_boss_hearts_in_pool == 0)` displayed as
  "Shuffle boss heart containers"; checked → `= 0` (shuffled), unchecked → `= 1`
  (pinned, default). The misleading raw field value is never shown to the user.
- The stale read-only "Region boss hearts in pool" line was removed from the
  "Locked settings" block (no longer references boss hearts there).
- Placer pin branch pins boss Drop slots (type=Drop, vanilla_item=51) when
  `region_boss_hearts_in_pool != 0`. The type=Drop guard correctly excludes the
  Sanctuary chest (also vanilla_item=51 but type=Chest). Correct.
- The previously-backwards comment at rando_placement.c:344 is fixed and now states
  the inversion explicitly (non-zero/default 1 = pinned).
- **Logic-safety invariant VERIFIED**: all 10 boss Drop `can_reach` predicates gate
  on a `CanKill<Boss>` macro — EP/CanKillArmosKnights, DP/CanKillLanmolas,
  TH/CanKillMoldorm, PoD/CanKillHelmasaurKing, SP/CanKillArrghus, SW/CanKillMothula,
  TT/CanKillBlind, IP/CanKillKholdstare, MM/CanKillVitreous, TR/CanKillTrinexx —
  plus the items to reach/open each boss room. So freeing the slots into assumed-fill
  cannot strand progression behind an unbeatable boss. (Note: the winning EP entry is
  the logic_parts/01 override, not the logic.yaml entry — last-wins; it still carries
  CanKillArmosKnights, so the CLAUDE.md last-wins trap does not bite here.)
- No `Settings_SetDefaults` / canonical-byte / `kSettingsCanonicalLen` /
  `settings_hash` / `kGeneratorVersion` change, consistent with "default unchanged,
  no regen" claim. audit-guard / determinism / codegen-wiring all PASS.

### NEW findings

(none) — zero new findings. This is a genuinely minimal, baseline-safe UI+comment
change; the underlying placer/runtime path already shipped and is corpus-exercised,
and the logic-safety precondition is verified above.

### Verdict
Archive-ready (audit-wise). The only remaining gate is the standard manual UI
verification (toggle flips the field, seed shuffles boss hearts) per the proposal —
not a correctness concern.
