## Why

Phase A specced item and location trackers as Phase-B-deferred Optional requirements (`randomizer-ui/spec.md:142-164`) with the WHEN/THEN contracts already drafted: reachability-counter gating, OAM caching, location grouped by region, checked-location bitmap persistence. **Implementation has not landed.**

Trackers are the highest player-impact UX Phase B can ship. Three reasons to ship them early:

1. **Player experience.** A 200-location seed is unplayable for many people without a tracker — they spend half the run remembering which chests they've opened.
2. **Reachability bug surfacing.** Every inventory change re-computes reachability via the tracker overlay. Any flaky predicate in `assets/rando/logic_parts/*.yaml` becomes immediately visible — the tracker recoloring "wrong" is faster signal than a multi-hour playthrough.
3. **Test substrate for #4a Inverted.** Inverted world-state will surface a lot of new reachability behavior. Trackers landing before #4a means we have a tool for verifying the new graph as we author it.

Backend compatibility is the long pole: SDL software, SDL hardware-accelerated, OpenGL, OpenGL ES, and Switch all need to render the overlay correctly. The Phase A spec scenarios mention the gating but don't enumerate the backend matrix — this change fills that in.

## What Changes

- **Implement the existing Phase A spec** `randomizer-ui/spec.md:142-164` item + location tracker requirements. No new SHALL beyond what Phase A drafted, but the existing scenarios get refined for the backend matrix.
- **Tracker-toggle keybindings** in `src/config.c` `kKeys_*` (Phase A specced this informally; this change codifies it). Two bindings: `kKeys_RandoToggleItemTracker`, `kKeys_RandoToggleLocationTracker`. Default to unbound; documented in `README.md`. Per-slot toggle state is in-memory (not persisted to sidecar; trackers come back hidden on each fresh launch).
- **Renderer-backend compatibility matrix.** The overlay draws on top of the game's regular OAM/BG layers; each of the five backends (SDL software, SDL hardware, OpenGL, OpenGL ES, Switch) needs the overlay to composite correctly without affecting `g_ram` or PPU register writes. Spec the contract; tasks enumerate per-backend touchpoints.
- **Checked-location bitmap write path.** Phase A spec covers the read invariant (`randomizer-save/spec.md:98-110`); the write side (when does the bit get set?) needs a SHALL. Contract: bit is set when the dispatcher fires for that location, or when an audit-exempt direct-write site bumps `reachability_state_counter`. Persists via the existing sidecar write path on next save.
- **Forward-compat with Phase C TLV chain** (`randomizer-save/spec.md:50`). The Phase A spec already reserves a TLV chain after the bitmap for Phase C entrance-shuffle. This change's checked-bitmap write must not break that forward-compat reserve — the writer always positions TLV chain immediately after the bitmap, regardless of bitmap content.
- **No `kGeneratorVersion` bump.** Pure UI; placement output is unchanged.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-ui`: MODIFIED Requirements on "Optional in-game item tracker (Phase B)" and "Optional in-game location tracker (Phase B)" — refine for backend matrix, toggle bindings, OAM-cache invalidation guarantees. ADDED Requirement for the renderer-backend compatibility matrix as an explicit contract.
- `randomizer-save`: ADDED Requirement for the checked-location bitmap write path (the Phase A spec covers read invariants only).

## Impact

- **Code**: `src/hud.c` + `src/hud.h` (overlay rendering — new tracker draw paths), `src/config.c` (kKeys_*), `src/main.c` (renderer-path glue per backend), `src/rando/rando_save.c` (bitmap write path), `src/rando/rando.c` (bitmap set on dispatcher fire + audit-exempt event-flag bumps), `README.md` (keybinding docs).
- **Cross-backend testing required**: SDL software, SDL hardware, OpenGL, OpenGL ES, Switch. Per-backend smoke test enumerated in tasks.
- **No new assets** — overlay reuses the existing HUD/inventory tiles. The icon-tile lookup overlaps with `add-rando-confirmation-icons`'s `direct_grant_icons.yaml`; if both changes land, the YAML may share entries.
- **Determinism guard**: no-op — tracker doesn't write tracked-inventory cells.
- **Audit guard**: bitmap-set call inside `Rando_OnLocationCheck` is the dispatcher path itself, so no exemption needed. Audit-exempt event-flag sites that bump `reachability_state_counter` (Aga 1, dungeon-boss-cleared, NPC-satisfied, pyramid-opened, master-sword-pulled, king's-tomb-item-taken — per Phase A `randomizer-placement / Reachability-affecting event-flag bumps`) also need to mark the corresponding location bit; ensure those sites still have `// rando-exempt:` comments.
- **`placement_digest_hex` byte-identical** before/after.
- **Switch parity**: tracker draw path must run on Switch with the same per-frame OAM-cache optimization as desktop.
