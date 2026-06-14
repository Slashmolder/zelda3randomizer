# add-rando-share-string-v2 — entry-point index

Share-string **v2**: embed the full canonical settings in the share string so
"Paste share string" restores a seed losslessly — settings AND seed — instead
of seed-only with a quiet warning (the 2026-06-12 playtest incident: pasted
string + different window settings → silently different seed).

| File | What | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why / what changes / impact | ✅ authored |
| [design.md](design.md) | D1-D8: transport-only format, exact v2 layout (44 bytes → 71 chars), generator-version + customizer stances, determinism proof | ✅ authored |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | MODIFIED: Share-string format (v1+v2, magic dispatch, identity surfaces pinned) | ✅ authored |
| [specs/randomizer-native-window/spec.md](specs/randomizer-native-window/spec.md) | MODIFIED: copy/paste (v2 restore, v1 warn); ADDED: Generate mismatch-confirmation modal | ✅ authored |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | ADDED: Switch/file-select v2 deferral statement | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist | ✅ code+verify done; ⏳ owner playtest §4.5, archive §5.3 |

Key decisions: v2 is **UI-transport only** (sidecar/ZRSR/spoiler/banner keep
the v1 form — zero save/race/corpus impact, **no kGeneratorVersion bump**);
v2 = `ZRS2 | generator_version | settings_len | canonical[28] | seed | crc16`; customizer
seeds are fenced out of v2 until `customizer_seed` lands (D2 §6.4); paste of a
different-generator-version v2 warns, newer-`settings_len` refuses.
