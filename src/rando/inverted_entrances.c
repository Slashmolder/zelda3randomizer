// inverted_entrances.c — static Inverted-mode entrance/exit override. See header.
#include "inverted_entrances.h"

#include "rando_settings.h"  // kWorldState_Inverted
#include "../assets.h"       // g_asset_ptrs[] / g_asset_sizes[] + asset-index macros

#include <stdio.h>           // fprintf (oversize-asset diagnostic)
#include <string.h>          // memcpy

// ---------------------------------------------------------------------------
// Override table. Each row repoints one element of one asset array. `width` is
// the element size in bytes (1 or 2) — it must match the asset array's C type
// (asset 126 = uint8, asset 130 = uint8, asset 131 = uint16). Adding a future
// swap (DM-west / Bumper Cave, spawn, etc.) is a pure data edit here, provided
// the touched asset index is registered in g_shadows below.
//
// All values verified against ALTTPR app/Rom.php setInvertedMode() AND the
// fork's own interior-id constants (shuffle_entrance.c). See the change notes.
typedef struct InvertedOverride {
  uint8  asset_index;  // g_asset_ptrs[] / g_asset_sizes[] slot
  uint8  width;        // element width in bytes: 1 or 2
  uint16 elem_index;   // element index within the array
  uint16 value;        // new value (low byte used when width == 1)
} InvertedOverride;

// Asset indices touched (mirrors src/assets.h:254,262,264).
enum {
  kAsset_Overworld_Entrance_Id = 126,  // uint8:  door-slot -> entrance-id
  kAsset_ExitData_ScreenIndex  = 130,  // uint8:  exit-id   -> screen index
  kAsset_ExitDataRooms         = 131,  // uint16: exit-id   -> destination room
};

static const InvertedOverride kInvertedOverrides[] = {
  // --- Link's House <-> Bomb Shop ---
  // Link's House door (slot 0x00) now loads the Bomb Shop interior (0x53).
  { kAsset_Overworld_Entrance_Id, 1, 0x00, 0x53 },
  // Bomb Shop door (slot 0x52) now loads the Link's House interior (0x01).
  { kAsset_Overworld_Entrance_Id, 1, 0x52, 0x01 },
  // Link's House exit screen -> dark world (PHP: 0x15B8C +0x00 = 0x6C).
  { kAsset_ExitData_ScreenIndex,  1, 0x00, 0x6C },

  // --- Ganon's Tower <-> Agahnim's (Hyrule Castle) Tower ---
  // Vanilla AT door (slot 0x23) now loads the GT interior (0x37).
  { kAsset_Overworld_Entrance_Id, 1, 0x23, 0x37 },
  // Vanilla GT door (slot 0x36) now loads the AT interior (0x24).
  { kAsset_Overworld_Entrance_Id, 1, 0x36, 0x24 },
  // AT exit (exit-id 0x38) -> GT overworld room (PHP: 0x15AEE +2*0x38 = 0x00E0).
  { kAsset_ExitDataRooms,         2, 0x38, 0x00E0 },
  // GT exit (exit-id 0x25) -> AT overworld room (PHP: 0x15AEE +2*0x25 = 0x000C).
  { kAsset_ExitDataRooms,         2, 0x25, 0x000C },
};
#define kInvertedOverrideCount \
    (sizeof(kInvertedOverrides) / sizeof(kInvertedOverrides[0]))

// ---------------------------------------------------------------------------
// Shadow buffers. One static buffer per touched asset array. Sized generously;
// kShadowMaxBytes guards copies against an unexpectedly large asset. Each shadow
// has a saved "original" g_asset_ptrs[] pointer (NULL when not installed) used
// by Teardown.
#define kShadowMaxBytes 4096

typedef struct InvertedShadow {
  uint8        asset_index;          // which g_asset_ptrs[] slot this owns
  uint8        buf[kShadowMaxBytes]; // copy of the vanilla array + overrides
  const uint8 *orig;                 // saved g_asset_ptrs[idx] while installed
} InvertedShadow;

// One row per DISTINCT asset index in kInvertedOverrides. If you add a swap that
// touches a new asset array, add its index here too.
static InvertedShadow g_shadows[] = {
  { kAsset_Overworld_Entrance_Id, {0}, NULL },
  { kAsset_ExitData_ScreenIndex,  {0}, NULL },
  { kAsset_ExitDataRooms,         {0}, NULL },
};
#define kInvertedShadowCount (sizeof(g_shadows) / sizeof(g_shadows[0]))

static InvertedShadow *FindShadow(uint8 asset_index) {
  for (uint32 i = 0; i < kInvertedShadowCount; i++) {
    if (g_shadows[i].asset_index == asset_index) return &g_shadows[i];
  }
  return NULL;
}

void InvertedEntrances_Teardown(void) {
  for (uint32 i = 0; i < kInvertedShadowCount; i++) {
    if (g_shadows[i].orig != NULL) {
      g_asset_ptrs[g_shadows[i].asset_index] = (void *)g_shadows[i].orig;
      g_shadows[i].orig = NULL;
    }
  }
}

void InvertedEntrances_Install(uint8 world_state) {
  // Tear down any prior install first, so a slot-switch without an intervening
  // Teardown is safe (mirrors Entrance_RuntimeInstall's leading Teardown).
  InvertedEntrances_Teardown();

  // No-op for every non-Inverted world state — non-inverted/non-rando stay
  // byte-identical (the asset pointers are never touched).
  if (world_state != (uint8)kWorldState_Inverted) return;

  // Pass 1: copy each touched vanilla array into its shadow. Guard against a
  // NULL or oversize source (same defensive checks as rando.c's overlay). If
  // ANY required shadow can't be prepared, abort the whole install (the
  // already-restored vanilla pointers stay live) so we never apply a partial
  // override.
  for (uint32 i = 0; i < kInvertedShadowCount; i++) {
    InvertedShadow *s = &g_shadows[i];
    const uint8 *src = (const uint8 *)g_asset_ptrs[s->asset_index];
    uint32 size = g_asset_sizes[s->asset_index];
    if (src == NULL || size == 0 || size > kShadowMaxBytes) {
      // Abort the whole install rather than apply a partial override. The
      // leading Teardown already restored any prior install, so the live asset
      // pointers are still vanilla here (Pass 3 is what repoints) — this
      // Teardown is a defensive no-op. Warn on the oversize case so a future
      // maintainer who registers a larger asset notices the silent fallback.
      if (size > kShadowMaxBytes)
        fprintf(stderr,
                "InvertedEntrances: asset %u (%u bytes) exceeds shadow cap %u; "
                "inverted entrance override skipped.\n",
                (unsigned)s->asset_index, (unsigned)size,
                (unsigned)kShadowMaxBytes);
      InvertedEntrances_Teardown();
      return;
    }
    memcpy(s->buf, src, size);
  }

  // Pass 2: apply each override into its shadow buffer. Bounds-check every
  // element against the live asset size before writing.
  for (uint32 i = 0; i < kInvertedOverrideCount; i++) {
    const InvertedOverride *o = &kInvertedOverrides[i];
    InvertedShadow *s = FindShadow(o->asset_index);
    if (s == NULL) continue;  // override references an unregistered shadow
    uint32 size = g_asset_sizes[o->asset_index];
    if (o->width == 2) {
      // 2-byte element: byte offset = elem_index * 2; need 2 bytes in range.
      uint32 byte_off = (uint32)o->elem_index * 2u;
      if (byte_off + 2u > size) continue;
      // Little-endian store, matching the byte order the game reads uint16 from.
      s->buf[byte_off + 0] = (uint8)(o->value & 0xFF);
      s->buf[byte_off + 1] = (uint8)(o->value >> 8);
    } else {
      if (o->elem_index >= size) continue;
      s->buf[o->elem_index] = (uint8)(o->value & 0xFF);
    }
  }

  // Pass 3: save originals + repoint. Done last so an early abort above never
  // leaves a dangling repoint.
  for (uint32 i = 0; i < kInvertedShadowCount; i++) {
    InvertedShadow *s = &g_shadows[i];
    s->orig = (const uint8 *)g_asset_ptrs[s->asset_index];
    g_asset_ptrs[s->asset_index] = s->buf;
  }
}
