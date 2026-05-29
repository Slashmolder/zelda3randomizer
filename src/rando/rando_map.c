// rando_map.c — decode the overworld map graphic to RGBA. See rando_map.h.
#include "rando_map.h"
#include "../assets.h"

#include <stdio.h>
#include <string.h>

bool RandoMap_Decode(bool dark, uint8 *out) {
  const uint8 *gfx = kOverworldMapGfx;             // 256 tiles * 64 bytes, 8bpp
  const uint16 *pal = kOverworldMapPaletteData;    // 256 BGR555
  if (gfx == NULL || pal == NULL || out == NULL) return false;

  // Reconstruct the 64x64 tile-index grid the Mode-7 map renders.
  static uint8 grid[64][64];
  memset(grid, 0, sizeof(grid));
  if (!dark) {
    // Light: kLightOverworldTilemap is quadrant-major (4 x 32x32), laid out into
    // a 128-stride VRAM tilemap at dsts {0, 0x20, 0x1000, 0x1020} — i.e. the four
    // 32x32 quadrants of a 64x64 grid (NMI_UpdateLoadLightWorldMap, nmi.c:327).
    const uint8 *src = kLightOverworldTilemap;
    if (src == NULL) return false;
    static const int qcol[4] = {0, 32, 0, 32};
    static const int qrow[4] = {0, 0, 32, 32};
    for (int q = 0; q < 4; q++)
      for (int r = 0; r < 32; r++)
        for (int c = 0; c < 32; c++)
          grid[qrow[q] + r][qcol[q] + c] = src[q * 1024 + r * 32 + c];
  } else {
    // Dark: kDarkOverworldTilemap is 1024 bytes (32x32). Best-effort placement;
    // verified/adjusted empirically. (The in-game dark load goes through a
    // separate NMI path — WorldMap_LoadDarkWorldMap, messaging.c:1144.)
    const uint8 *src = kDarkOverworldTilemap;
    if (src == NULL) return false;
    for (int r = 0; r < 32; r++)
      for (int c = 0; c < 32; c++)
        grid[r][c] = src[r * 32 + c];
  }

  for (int py = 0; py < kRandoMapPixels; py++) {
    for (int px = 0; px < kRandoMapPixels; px++) {
      uint8 tile = grid[py >> 3][px >> 3];
      uint8 idx = gfx[tile * 64 + (py & 7) * 8 + (px & 7)];
      uint16 c = pal[idx];  // BGR555
      uint8 *o = out + ((size_t)py * kRandoMapPixels + px) * 4;
      o[0] = (uint8)((c & 0x1f) << 3);          // R
      o[1] = (uint8)(((c >> 5) & 0x1f) << 3);   // G
      o[2] = (uint8)(((c >> 10) & 0x1f) << 3);  // B
      o[3] = 255;
    }
  }
  return true;
}

static bool write_ppm(const char *path, const uint8 *rgba) {
  FILE *f = fopen(path, "wb");
  if (f == NULL) return false;
  fprintf(f, "P6\n%d %d\n255\n", kRandoMapPixels, kRandoMapPixels);
  for (int i = 0; i < kRandoMapPixels * kRandoMapPixels; i++)
    fwrite(rgba + (size_t)i * 4, 1, 3, f);  // RGB (drop alpha)
  fclose(f);
  return true;
}

bool RandoMap_DumpPpm(const char *prefix) {
  static uint8 buf[kRandoMapPixels * kRandoMapPixels * 4];
  char path[512];
  if (!RandoMap_Decode(false, buf)) return false;
  snprintf(path, sizeof(path), "%s_light.ppm", prefix);
  if (!write_ppm(path, buf)) return false;
  if (!RandoMap_Decode(true, buf)) return false;
  snprintf(path, sizeof(path), "%s_dark.ppm", prefix);
  if (!write_ppm(path, buf)) return false;
  return true;
}
