# Audit — Cosmetic shuffles

## Cosmetic provenance (task 1.1)

Upstream commits the C re-implementation was grounded against, pinned for
reproducibility. Verified present as sibling checkouts and `git rev-parse`'d on
2026-06-02:

| Upstream | Role | Pinned commit | Date |
|---|---|---|---|
| `../alttp_vt_randomizer/` (PHP) | Placement/logic + the `Randomize.php` ROM-patcher call sites referenced in `design.md` | `219fcafd029dab597b8db400efafd8f56f8b4edb` | 2024-02-18 |
| `../z3randomizer/` (asm) | Runtime ROM behavior reference | `dcb0a2b42d14445f7994a0e9e4d63cbecf4b98d3` | 2024-02-18 |

**Reference-only caveat (important):** ALTTPR's cosmetic palette transforms live
in the **JavaScript browser ROM-patcher**, *not* in the PHP or the asm repo. The
PHP `Randomize.php` only records *which* cosmetic options were chosen and writes a
few ROM setters; the per-pixel palette math is JS. The fork therefore does **not**
byte-match ALTTPR's transforms — it re-implements deterministic BGR555 transforms
in C (`shuffle_cosmetic.c`). The pinned hashes establish *which upstream the
design grounded against*, not a byte-for-byte port target. Self-consistency
(identical `cosmetic_seed` → identical look on every platform) is the only
determinism guarantee, per `design.md §"Determinism & verification"`.

Sprite/music provenance: the ZSPR write site (`Randomize.php:204`, @0xDD308) and
the song-table remap are baked into the ROM by the same JS patcher; the fork
re-drives selection over its own loaders (`design.md` axis table).

## Fresh-eyes audit (task 8.1)

Fresh-eyes pass completed 2026-06-02 (read-only auditor over `shuffle_cosmetic.{c,h}`
+ call sites in `main.c`/`nmi.c`/`audio.c`/`config.c`/`rando.c`).

**No HIGH findings.** Core invariants verified against source:
- Cosmetic RNG is a genuinely separate stack-local `RandoRng` seeded from the
  cosmetic/slot seed; never touches the placement/fill RNG. No gameplay-RNG
  perturbation.
- No cosmetic path writes `g_ram`. Palette transforms only the PPU CGRAM copy
  (`nmi.c:213-216`), after the vanilla `main_palette_buffer` memcpy; CGRAM is
  never read back into game RAM and is NOT part of the side-by-side RAM/SRAM/VRAM
  compare → palette-on cannot diverge the RAM compare; re-copy each flush prevents
  accumulation.
- Music remap (`audio.c:142`) runs after `last_music_control`/`music_unk1` are set
  from the original id; band `[0x01,0x0F]` is a song→song bijection; control codes
  and ids ≥0x10 pass through.
- Off ⟹ vanilla per axis (palette mode 0 early-returns; music gated; sprite NULL → fallback).

| # | Sev | Site | Finding | Disposition | Verify |
|---|---|---|---|---|---|
| C1 | MED | `shuffle_cosmetic.c` sprite-folder sort (`cosmetic_stricmp`) | Non-ASCII `.zspr` filenames sort differently under POSIX `strcasecmp` (locale) vs Win32 `_stricmp` (ASCII) → cross-platform sprite-pick divergence, violating the same-seed→same-sprite determinism contract. | **FIX (build-verifiable):** replace with a byte-wise ASCII-lowercased comparator. Landed in this branch. | CODEGEN/BUILD |
| C2 | MED | `Cosmetic_PickSpriteFile` (`main.c:1336`) / `rando.c:1542` | Default `cosmetic_seed=0` never tracks the *slot* seed for the SPRITE axis (pick happens once at launch before a slot is active); palette+music do track it. Spec scenario "default tracks slot seed" only partially met. Already carries an in-code "documented limitation" comment. | **DEFER → spec/doc:** record the sprite carve-out in the spec scenario; optional follow-up to re-pick on slot load. | PLAYTEST |
| C3 | LOW | `Config_ApplyLive` (`config.c:1632`) | Live-setting `cosmetic_seed` back to 0 during a rando slot reseeds tables with literal 0, not the slot seed → look differs from a fresh slot-load until reload (self-consistent within a run). | **DEFER → follow-up:** pass active slot `seed_u64` instead of 0. | PLAYTEST |
| C4 | LOW | palette `shuffled`/`negative` (whole-CGRAM) | Transform covers the HUD/text palette group; can reduce HUD readability (contrast preserved, so still distinguishable). | **DEFER → playtest 4.3** (already tracks legibility); consider excluding HUD group. | PLAYTEST |

Auditor confidence: high on the no-HIGH conclusion (RNG isolation, g_ram-clean
CGRAM path, and post-state-store music remap all verified directly against source
+ the snapshot-compare routines).
