# Audit log — add-rando-entrance-shuffle

## Fresh-eyes audit 2026-06-01 (archive-readiness)

Scope note: this change is 46/50, with Stage 4 / Insanity explicitly deferred
(per proposal + memory `entrance_shuffle_stage1_asbuilt`). I audited the as-built
Stages 1-3 (caves + dungeons + crossed + decoupled), not the deferred Insanity path.
Reviewed: proposal.md, design.md (skim), `src/rando/shuffle_entrance.c` (dungeon
table, medallion/GT keying, pool eligibility), `src/rando/rando_settings.c`
(`Settings_CanonicalSerialize`/`Deserialize` byte [25] packing, `apply_derived_rules`).

Verified clean:
- Canonical serialization appends the entrance axes into the formerly-zero pad byte
  [25] with NO byte shift; `kSettingsCanonicalLen` stays 28; default packs to 0x00 →
  corpus byte-identical for non-entrance seeds. Deserializer unpacks [25] symmetrically
  and leaves [26..27] permissive (forward-compat). Correct.
- `apply_derived_rules` normalization is thorough and correctly ordered: coupled is
  fully derived (= shuffle-on && !decoupled); cross_category cleared unless BOTH pools
  shuffle (clear runs AFTER the both-off clear so it sees final values); GT opt-in
  cleared unless dungeon shuffle on; all axes cleared under Inverted/Retro. This makes
  the (struct → canonical) map many-to-one in a well-defined, hash-stable way.
- Medallion (MM/TR) and GT door gates are now keyed on the INTERIOR (lobby) region via
  `dungeon_override_key` / `interior_region_name`, tying the gate to the SOURCE door's
  edge predicate so it stays with the overworld SPOT — the documented task 2.8 design
  (ALTTPR-consistent: cast the medallion at MM/TR's overworld spot regardless of what's
  behind the door).

### NEW findings

(none NEW that block archive at the audited stage.)

### Pre-existing / cross-referenced (NOT new — recorded for completeness)

- The MM/TR medallion-gate model-vs-runtime concern is already tracked in memory
  `entrance_shuffle_medallion_gate_mismatch` as HIGH playtest-pending (task 2.8). The
  current code keys the override on the interior region to address it; whether the
  RUNTIME medallion-cast gate matches the model when a medallion dungeon is the shuffle
  SOURCE is still a playtest item, not a new code finding.
- **Cross-ref to the race-mode-reveal HIGH**: race_mode + any entrance-shuffle axis is
  a reachable combination, and `Rando_RevealSpoiler` does NOT regenerate the entrance
  assignments, so revealing such a seed always fails with a false stamp mismatch. The
  fix belongs in `add-rando-race-mode-reveal` (see that change's audit.md), but it is
  the entrance_mapping spoiler section (rando_spoiler.c:264-268) that the reveal path
  fails to reproduce. Whoever archives race-mode-reveal must coordinate with this change.

### Verdict
Archive-ready for the as-built Stages 1-3 ONLY, contingent on (a) the task 2.8
medallion playtest landing and (b) NOT archiving while race-mode-reveal's entrance
interaction is unfixed (otherwise a documented-supported race+entrance seed is
un-revealable). Stage 4 / Insanity remains correctly out of scope. No new code bugs
found at the audited stage.

---

## Fresh-eyes audit — 2026-06-02 (re §3.3 audit portion)

Read-only fresh-eyes pass over `shuffle_entrance.{c,h}`, `inverted_entrances.{c,h}`,
and the `overworld.c`/`dungeon.c`/`player.c` entrance hooks. Detail + patches below.

- **Medallion gate (task 2.8) — CLEARED, not a bug.** Traced runtime + model: the
  MM/TR medallion carves the door via a screen-index ancilla independent of the
  entrance overlay; the edge-override re-keys `OP_MEDALLION_OPENS` to the source spot
  in both shuffle directions. Model and runtime agree. (One confirming playtest still
  nice, but no code change indicated.)
- **HIGH — dungeon-decoupled one-way exit stranding** (`shuffle_entrance.c:802`).
  In the **deferred** Insanity/decoupled mode only: destination remap can drop Link at
  a gated-to-leave spot (Ice Palace island) with no logic edge, so the accessibility
  gate can't reject. Fix = directed overworld→overworld warp edge (NOT lobby→entry,
  which was the previously-reverted HIGH). Leave with the deferred Insanity work. VERIFY=PLAYTEST.
- **LOW — cross-pool GT eligibility** relies on GT's inbound count, not the
  `shuffle_ganons_tower_entrance` opt-in (latent). VERIFY=BUILD.
- **LOW — `Entrance_SelfCheck`** doesn't assert interior (lobby) key distinctness (latent). VERIFY=BUILD.
