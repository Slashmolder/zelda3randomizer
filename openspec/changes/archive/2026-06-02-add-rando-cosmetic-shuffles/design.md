# Design — Cosmetic shuffles (palette / sprite / music)

## Context & the reframe

The Phase-A stub estimated "3-4 weeks, build each axis from scratch." Grounding
against the fork's own source shows that is wrong: **the fork already owns every
cosmetic *rendering* primitive** that ALTTPR applies via its browser ROM-patcher.
This change is a deterministic **selection/transform driver**, not a from-scratch
build.

| Axis | ALTTPR applies it via | Fork already has | Net-new work here |
|---|---|---|---|
| **Sprite** | ZSPR write @0xDD308 in the JS patcher (`Randomize.php:204`) | ZSPR loader (`src/main.c:2278`, commit `3abb560`); `unchanged_sprites` config (`config.c`) | deterministic **pick** of one `.zspr` from a folder |
| **Music** | song-table remap baked into the ROM | SPC player (`spc_player.c`) + full MSU-1 / Opus / Deluxe (`ae5af72`, `c1439dd`) | deterministic **remap** of the song the engine queues |
| **Palette** | per-group pixel transforms in the JS patcher (NOT in PHP) | `main_palette_buffer` / `aux_palette_buffer`, `ApplyPaletteFilter_*` (`load_gfx.c`), armor-palette override (`38bbbe8`) | C **transforms** over BGR555 buffers at palette-load time |

**The single most important property:** cosmetics touch **no** placement, logic,
or `settings_hash` state. Therefore this change has **no `kGeneratorVersion`
bump, no corpus regen, and no canonical-size cascade** — uniquely among Phase
B/C/D changes. See `[[canonical-size-coupling]]`. Regression risk is structural
zero on the generation path.

## Decision 1 — `cosmetic_seed` lives in client config (`zelda3.ini`), NOT the slot

The stub originally proposed a `cosmetic_seed` slot-header field. **Rejected.**
The proposal's own rationale (tournaments hand every player the same
`share_string`; each personalizes their look) argues for the opposite, and a
slot-header field would force a `RandoSlotHeader` / canonical-size change — the
exact coupling that has bitten this project before (`[[canonical-size-coupling]]`,
`[[slot-world-state-persistence]]`).

```
        SHARE STRING (slot)                 CLIENT CONFIG (zelda3.ini)
   ┌──────────────────────────┐        ┌──────────────────────────────┐
   │ seed_u64                  │        │ [Graphics]                   │
   │ settings (canonical)      │        │   CosmeticSeed = <u64>       │
   │ settings_hash             │        │   PaletteShuffle = grayscale │
   │ placement table           │        │   SpriteShuffle  = on (dir)  │
   │ hints, goal, world_state  │        │ [Sound]                      │
   └──────────────────────────┘        │   MusicShuffle   = on        │
        identical for all players       └──────────────────────────────┘
                                              per-player, never travels
```

Consequences:
- **No save-format change.** The sidecar slot stays byte-identical. The
  `randomizer-save` spec is untouched (the stub's `randomizer-save` delta is
  dropped).
- `cosmetic_seed` is a `[Graphics]` INI key parsed by `config.c`. Default `0`
  means "derive from the active slot's `seed_u64`" so a fresh user still gets a
  shuffled-but-reproducible look without editing the INI; any non-zero value
  overrides.
- Cosmetics apply to **any** run, rando or vanilla — but the per-axis enables
  default off, so vanilla play is byte-identical unless the user opts in.

## Decision 2 — Palette shuffle ships 4 MVP modes

`vanilla` / `shuffled` / `grayscale` / `negative` — all simple deterministic
ops over a BGR555 buffer. ALTTPR's gimmick modes (`dizzy` / `sick` / `puke` /
`blackout`) are deferred to a follow-up; they need per-frame animated transforms,
not a one-shot buffer pass, and carry no logic value.

- `shuffled`: deterministic hue-rotation of each palette entry by a per-group
  angle drawn from the cosmetic RNG. Operates per 16-colour palette group so
  intra-group contrast is preserved (sprites stay legible).
- `grayscale`: luma-collapse each BGR555 entry.
- `negative`: 1's-complement each 5-bit channel.

## Architecture

New module `src/rando/shuffle_cosmetic.{c,h}`, built by the Makefile glob and
added to `zelda3.vcxproj` + the Switch makefile (per the multi-build convention,
`[[build-commands]]`). It exposes:

```c
void Cosmetic_Init(uint64 cosmetic_seed);     // called once at slot/world load
void Cosmetic_ApplyPaletteBuffer(uint16 *buf, int count, int group_base);
int  Cosmetic_PickSpriteIndex(int sprite_count);   // deterministic folder pick
uint8 Cosmetic_RemapSong(uint8 requested_song);    // identity unless music shuffle on
```

RNG: a dedicated `Rng` stream forked from `cosmetic_seed` (xoshiro256\*\* per
`randomizer-core / RNG family`) — never the fill RNG. Self-consistency is the
guarantee (identical `cosmetic_seed` → identical look on every platform); we do
NOT byte-match ALTTPR's JS transforms.

### Hook points (as-built — apply-time grounding found single chokepoints)

The original guess (patch each `load_gfx.c` palette-load site) was replaced by a
single chokepoint per axis, discovered by grounding:

- **Palette** — `nmi.c:211` is the *one* flush of `main_palette_buffer` to the
  PPU CGRAM (`memcpy(g_zenv.ppu->cgram, main_palette_buffer, 0x200)`).
  `Cosmetic_ApplyPaletteCgram` transforms the **CGRAM copy** in place right after
  that memcpy, so game RAM (`main_palette_buffer`) stays vanilla — no RAM-compare
  divergence, and because each flush re-copies from the vanilla source there is
  no frame-to-frame accumulation. Per-group transforms are precomputed once in
  `Cosmetic_SetSeed`; the per-frame path draws no RNG.
- **Sprite** — `LoadLinkGraphics` (`main.c`): `Cosmetic_PickSpriteFile` enumerates
  `*.zspr` in the folder, sorts by name (stable cross-platform), picks via a
  salted cosmetic-RNG draw, and feeds the path to the existing ZSPR loader; an
  unreadable pick falls back to the configured `LinkGraphics`. Launch-time only.
- **Music** — `ZeldaPlayMsuAudioTrack` (`audio.c:137`) is the single point where a
  song command reaches *both* MSU and the SPC engine (`APUI00`). Remapping
  `music_ctrl` at the top keeps MSU+SPC consistent. The caller (`nmi.c:116`) has
  already stored the **original** id in `last_music_control` / `music_unk1`, so
  game-facing music state and `ZeldaIsPlayingMusicTrack` checks are unaffected —
  only the audible output changes. The shuffle band is `[0x01, 0x0F]` (overworld
  songs are masked `& 0xf`); control codes (`& 0xf0 == 0xf0`) and ids ≥ 0x10 pass
  through. The exact band is a playtest-tunable.

## Determinism & verification

- No generation-path change → the regression corpus is **unaffected**; a
  default-settings seed's `placement_digest_hex` is byte-identical pre/post.
  CI adds a *separate* cosmetic-determinism check: fixed `cosmetic_seed` →
  identical palette/sprite/music selections across builds (analogous to the hint
  determinism step).
- `check_audit_guard.py`: the module writes only to palette buffers / a local
  song-remap table / sprite-index state — none are tracked inventory cells; no
  new exemptions expected (verify at apply time).
- **Playtest is the only real net** (`CLAUDE.md`): visually confirm each axis,
  and confirm per-axis-off is indistinguishable from vanilla.

## Native-window UI (as-built)

Originally INI-only with the UI deferred. After the owner noted there was no
discoverable surface, a **"Cosmetics" tab** was added to the PC native settings
window (`game_config_panels.cpp`, alongside Video/Audio — the natural home, since
these are local presentation settings, not seed-generation). It exposes the
palette mode, music toggle, sprite folder, and a hex **cosmetic-seed field with a
"New random seed" button** (UI-only SplitMix64 entropy mirroring the rando
window's seed roll). Consequences:

- The four keys became **managed** in `Config_WriteIniFile` (`kGfxKeys`/`kSndKeys`
  + `RenderManagedValue`) so Apply persists them (and inserts them at defaults),
  superseding the original "unmanaged / preserve-only" plan.
- `Config_ApplyLive` rebuilds the cosmetic tables on a seed change; palette mode +
  music toggle apply live; the sprite-folder pick stays launch-time (restart-tagged).
- The INI keys remain the **normative** source; the panel is a convenience layer.
  Cosmetics still never enter the slot/share string.

## Out of scope (explicit)

- Gimmick palette modes (dizzy/sick/puke/blackout) — follow-up.
- Heart color / heart-beep speed / menu speed / quickswap — these are ALTTPR
  *cosmetic ROM setters*, not *shuffles*; they belong in a separate QoL change,
  not the three-axis shuffle contract.
- Enemizer / sprite (enemy) shuffle — that is gameplay, tracked under
  `add-rando-shuffles-and-minigames`.
