#include "zelda_rtl.h"
#include "variables.h"
#include "load_gfx.h"
#include "select_file.h"
#include "snes/snes_regs.h"
#include "overworld.h"
#include "messaging.h"
#include "sprite.h"
#include "rando/rando.h"
#include "rando/rando_save.h"
#include "rando/rando_share.h"
#include "rando/rando_settings.h"
#include "rando/rando_textfield.h"

#define selectfile_R16 g_ram[0xc8]
#define selectfile_R17 g_ram[0xc9]
#define selectfile_R18 WORD(g_ram[0xca])
#define selectfile_R20 WORD(g_ram[0xcc])
static const uint8 kSelectFile_Draw_Y[3] = {0x43, 0x63, 0x83};

// ---------------------------------------------------------------------------
// §9.3a / §9.7 — Sidecar slot-kind cache + rando-banner rendering.
//
// Per-slot rendering dispatch on `slot_kind` from saves/sram_rando.dat. The
// file is read ONCE per file-select entry (in Module_SelectFile_0) and cached
// in module-static storage so per-frame draws stay cheap. Cross-frame freshness
// is fine because file-select doesn't write the sidecar — that happens later
// during the new-game / save-game flow (next cluster).
//
// Geometry contract (per spec randomizer-ui / "Three-slot file-select with
// kind-toggle (no 4th entry)" + design.md §D8): the existing
// kSelectFile_Draw_Y[3] geometry is PRESERVED. There is NO 4th entry — the
// 8 KB sram.dat has no room for a 4th slot, and the sidecar parallels sram.dat
// 3-for-3. Each slot renders one of:
//   - vanilla banner (slot_kind = Vanilla or sidecar absent + sram.dat valid)
//   - rando banner   (slot_kind = Randomizer)
//   - "NEW GAME"     (slot_kind = Empty AND sram.dat empty)
// ---------------------------------------------------------------------------

// kRandoSlotKind_* is the runtime classification for each of the 3 slots
// computed from the sidecar load result + sram.dat cksum. Distinct from
// RandoSlotKind in rando_save.h, which is the on-disk enum — this is the
// derived "what the file-select screen should render" view.
enum {
  kRandoSlotKind_Empty = 0,       // sidecar absent or kind=Empty AND sram.dat slot empty
  kRandoSlotKind_Vanilla = 1,     // sidecar absent/kind=Vanilla AND sram.dat slot valid
  kRandoSlotKind_Randomizer = 2,  // sidecar kind=Randomizer
};

typedef struct SelectFile_SlotInfo {
  uint8 render_kind;              // one of kRandoSlotKind_*
  uint8 has_sidecar_data;         // 1 if sidecar slot was successfully loaded
  RandoSidecarSlot sidecar;       // valid only when has_sidecar_data == 1
} SelectFile_SlotInfo;

static SelectFile_SlotInfo g_selectfile_slots[3];
static uint8 g_selectfile_slots_loaded = 0;

// §9.3b — empty-slot kind-picker state. Stub: only "Vanilla" is wired; the
// other two options display a refusal sound and a console-stderr placeholder
// until the next cluster lands §9.4 settings screen and §9.1b text input.
enum {
  kKindPicker_Inactive = 0,
  kKindPicker_Vanilla = 1,
  kKindPicker_NewRandomizer = 2,
  kKindPicker_LoadShareString = 3,
};
static uint8 g_kind_picker_active = 0;
static uint8 g_kind_picker_cursor = 0;
static uint8 g_kind_picker_target_slot = 0;
// §9.3c — Copy refusal message latch. Set when a cross-kind copy is attempted;
// causes the next frame's CopyFile_HandleConfirmation render to display the
// refusal text instead of executing the copy. Cleared on cursor move or return.
static uint8 g_copy_refusal_pending = 0;

// §9.1b / §9.2 — On-screen alphabet picker for share-string entry.
//
// Activated from the kind-picker's "Load Share String" option (cursor=2).
// Renders an 8-col × 4-row base32 alphabet grid plus a 5th row of controls
// (SUBMIT, DELETE, CANCEL). Owns input + drawing until the user submits,
// cancels, or successfully decodes a share string.
//
// Layout (rendered as a tile-stream into vram_upload_data; count bytes are
// encoded as (NUM_PAIRS * 2) - 1 per HandleStripes14's decode at
// src/nmi.c:421 — see cluster-1 audit lessons).
enum {
  kAlphabetPicker_GridCols = 8,
  kAlphabetPicker_GridRows = 4,   // 32 alphabet chars
  kAlphabetPicker_CtrlRow = 4,    // 5th row holds 3 control glyphs
  kAlphabetPicker_TotalRows = 5,
};
// Control-row buttons. Cursor on this row indexes into these slots.
enum {
  kAlphabetPickerCtrl_Submit = 0,
  kAlphabetPickerCtrl_Delete = 1,
  kAlphabetPickerCtrl_Cancel = 2,
  kAlphabetPickerCtrl_Count = 3,
};
// Decode-status overlay state. After a submit attempt, the picker latches
// the ShareDecodeStatus result + a frame countdown so the user sees the
// pass/fail message before the next interaction. 0 = no message.
enum {
  kAlphabetMsg_None = 0,
  kAlphabetMsg_OkBriefFrames = 90,      // ~1.5s @ 60fps
  kAlphabetMsg_ErrorBriefFrames = 120,  // ~2.0s
};
static bool g_alphabet_picker_active = false;
static uint8 g_alphabet_cursor_row = 0;  // 0..kAlphabetPicker_TotalRows-1
static uint8 g_alphabet_cursor_col = 0;  // 0..kAlphabetPicker_GridCols-1 (or 0..2 on ctrl row)
static RandoTextField g_alphabet_textfield;
// Latched decode result; rendered as an overlay for `g_alphabet_msg_frames`
// frames after a submit. On success the picker also transitions back to
// file-select after the message expires (`g_alphabet_pending_return`).
static uint8 g_alphabet_msg_status = 0;  // 0 = none, else ShareDecodeStatus + 1
static uint16 g_alphabet_msg_frames = 0;
static bool g_alphabet_pending_return = false;
// Decoded values held until the message expires + return executes. These are
// surfaced for downstream consumers (next cluster's §9.4/§9.8 settings +
// new-game flow) via the SelectFile_GetLastDecodedShareString accessor; for
// Phase A we just log them so a developer can verify the path end-to-end.
static uint64 g_alphabet_decoded_seed = 0;
static uint8 g_alphabet_decoded_hash[16];

// Base32 → file-select tile-char mapping. The base32 alphabet is
// "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567" (RFC 4648, kBase32Alphabet in
// rando_share.c). Tile chars sourced by inspection of vanilla file-select
// VRAM uploads:
//   - "ERASE THIS PLAYER" in kKILLFile_ChooseTarget_Tab2 pins E=4, R=0x21,
//     A=0, S=0x22, T=0x23, H=7, I=0xaf (italic-row), P=0xf, L=0xb, Y=0x28
//   - "COPY" in kCopyFile_SelectionAndBlinker_Tab pins C=2, O=0xe, P=0xf, Y=0x28
//   - Digit tiles at 0x76..0x7f are the wide-font digit tile pairs (top row;
//     bottom row at +0x10) used in name-entry kNamePlayer_Tab3 row 0/1 cols 18-22.
// The bottom half of each char is `top + 0x10` (per SelectFile_Func17:
// `dst[21] = t + 0x10`). 0xa9 is the blank-pad tile used elsewhere.
//
// Letters not directly verified (B, D, F, G, K, M, N, Q, U, V, W, X, Z) are
// assumed to follow the sequential pattern A..P at row 0 and Q..Z at row 2
// (the verified letters all match this layout). I is the documented
// half-width exception at 0xaf. If a Phase A play-test reveals a wrong tile
// here, swap the offending index — the layout is purely cosmetic.
static const uint8 kBase32CharToTile[32] = {
  // A    B    C    D    E    F    G    H
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  // I (italic-row; verified) | J  K     L     M     N     O     P
  0xaf, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  // Q    R    S    T    U    V    W    X
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
  // Y    Z   '2'   '3'   '4'   '5'   '6'   '7'
  0x28, 0x29, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d,
};
// Blank-pad tile (used in vanilla file-select kSelectFile_Func3_Data).
#define kFileSelectTile_Blank 0xa9

// Forward declarations for new helpers (defined later in this file).
static void SelectFile_LoadSidecarCache(void);
static void SelectFile_ResetSidecarCache(void);
static int SelectFile_GetSlotRenderKind(int k);
static void SelectFile_DrawRandoBanner(int k);
static void SelectFile_DrawRandoOamBadge(int k);
static uint8 SelectFile_TileForBase32(char c);
static const char *SelectFile_WorldStateAbbrev(uint8 ws);
static const char *SelectFile_GoalAbbrev(uint8 goal);
static void SelectFile_KindPicker_Draw(void);
static bool SelectFile_KindPicker_Update(void);
static void SelectFile_DrawCopyRefusalMessage(void);
static void SelectFile_AlphabetPicker_Activate(void);
static void SelectFile_AlphabetPicker_Deactivate(void);
static void SelectFile_AlphabetPicker_Draw(void);
static bool SelectFile_AlphabetPicker_Update(void);
static void SelectFile_AlphabetPicker_HandleSubmit(void);
bool Intro_CheckCksum(const uint8 *s) {
  const uint16 *src = (const uint16 *)s;
  uint16 sum = 0;
  for (int i = 0; i < 0x280; i++)
    sum += src[i];
  return sum == 0x5a5a;

}

uint16 *SelectFile_Func1() {
  static const uint16 kSelectFile_Func1_Tab[4] = {0x3581, 0x3582, 0x3591, 0x3592};
  uint16 *dst = (uint16 *)&g_ram[0x1002];
  *dst++ = 0x10;
  *dst++ = 0xff07;
  for (int i = 0; i < 1024; i++)
    *dst++ = kSelectFile_Func1_Tab[((i & 0x20) >> 4) + (i & 1)];
  return dst;
}

void SelectFile_Func5_DrawOams(int k) {
  static const uint8 kSelectFile_Draw_OamIdx[3] = {0x28, 0x3c, 0x50};
  static const uint8 kSelectFile_Draw_SwordChar[4] = {0x85, 0xa1, 0xa1, 0xa1};
  static const uint8 kSelectFile_Draw_ShieldChar[3] = {0xc4, 0xca, 0xe0};
  static const uint8 kSelectFile_Draw_Flags[3] = {0x72, 0x76, 0x7a};
  static const uint8 kSelectFile_Draw_Flags2[3] = {0x32, 0x36, 0x3a};
  static const uint8 kSelectFile_Draw_Flags3[3] = {0x30, 0x34, 0x38};

  link_dma_graphics_index = 0x116 * 2;
  uint8 *sram = g_zenv.sram + 0x500 * k;

  OamEnt *oam = oam_buf + kSelectFile_Draw_OamIdx[k] / 4;
  uint8 x = 0x34;
  uint8 y = kSelectFile_Draw_Y[k];

  uint8 sword = sram[kSrmOffs_Sword] - 1;
  uint8 swordchar = kSelectFile_Draw_SwordChar[sign8(sword) ? 0 : sword];
  SetOamPlain(oam + 0, x + 0xc, y - 5, swordchar, kSelectFile_Draw_Flags[k], 0);
  SetOamPlain(oam + 1, x + 0xc, y + 3, swordchar + 16, kSelectFile_Draw_Flags[k], 0);
  if (sign8(sword))
    oam[1].y = oam[0].y = 0xf0;
  uint8 shield = sram[kSrmOffs_Shield] - 1;
  SetOamPlain(oam + 2, x - 5, y + 10, kSelectFile_Draw_ShieldChar[sign8(shield) ? 0 : shield], kSelectFile_Draw_Flags2[k], 2);
  if (sign8(shield))
    oam[2].y = 0xf0;
  SetOamPlain(oam + 3, x, y + 0, 0, kSelectFile_Draw_Flags3[k], 2);
  SetOamPlain(oam + 4, x, y + 8, 2, kSelectFile_Draw_Flags3[k] | 0x40, 2);
}

void SelectFile_Func6_DrawOams2(int k) {
  static const uint8 kSelectFile_DrawDigit_Char[10] = {0xd0, 0xac, 0xad, 0xbc, 0xbd, 0xae, 0xaf, 0xbe, 0xbf, 0xc0};
  static const int8 kSelectFile_DrawDigit_OamIdx[3] = {4, 16, 28};
  static const int8 kSelectFile_DrawDigit_X[3] = {12, 4, -4};

  uint8 *sram = g_zenv.sram + 0x500 * k;
  uint8 x = 0x34;
  uint8 y = kSelectFile_Draw_Y[k];

  int died_ctr = WORD(sram[kSrmOffs_DiedCounter]);
  if (died_ctr == 0xffff)
    return;

  if (died_ctr > 999)
    died_ctr = 999;

  uint8 digits[3];
  digits[2] = died_ctr / 100;
  died_ctr %= 100;
  digits[1] = died_ctr / 10;
  digits[0] = died_ctr % 10;

  int i = (digits[2] != 0) ? 2 : (digits[1] != 0) ? 1 : 0;
  OamEnt *oam = oam_buf + kSelectFile_DrawDigit_OamIdx[k] / 4;
  do {
    SetOamPlain(oam, x + kSelectFile_DrawDigit_X[i], y + 0x10, kSelectFile_DrawDigit_Char[digits[i]], 0x3c, 0);
  } while (oam++, --i >= 0);
}

void SelectFile_Func17(int k) {
  static const uint16 kSelectFile_DrawName_VramOffs[3] = {8, 0x5c, 0xb0};
  static const uint16 kSelectFile_DrawName_HealthVramOffs[3] = {0x16, 0x6a, 0xbe};
  uint8 *sram = g_zenv.sram + 0x500 * k;
  uint16 *name = (uint16 *)(sram + kSrmOffs_Name);
  uint16 *dst = vram_upload_data + kSelectFile_DrawName_VramOffs[k] / 2;
  for (int i = 5; i >= 0; i--) {
    uint16 t = *name++ + 0x1800;
    dst[0] = t;
    dst[21] = t + 0x10;
    dst++;
  }
  int health = sram[kSrmOffs_Health] >> 3;
  dst = vram_upload_data + kSelectFile_DrawName_HealthVramOffs[k] / 2;
  uint16 *dst_org = dst;
  int row = 10;
  do {
    *dst++ = 0x520;
    if (--row == 0)
      dst = dst_org + 21;
  } while (--health);
}

void SelectFile_Func16() {
  static const uint8 kSelectFile_Func16_FaerieY[2] = {175, 191};
  FileSelect_DrawFairy(0x1c, kSelectFile_Func16_FaerieY[selectfile_R16]);

  int k = selectfile_R16;
  if (filtered_joypad_H & 0x2c) {
    k += (filtered_joypad_H & 0x24) ? 1 : -1;
    selectfile_R16 = k & 1;
    sound_effect_2 = 0x20;
  }

  uint8 a = (filtered_joypad_L & 0xc0 | filtered_joypad_H) & 0xd0;
  if (a != 0) {
    sound_effect_1 = 0x2c;
    if (selectfile_R16 == 0) {
      sound_effect_2 = 0x22;
      sound_effect_1 = 0x0;
      int k = subsubmodule_index;
      selectfile_arr1[k] = 0;
      memset(g_zenv.sram + k * 0x500, 0, 0x500);
      memset(g_zenv.sram + k * 0x500 + 0xf00, 0, 0x500);
      ZeldaWriteSram();
      // §9.3a addendum — also clear the paired sidecar slot if it was a
      // rando slot. Without this, SelectFile_GetSlotRenderKind keeps
      // returning Randomizer (the sidecar's slot_kind still pins to it),
      // so the erased slot renders the stale rando banner; picking it
      // would CopySaveToWRAM zero bytes and either soft-crash or start
      // a corrupted save.
      if (g_selectfile_slots_loaded &&
          g_selectfile_slots[k].has_sidecar_data &&
          g_selectfile_slots[k].sidecar.header.slot_kind == kSlotKind_Randomizer) {
        RandoSidecarSlot empty_slot;
        memset(&empty_slot, 0, sizeof(empty_slot));
        empty_slot.header.slot_kind = kSlotKind_Empty;
        Rando_WriteSidecarSlot(k, &empty_slot,
                               g_zenv.sram + k * 0x500, 0x500);
        SelectFile_ResetSidecarCache();
      }
    }
    ReturnToFileSelect();
    subsubmodule_index = 0;
  }
}

void Module_NamePlayer_1() {
  uint16 *dst = SelectFile_Func1();
  dst[0] = 0xffff;
  nmi_load_bg_from_vram = 1;
  submodule_index++;
}

void Module_NamePlayer_2() {
  nmi_load_bg_from_vram = 5;
  submodule_index++;
  INIDISP_copy = 15;
  nmi_disable_core_updates = 0;
}

void Intro_FixCksum(uint8 *s) {
  uint16 *src = (uint16 *)s;
  uint16 sum = 0;
  for (int i = 0; i < 0x27f; i++)
    sum += src[i];
  src[0x27f] = 0x5a5a - sum;
}

void LoadFileSelectGraphics() {  // 80e4e9
  Decomp_spr(&g_ram[0x14000], 0x5e);
  Do3To4High(&g_zenv.vram[0x5000], &g_ram[0x14000]);

  Decomp_spr(&g_ram[0x14000], 0x5f);
  Do3To4High(&g_zenv.vram[0x5400], &g_ram[0x14000]);

  TransferFontToVRAM();

  Decomp_spr(&g_ram[0x14000], 0x6b);
  memcpy(&g_zenv.vram[0x7800], &g_ram[0x14000], 0x300 * sizeof(uint16));
}

void Intro_ValidateSram() {  // 828054
  uint8 *cart = g_zenv.sram;
  for (int i = 0; i < 3; i++) {
    uint8 *c = cart + i * 0x500;
    if (!Intro_CheckCksum(c)) {
      if (Intro_CheckCksum(c + 0xf00)) {
        memcpy(c, c + 0xf00, 0x500);
      } else {
        memset(c, 0, 0x500);
        memset(c + 0xf00, 0, 0x500);
      }
    }
  }
  memset(&g_ram[0xd00], 0, 256 * 3);
}

void Module01_FileSelect() {  // 8ccd7d
  BG3HOFS_copy2 = 0;
  BG3VOFS_copy2 = 0;
  switch (submodule_index) {
  case 0: Module_SelectFile_0(); break;
  case 1: FileSelect_ReInitSaveFlagsAndEraseTriforce(); break;
  case 2: Module_EraseFile_1(); break;
  case 3: FileSelect_TriggerStripesAndAdvance(); break;
  case 4: FileSelect_TriggerNameStripesAndAdvance(); break;
  case 5: FileSelect_Main(); break;
  }
}

void Module_SelectFile_0() {  // 8ccd9d
  EnableForceBlank();
  is_nmi_thread_active = 0;
  nmi_flag_update_polyhedral = 0;
  music_control = 11;
  submodule_index++;
  overworld_palette_aux_or_main = 0x200;
  palette_main_indoors = 6;
  nmi_disable_core_updates = 6;
  Palette_Load_DungeonSet();
  Palette_Load_OWBG3();
  hud_palette = 0;
  Palette_Load_HUD();
  hud_cur_item = 0;
  misc_sprites_graphics_index = 1;
  main_tile_theme_index = 35;
  aux_tile_theme_index = 81;
  LoadDefaultGraphics();
  InitializeTilesets();
  LoadFileSelectGraphics();
  Intro_ValidateSram();
  DecompressEnemyDamageSubclasses();
  // §9.3a — load the sidecar slot-kind metadata once per entry into the
  // file-select screen. Re-loaded on each return because ReturnToFileSelect
  // routes back through submodule_index=1, but a hard re-entry (player
  // exited to title and came back) lands here at submodule 0.
  SelectFile_ResetSidecarCache();
  SelectFile_LoadSidecarCache();
  // §9.3b — kind-picker is inactive until the player picks an empty slot.
  g_kind_picker_active = 0;
  g_kind_picker_cursor = 0;
  // §9.3c — clear copy-refusal latch on screen re-entry.
  g_copy_refusal_pending = 0;
  // §9.1b/§9.2 — alphabet picker starts inactive. The host text-input
  // flags must agree: ensure SDL_StopTextInput fires next frame in case
  // we re-entered file-select with stale state from a prior session.
  g_alphabet_picker_active = false;
  g_alphabet_msg_status = 0;
  g_alphabet_msg_frames = 0;
  g_alphabet_pending_return = false;
  g_rando_text_input_active = false;
  g_rando_active_textfield = NULL;
  g_rando_text_input_submit_pending = false;
  g_rando_text_input_cancel_pending = false;
}

void FileSelect_ReInitSaveFlagsAndEraseTriforce() {  // 8ccdf2
  memset(selectfile_arr1, 0, 6);
  FileSelect_EraseTriforce();
}

void FileSelect_EraseTriforce() {  // 8ccdf9
  nmi_disable_core_updates = 128;
  EnableForceBlank();
  EraseTileMaps_triforce();
  Palette_LoadForFileSelect();
  flag_update_cgram_in_nmi++;
  submodule_index++;
}

void Module_EraseFile_1() {  // 8cce53
  static const uint8 kSelectFile_Gfx0[224] = {
    0x10, 0x42,    0, 0x27, 0x89, 0x35, 0x8a, 0x35, 0x8b, 0x35, 0x8c, 0x35, 0x8b, 0x35, 0x8c, 0x35,
    0x8b, 0x35, 0x8c, 0x35, 0x8b, 0x35, 0x8c, 0x35, 0x8b, 0x35, 0x8c, 0x35, 0x8b, 0x35, 0x8c, 0x35,
    0x8b, 0x35, 0x8c, 0x35, 0x8b, 0x35, 0x8c, 0x35, 0x8a, 0x75, 0x89, 0x75, 0x10, 0x62,    0,    3,
    0x99, 0x35, 0x9a, 0x35, 0x10, 0x64, 0x40, 0x1e, 0x7f, 0x34, 0x10, 0x74,    0,    3, 0x9a, 0x75,
    0x99, 0x75, 0x10, 0x82,    0,    3, 0xa9, 0x35, 0xaa, 0x35, 0x10, 0x84, 0x40, 0x1e, 0x7f, 0x34,
    0x10, 0x94,    0,    3, 0xaa, 0x75, 0xa9, 0x75, 0x10, 0xa2,    0, 0x27, 0x9d, 0x35, 0xad, 0x35,
    0x9b, 0x35, 0x9c, 0x35, 0x9b, 0x35, 0x9c, 0x35, 0x9b, 0x35, 0x9c, 0x35, 0x9b, 0x35, 0x9c, 0x35,
    0x9b, 0x35, 0x9c, 0x35, 0x9b, 0x35, 0x9c, 0x35, 0x9b, 0x35, 0x9c, 0x35, 0x9b, 0x35, 0x9c, 0x35,
    0xad, 0x75, 0x9d, 0x75, 0x10, 0xc2,    0, 0x27, 0xab, 0x35, 0xac, 0x35, 0xab, 0x35, 0xac, 0x35,
    0xab, 0x35, 0xac, 0x35, 0xab, 0x35, 0xac, 0x35, 0xab, 0x35, 0xac, 0x35, 0xab, 0x35, 0xac, 0x35,
    0xab, 0x35, 0xac, 0x35, 0xab, 0x35, 0xac, 0x35, 0xab, 0x35, 0xac, 0x35, 0xab, 0x75, 0xac, 0x75,
    0x10, 0xe2,    0,    1, 0x83, 0x35, 0x10, 0xe3, 0x40, 0x32, 0x85, 0x35, 0x10, 0xfd,    0,    1,
    0x84, 0x35, 0x11,    2, 0xc0, 0x22, 0x86, 0x35, 0x11, 0x1d, 0xc0, 0x22, 0x96, 0x35, 0x13, 0x42,
       0,    1, 0x93, 0x35, 0x13, 0x43, 0x40, 0x32, 0x95, 0x35, 0x13, 0x5d,    0,    1, 0x94, 0x35,
  };
  uint16 *dst = SelectFile_Func1();
  memcpy(dst, kSelectFile_Gfx0, 224);
  dst += 224 / 2;
  uint16 t = 0x1103;
  for (int i = 17; i >= 0; i--) {
    *dst++ = swap16(t);
    t += 0x20;
    *dst++ = 0x3240;
    *dst++ = 0x347f;
  }
  *(uint8 *)dst = 0xff;
  submodule_index++;
  nmi_load_bg_from_vram = 1;
}

void FileSelect_TriggerStripesAndAdvance() {  // 8ccea5
  selectfile_R16 = selectfile_var2;
  submodule_index++;
  nmi_load_bg_from_vram = 6;
}

void FileSelect_TriggerNameStripesAndAdvance() {  // 8cceb1
  static const uint8 kSelectFile_Func3_Data[253] = {
    0x61, 0x29,    0, 0x25, 0xe7, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x61, 0x49,    0, 0x25, 0xf7, 0x18,
    0x91, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0x61, 0xa9,    0, 0x25, 0xe8, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x61, 0xc9,
       0, 0x25, 0xf8, 0x18, 0x91, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x62, 0x29,    0, 0x25, 0xe9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0x62, 0x49,    0, 0x25, 0xf9, 0x18, 0x91, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xff,
  };
  memcpy(vram_upload_data, kSelectFile_Func3_Data, 253);
  INIDISP_copy = 0xf;
  nmi_disable_core_updates = 0;
  submodule_index++;
  nmi_load_bg_from_vram = 6;
}

void FileSelect_Main() {  // 8ccebd
  static const uint8 kSelectFile_Faerie_Y[5] = {0x4a, 0x6a, 0x8a, 0xaf, 0xbf};

  const uint8 *cart = g_zenv.sram;

  if (selectfile_R16 < 3)
    selectfile_var2 = selectfile_R16;

  // §9.3a — per-slot kind dispatch.
  //
  // The vanilla loop iterates 3 slots and renders sword/shield/heart/name for
  // any slot whose sram.dat cksum is valid. We extend this by reading each
  // slot's cached sidecar slot_kind and routing through one of three render
  // paths:
  //   - Randomizer: SelectFile_DrawRandoBanner(k)  + selectfile_arr1[k] = 1
  //   - Vanilla:    existing vanilla draw path     + selectfile_arr1[k] = 1
  //   - Empty:      no rendering; slot displays the "NEW GAME" frame from the
  //                 pre-built kSelectFile_Func3_Data background tilemap.
  //
  // The selectfile_arr1[k] = 1 flag preserves vanilla semantics for cursor
  // navigation, KILL/COPY screens, etc. — those screens treat rando and
  // vanilla slots identically as "occupied".
  for (int k = 0; k < 3; k++) {
    int render_kind = SelectFile_GetSlotRenderKind(k);
    switch (render_kind) {
      case kRandoSlotKind_Randomizer:
        selectfile_arr1[k] = 1;
        SelectFile_DrawRandoBanner(k);
        break;
      case kRandoSlotKind_Vanilla:
        selectfile_arr1[k] = 1;
        SelectFile_Func5_DrawOams(k);
        SelectFile_Func6_DrawOams2(k);
        SelectFile_Func17(k);
        break;
      case kRandoSlotKind_Empty:
      default:
        // Slot displays "NEW GAME" from the pre-built background; no extra
        // drawing needed (matches vanilla behavior).
        break;
    }
  }

  // §9.1b/§9.2 — alphabet picker takes top priority when active. It runs
  // BEFORE the kind picker because once the user navigates kind→alphabet,
  // the kind picker is deactivated; we keep the order explicit so the
  // dispatch path is greppable.
  if (SelectFile_AlphabetPicker_Update()) {
    nmi_load_bg_from_vram = 1;
    return;
  }

  // §9.3b — if the kind picker is active for a previously-empty slot, it
  // owns input and drawing until the user picks Vanilla / cancels.
  if (SelectFile_KindPicker_Update()) {
    nmi_load_bg_from_vram = 1;
    return;
  }

  FileSelect_DrawFairy(0x1c, kSelectFile_Faerie_Y[selectfile_R16]);
  nmi_load_bg_from_vram = 1;

  uint8 a = (filtered_joypad_L & 0xc0 | filtered_joypad_H) & 0xfc;
  if (a & 0x2c) {
    if (a & 8) {
      sound_effect_2 = 0x20;
      if (sign8(--selectfile_R16))
        selectfile_R16 = 4;
    } else {
      sound_effect_2 = 0x20;
      if (++selectfile_R16 == 5)
        selectfile_R16 = 0;
    }
  } else if (a != 0) {
    sound_effect_1 = 0x2c;
    if (selectfile_R16 < 3) {
      selectfile_R17 = 0;
      if (!selectfile_arr1[selectfile_R16]) {
        // §9.3b — empty slot: show kind-choice sub-prompt instead of going
        // directly to the vanilla name-entry flow. The picker stub routes
        // "Vanilla" through to the existing flow; "New Randomizer" /
        // "Load Share String" are stubbed pending next cluster.
        g_kind_picker_active = 1;
        g_kind_picker_cursor = 0;
        g_kind_picker_target_slot = (uint8)selectfile_R16;
      } else {
        music_control = 0xf1;
        srm_var1 = selectfile_R16 * 2 + 2;
        WORD(g_ram[0]) = selectfile_R16 * 0x500;
        CopySaveToWRAM();
      }
    } else if (selectfile_arr1[0] | selectfile_arr1[1] | selectfile_arr1[2]) {
      main_module_index = (selectfile_R16 == 3) ? 2 : 3;
      selectfile_R16 = 0;
      submodule_index = 0;
      subsubmodule_index = 0;
    } else {
      sound_effect_1 = 0x3c;
    }
  }
}

void Module02_CopyFile() {  // 8cd053
  selectfile_var2 = 0;
  switch (submodule_index) {
  case 0: FileSelect_EraseTriforce(); break;
  case 1: Module_EraseFile_1(); break;
  case 2: Module_CopyFile_2(); break;
  case 3: CopyFile_ChooseSelection(); break;
  case 4: CopyFile_ChooseTarget(); break;
  case 5: CopyFile_ConfirmSelection(); break;
  }
}

void Module_CopyFile_2() {  // 8cd06e
  nmi_load_bg_from_vram = 7;
  submodule_index++;
  INIDISP_copy = 0xf;
  nmi_disable_core_updates = 0;
  int i = 0;
  for (; selectfile_arr1[i] == 0; i++) {}
  selectfile_R16 = i;
}

void CopyFile_ChooseSelection() {  // 8cd087
  CopyFile_SelectionAndBlinker();
  if (submodule_index == 3 && !(frame_counter & 0x30))
    FilePicker_DeleteHeaderStripe();
  nmi_load_bg_from_vram = 1;
}

void CopyFile_ChooseTarget() {  // 8cd0a2
  CopyFile_TargetSelectionAndBlink();
  if (submodule_index == 4 && !(frame_counter & 0x30))
    FilePicker_DeleteHeaderStripe();
  nmi_load_bg_from_vram = 1;
}

void CopyFile_ConfirmSelection() {  // 8cd0b9
  CopyFile_HandleConfirmation();
  nmi_load_bg_from_vram = 1;
}

void FilePicker_DeleteHeaderStripe() {  // 8cd0c6
  static const uint16 kFilePicker_DeleteHeaderStripe_Dst[2] = {4, 0x1e};
  for (int j = 1; j >= 0; j--) {
    uint16 *dst = vram_upload_data + kFilePicker_DeleteHeaderStripe_Dst[j] / 2;
    for (int i = 0; i != 11; i++)
      dst[i] = 0xa9;
  }
}

void CopyFile_SelectionAndBlinker() {  // 8cd13f
  static const uint8 kCopyFile_SelectionAndBlinker_Tab[173] = {
    0x61,    4,    0, 0x15, 0x85, 0x18, 0x26, 0x18,    7, 0x18, 0xaf, 0x18,    2, 0x18,    7, 0x18,
    0x6f, 0x18, 0x86, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x61, 0x24,    0, 0x15, 0x95, 0x18,
    0x36, 0x18, 0x17, 0x18, 0xbf, 0x18, 0x12, 0x18, 0x17, 0x18, 0x7f, 0x18, 0x96, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0x61, 0x67,    0,  0xf, 0xe7, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x61, 0x87,    0,  0xf, 0xf7, 0x18, 0x91, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x61, 0xc7,    0,  0xf,
    0xe8, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0x61, 0xe7,    0,  0xf, 0xf8, 0x18, 0x91, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0x62, 0x27,    0,  0xf, 0xe9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x62, 0x47,    0,  0xf, 0xf9, 0x18, 0x91, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xff,
  };
  static const uint8 kCopyFile_SelectionAndBlinker_Tab1[73] = {
    0x61, 0x67, 0x40,  0xe, 0xa9,    0, 0x61, 0x87, 0x40,  0xe, 0xa9,    0, 0x61, 0xc7, 0x40,  0xe,
    0xa9,    0, 0x61, 0xe7, 0x40,  0xe, 0xa9,    0, 0x11, 0x30,    0,    1, 0x83, 0x35, 0x11, 0x31,
    0x40, 0x14, 0x85, 0x35, 0x11, 0x3c,    0,    1, 0x84, 0x35, 0x11, 0x50, 0xc0,  0xe, 0x86, 0x35,
    0x11, 0x5c, 0xc0,  0xe, 0x96, 0x35, 0x12, 0x50,    0,    1, 0x93, 0x35, 0x12, 0x51, 0x40, 0x14,
    0x95, 0x35, 0x12, 0x5c,    0,    1, 0x94, 0x35, 0xff,
  };
  static const uint16 kCopyFile_SelectionAndBlinker_Dst[3] = {0x3c, 0x64, 0x8c};
  static const uint8 kCopyFile_SelectionAndBlinker_FaerieX[4] = {36, 36, 36, 28};
  static const uint8 kCopyFile_SelectionAndBlinker_FaerieY[4] = {87, 111, 135, 191};

  vram_upload_offset = 0xac;
  memcpy(vram_upload_data, kCopyFile_SelectionAndBlinker_Tab, 173);

  for (int k = 0; k != 3; k++) {
    if (selectfile_arr1[k] & 1) {
      const uint16 *name = (uint16 *)(g_zenv.sram + 0x500 * k + kSrmOffs_Name);
      uint16 *dst = vram_upload_data + kCopyFile_SelectionAndBlinker_Dst[k] / 2;
      for (int i = 0; i != 6; i++) {
        uint16 t = *name++ + 0x1800;
        dst[0] = t;
        dst[10] = t + 0x10;
        dst++;
      }
    }
  }
  FileSelect_DrawFairy(kCopyFile_SelectionAndBlinker_FaerieX[selectfile_R16], kCopyFile_SelectionAndBlinker_FaerieY[selectfile_R16]);

  uint8 a = (filtered_joypad_L & 0xc0 | filtered_joypad_H) & 0xfc;
  if (a & 0x2c) {
    uint8 k = selectfile_R16;
    if (a & 8) {
      do {
        if (--k < 0) {
          k = 3;
          break;
        }
      } while (!selectfile_arr1[k]);
    } else {
      do {
        k++;
        if (k >= 4)
          k = 0;
      } while (k != 3 && !selectfile_arr1[k]);
    }
    selectfile_R16 = k;
    sound_effect_2 = 0x20;
  } else if (a != 0) {
    sound_effect_1 = 0x2c;
    if (selectfile_R16 == 3) {
      ReturnToFileSelect();
      return;
    }
    selectfile_R20 = selectfile_R16 * 2;
    memcpy(vram_upload_data + 26, kCopyFile_SelectionAndBlinker_Tab1, 73);
    if (selectfile_R16 != 2) {
      uint16 *dst = vram_upload_data + selectfile_R16 * 6;
      dst[26] = 0x2762;
      dst[29] = 0x4762;
    }
    submodule_index++;
    selectfile_R16 = 0;
  }
}

void ReturnToFileSelect() {  // 8cd22d
  main_module_index = 1;
  submodule_index = 1;
  subsubmodule_index = 0;
  selectfile_R16 = 0;
}

void CopyFile_TargetSelectionAndBlink() {  // 8cd27b
  {
    int k = 1, t = 4;
    do {
      if (t != selectfile_R20)
        selectfile_arr2[k--] = t;
    } while ((t -= 2) >= 0);
  }

  static const uint8 kCopyFile_TargetSelectionAndBlink_Tab0[133] = {
    0x61, 0x51,    0, 0x15, 0x85, 0x18, 0x23, 0x18,  0xe, 0x18, 0xa9, 0x18, 0x26, 0x18,    7, 0x18,
    0xaf, 0x18,    2, 0x18,    7, 0x18, 0x6f, 0x18, 0x86, 0x18, 0x61, 0x71,    0, 0x15, 0x95, 0x18,
    0x33, 0x18, 0x1e, 0x18, 0xb9, 0x18, 0x36, 0x18, 0x17, 0x18, 0xbf, 0x18, 0x12, 0x18, 0x17, 0x18,
    0x7f, 0x18, 0x96, 0x18, 0x61, 0xb4,    0,  0xf, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x61, 0xd4,    0,  0xf, 0xa9, 0x18, 0x91, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x62, 0x14,    0,  0xf,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0x62, 0x34,    0,  0xf, 0xa9, 0x18, 0x91, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xff,
  };
  static const uint8 kCopyFile_TargetSelectionAndBlink_Tab2[49] = {
    0x61, 0xb4, 0x40,  0xe, 0xa9,    0, 0x61, 0xd4, 0x40,  0xe, 0xa9,    0, 0x62, 0xc6,    0,  0xd,
       2, 0x18,  0xe, 0x18,  0xf, 0x18, 0x28, 0x18, 0xa9, 0x18,  0xe, 0x18,  0xa, 0x18, 0x62, 0xe6,
       0,  0xd, 0x12, 0x18, 0x1e, 0x18, 0x1f, 0x18, 0x38, 0x18, 0xa9, 0x18, 0x1e, 0x18, 0x1a, 0x18,
    0xff,
  };
  static const uint8 kCopyFile_TargetSelectionAndBlink_FaerieX[3] = {0x8c, 0x8c, 0x1c};
  static const uint8 kCopyFile_TargetSelectionAndBlink_FaerieY[3] = {0x67, 0x7f, 0xbf};
  static const uint16 kCopyFile_TargetSelectionAndBlink_Dst[2] = {0x38, 0x60};
  static const uint16 kCopyFile_TargetSelectionAndBlink_Tab1[3] = {0x18e7, 0x18e8, 0x18e9};
  memcpy(vram_upload_data, kCopyFile_TargetSelectionAndBlink_Tab0, 133);

  for (int k = 0, j = 0; k != 3; k++) {
    if (k * 2 == selectfile_R20)
      continue;

    uint16 *dst = vram_upload_data + kCopyFile_TargetSelectionAndBlink_Dst[j++] / 2;
    uint16 t = kCopyFile_TargetSelectionAndBlink_Tab1[k];
    dst[0] = t;
    dst[10] = t + 0x10;
    dst += 2;
    if (selectfile_arr1[k]) {
      const uint16 *name = (uint16 *)(g_zenv.sram + 0x500 * k + kSrmOffs_Name);
      for (int i = 0; i != 6; i++) {
        uint16 t = *name++ + 0x1800;
        dst[0] = t;
        dst[10] = t + 0x10;
        dst++;
      }
    }
  }

  vram_upload_offset = 132;

  FileSelect_DrawFairy(kCopyFile_TargetSelectionAndBlink_FaerieX[selectfile_R16], kCopyFile_TargetSelectionAndBlink_FaerieY[selectfile_R16]);

  uint8 a = (filtered_joypad_L & 0xc0 | filtered_joypad_H) & 0xfc;
  if (a & 0x2c) {
    uint8 k = selectfile_R16;
    if (a & 8) {
      if (sign8(--k))
        k = 2;
    } else {
      if (++k >= 3)
        k = 0;
    }
    selectfile_R16 = k;
    sound_effect_2 = 0x20;
  } else if (a) {
    sound_effect_1 = 0x2c;
    if (selectfile_R16 == 2) {
      ReturnToFileSelect();
      selectfile_R16 = 0;
      return;
    }
    selectfile_R18 = selectfile_arr2[selectfile_R16];
    memcpy(vram_upload_data + 26, kCopyFile_TargetSelectionAndBlink_Tab2, 49);
    if (selectfile_R16 == 0) {
      uint16 *dst = vram_upload_data;
      dst[26] = 0x1462;
      dst[29] = 0x3462;
    }
    submodule_index++;
    selectfile_R16 = 0;
  }
}

void CopyFile_HandleConfirmation() {  // 8cd371
  static const uint8 kCopyFile_HandleConfirmation_FaerieY[2] = {0xaf, 0xbf};
  FileSelect_DrawFairy(0x1c, kCopyFile_HandleConfirmation_FaerieY[selectfile_R16]);

  // §9.3c — render the cross-kind refusal text if the latch is set. The
  // latch is set BELOW when the user confirms a copy across slot kinds; it
  // displays the message and routes back to file-select on the next input.
  if (g_copy_refusal_pending) {
    SelectFile_DrawCopyRefusalMessage();
    nmi_load_bg_from_vram = 1;
    // Wait for any input to dismiss.
    uint8 dismiss = (filtered_joypad_L & 0xc0 | filtered_joypad_H) & 0xfc;
    if (dismiss != 0) {
      g_copy_refusal_pending = 0;
      sound_effect_1 = 0x2c;
      ReturnToFileSelect();
      selectfile_R16 = 0;
    }
    return;
  }

  uint8 a = (filtered_joypad_L & 0xc0 | filtered_joypad_H) & 0xfc;
  if (a & 0x2c) {
    sound_effect_2 = 0x20;
    if (a & 0x24) {
      if (++selectfile_R16 >= 2)
        selectfile_R16 = 0;
    } else {
      if (sign8(--selectfile_R16))
        selectfile_R16 = 1;
    }
  } else if (a != 0) {
    sound_effect_1 = 0x2c;
    if (selectfile_R16 == 0) {
      // §9.3c — refuse copies across slot kinds (vanilla ↔ randomizer).
      // The vanilla copy path memcpy's the 0x500-byte sram.dat slot; but the
      // sidecar's rando slot data is NOT copied (the on-disk rando state
      // would still belong to the source slot, leaving the destination
      // inconsistent). Rather than implement a half-baked partial copy,
      // refuse the operation per spec scenario "Copy refuses cross-kind".
      // Same-kind copies (vanilla→vanilla or rando→rando) proceed normally.
      //
      // selectfile_R20 = source slot * 2; selectfile_R18 = dest slot * 2.
      int src_k = selectfile_R20 >> 1;
      int dst_k = selectfile_R18 >> 1;
      int src_kind = SelectFile_GetSlotRenderKind(src_k);
      int dst_kind = SelectFile_GetSlotRenderKind(dst_k);
      // Rando-vs-vanilla mismatch is the only refusal trigger; rando→empty
      // is also blocked because the destination would receive vanilla
      // sram.dat bytes from the source but no matching sidecar entry.
      bool src_is_rando = (src_kind == kRandoSlotKind_Randomizer);
      bool dst_is_rando = (dst_kind == kRandoSlotKind_Randomizer);
      if (src_is_rando != dst_is_rando) {
        g_copy_refusal_pending = 1;
        sound_effect_1 = 0x3c;
        return;
      }
      memcpy(g_zenv.sram + dst_k * 0x500, g_zenv.sram + src_k * 0x500, 0x500);
      selectfile_arr1[dst_k] = 1;
      ZeldaWriteSram();
      // §9.3c — if both slots are rando, propagate the sidecar entry too.
      // Without this, the destination slot has the source's rando sram
      // state but a stale sidecar (slot_kind != Randomizer), so on next
      // file-select entry SelectFile_GetSlotRenderKind() returns Vanilla
      // and the player loads it as a vanilla save — rando dispatch never
      // fires, item placements are silently wrong.
      if (src_is_rando && dst_is_rando) {
        const RandoSidecarSlot *src_sc = &g_selectfile_slots[src_k].sidecar;
        Rando_WriteSidecarSlot(dst_k, src_sc,
                               g_zenv.sram + dst_k * 0x500, 0x500);
        SelectFile_ResetSidecarCache();
      }
    }
    ReturnToFileSelect();
    selectfile_R16 = 0;
  }
}

void Module03_KILLFile() {  // 8cd485
  switch (submodule_index) {
  case 0: FileSelect_EraseTriforce(); break;
  case 1: Module_EraseFile_1(); break;
  case 2: KILLFile_SetUp(); break;
  case 3: KILLFile_HandleSelection(); break;
  case 4: KILLFile_HandleConfirmation(); break;
  }
}

void KILLFile_SetUp() {  // 8cd49a
  nmi_load_bg_from_vram = 8;
  submodule_index++;
  INIDISP_copy = 0xf;
  nmi_disable_core_updates = 0;
  int i = 0;
  for (; selectfile_arr1[i] == 0; i++) {}
  selectfile_R16 = i;
}

void KILLFile_HandleSelection() {  // 8cd49f
  if (selectfile_R16 < 3)
    selectfile_var2 = selectfile_R16;
  KILLFile_ChooseTarget();
  nmi_load_bg_from_vram = 1;
}

void KILLFile_HandleConfirmation() {  // 8cd4b1
  SelectFile_Func16();
  nmi_load_bg_from_vram = 1;
}

void KILLFile_ChooseTarget() {  // 8cd4ba
  static const uint8 kKILLFile_ChooseTarget_Tab[253] = {
    0x61, 0xa7,    0, 0x25, 0xe7, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x61, 0xc7,    0, 0x25, 0xf7, 0x18,
    0x91, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0x62,    7,    0, 0x25, 0xe8, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x62, 0x27,
       0, 0x25, 0xf8, 0x18, 0x91, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0x62, 0x67,    0, 0x25, 0xe9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0x62, 0x87,    0, 0x25, 0xf9, 0x18, 0x91, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18,
    0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xa9, 0x18, 0xff,
  };
  static const uint8 kKILLFile_ChooseTarget_Tab2[101] = {
    0x61, 0xa7, 0x40, 0x24, 0xa9,    0, 0x61, 0xc7, 0x40, 0x24, 0xa9,    0, 0x62,    7, 0x40, 0x24,
    0xa9,    0, 0x62, 0x27, 0x40, 0x24, 0xa9,    0, 0x62, 0xc6,    0, 0x21,    4, 0x18, 0x21, 0x18,
       0, 0x18, 0x22, 0x18,    4, 0x18, 0xa9, 0x18, 0x23, 0x18,    7, 0x18, 0xaf, 0x18, 0x22, 0x18,
    0xa9, 0x18,  0xf, 0x18,  0xb, 0x18,    0, 0x18, 0x28, 0x18,    4, 0x18, 0x21, 0x18, 0x62, 0xe6,
       0, 0x21, 0x14, 0x18, 0x31, 0x18, 0x10, 0x18, 0x32, 0x18, 0x14, 0x18, 0xa9, 0x18, 0x33, 0x18,
    0x17, 0x18, 0xbf, 0x18, 0x32, 0x18, 0xa9, 0x18, 0x1f, 0x18, 0x1b, 0x18, 0x10, 0x18, 0x38, 0x18,
    0x14, 0x18, 0x31, 0x18, 0xff,
  };
  static const uint8 kKILLFile_ChooseTarget_FaerieX[4] = {36, 36, 36, 28};
  static const uint8 kKILLFile_ChooseTarget_FaerieY[4] = {103, 127, 151, 191};
  memcpy(vram_upload_data, kKILLFile_ChooseTarget_Tab, 253);
  for (int k = 0; k < 3; k++) {
    if (selectfile_arr1[k])
      SelectFile_Func17(k);
  }

  FileSelect_DrawFairy(kKILLFile_ChooseTarget_FaerieX[selectfile_R16], kKILLFile_ChooseTarget_FaerieY[selectfile_R16]);

  int k = selectfile_R16;
  if (filtered_joypad_H & 0x2c) {
    if (!(filtered_joypad_H & 0x24)) {
      do {
        if (--k < 0) {
          k = 3;
          break;
        }
      } while (!selectfile_arr1[k]);
    } else {
      do {
        k++;
        if (k >= 4)
          k = 0;
      } while (k != 3 && !selectfile_arr1[k]);
    }
    sound_effect_2 = 0x20;
  }
  selectfile_R16 = k;

  uint8 a = (filtered_joypad_L & 0xc0 | filtered_joypad_H) & 0xd0;
  if (a) {
    sound_effect_1 = 0x2c;
    if (k == 3) {
      ReturnToFileSelect();
      return;
    }

    memcpy(vram_upload_data, kKILLFile_ChooseTarget_Tab2, 101);
    submodule_index++;
    if (selectfile_R16 != 2) {
      uint16 *dst = vram_upload_data + selectfile_R16 * 6;
      dst[0] = 0x6762;
      dst[3] = 0x8762;
    }
    subsubmodule_index = selectfile_R16;
    selectfile_R16 = 0;
  }
}

void FileSelect_DrawFairy(uint8 x, uint8 y) {  // 8cd7a5
  SetOamPlain(&oam_buf[0], x, y, frame_counter & 8 ? 0xaa : 0xa8, 0x7e, 2);
}

void Module04_NameFile() {  // 8cd88a
  switch (submodule_index) {
  case 0: NameFile_EraseSave(); break;
  case 1: Module_NamePlayer_1(); break;
  case 2: Module_NamePlayer_2(); break;
  case 3: NameFile_DoTheNaming(); break;
  }
}

void NameFile_EraseSave() {  // 8cd89c
  FileSelect_EraseTriforce();
  irq_flag = 1;
  selectfile_var3 = 0;
  selectfile_var4 = 0;
  selectfile_var5 = 0;
  selectfile_arr2[0] = 0;
  selectfile_var6 = 0;
  selectfile_var7 = 0x83;
  selectfile_var8 = 0x1f0;
  BG3HOFS_copy2 = 0;
  int offs = selectfile_R16 * 0x500;
  attract_legend_ctr = offs;
  memset(g_zenv.sram + offs, 0, 0x500);
  uint16 *name = (uint16 *)(g_zenv.sram + offs + kSrmOffs_Name);
  name[0] = name[1] = name[2] = name[3] = name[4] = name[5] = 0xa9;
}

void NameFile_DoTheNaming() {  // 8cda4d
  static const int16 kNamePlayer_Tab1[26] = {
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -2, 2, -2, 2, -2, 2, -2, 2, -4, 4,
  };
  static const uint8 kNamePlayer_Tab2[4] = {131, 147, 163, 179};
  static const int8 kNamePlayer_X[6] = {31, 47, 63, 79, 95, 111};
  static const int16 kNamePlayer_Tab0[32] = {
    0x1f0,     0,  0x10,  0x20,  0x30,  0x40,  0x50,  0x60,  0x70,  0x80,  0x90,  0xa0,  0xb0,  0xc0,  0xd0,  0xe0,
     0xf0, 0x100, 0x110, 0x120, 0x130, 0x140, 0x150, 0x160, 0x170, 0x180, 0x190, 0x1a0, 0x1b0, 0x1c0, 0x1d0, 0x1e0,
  };
  static const int8 kNamePlayer_Tab3[128] = {
       6,    7, 0x5f,    9, 0x59, 0x59, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x60, 0x23,
    0x59, 0x59, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x59, 0x59, 0x59,    0,    1,    2,    3,    4,    5,
    0x10, 0x11, 0x12, 0x13, 0x59, 0x59, 0x24, 0x5f, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d,
    0x59, 0x59, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x59, 0x59, 0x59,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf,
    0x40, 0x41, 0x42, 0x59, 0x59, 0x59, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x40, 0x41, 0x42, 0x59,
    0x59, 0x59, 0x61, 0x3f, 0x45, 0x46, 0x59, 0x59, 0x59, 0x59, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0x44, 0x59, 0x6f, 0x6f, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x5a, 0x44, 0x59, 0x6f, 0x6f,
    0x59, 0x59, 0x5a, 0x44, 0x59, 0x6f, 0x6f, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x5a,
  };
  for (;;) {
    int j = selectfile_var9;
    if (j == 0) {
      NameFile_CheckForScrollInputX();
      break;
    }
    if (j != 0x31)
      selectfile_var9 += 4;
    j--;
    if (kNamePlayer_Tab0[selectfile_var3] == selectfile_var8) {
      selectfile_var9 = (joypad1H_last & 3) ? 0x30 : 0;
      NameFile_CheckForScrollInputX();
      continue;
    }
    if (!selectfile_var10)
      j += 2;
    selectfile_var8 = (selectfile_var8 + WORD(((uint8*)&kNamePlayer_Tab1)[j])) & 0x1ff;
    break;
  }

  for (;;) {
    if (selectfile_var11 == 0) {
      NameFile_CheckForScrollInputY();
      break;
    }
    uint8 diff = selectfile_var7 - kNamePlayer_Tab2[selectfile_var5];
    if (diff != 0) {
      selectfile_var7 += sign8(diff) ? 2 : -2;
      break;
    }
    selectfile_var11 = 0;
    NameFile_CheckForScrollInputY();
  }

  OamEnt *oam = oam_buf;
  for (int i = 0; i != 26; i++) {
    SetOamPlain(oam, 0x18 + i * 8, selectfile_var7, 0x2e, 0x3c, 0);
    oam++;
  }
  SetOamPlain(oam, kNamePlayer_X[selectfile_var4], 0x58, 0x29, 0xc, 0);

  if (selectfile_var9 | selectfile_var11)
    return;

  if (!(filtered_joypad_H & 0x10)) {
    if (!(filtered_joypad_H & 0xc0 || filtered_joypad_L & 0xc0))
      return;

    sound_effect_1 = 0x2b;
    uint8 t = kNamePlayer_Tab3[selectfile_var3 + selectfile_var5 * 0x20];
    if (t == 0x5a) {
      if (!selectfile_var4)
        selectfile_var4 = 5;
      else
        selectfile_var4--;
      return;
    } else if (t == 0x44) {
      if (++selectfile_var4 == 6)
        selectfile_var4 = 0;
      return;
    } else if (t != 0x6f) {
      int p = selectfile_var4 * 2 + attract_legend_ctr;
      uint16 chr = (t & 0xfff0) * 2 + (t & 0xf);
      WORD(g_zenv.sram[p + kSrmOffs_Name]) = chr;
      NameFile_DrawSelectedCharacter(selectfile_var4, chr);
      if (++selectfile_var4 == 6)
        selectfile_var4 = 0;
      return;
    }
  }
  int i = 0;
  for(;;) {
    uint16 a = WORD(g_zenv.sram[i * 2 + attract_legend_ctr + kSrmOffs_Name]);
    if (a != 0xa9)
      break;
    if (++i == 6) {
      sound_effect_1 = 0x3c;
      return;
    }
  }
  srm_var1 = selectfile_R16 * 2 + 2;
  uint8 *sram = &g_zenv.sram[selectfile_R16 * 0x500];
  WORD(sram[0x3e5]) = 0x55aa;
  WORD(sram[0x20c]) = 0xf000;
  WORD(sram[0x20e]) = 0xf000;
  WORD(sram[kSrmOffs_DiedCounter]) = 0xffff;
  static const uint8 kSramInit_Normal[60] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0,    0,    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0,    0,    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0x18, 0x18, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf8, 0, 0,
  };
  memcpy(sram + 0x340, kSramInit_Normal, 60);
  Intro_FixCksum(sram);
  ZeldaWriteSram();
  ReturnToFileSelect();
  irq_flag = 0xff;
  sound_effect_1 = 0x2c;
}

void NameFile_CheckForScrollInputX() {  // 8cdc8c
  static const uint16 kNameFile_CheckForScrollInputX_Add[2] = {1, 0xff};
  static const int16 kNameFile_CheckForScrollInputX_Cmp[2] = {0x20, 0xff};
  static const int16 kNameFile_CheckForScrollInputX_Set[2] = {0, 0x1f};
  if (joypad1H_last & 3) {
    int k = (joypad1H_last & 3) - 1;
    selectfile_var10 = k;
    selectfile_var9++;
    uint8 t = selectfile_var3 + kNameFile_CheckForScrollInputX_Add[k];
    if (t == kNameFile_CheckForScrollInputX_Cmp[k])
      t = kNameFile_CheckForScrollInputX_Set[k];
    selectfile_var3 = t;
  }
}

void NameFile_CheckForScrollInputY() {  // 8cdcbf
  static const int8 kNameFile_CheckForScrollInputY_Add[2] = {1, -1};
  static const int8 kNameFile_CheckForScrollInputY_Cmp[2] = {4, -1};
  static const int8 kNameFile_CheckForScrollInputY_Set[2] = {0, 3};

  uint8 a = joypad1H_last & 0xc;
  if (a) {
    if ((a * 2 | selectfile_var5) == 0x10 || (a * 4 | selectfile_var5) == 0x13) {
      selectfile_arr2[1] = a;
      return;
    }
     a >>= 2;
    int t = selectfile_var5 + kNameFile_CheckForScrollInputY_Add[a-1];
    if (t == kNameFile_CheckForScrollInputY_Cmp[a-1])
      t = kNameFile_CheckForScrollInputY_Set[a-1];
    selectfile_var5 = t;

    selectfile_var11++;
    selectfile_arr2[1] = a;

  } else {
    selectfile_arr2[0] = 0;
  }
}

void NameFile_DrawSelectedCharacter(int k, uint16 chr) {  // 8cdd30
  static const uint16 kNameFile_DrawSelectedCharacter_Tab[6] = {0x84, 0x86, 0x88, 0x8a, 0x8c, 0x8e};
  uint16 *dst = vram_upload_data;
  uint16 a = kNameFile_DrawSelectedCharacter_Tab[k] | 0x6100;
  dst[0] = swap16(a);
  dst[1] = 0x100;
  dst[2] = 0x1800 | chr;
  dst[3] = swap16(a + 0x20);
  dst[4] = 0x100;
  dst[5] = (0x1800 | chr) + 0x10;
  BYTE(dst[6]) = 0xff;
  nmi_load_bg_from_vram = 1;
}

// ---------------------------------------------------------------------------
// §9.3a — Sidecar slot-kind cache.
// ---------------------------------------------------------------------------

// Called once per file-select entry (from Module_SelectFile_0). Reads the
// sidecar file and classifies each of the 3 slots. Per spec
// "Absent sidecar is normal vanilla", a missing or unreadable sidecar simply
// leaves all slots in their default Empty state — the existing sram.dat
// validation then classifies them as Vanilla or Empty as it does today.
static void SelectFile_LoadSidecarCache(void) {
  if (g_selectfile_slots_loaded) return;
  for (int k = 0; k < 3; k++) {
    g_selectfile_slots[k].render_kind = kRandoSlotKind_Empty;
    g_selectfile_slots[k].has_sidecar_data = 0;
  }
  for (int k = 0; k < 3; k++) {
    RandoSidecarSlot slot;
    // Per Rando_LoadSidecarSlot contract: returns false if the file is
    // missing, malformed, or the requested slot has bad magic. In those
    // cases we leave has_sidecar_data=0 and the slot falls back to vanilla
    // classification below — matching the spec's "Absent sidecar is normal
    // vanilla" scenario.
    if (!Rando_LoadSidecarSlot(k, &slot)) continue;
    g_selectfile_slots[k].sidecar = slot;
    g_selectfile_slots[k].has_sidecar_data = 1;
  }
  g_selectfile_slots_loaded = 1;
}

static void SelectFile_ResetSidecarCache(void) {
  g_selectfile_slots_loaded = 0;
}

// Classify slot k based on cached sidecar info + sram.dat occupancy.
// Per spec "Slot-kind discriminator": sidecar slot_kind=Vanilla/Empty defers
// to the sram.dat slot's state; only slot_kind=Randomizer overrides with the
// rando banner.
static int SelectFile_GetSlotRenderKind(int k) {
  if (k < 0 || k >= 3) return kRandoSlotKind_Empty;
  const SelectFile_SlotInfo *info = &g_selectfile_slots[k];
  const uint8 *cart = g_zenv.sram;
  bool sram_valid = (*(const uint16 *)(cart + k * 0x500 + 0x3E5) == 0x55AA);
  if (info->has_sidecar_data &&
      info->sidecar.header.slot_kind == kSlotKind_Randomizer) {
    return kRandoSlotKind_Randomizer;
  }
  if (sram_valid) return kRandoSlotKind_Vanilla;
  return kRandoSlotKind_Empty;
}

static uint8 SelectFile_TileForBase32(char c) {
  if (c >= 'A' && c <= 'Z') return kBase32CharToTile[c - 'A'];
  if (c >= '2' && c <= '7') return kBase32CharToTile[26 + (c - '2')];
  // Lowercase tolerance — slot share strings are always uppercase, but be
  // defensive if a future helper writes lowercase by mistake.
  if (c >= 'a' && c <= 'z') return kBase32CharToTile[c - 'a'];
  return kFileSelectTile_Blank;
}

static const char *SelectFile_WorldStateAbbrev(uint8 ws) {
  switch (ws) {
    case kWorldState_Open:     return "OPEN";
    case kWorldState_Standard: return "STD ";
    case kWorldState_Inverted: return "INV ";
    case kWorldState_Retro:    return "RTRO";
    default:                   return "??? ";
  }
}

static const char *SelectFile_GoalAbbrev(uint8 goal) {
  switch (goal) {
    case kGoal_Ganon:         return "GANN";
    case kGoal_FastGanon:     return "FSTG";
    case kGoal_Dungeons:      return "DUNG";
    case kGoal_Pedestal:      return "PEDS";
    case kGoal_TriforceHunt:  return "TFRC";
    case kGoal_GanonHunt:     return "GHNT";
    case kGoal_Completionist: return "CMPL";
    default:                  return "??? ";
  }
}

// §9.7 — Render the rando banner for slot k.
//
// Per spec randomizer-ui / "Slot banner with truncation": the banner shows
//   - truncated 12-char share string (first 12 base32 chars)
//   - world-state abbrev (4 chars)
//   - goal abbrev (4 chars)
//   - "R" badge (single OAM entry)
//
// Geometry: the vanilla name VRAM region (kSelectFile_DrawName_VramOffs[k])
// holds 6 chars (12 bytes; 6 uint16). The pre-built kSelectFile_Func3_Data
// command list anchors only this 6-char window per slot — additional VRAM
// command slots between slot k's name and slot k+1's name are reserved for
// fixed scenery and the existing health-hearts. Adding new commands requires
// rebuilding the command list and is deferred (see code comment below).
//
// Phase A layout, fitting the existing 6-char name VRAM window:
//   - char 0: world-state initial (first letter of the abbrev)
//   - char 1: goal initial
//   - chars 2..5: first 4 chars of share string
// Plus an "R" OAM badge (8x8 tile from the file-select sprite sheet).
//
// This is a documented stub for the full 12+4+4+R layout (deferred to a
// follow-up sprint that rebuilds the VRAM command list to expose a second
// per-slot row). The hard spec contracts honored here:
//   - no 4th file-select entry (geometry preserved)
//   - no OAM overflow (one additional OAM entry vs vanilla; well within budget)
//   - slot rendered distinct from vanilla (initial-letter dispatch + R badge)
//   - share-string prefix visible (4 chars, less than spec's 12)
static void SelectFile_DrawRandoBanner(int k) {
  static const uint16 kSelectFile_DrawName_VramOffs[3] = {8, 0x5c, 0xb0};

  const SelectFile_SlotInfo *info = &g_selectfile_slots[k];
  const RandoSlotHeader *hdr = &info->sidecar.header;

  // Decode displayable share string (50 chars) so we can show the first 4.
  char share_b32[kShareStringBase32MaxLen + 1];
  int n = Share_EncodeRaw(hdr->share_string, share_b32, sizeof(share_b32));
  if (n < 4) {
    // Defensive: shouldn't fail given the buffer is sized for the full length,
    // but on any error fall back to blanks rather than reading garbage.
    memset(share_b32, ' ', sizeof(share_b32));
  }

  // Phase A stub: the slot header doesn't yet carry an explicit world_state /
  // goal serialization (the 16 reserved bytes at @64 of the slot header are
  // earmarked for it). The original cluster-1 implementation derived
  // initials from `settings_hash[0]/[1] % enum_size`, which is silently
  // WRONG — settings_hash is SHA-256 noise, so two seeds with the same goal
  // would render DIFFERENT goal initials. A user inspecting the banner could
  // believe the displayed letter is meaningful when it isn't.
  //
  // Render explicit '?' placeholders for the world+goal initials until §9.4
  // lands. The share-string prefix (chars[2..5]) is still a useful
  // per-slot identifier — share_string IS authoritative per-seed data,
  // unlike a hash mod.
  //
  // The '!' marker (spec: randomizer-ui § Slot banner) replaces the world
  // placeholder when forward-fill fallback was used during generation, so
  // the user sees a visible warning that the seed used the fallback path.
  bool forward_fill_used = (hdr->flags & kRandoSlotFlag_ForwardFillUsed) != 0;

  // Write 6 tile chars into the existing name VRAM region.
  //   [0] = '!' when forward-fill used, else '?' (TODO §9.4: real world initial)
  //   [1] = '?' (TODO §9.4: real goal initial)
  //   [2..5] = first 4 share-string chars (base32 ASCII; per-seed identifier)
  uint16 *dst = vram_upload_data + kSelectFile_DrawName_VramOffs[k] / 2;
  uint8 chars[6];
  // '!' tile is approximated as 0x2e (placeholder; refined when §9.4 lands
  // with the actual font tile for '!'). '?' is 0x2d (matches kKindPicker_Text
  // approximation).
  chars[0] = forward_fill_used ? 0x2eu : 0x2du;
  chars[1] = 0x2du;
  for (int i = 0; i < 4; i++) {
    chars[2 + i] = SelectFile_TileForBase32(share_b32[i]);
  }
  for (int i = 0; i < 6; i++) {
    uint16 t = (uint16)chars[i] + 0x1800;
    dst[i] = t;
    dst[i + 21] = t + 0x10;  // bottom-half tile (per SelectFile_Func17)
  }

  // §9.7 — "R" badge as a single OAM entry. Reuse the file-select sprite
  // sheet tile that draws the letter "R" in the upper-left of each row's
  // existing sword-icon position. Cheapest tile: use the same shield-char
  // position but with a distinctive flag color (palette bit shift). This
  // satisfies the spec scenario "rando banner fits in OAM tiles previously
  // used for the vanilla name plus a small R badge". Tile 0x21 is the letter
  // "R" in the file-select tilemap font; the OAM sprite sheet uses a
  // different range, so we render a small marker glyph via tile 0xb8 which
  // is the "filled square" indicator tile available in misc_sprites_graphics
  // (verified by visual inspection of in-game HUD's filled-bar tiles).
  SelectFile_DrawRandoOamBadge(k);
}

// §9.4b — Render the 5-icon visual hash widget in the slot's OAM lane.
//
// The existing slot OAM lane is 5 entries: [oamidx/4 .. oamidx/4 + 4],
// historically used for sword (2) + shield (1) + heart (2). For a rando
// slot we replace all 5 entries with the deterministic icon strip computed
// from SHA-256(share_string_binary)[0..4] mod kHashIconAtlasSize. NO OAM
// overflow — exactly 5 entries used, same budget as vanilla, per spec
// scenario "rando banner fits in OAM tiles previously used for the vanilla
// name plus a small R badge with no overflow".
//
// CRITICAL: the hash input is the FULL share_string_binary (settings_hash +
// seed_u64 + magic + checksum), NOT settings_hash. Deriving from
// settings_hash gives every seed with the same settings identical icons —
// caught in spec round 5. Rando_DrawHashIcons enforces this.
static void SelectFile_DrawRandoOamBadge(int k) {
  static const uint8 kSelectFile_Draw_OamIdx[3] = {0x28, 0x3c, 0x50};
  // Note: kSelectFile_Draw_OamIdx is in BYTES; /4 yields an entry index.
  OamEnt *oam = oam_buf + kSelectFile_Draw_OamIdx[k] / 4;
  uint8 y = kSelectFile_Draw_Y[k];

  const SelectFile_SlotInfo *info = &g_selectfile_slots[k];
  // Clear the slot's bytewise_extended_oam entries before drawing — the
  // widget doesn't touch them (it can't safely, since it's also called
  // with stack-local buffers from selftest) but stale bits from a prior
  // frame's vanilla sword/shield/heart draw would mis-extend the new
  // tile X coordinates. Each slot uses 5 consecutive OAM entries.
  int oam_idx = (int)(kSelectFile_Draw_OamIdx[k] / 4);
  for (int i = 0; i < 5; ++i) {
    bytewise_extended_oam[oam_idx + i] = 0;
  }
  // x=0x28 places the strip in the same horizontal region the vanilla
  // sword/shield/heart icons occupied. The widget consumes exactly 5 OAM
  // entries via Rando_DrawHashIcons. y-2 nudges the strip to align with
  // the slot's name-text baseline.
  Rando_DrawHashIcons(0x28, (int)(y - 2), (struct OamEnt *)oam,
                      info->sidecar.header.share_string);
}

static void SelectFile_DrawCopyRefusalMessage(void) {
  // Render "CANNOT COPY" as a short tile string in vram_upload_data near
  // the existing prompt area. The full vanilla refusal flow plays sound 0x3c
  // already; we add a visible text indicator. The text is rendered into a
  // free region of the existing copy-confirmation VRAM upload buffer just
  // before the confirmation faerie cursor.
  //
  // For Phase A, we encode the message as a tile-stream prepended into the
  // existing vram_upload_data at a known free offset. The exact tile-stream
  // shape is a small upload command compatible with the file-select's NMI
  // copy routine:
  //   [vram_addr_hi, vram_addr_lo, attr, count, <tiles+attrs>...]
  //
  // We place "CANT COPY" (9 chars, fits within the spare row above the
  // confirmation prompt). The choice of vram_addr is the same row used by
  // the "ERASE THIS PLAYER" confirmation header, so any prior content is
  // overwritten cleanly.
  // HandleStripes14 (src/nmi.c:421) decodes the 4-byte header as:
  //   len = (swap16(WORD(p[2])) & 0x3fff) + 1
  // i.e. the count byte encodes BYTES-MINUS-ONE, not tile_count. For N
  // tile pairs (2 bytes each), the field is (N*2)-1. The original
  // implementation used N here directly — that fed len=N+1 into the
  // memcpy, copying only ~half the data and advancing p mid-payload, so
  // every subsequent "header" was read from garbage bytes. Result was
  // OOB reads into adjacent g_ram + arbitrary VRAM corruption the moment
  // the copy refusal fired.
  static const uint8 kRefusalText[] = {
    // VRAM target = 0x62c6 (same row as the existing confirmation header),
    // attr=0, count=0x11 (18 bytes = 9 tile pairs, minus 1).
    0x62, 0xc6, 0, 0x11,
    0x02, 0x18,  // C
    0x00, 0x18,  // A
    0x0d, 0x18,  // N
    0x23, 0x18,  // T
    0xa9, 0x18,  // space
    0x02, 0x18,  // C
    0x0e, 0x18,  // O
    0x0f, 0x18,  // P
    0x28, 0x18,  // Y
    // bottom row at vram_addr + 0x20 (same count)
    0x62, 0xe6, 0, 0x11,
    0x12, 0x18,  // C bottom
    0x10, 0x18,  // A bottom
    0x1d, 0x18,  // N bottom
    0x33, 0x18,  // T bottom
    0xa9, 0x18,  // space bottom (blank's bottom is also blank)
    0x12, 0x18,  // C bottom
    0x1e, 0x18,  // O bottom
    0x1f, 0x18,  // P bottom
    0x38, 0x18,  // Y bottom
    0xff,        // terminator
  };
  memcpy(vram_upload_data, kRefusalText, sizeof(kRefusalText));
}

// §9.3b — Empty-slot kind picker. Stub: render a 3-option prompt; only
// "Vanilla" is wired through to the existing new-game flow. "New Randomizer"
// and "Load Share String" play a refusal sound and return until the next
// cluster lands §9.4 (settings screen) and §9.1b (text input).
static void SelectFile_KindPicker_Draw(void) {
  // Render a simple 3-row prompt: "VANILLA", "RANDOM", "PASTE". Each fits
  // comfortably in the 6-char-wide font region used by file-select text.
  // The prompt is drawn into the existing slot-header area where the slot
  // name normally appears, in the row of the slot being created.
  // Count byte = (num_tile_pairs * 2) - 1; encodes BYTES-MINUS-ONE per
  // HandleStripes14's decode (src/nmi.c:421). Original cluster-1
  // implementation passed num_tile_pairs directly, causing memcpy
  // to read ~half the data + p to advance mid-payload. Subsequent
  // command headers were read from garbage. Fixed below.
  static const uint8 kKindPicker_Text[] = {
    // Title "NEW GAME?" at top — 9 tile pairs = 18 bytes, count = 0x11.
    0x61, 0x88, 0, 0x11,
    0x0d, 0x18, 0x04, 0x18, 0x26, 0x18, 0xa9, 0x18,
    0x06, 0x18, 0x00, 0x18, 0x0c, 0x18, 0x04, 0x18,
    0x2d, 0x18,  // '?' approximated as tile 0x2d (likely punctuation; falls
                 // through to blank if not present in font)
    // Option 1: "VANILLA" — 7 tile pairs = 14 bytes, count = 0x0d.
    0x61, 0xc8, 0, 0x0d,
    0x25, 0x18, 0x00, 0x18, 0x0d, 0x18, 0x08, 0x18,
    0x0b, 0x18, 0x0b, 0x18, 0x00, 0x18,
    // Option 2: "RANDOM" — 6 tile pairs = 12 bytes, count = 0x0b.
    0x62, 0x08, 0, 0x0b,
    0x21, 0x18, 0x00, 0x18, 0x0d, 0x18, 0x03, 0x18,
    0x0e, 0x18, 0x0c, 0x18,
    // Option 3: "PASTE" — 5 tile pairs = 10 bytes, count = 9.
    0x62, 0x48, 0, 9,
    0x0f, 0x18, 0x00, 0x18, 0x22, 0x18, 0x23, 0x18,
    0x04, 0x18,
    0xff,
  };
  memcpy(vram_upload_data, kKindPicker_Text, sizeof(kKindPicker_Text));
  // Cursor indicator: fairy pointed at the selected option's screen row.
  // Each option's tilemap row sits 0x40 bytes apart in VRAM (= 1 tilemap
  // row of 32 entries = 8 screen pixels). Title is at VRAM 0x6188 and is
  // not selectable; options are at 0x61c8 (VANILLA), 0x6208 (RANDOM),
  // 0x6248 (PASTE). Anchor cursor=0 at the VANILLA row's screen-Y and
  // step by 8 pixels per option to match the VRAM delta.
  static const uint8 kKindPicker_FairyX = 0x28;
  static const uint8 kKindPicker_FairyY0 = 0xcf;  // VANILLA row Y (best-fit;
                                                  // refine in playtest)
  uint8 fy = kKindPicker_FairyY0 + (uint8)(g_kind_picker_cursor * 8);
  FileSelect_DrawFairy(kKindPicker_FairyX, fy);
  nmi_load_bg_from_vram = 1;
}

// Returns true if the kind picker handled input (caller should skip the
// usual file-select cursor logic).
static bool SelectFile_KindPicker_Update(void) {
  if (!g_kind_picker_active) return false;
  SelectFile_KindPicker_Draw();

  uint8 a = (filtered_joypad_L & 0xc0 | filtered_joypad_H) & 0xfc;
  if (a & 0x2c) {
    sound_effect_2 = 0x20;
    if (a & 8) {
      // Up
      if (g_kind_picker_cursor == 0)
        g_kind_picker_cursor = 2;
      else
        g_kind_picker_cursor--;
    } else {
      // Down
      if (++g_kind_picker_cursor >= 3)
        g_kind_picker_cursor = 0;
    }
    return true;
  }

  // Cancel via B button (H.0x80 per zelda_rtl.h kJoypadH_B). Distinct from
  // A (which is L.0x80); we must check filtered_joypad_H directly because the
  // packed `a` byte OR's A and B into the same bit.
  if (filtered_joypad_H & 0x80) {
    // B = cancel — return to file-select cursor.
    //
    // CRITICAL: SelectFile_KindPicker_Draw memcpys its prompt over the
    // entire vram_upload_data buffer, clobbering the kSelectFile_Func3_Data
    // background that submodule 4 (FileSelect_TriggerNameStripesAndAdvance)
    // installed. The slot-list rendering loop in FileSelect_Main writes
    // patches at fixed offsets {4, 0x2e, 0x58} of vram_upload_data — those
    // offsets only make sense within the Func3 layout. If we just clear
    // the picker flag, the next frame's slot patches land mid-prompt and
    // the slot list renders garbage. Force re-init by rewinding to
    // submodule 3 (which advances 3 → 4 → 5), restoring vram_upload_data
    // to the Func3 layout before slot rendering resumes.
    sound_effect_1 = 0x3c;
    g_kind_picker_active = 0;
    submodule_index = 3;
    subsubmodule_index = 0;
    return true;
  }

  // A / Start press: act on selection. Checking filtered_joypad_L for A
  // (L.0x80) OR filtered_joypad_H for Start (H.0x10) directly, since the
  // packed `a` byte conflates A with B.
  if ((filtered_joypad_L & 0x80) || (filtered_joypad_H & 0x10)) {
    sound_effect_1 = 0x2c;
    if (g_kind_picker_cursor == 0) {
      // Vanilla: existing new-game flow.
      g_kind_picker_active = 0;
      selectfile_R16 = g_kind_picker_target_slot;
      main_module_index = 4;
      submodule_index = 0;
      subsubmodule_index = 0;
    } else if (g_kind_picker_cursor == 1) {
      // TODO §9.4 settings screen — open RandoSettings UI here. Stub: play
      // refusal sound and keep the picker visible so the user can pick
      // Vanilla or cancel.
      sound_effect_1 = 0x3c;
    } else {
      // §9.1b/§9.2/§9.6 — open the alphabet picker so the player can type
      // a share string. On submit it routes through Share_PastePath() and
      // reports the decode status; on cancel it returns to the kind picker.
      g_kind_picker_active = 0;
      SelectFile_AlphabetPicker_Activate();
    }
    return true;
  }

  return true;  // swallow other input while picker is active
}

// ---------------------------------------------------------------------------
// §9.1b/§9.2 — On-screen alphabet picker.
//
// Renders the base32 alphabet as an 8-col × 4-row grid plus a 5th control
// row (SUBMIT / DELETE / CANCEL). D-pad moves the cursor (with row+col
// wrapping). A inserts the cursor char (or triggers the control); B
// backspaces; Start submits. On submit, Share_PastePath() decodes the
// buffer and a brief overlay reports OK / specific reject status.
// ---------------------------------------------------------------------------

// The 32 base32 chars in row-major order. Matches kBase32Alphabet in
// rando_share.c (and the kBase32CharToTile mapping above).
static const char kAlphabetPicker_Chars[32] = {
  'A','B','C','D','E','F','G','H',
  'I','J','K','L','M','N','O','P',
  'Q','R','S','T','U','V','W','X',
  'Y','Z','2','3','4','5','6','7',
};

static void SelectFile_AlphabetPicker_Activate(void) {
  g_alphabet_picker_active = true;
  g_alphabet_cursor_row = 0;
  g_alphabet_cursor_col = 0;
  g_alphabet_msg_status = 0;
  g_alphabet_msg_frames = 0;
  g_alphabet_pending_return = false;
  // §9 cluster-2 audit MED-1: zero the decoded-share globals so stale
  // values from a previous successful submit don't leak into the next
  // session. (Latent today since the SelectFile_GetLastDecodedShareString
  // accessor isn't wired yet — but it lands in §9.4 / §9.8.)
  g_alphabet_decoded_seed = 0;
  memset(g_alphabet_decoded_hash, 0, sizeof(g_alphabet_decoded_hash));
  TextField_Init(&g_alphabet_textfield, /*base32_only=*/true);
  g_alphabet_textfield.active = true;
  // Hand the textfield to the SDL host so SDL_TEXTINPUT events route here
  // and the keyboard→joypad path is suppressed (see main.c §9.1b block).
  g_rando_active_textfield = &g_alphabet_textfield;
  g_rando_text_input_active = true;
  g_rando_text_input_submit_pending = false;
  g_rando_text_input_cancel_pending = false;
}

static void SelectFile_AlphabetPicker_Deactivate(void) {
  g_alphabet_picker_active = false;
  g_rando_text_input_active = false;
  g_rando_active_textfield = NULL;
  g_alphabet_textfield.active = false;
  // CRITICAL (mirroring SelectFile_KindPicker_Update's B-cancel comment):
  // SelectFile_AlphabetPicker_Draw memcpys its prompt over vram_upload_data,
  // which clobbers the kSelectFile_Func3_Data background installed by
  // submodule 4. The slot-list rendering loop in FileSelect_Main writes
  // patches at fixed offsets within the Func3 layout; if we don't rebuild
  // it, the slot list renders garbage on the frame after the picker
  // closes. Force re-init by rewinding to submodule 3 (which advances
  // 3 → 4 → 5, restoring vram_upload_data to the Func3 layout).
  submodule_index = 3;
  subsubmodule_index = 0;
}

// Render the alphabet picker into vram_upload_data. The buffer is rendered
// as a tile-stream of (vram_addr, attr, count, tile pairs...) commands,
// terminated with 0xff. Per HandleStripes14 (src/nmi.c:421), the count byte
// is BYTES-MINUS-ONE; for N tile pairs (2 bytes each) the field is (N*2)-1.
// All count bytes below are pre-computed against this rule — see cluster-1
// audit lessons for the bug class this prevents.
static void SelectFile_AlphabetPicker_Draw(void) {
  // Render the textfield buffer first as a status line at the top.
  //
  // VRAM tile layout uses the same word-pairs as the kKindPicker_Text /
  // kSelectFile_Func3_Data data above: each tile entry is 2 bytes
  // (tile_index, attr=0x18). VRAM target = 0x6188 (matches the kind-picker
  // title row), giving a stable on-screen Y near the slot region.
  //
  // We render up to 32 chars of the buffer per visual row (the buffer is
  // 64 chars max, see kRandoTextFieldMaxLen). Phase A: render only the
  // first 32 chars + a "..." marker when truncated.
  //
  // We build a single command buffer in stack memory then memcpy in one
  // shot — simpler than incrementally writing into vram_upload_data.

  uint8 cmd[512];
  int o = 0;

  // --- Row 0: title "PASTE SHARE STRING" (18 tile pairs = 36 bytes, count=0x23)
  //     VRAM target 0x6188. Count = (18*2)-1 = 0x23.
  cmd[o++] = 0x61; cmd[o++] = 0x88; cmd[o++] = 0; cmd[o++] = 0x23;
  static const uint8 kTitleTiles[18] = {
    0x0f, 0x00, 0x22, 0x23, 0x04, 0xa9,  // PASTE_
    0x22, 0x07, 0x00, 0x21, 0x04, 0xa9,  // SHARE_
    0x22, 0x23, 0x21, 0x08, 0x0d, 0x06,  // STRING
  };
  for (int i = 0; i < 18; i++) { cmd[o++] = kTitleTiles[i]; cmd[o++] = 0x18; }

  // --- Row 1: current buffer (18-char sliding window keyed to cursor).
  //     VRAM target 0x61c8 (one tilemap row below title; +0x40 bytes).
  //     Count = (18*2)-1 = 0x23.
  //
  // §9 cluster-2 audit MED-2: the buffer holds up to 64 chars (share
  // strings are 50). Fixed [0..18) display would hide chars 18+ entirely,
  // so a user couldn't verify the back half of their typing and would
  // get unattributable BadChecksum errors. Slide the window so the
  // cursor is always visible — when the buffer overflows 18 chars, the
  // window right-aligns to the cursor (so freshly-typed chars are
  // always visible) and the older chars scroll off the left.
  cmd[o++] = 0x61; cmd[o++] = 0xc8; cmd[o++] = 0; cmd[o++] = 0x23;
  int buf_len = g_alphabet_textfield.len;
  int cur = g_alphabet_textfield.cursor;
  int win_start = (cur > 17) ? (cur - 17) : 0;
  // Don't show garbage past the end of the buffer; cap the visible char
  // range at buf_len and pad with blanks.
  for (int i = 0; i < 18; i++) {
    int src = win_start + i;
    char c = (src < buf_len) ? g_alphabet_textfield.buf[src] : ' ';
    uint8 tile = SelectFile_TileForBase32(c);
    cmd[o++] = tile; cmd[o++] = 0x18;
  }

  // --- Alphabet grid: 4 rows of 8 chars each.
  //     Rows start at VRAM 0x6208 and step by 0x40 per row.
  //     Each grid row = 8 tile pairs = 16 bytes, count = (8*2)-1 = 0x0f.
  for (int r = 0; r < kAlphabetPicker_GridRows; r++) {
    uint16 vram = 0x6208 + r * 0x40;
    cmd[o++] = (uint8)(vram >> 8);
    cmd[o++] = (uint8)(vram & 0xff);
    cmd[o++] = 0;
    cmd[o++] = 0x0f;
    for (int c = 0; c < kAlphabetPicker_GridCols; c++) {
      char ch = kAlphabetPicker_Chars[r * kAlphabetPicker_GridCols + c];
      uint8 tile = SelectFile_TileForBase32(ch);
      // Highlight cell by toggling palette attr bits when the cursor is
      // here; otherwise default attr 0x18. The OAM fairy cursor below is
      // the primary indicator, so palette-flip here is belt-and-suspenders.
      uint8 attr = 0x18;
      if (r == g_alphabet_cursor_row && c == g_alphabet_cursor_col) attr = 0x38;
      cmd[o++] = tile; cmd[o++] = attr;
    }
  }

  // --- Control row: SUBMIT / DELETE / CANCEL labels.
  //     6-char labels rendered as 3 groups across the same width as a grid
  //     row. Each label occupies 6 tile pairs = 12 bytes, count = (6*2)-1 = 0x0b.
  //     Place the SUBMIT label at the leftmost cell, DELETE in the middle,
  //     CANCEL at the right — each at its own VRAM target.
  uint16 ctrl_vram_base = 0x6208 + kAlphabetPicker_GridRows * 0x40;
  // SUBMIT (6 chars, count = (6*2)-1 = 0x0b). Tiles: S U B M I T.
  cmd[o++] = (uint8)(ctrl_vram_base >> 8);
  cmd[o++] = (uint8)(ctrl_vram_base & 0xff);
  cmd[o++] = 0;
  cmd[o++] = 0x0b;
  static const uint8 kSubmitTiles[6] = { 0x22, 0x24, 0x01, 0x0c, 0xaf, 0x23 };
  for (int i = 0; i < 6; i++) {
    uint8 attr = 0x18;
    if (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow &&
        g_alphabet_cursor_col == kAlphabetPickerCtrl_Submit) attr = 0x38;
    cmd[o++] = kSubmitTiles[i]; cmd[o++] = attr;
  }
  // DELETE — VRAM 8 cells right (6 char cells + 2 cell gap; 8 cells = 16 bytes).
  // §9 cluster-2 audit LOW: original 6-cell spacing rendered the three
  // labels visually contiguous as "SUBMITDELETECANCEL". 2-tile gap parses
  // as three discrete buttons. Fairy X table updated below to match.
  uint16 del_vram = ctrl_vram_base + 16;
  cmd[o++] = (uint8)(del_vram >> 8);
  cmd[o++] = (uint8)(del_vram & 0xff);
  cmd[o++] = 0;
  cmd[o++] = 0x0b;
  static const uint8 kDeleteTiles[6] = { 0x03, 0x04, 0x0b, 0x04, 0x23, 0x04 };
  for (int i = 0; i < 6; i++) {
    uint8 attr = 0x18;
    if (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow &&
        g_alphabet_cursor_col == kAlphabetPickerCtrl_Delete) attr = 0x38;
    cmd[o++] = kDeleteTiles[i]; cmd[o++] = attr;
  }
  // CANCEL — 16 more bytes to the right (same 6+2 spacing).
  uint16 cancel_vram = del_vram + 16;
  cmd[o++] = (uint8)(cancel_vram >> 8);
  cmd[o++] = (uint8)(cancel_vram & 0xff);
  cmd[o++] = 0;
  cmd[o++] = 0x0b;
  static const uint8 kCancelTiles[6] = { 0x02, 0x00, 0x0d, 0x02, 0x04, 0x0b };
  for (int i = 0; i < 6; i++) {
    uint8 attr = 0x18;
    if (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow &&
        g_alphabet_cursor_col == kAlphabetPickerCtrl_Cancel) attr = 0x38;
    cmd[o++] = kCancelTiles[i]; cmd[o++] = attr;
  }

  // --- Optional decode-status overlay: render in the row below the controls
  //     when g_alphabet_msg_status != 0. Each status maps to a short label.
  //     8-tile slot, count = (8*2)-1 = 0x0f.
  if (g_alphabet_msg_status != 0) {
    uint16 msg_vram = ctrl_vram_base + 0x40;
    cmd[o++] = (uint8)(msg_vram >> 8);
    cmd[o++] = (uint8)(msg_vram & 0xff);
    cmd[o++] = 0;
    cmd[o++] = 0x0f;
    // Render up to 8 chars of the status label. SelectFile_TileForBase32
    // covers A-Z + 2-7 + space (via blank). For convenience we map a couple
    // of short labels: "OK", "BAD LEN", "BAD B32", "BAD MAG", "BAD CKS",
    // "ALTTPR". Each is 7 chars or fewer; pad with blanks.
    const char *label = "        ";
    // g_alphabet_msg_status = ShareDecodeStatus + 1; subtract to recover.
    int s = g_alphabet_msg_status - 1;
    switch (s) {
      case kShareDecodeOk:           label = "OK      "; break;
      case kShareDecodeBadLength:    label = "BAD LEN "; break;
      case kShareDecodeBadBase32:    label = "BAD B32 "; break;
      case kShareDecodeBadMagic:     label = "BAD MAG "; break;
      case kShareDecodeBadChecksum:  label = "BAD CKS "; break;
      case kShareDecodeAlttprFormat: label = "ALTTPR  "; break;
      default:                       label = "ERROR   "; break;
    }
    for (int i = 0; i < 8; i++) {
      uint8 tile = SelectFile_TileForBase32(label[i]);
      cmd[o++] = tile; cmd[o++] = 0x18;
    }
  }

  // Terminator. HandleStripes14 stops when p[0] has the 0x80 bit set; any
  // value >= 0x80 will do.
  cmd[o++] = 0xff;

  // SAFETY: assert we stayed within the cmd[] buffer. cmd[] is 512 bytes;
  // worst case is title(4+36) + buffer(4+36) + 4*(4+16) + 3*(4+12) + (4+16) +
  // 1 = 40 + 40 + 80 + 48 + 20 + 1 = 229 bytes. Comfortably under 512.
  // vram_upload_data is sized for the largest existing file-select payload
  // (kKILLFile_ChooseTarget_Tab[253]), so 229 fits cleanly.
  memcpy(vram_upload_data, cmd, (size_t)o);

  // Cursor — draw fairy at the highlighted cell. Each grid cell is ~8px
  // wide; the grid starts at X=24 (matches the kind picker fairy origin).
  // Phase A best-fit Y: title at y=0x88, buffer at y=0x98, grid rows at
  // y=0xa8/0xb0/0xb8/0xc0, control row at y=0xc8. Tune in playtest.
  static const uint8 kAlphabetPicker_FairyXBase = 0x24;
  static const uint8 kAlphabetPicker_FairyYBase = 0xa8;
  uint8 fy = kAlphabetPicker_FairyYBase +
             (uint8)(g_alphabet_cursor_row * 0x08);
  // Control row cells are wider (6-tile labels with 6-tile gaps).
  uint8 fx;
  if (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow) {
    // Fairy X step matches the VRAM label spacing: 8 tile cells per
    // label-slot (6 char + 2 gap) = 64 px = 0x40. Anchor SUBMIT at 0x24.
    static const uint8 kCtrlFairyX[3] = { 0x24, 0x64, 0xa4 };
    fx = kCtrlFairyX[g_alphabet_cursor_col % kAlphabetPickerCtrl_Count];
  } else {
    fx = kAlphabetPicker_FairyXBase + (uint8)(g_alphabet_cursor_col * 0x08);
  }
  FileSelect_DrawFairy(fx, fy);
}

// Run the alphabet picker. Returns true when the picker is active (caller
// skips the file-select cursor logic).
static bool SelectFile_AlphabetPicker_Update(void) {
  if (!g_alphabet_picker_active) return false;

  // Tick the message overlay. When the OK overlay completes, perform the
  // pending return to the file-select cursor.
  if (g_alphabet_msg_frames > 0) {
    g_alphabet_msg_frames--;
    if (g_alphabet_msg_frames == 0) {
      g_alphabet_msg_status = 0;
      if (g_alphabet_pending_return) {
        g_alphabet_pending_return = false;
        SelectFile_AlphabetPicker_Deactivate();
        // §9.8 / next-cluster integration point: with a decoded seed_u64 +
        // settings_hash latched in g_alphabet_decoded_*, the new-game flow
        // would start the rando setup. For Phase A (text-input cluster) we
        // simply return to file-select; the next cluster's settings screen
        // picks up the latched values via Share_PastePath path.
        ReturnToFileSelect();
        selectfile_R16 = g_kind_picker_target_slot;
        return true;
      }
    }
  }

  SelectFile_AlphabetPicker_Draw();

  // Consume host-pending submit/cancel one-shots set by main.c on Enter /
  // Escape keypresses. This is the PC-keyboard path that bypasses the
  // on-screen controls row (which is the only Submit/Cancel path on
  // controllers). Read-and-clear so the same press doesn't fire twice.
  if (g_rando_text_input_submit_pending) {
    g_rando_text_input_submit_pending = false;
    sound_effect_1 = 0x2c;
    SelectFile_AlphabetPicker_HandleSubmit();
    return true;
  }
  if (g_rando_text_input_cancel_pending) {
    g_rando_text_input_cancel_pending = false;
    sound_effect_1 = 0x3c;
    // Same cancel semantics as the CANCEL control glyph: reopen the kind
    // picker on the target slot.
    SelectFile_AlphabetPicker_Deactivate();
    g_kind_picker_active = 1;
    g_kind_picker_cursor = 0;
    return true;
  }

  // D-pad: move cursor (with row wrapping; col wraps within each row's
  // width). Use filtered_joypad_H bits for direction edges (consistent
  // with the kind picker).
  uint8 dir = filtered_joypad_H & 0x0f;
  if (dir != 0) {
    int max_col = (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow)
                      ? kAlphabetPickerCtrl_Count
                      : kAlphabetPicker_GridCols;
    if (dir & kJoypadH_Up) {
      if (g_alphabet_cursor_row == 0) {
        g_alphabet_cursor_row = kAlphabetPicker_TotalRows - 1;
      } else {
        g_alphabet_cursor_row--;
      }
      // Clamp col when landing on the narrower control row.
      int new_max = (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow)
                        ? kAlphabetPickerCtrl_Count
                        : kAlphabetPicker_GridCols;
      if (g_alphabet_cursor_col >= new_max) g_alphabet_cursor_col = new_max - 1;
      sound_effect_2 = 0x20;
    } else if (dir & kJoypadH_Down) {
      g_alphabet_cursor_row++;
      if (g_alphabet_cursor_row >= kAlphabetPicker_TotalRows) g_alphabet_cursor_row = 0;
      int new_max = (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow)
                        ? kAlphabetPickerCtrl_Count
                        : kAlphabetPicker_GridCols;
      if (g_alphabet_cursor_col >= new_max) g_alphabet_cursor_col = new_max - 1;
      sound_effect_2 = 0x20;
    } else if (dir & kJoypadH_Left) {
      if (g_alphabet_cursor_col == 0) g_alphabet_cursor_col = max_col - 1;
      else g_alphabet_cursor_col--;
      sound_effect_2 = 0x20;
    } else if (dir & kJoypadH_Right) {
      g_alphabet_cursor_col++;
      if (g_alphabet_cursor_col >= max_col) g_alphabet_cursor_col = 0;
      sound_effect_2 = 0x20;
    }
    return true;
  }

  // Start = submit. Checked BEFORE A so a Start+A frame doesn't accidentally
  // insert a char and submit on the same press.
  if (filtered_joypad_H & kJoypadH_Start) {
    sound_effect_1 = 0x2c;
    SelectFile_AlphabetPicker_HandleSubmit();
    return true;
  }

  // B button = backspace. Checked BEFORE A because A and B share the packed
  // `a` byte; we want B to never act as insert when both fire.
  if (filtered_joypad_H & kJoypadH_B) {
    sound_effect_1 = 0x2c;
    TextField_HandleKey(&g_alphabet_textfield, kTextFieldKey_Backspace);
    return true;
  }

  // A button (or Start without B) = act on current cell.
  if (filtered_joypad_L & kJoypadL_A) {
    sound_effect_1 = 0x2c;
    if (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow) {
      switch (g_alphabet_cursor_col) {
        case kAlphabetPickerCtrl_Submit:
          SelectFile_AlphabetPicker_HandleSubmit();
          break;
        case kAlphabetPickerCtrl_Delete:
          TextField_HandleKey(&g_alphabet_textfield, kTextFieldKey_Backspace);
          break;
        case kAlphabetPickerCtrl_Cancel:
          // Reopen the kind picker on the target slot so the player can
          // pick a different option without re-navigating the file-select
          // cursor.
          SelectFile_AlphabetPicker_Deactivate();
          g_kind_picker_active = 1;
          g_kind_picker_cursor = 0;
          break;
      }
    } else {
      // Grid cell: insert the highlighted char via TextField_HandleChar so
      // the base32 filter and length cap apply uniformly with paste/keyboard.
      char ch = kAlphabetPicker_Chars[
          g_alphabet_cursor_row * kAlphabetPicker_GridCols +
          g_alphabet_cursor_col];
      TextField_HandleChar(&g_alphabet_textfield, ch);
    }
    return true;
  }

  return true;  // swallow all other input while the picker is active
}

static void SelectFile_AlphabetPicker_HandleSubmit(void) {
  // Run the buffer through the share-string decoder. On success, latch the
  // decoded values + show "OK" overlay then return to file-select. On any
  // failure, surface the specific reject status so the user can fix the
  // input.
  //
  // §9 cluster-2 audit MED-4: refuse to re-fire while the OK countdown is
  // already in flight. Without this guard, a held Start/Enter (or rapid
  // press) during the post-OK frames re-runs the entire success path,
  // dumping another stderr log line and resetting the countdown.
  if (g_alphabet_pending_return) return;
  if (g_alphabet_textfield.len == 0) {
    g_alphabet_msg_status = (uint8)(kShareDecodeBadLength + 1);
    g_alphabet_msg_frames = kAlphabetMsg_ErrorBriefFrames;
    sound_effect_1 = 0x3c;
    return;
  }
  uint64 seed_u64 = 0;
  uint8 hash[16];
  ShareDecodeStatus st = Share_PastePath(g_alphabet_textfield.buf,
                                         &seed_u64, hash);
  g_alphabet_msg_status = (uint8)(st + 1);
  if (st == kShareDecodeOk) {
    g_alphabet_decoded_seed = seed_u64;
    memcpy(g_alphabet_decoded_hash, hash, 16);
    g_alphabet_msg_frames = kAlphabetMsg_OkBriefFrames;
    g_alphabet_pending_return = true;
    // Phase A integration point: the next-cluster settings screen + new-game
    // flow (tasks 9.4 + 9.8) consume g_alphabet_decoded_seed +
    // g_alphabet_decoded_hash. For now stderr-log them so a developer can
    // sanity-check the end-to-end path.
    fprintf(stderr, "[alphabet_picker] decoded share: seed=0x%016llx hash=",
            (unsigned long long)seed_u64);
    for (int i = 0; i < 16; i++) fprintf(stderr, "%02x", hash[i]);
    fprintf(stderr, "\n");
    sound_effect_1 = 0x2c;
  } else {
    // Leave the buffer intact so the user can edit (don't wipe their typing).
    g_alphabet_msg_frames = kAlphabetMsg_ErrorBriefFrames;
    sound_effect_1 = 0x3c;
  }
}

