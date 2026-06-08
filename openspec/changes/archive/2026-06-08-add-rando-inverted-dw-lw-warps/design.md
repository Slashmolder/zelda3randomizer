## Context

The Inverted DW→LW warps were already implemented across two layers: type-`0x82` warp records (`InvertedSecrets_Install`) and the inverted overworld overlay that paints the rocks over them (`Overworld_ApplyInvertedTiles`, data in `inverted_maps.c`). The overlay, however, runs only inside `Overworld_DrawQuadrantsAndOverlays` / `SomeTileMapChange`, both of which fully decompress the screen — i.e. only on **full screen rebuilds** (mirror / flute / cave-exit / save-and-quit). The normal **walking-scroll** path streams tiles incrementally and never runs the overlay (documented in `inverted_maps_apply.c`: "walking-scroll never runs this overlay"). So walking onto a warp screen showed bare ground and there was nothing to lift — the symptom this change fixes.

Constraint: the overworld map16 buffer is `dung_bg2` (`g_ram+0x2000`), the *same* buffer the lift-collision path reads as `overworld_tileattr`. Writing the rock tile there makes it both render (via a single-tile VRAM upload) and become liftable.

## Goals / Non-Goals

**Goals:**
- The warp rock is present at its canonical position on a warp screen regardless of entry method, including plain walking.
- Faithful to ALTTPR: warp under the relocated vanilla rock, glove-gated, reusable world flip.
- Zero effect outside Inverted mode; no placement/logic/asset/save change.

**Non-Goals:**
- Rendering the *decorative* surround tiles that some overlay rows add (screens `0x47`, `0x75`) on walk-in — only the single liftable rock tile is asserted; full decoration still appears on a full rebuild.
- Making the full inverted overlay run on walking-scroll (out of scope; higher risk — see Decisions).
- Changing the logic graph's Power-Glove gate on Dark-World escape (already correct).

## Decisions

- **Per-frame single-tile re-assert in `Module09_00_PlayerControl`, not "run the overlay on walk-in."** The full overlay does large multi-tile rewrites and is entangled with the incremental-scroll / VRAM-stripe upload path; running it there risks the same class of glitches already seen for the screen-`0x1B` overlay. Instead `Overworld_EnsureInvertedWarpRock` writes just the one liftable rock tile for the current warp screen via `Overworld_DrawMap16_Persist` (which the secret-reveal path already uses mid-screen), making the change small and low-risk. Alternative considered: hook the scroll/stripe loader directly — rejected as invasive in a hot path.
- **Idempotent, guarded write.** The placer skips when `dung_bg2[pos>>1]` already equals the rock OR equals the revealed warp `0x0212`. This makes the VRAM upload fire at most once per screen entry (no per-frame spam) and — critically — never re-covers a warp the player just revealed before they can step on it. Lifting `0x020F`/`0x0239` transitions the tile rock→`0x0212` synchronously with no intermediate map16 value at that offset, so the two-value skip set is complete.
- **Re-place on re-entry is correct, not a bug.** Vanilla overworld rocks/bushes reappear when you leave and return to a screen; the per-frame placer reproduces that (overlay/scroll reloads the base tile, placer re-asserts the rock), so the warp is repeatable — matching the reusable-warp requirement.
- **Rejected alternative: retype the warps onto bare-hand bushes.** A prior workaround moved each warp onto an existing under-bush item so it would survive walking-scroll. It worked but diverged from ALTTPR (wrong object, wrong spot, removed the glove gate) and desynced from the placer's rock positions. Reverted in favor of fixing the rendering.

## Risks / Trade-offs

- **Per-frame cost in the player-control path** → Negligible: a ≤9-entry table scan + one `uint16` compare; the guarded write fires once per entry.
- **Decorative surround tiles missing on walk-in for `0x47`/`0x75`** → Cosmetic only; the liftable rock (the functional part) is present, and full decoration renders on any full rebuild. Can be extended later if desired.
- **Three layers must stay in sync** (warp record pos ↔ overlay rock pos ↔ placer table) → Fresh-eyes review verified all 9 entries agree byte-for-byte; the spec records the canonical table so future edits have a single reference.
