// shuffle_cosmetic.c — see shuffle_cosmetic.h.
//
// Windows headers must precede "types.h" (types.h defines BYTE/WORD/DWORD as
// function-style macros that collide with winbase.h typedefs). Mirrors config.c.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // FindFirstFileA / FindNextFileA
#define cosmetic_stricmp _stricmp
#else
#include <dirent.h>
#include <strings.h>   // strcasecmp
#define cosmetic_stricmp strcasecmp
#endif

#include "shuffle_cosmetic.h"
#include "../types.h"
#include "../config.h"     // g_config, Config_InternString
#include "rando_rng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Conservative shuffled-song band. Overworld music is masked `& 0xf`
// (messaging.c), so area background songs live in the low-id range; control
// codes are >= 0xf0. Shuffling only [lo, hi] keeps special tracks (boss /
// credits / triforce, ids >= 0x10) intact. The exact band is a playtest-tunable
// (tasks.md §6) — widen once the per-id semantics are confirmed in-game.
enum { kCosmeticSongLo = 0x01, kCosmeticSongHi = 0x0F };

#define COSMETIC_MAX_SPRITES 2048

// Derived tables, rebuilt by Cosmetic_SetSeed.
static uint8  g_group_perm[16];   // per 16-color CGRAM group: channel perm 0..5
static uint8  g_song_map[256];    // identity outside [lo,hi]; bijection within
static uint64 g_eff_seed;         // effective cosmetic seed (for the sprite pick)
static bool   g_tables_ready;

uint8 Cosmetic_ParsePaletteMode(const char *s) {
  if (!s) return kCosmeticPalette_Vanilla;
  if (!cosmetic_stricmp(s, "shuffled"))  return kCosmeticPalette_Shuffled;
  if (!cosmetic_stricmp(s, "grayscale") || !cosmetic_stricmp(s, "greyscale"))
    return kCosmeticPalette_Grayscale;
  if (!cosmetic_stricmp(s, "negative") || !cosmetic_stricmp(s, "invert"))
    return kCosmeticPalette_Negative;
  return kCosmeticPalette_Vanilla;  // "vanilla", "off", unknown
}

void Cosmetic_SetSeed(uint64 config_cosmetic_seed, uint64 slot_seed_u64) {
  g_eff_seed = config_cosmetic_seed ? config_cosmetic_seed : slot_seed_u64;

  RandoRng rng;
  Rng_SeedFromU64(&rng, g_eff_seed);

  // Palette: one channel permutation (of 6) per 16-color CGRAM group.
  for (int i = 0; i < 16; i++)
    g_group_perm[i] = (uint8)Rng_NextRange(&rng, 6);

  // Music: Fisher-Yates over the song band; identity elsewhere.
  for (int i = 0; i < 256; i++) g_song_map[i] = (uint8)i;
  int n = kCosmeticSongHi - kCosmeticSongLo + 1;
  for (int i = n - 1; i >= 1; i--) {
    int j = (int)Rng_NextRange(&rng, (uint32)(i + 1));
    uint8 t = g_song_map[kCosmeticSongLo + i];
    g_song_map[kCosmeticSongLo + i] = g_song_map[kCosmeticSongLo + j];
    g_song_map[kCosmeticSongLo + j] = t;
  }
  g_tables_ready = true;
}

// Apply channel permutation `p` (0..5) to a BGR555 color, preserving bit 15.
static uint16 PermuteChannels(uint16 c, uint8 p) {
  int r = c & 0x1f, g = (c >> 5) & 0x1f, b = (c >> 10) & 0x1f;
  int nr, ng, nb;
  switch (p) {
    default:
    case 0: nr = r; ng = g; nb = b; break;
    case 1: nr = r; ng = b; nb = g; break;
    case 2: nr = g; ng = r; nb = b; break;
    case 3: nr = g; ng = b; nb = r; break;
    case 4: nr = b; ng = r; nb = g; break;
    case 5: nr = b; ng = g; nb = r; break;
  }
  return (uint16)((c & 0x8000) | (nb << 10) | (ng << 5) | nr);
}

void Cosmetic_ApplyPaletteCgram(uint16 *cgram, int count) {
  uint8 mode = g_config.cosmetic_palette_mode;
  if (mode == kCosmeticPalette_Vanilla || !cgram) return;
  for (int i = 0; i < count; i++) {
    uint16 c = cgram[i];
    if (mode == kCosmeticPalette_Shuffled) {
      cgram[i] = PermuteChannels(c, g_group_perm[(i >> 4) & 15]);
    } else if (mode == kCosmeticPalette_Grayscale) {
      int r = c & 0x1f, g = (c >> 5) & 0x1f, b = (c >> 10) & 0x1f;
      int l = (r * 77 + g * 150 + b * 29) >> 8;  // weights sum to 256 -> l in 0..31
      cgram[i] = (uint16)((c & 0x8000) | (l << 10) | (l << 5) | l);
    } else {  // negative
      cgram[i] = (uint16)(c ^ 0x7fff);  // 1's-complement all three 5-bit channels
    }
  }
}

uint8 Cosmetic_RemapSong(uint8 music_ctrl) {
  if (!g_config.cosmetic_music_shuffle || !g_tables_ready) return music_ctrl;
  if ((music_ctrl & 0xf0) == 0xf0) return music_ctrl;  // control codes
  return g_song_map[music_ctrl];                       // identity outside band
}

// --- Sprite folder pick ------------------------------------------------------

static bool HasZsprExt(const char *name) {
  size_t n = strlen(name);
  return n > 5 && !cosmetic_stricmp(name + n - 5, ".zspr");
}

static int CompareNames(const void *a, const void *b) {
  return cosmetic_stricmp(*(const char *const *)a, *(const char *const *)b);
}

const char *Cosmetic_PickSpriteFile(void) {
  const char *dir = g_config.cosmetic_sprite_dir;
  if (!dir || !dir[0]) return NULL;

  char **names = (char **)malloc(sizeof(char *) * COSMETIC_MAX_SPRITES);
  if (!names) return NULL;
  int count = 0;

#if defined(_WIN32)
  char pattern[1024];
  snprintf(pattern, sizeof(pattern), "%s\\*.zspr", dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
          HasZsprExt(fd.cFileName) && count < COSMETIC_MAX_SPRITES) {
        names[count] = _strdup(fd.cFileName);
        if (names[count]) count++;
      }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
#else
  DIR *d = opendir(dir);
  if (d) {
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < COSMETIC_MAX_SPRITES) {
      if (HasZsprExt(e->d_name)) {
        names[count] = strdup(e->d_name);
        if (names[count]) count++;
      }
    }
    closedir(d);
  }
#endif

  const char *result = NULL;
  if (count > 0) {
    qsort(names, count, sizeof(char *), CompareNames);  // stable cross-platform order
    RandoRng rng;
    Rng_SeedFromU64(&rng, g_eff_seed ^ 0x53504e4b53504e4bULL /* "SPNK" salt */);
    int idx = (int)Rng_NextRange(&rng, (uint32)count);
    char full[1280];
    snprintf(full, sizeof(full), "%s/%s", dir, names[idx]);
    result = Config_InternString(full);
  }

  for (int i = 0; i < count; i++) free(names[i]);
  free(names);
  return result;
}
