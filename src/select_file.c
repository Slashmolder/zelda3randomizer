#include "zelda_rtl.h"
#include "variables.h"
#include "load_gfx.h"
#include "select_file.h"
#include "snes/snes_regs.h"
#include "overworld.h"
#include "messaging.h"
#include "sprite.h"
#include "config.h"
#include "features.h"
#include "rando/rando.h"
#include "rando/rando_placement.h"
#include "rando/rando_save.h"
#include "rando/rando_share.h"
#include "rando/rando_settings.h"
#include "rando/rando_spoiler.h"
#include "rando/rando_textfield.h"
#include "rando/vanilla_assets_hash.h"  // kVanillaAssetsHash + kVanillaAssetsHashKnown
#include "third_party/sha256/sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
// encoded as (NUM_PAIRS * 2) - 1 per HandleStripes14's decode
// — see cluster-1 audit lessons).
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
static int emit_text_run(uint8 *cmd, int o, uint16 vram_addr,
                         const char *text, int max_chars, uint8 attr);
// Emit a stripes-format memset command that fills `num_words` consecutive
// tilemap entries starting at `vram_addr` with the blank tile (0xa9, attr
// 0x18). Used to clear stale VRAM from prior screens before drawing.
static int emit_clear_area(uint8 *cmd, int o, uint16 vram_addr, int num_words);
// Forward declaration — actual static defined deep in this file, but the
// slot-loop in FileSelect_Main needs to check it for modal-active gating.
static bool g_settings_active;
static void SelectFile_AlphabetPicker_Activate(void);
static void SelectFile_AlphabetPicker_Deactivate(void);
static void SelectFile_AlphabetPicker_Draw(void);
static bool SelectFile_AlphabetPicker_Update(void);
static void SelectFile_AlphabetPicker_HandleSubmit(void);
// §9.4 — settings screen forward decls.
static void SelectFile_Settings_Activate(uint8 target_slot,
                                          bool prepopulate_from_share);
static void SelectFile_Settings_Deactivate(void);
static bool SelectFile_Settings_Update(void);
static void SelectFile_Settings_Draw(void);
static void SelectFile_Settings_HandleGenerate(void);
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
  // §9.4 — settings screen reset on file-select entry so a hard re-entry
  // (player quit to title and came back) doesn't inherit stale state.
  SelectFile_Settings_Deactivate();
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
  //
  // Skip the slot OAM draw when a modal picker is active. Func5/Func6 (and
  // the older hash-icon path) write sword/shield/heart sprites to oam_buf;
  // those entries are NOT cleared by the modal's draw routines, so they
  // would otherwise persist on top of the kind picker, alphabet picker, or
  // settings screens. Still mark each slot as occupied for cursor logic.
  bool modal_active = g_settings_active || g_alphabet_picker_active ||
                      g_kind_picker_active;
  for (int k = 0; k < 3; k++) {
    int render_kind = SelectFile_GetSlotRenderKind(k);
    switch (render_kind) {
      case kRandoSlotKind_Randomizer:
        selectfile_arr1[k] = 1;
        if (!modal_active) SelectFile_DrawRandoBanner(k);
        break;
      case kRandoSlotKind_Vanilla:
        selectfile_arr1[k] = 1;
        if (!modal_active) {
          SelectFile_Func5_DrawOams(k);
          SelectFile_Func6_DrawOams2(k);
          SelectFile_Func17(k);
        }
        break;
      case kRandoSlotKind_Empty:
      default:
        // Slot displays "NEW GAME" from the pre-built background; no extra
        // drawing needed (matches vanilla behavior).
        break;
    }
  }

  // §9.4 — settings screen takes priority over alphabet picker / kind picker
  // when active. Once the player enters settings (via kind=NewRandomizer or
  // alphabet=submit-ok), neither sub-prompt is reachable until the screen
  // closes via Generate or Cancel. The check runs first so its rendering
  // owns vram_upload_data for the frame.
  if (SelectFile_Settings_Update()) {
    nmi_load_bg_from_vram = 1;
    return;
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
        // §9.X — if the picked slot is a randomizer slot, install its
        // placement table + set kFeatures1_RandomizerActive BEFORE
        // CopySaveToWRAM. Without this, the runtime dispatcher sees no
        // active placement and falls through to vanilla item grants at
        // every location — i.e. "randomizer" plays like vanilla.
        //
        // The sidecar cache is reset after every Generate; reload it now
        // to pick up any freshly-written sidecar data before reading it.
        SelectFile_LoadSidecarCache();
        if (g_selectfile_slots_loaded &&
            g_selectfile_slots[selectfile_R16].has_sidecar_data &&
            g_selectfile_slots[selectfile_R16].sidecar.header.slot_kind
                == kSlotKind_Randomizer) {
          Rando_ActivateSidecarSlot(&g_selectfile_slots[selectfile_R16].sidecar);
        } else {
          Rando_DeactivateSlot();
        }
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
  // Also clear per-slot data. GetSlotRenderKind reads has_sidecar_data /
  // slot_kind directly and does NOT gate on g_selectfile_slots_loaded, so
  // flipping only the loaded flag leaves stale Randomizer classification
  // in place after erase/copy and the slot keeps rendering the rando banner
  // (and remains selectable, starting a corrupted game).
  for (int k = 0; k < 3; k++) {
    g_selectfile_slots[k].render_kind = kRandoSlotKind_Empty;
    g_selectfile_slots[k].has_sidecar_data = 0;
  }
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
  //
  // The prior placeholder approach used raw tiles 0x2d ('?') and 0x2e ('!')
  // for the world / goal initials, but neither tile is a verified glyph in
  // the file-select font — they render as 'd' / 'e' shapes (e.g. "1.ddLJJF").
  // Until the slot header carries explicit world_state / goal bytes, we use
  // share-string characters as the slot identifier (still per-seed unique).
  //
  // Layout:
  //   forward_fill_used=false: 6 share-string chars [0..5]
  //   forward_fill_used=true:  'F' prefix + 5 share-string chars [0..4]
  uint16 *dst = vram_upload_data + kSelectFile_DrawName_VramOffs[k] / 2;
  uint8 chars[6];
  int first_share = 0;
  if (forward_fill_used) {
    chars[0] = SelectFile_TileForBase32('F');  // forward-fill warning marker
    first_share = 1;
  }
  for (int i = first_share; i < 6; i++) {
    chars[i] = SelectFile_TileForBase32(share_b32[i - first_share]);
  }
  for (int i = 0; i < 6; i++) {
    uint16 t = (uint16)chars[i] + 0x1800;
    dst[i] = t;
    dst[i + 21] = t + 0x10;  // bottom-half tile (per SelectFile_Func17)
  }

  // OAM lane: use the vanilla sword/shield/heart draw rather than the
  // (currently-broken) 5-icon hash strip. The hash atlas references font
  // tile indices, but the OAM lane reads tile graphics from the sprite
  // base, so tile 0x00 etc. render as whatever sprite art sits there —
  // not the letter glyphs the atlas intends. Until a sprite-tile atlas
  // is sourced, render the standard vanilla icons; the rando slot is
  // still clearly identified by its share-string text in the name area.
  SelectFile_Func5_DrawOams(k);
  SelectFile_Func6_DrawOams2(k);
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
  // HandleStripes14 decodes the 4-byte header as:
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
  // Render a 4-row prompt (title + 3 options). The file-select font is
  // 16-pixel-tall: each glyph occupies two tilemap rows. emit_text_run
  // (defined far below) takes care of writing both halves at vram_addr
  // (top) and vram_addr + 0x20 (bottom, tile index + 0x10).
  //
  // Layout (16-px-tall glyphs, options spaced 0x40 word units = 16 px):
  //   Title  0x6188 (screen y≈91)
  //   VANILLA 0x61c8 (screen y≈107)
  //   RANDOM  0x6208 (screen y≈123)
  //   PASTE   0x6248 (screen y≈139)
  uint8 cmd[256];
  int o = 0;
  // Clear the slot list area BEFORE writing picker text. Without this,
  // any slot's BG2 content — vanilla heart tiles (0x520), the rando
  // banner's share-string letters, and the slot name — bleeds through
  // in cols the picker text doesn't overwrite. The original 256-word
  // clear at 0x6180 only covered rows 12-19 (where the picker text
  // lives); slot 1 sits at rows 9-11 so it leaked above the picker
  // when creating slot 2 or 3.
  // 640 words at 0x6100 = 20 tilemap rows × 32 cols, covering rows
  // 8-27 (all 3 slots). Same range Settings_DrawMain clears; this also
  // wipes COPY/ERASE PLAYER for the duration the picker is open, which
  // is fine — those controls aren't active in modal mode and reappear
  // when the picker closes (Func3 reinstall on submodule rewind).
  o = emit_clear_area(cmd, o, 0x6100, 640);
  // Title is "NEW GAME" without a trailing '?' — the '?' tile approximation
  // currently maps to a letter-shaped glyph in this font, so it reads as
  // "NEW GAMEd". Drop it for now; reintroduce when a verified punctuation
  // tile is wired into TileForAscii.
  o = emit_text_run(cmd, o, 0x6188, "NEW GAME", 8, 0x18);
  o = emit_text_run(cmd, o, 0x61c8, "VANILLA", 7, 0x18);
  o = emit_text_run(cmd, o, 0x6208, "RANDOM",  6, 0x18);
  o = emit_text_run(cmd, o, 0x6248, "PASTE",   5, 0x18);
  cmd[o++] = 0xff;
  memcpy(vram_upload_data, cmd, (size_t)o);
  // Fairy cursor. Options are spaced 0x40 word units in VRAM = 2 tilemap
  // rows = 16 screen pixels apart. Slot 1's row top at VRAM 0x6129 maps to
  // screen y=0x43=67, and the slot fairy sits at y=0x4a=74 (row_top + 7).
  // Applying the same offset:
  //   VANILLA row top y ≈ 107 → fairy y ≈ 114 (0x72)
  //   RANDOM  row top y ≈ 123 → fairy y ≈ 130 (0x82)
  //   PASTE   row top y ≈ 139 → fairy y ≈ 146 (0x92)
  static const uint8 kKindPicker_FairyX = 0x28;
  static const uint8 kKindPicker_FairyY0 = 0x72;
  uint8 fy = kKindPicker_FairyY0 + (uint8)(g_kind_picker_cursor * 0x10);
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
    // KindPicker_Draw memcpyd text into BG2 at 0x6188..0x6268. The submodule
    // 3->4 rebuild reinstalls kSelectFile_Func3_Data which only patches the
    // slot-name regions — the picker rows in between stay stale and bleed
    // through (NEW GAME / VANILLA / RANDOM / PASTE overlapping the slot
    // numbers). Push a clear over the picker's tilemap rows this frame,
    // same pattern as Settings_Deactivate.
    uint8 *p = (uint8 *)vram_upload_data;
    int o = 0;
    int clear_count = 256 * 2 - 1;  // 256 word cells = 8 tilemap rows
    p[o++] = 0x61;
    p[o++] = 0x80;  // 0x6180 — top of NEW GAME row, just below slot 1
    p[o++] = (uint8)(0x40 | ((clear_count >> 8) & 0x3f));
    p[o++] = (uint8)(clear_count & 0xff);
    p[o++] = 0xa9;
    p[o++] = 0x18;
    p[o++] = 0xff;
    nmi_load_bg_from_vram = 1;
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
      // §9.4 — open the settings screen on the target slot. The settings
      // screen owns input and drawing until the user generates or cancels.
      g_kind_picker_active = 0;
      SelectFile_Settings_Activate(g_kind_picker_target_slot,
                                   /*prepopulate_from_share=*/false);
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
  // Zero the decoded-share globals so stale values from a previous
  // successful submit don't leak into the next session. (Latent today
  // since the SelectFile_GetLastDecodedShareString accessor isn't wired
  // yet — it lands when the settings/generate path consumes it.)
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
  // 3 -> 4 -> 5, restoring vram_upload_data to the Func3 layout).
  //
  // Submodule rewind doesn't blank picker tiles in BG2 — Func3_Data only
  // patches the slot-name regions, so the picker's title/buffer/grid/
  // control rows (0x6180..0x6380) bleed through as "extra letters below
  // the 3rd slot" until something overwrites them. Push a stripes clear
  // now over the same range the picker's own Draw clears (512 words at
  // 0x6180). This frame's NMI uploads the clear; next frame Func3/Func4
  // reinstall the slot layout on top.
  uint8 *p = (uint8 *)vram_upload_data;
  int o = 0;
  int clear_count = 512 * 2 - 1;
  p[o++] = 0x61;
  p[o++] = 0x80;
  p[o++] = (uint8)(0x40 | ((clear_count >> 8) & 0x3f));
  p[o++] = (uint8)(clear_count & 0xff);
  p[o++] = 0xa9;
  p[o++] = 0x18;
  p[o++] = 0xff;
  nmi_load_bg_from_vram = 1;
  submodule_index = 3;
  subsubmodule_index = 0;
}

// Render the alphabet picker into vram_upload_data. The buffer is rendered
// as a tile-stream of (vram_addr, attr, count, tile pairs...) commands,
// terminated with 0xff. Per HandleStripes14, the count byte
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

  // The file-select font is 16 px tall, so emit_text_run writes both the
  // top and bottom halves of every glyph (see definition far below).
  // Rows here are spaced 0x40 word units = 16 px = abutting with no gap.
  uint8 cmd[1024];
  int o = 0;

  // Clear the slot list area BEFORE writing picker content. The picker
  // text only lives in rows 12-27, but slot 1's BG2 content sits at
  // rows 9-11 — clearing only rows 12+ leaks slot 1 banner/name above
  // the picker. 640 words at 0x6100 covers rows 8-27, matching
  // Settings_DrawMain and the kind picker's first-pass clear.
  o = emit_clear_area(cmd, o, 0x6100, 640);

  // Title at 0x6188, current buffer at 0x61c8. 18-char width per row.
  o = emit_text_run(cmd, o, 0x6188, "PASTE SHARE STRING", 18, 0x18);

  // The buffer holds up to 64 chars (share strings are 50). Fixed
  // [0..18) display would hide chars 18+ entirely; slide the window so
  // the cursor is always visible.
  char buffer_view[19];
  int buf_len = g_alphabet_textfield.len;
  int cur = g_alphabet_textfield.cursor;
  int win_start = (cur > 17) ? (cur - 17) : 0;
  for (int i = 0; i < 18; i++) {
    int src = win_start + i;
    buffer_view[i] = (src < buf_len) ? g_alphabet_textfield.buf[src] : ' ';
  }
  buffer_view[18] = 0;
  o = emit_text_run(cmd, o, 0x61c8, buffer_view, 18, 0x18);

  // Alphabet grid: 4 rows of 8 chars each.
  //
  // Layout: 24-px cell pitch (letter + 2 blanks = 3 tilemap cols). 16-px
  // pitch left only an 8-px gap for the 16-px-wide fairy sprite, so the
  // fairy always overlapped the cursor letter — and since the fairy sprite
  // has transparent pixels, the letter showed through and the cursor read
  // as "behind" the letter. 24-px pitch gives a 16-px gap, exactly fitting
  // the fairy with no letter overlap. Start the row at col 7 (one col left
  // of the old 8) so the rightmost letter H lands at col 28 (screen x=224)
  // with room to spare from the picker box edge.
  // String layout: "A  B  C  D  E  F  G  H" (22 chars).
  for (int r = 0; r < kAlphabetPicker_GridRows; r++) {
    char grid_row[23];
    int idx = 0;
    for (int c = 0; c < kAlphabetPicker_GridCols; c++) {
      grid_row[idx++] = kAlphabetPicker_Chars[r * kAlphabetPicker_GridCols + c];
      if (c < kAlphabetPicker_GridCols - 1) {
        grid_row[idx++] = ' ';
        grid_row[idx++] = ' ';
      }
    }
    grid_row[idx] = 0;
    uint8 row_attr = 0x18;
    if (r == g_alphabet_cursor_row) row_attr = 0x38;
    o = emit_text_run(cmd, o, (uint16)(0x6207 + r * 0x40), grid_row, idx, row_attr);
  }

  // Control row: SUBMIT / DELETE / CANCEL labels.
  //
  // Shifted left by 2 cols (col 8 -> col 6) so CANCEL ends at col 27
  // (screen x=216-223) instead of col 29 — at col 29 the L's right edge
  // overlapped the picker box border and rendered cropped.
  uint16 ctrl_vram_base = 0x6206 + kAlphabetPicker_GridRows * 0x40;
  uint8 submit_attr = 0x18;
  if (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow &&
      g_alphabet_cursor_col == kAlphabetPickerCtrl_Submit) submit_attr = 0x38;
  o = emit_text_run(cmd, o, ctrl_vram_base, "SUBMIT", 6, submit_attr);

  // DELETE — 2-tile gap after SUBMIT (8 cells right).
  uint16 del_vram = ctrl_vram_base + 8;
  uint8 del_attr = 0x18;
  if (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow &&
      g_alphabet_cursor_col == kAlphabetPickerCtrl_Delete) del_attr = 0x38;
  o = emit_text_run(cmd, o, del_vram, "DELETE", 6, del_attr);

  // CANCEL — 8 cells right of DELETE.
  uint16 cancel_vram = del_vram + 8;
  uint8 cancel_attr = 0x18;
  if (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow &&
      g_alphabet_cursor_col == kAlphabetPickerCtrl_Cancel) cancel_attr = 0x38;
  o = emit_text_run(cmd, o, cancel_vram, "CANCEL", 6, cancel_attr);

  // Optional decode-status overlay below the controls.
  if (g_alphabet_msg_status != 0) {
    uint16 msg_vram = ctrl_vram_base + 0x40;
    const char *label = "        ";
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
    o = emit_text_run(cmd, o, msg_vram, label, 8, 0x18);
  }

  // Terminator. HandleStripes14 stops when p[0] has the 0x80 bit set.
  cmd[o++] = 0xff;

  // Buffer sizing: cmd[] is 512 bytes; with both halves emit_text_run
  // doubles each text run. Worst case ~ 2 * (40 + 40 + 4*20 + 3*16 + 20)
  // + 1 = ~ 456 bytes. Fits comfortably.
  memcpy(vram_upload_data, cmd, (size_t)o);

  // Cursor — draw fairy at the highlighted cell. Each grid cell is ~8px
  // wide; the grid starts at X=24 (matches the kind picker fairy origin).
  // Phase A best-fit Y: title at y=0x88, buffer at y=0x98, grid rows at
  // y=0xa8/0xb0/0xb8/0xc0, control row at y=0xc8. Tune in playtest.
  // Fairy Y math: grid rows are written at VRAM 0x6208/0x6248/0x6288/0x62c8.
  // PPU word-stride = 0x20 words/row, so those map to tilemap rows
  // 16/18/20/22, screen y = 128/144/160/176, with 16 px between rows
  // (glyphs are 16 px tall, abutting). Fairy y = row_top + 2 matches the
  // kind picker convention (VANILLA row top 112 -> fairy 114). Earlier
  // base 0xa8 / step 0x08 placed cursor-row-0 (A) fairy at y=168, which
  // is between Q (160) and Y (176), and the half-sized step meant the
  // fairy never tracked the actual row.
  //   row 0 (A)    -> 0x82 (130)
  //   row 1 (I)    -> 0x92 (146)
  //   row 2 (Q)    -> 0xa2 (162)
  //   row 3 (Y)    -> 0xb2 (178)
  //   row 4 (ctrl) -> 0xc2 (194)
  // Fairy X: grid row starts at VRAM col 7 (screen x=56) with 24-px cell
  // pitch (letter + 2 blanks). Position the 16-px-wide fairy in the
  // 16-px blank to the LEFT of the cursor letter so it doesn't overlap
  // the letter at all (the fairy sprite has transparent pixels, so any
  // overlap reads as "behind" the letter even though OBJ priority 3
  // wins over BG2 low). Cursor col N letter at screen x = 56 + N*24;
  // fairy at letter_x - 16 = 40 + N*24. Step = 24 px per cursor col.
  static const uint8 kAlphabetPicker_FairyXBase = 0x28;
  static const uint8 kAlphabetPicker_FairyXStep = 0x18;
  static const uint8 kAlphabetPicker_FairyYBase = 0x82;
  uint8 fy = kAlphabetPicker_FairyYBase +
             (uint8)(g_alphabet_cursor_row * 0x10);
  // Control row cells are wider (6-tile labels with 6-tile gaps).
  uint8 fx;
  if (g_alphabet_cursor_row == kAlphabetPicker_CtrlRow) {
    // Fairy X step matches the VRAM label spacing: 8 tile cells per
    // label-slot (6 char + 2 gap) = 64 px = 0x40. Labels were shifted
    // left 2 cols (col 8 -> col 6) to avoid clipping CANCEL, so the fairy
    // table follows: SUBMIT label at screen x=48, fairy 28 px left at 20.
    static const uint8 kCtrlFairyX[3] = { 0x14, 0x54, 0x94 };
    fx = kCtrlFairyX[g_alphabet_cursor_col % kAlphabetPickerCtrl_Count];
  } else {
    fx = kAlphabetPicker_FairyXBase +
         (uint8)(g_alphabet_cursor_col * kAlphabetPicker_FairyXStep);
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
        // §9.4 — open the settings screen prepopulated with the decoded
        // seed_u64. settings_hash is one-way so we cannot regenerate the
        // settings struct from it; the user picks settings independently
        // (the screen surfaces the decoded settings_hash so they can
        // confirm/match). The seed_u64 is passed through so the same seed
        // input reproduces the original placement when paired with
        // matching settings.
        SelectFile_Settings_Activate(g_kind_picker_target_slot,
                                     /*prepopulate_from_share=*/true);
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
  // Refuse to re-fire while the OK countdown is already in flight.
  // Without this guard, a held Start/Enter (or rapid press) during the
  // post-OK frames re-runs the entire success path, dumping another
  // stderr log line and resetting the countdown.
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

// ===========================================================================
// §9.4 / §9.4a / §9.4b / §9.8 — Settings screen.
//
// Activated from the kind-picker (cursor=1 "New Randomizer") or from the
// alphabet-picker's OK path (so a pasted share string lands the user in
// settings with a pre-populated seed). Owns input + drawing until the user
// presses Generate (which runs the placement and returns to file-select on
// the just-generated slot) or Cancel (B-button: returns to kind picker).
//
// Layout: a scrollable list of ~20 rows. Each row is one of:
//   - Enum row (e.g., world_state, goal, item_pool_difficulty, dungeon-item
//     modes) — A/Left/Right cycles values.
//   - Slider row (e.g., crystals.ganon, crystals.tower, pieces_required,
//     pieces_placed) — Left decrements / Right or A increments.
//   - Bool row — A toggles.
//   - Disabled row (Phase-B-and-beyond shuffles) — rendered greyed, no input.
//   - Action row (PRESET, RECOMMENDED, SEED, GENERATE) — A activates.
//
// Visible window: ~10 rows fit comfortably on the 224px screen at the
// 8px line stride. The cursor stays at or near the middle when scrolling;
// out-of-window rows scroll into view on cursor movement. Scroll indicators
// show whether more rows exist above/below.
//
// Live settings_hash: the first 16 nibbles (8 hex chars) of
// Settings_ComputeHash output, rendered at the bottom of the screen and
// recomputed on every settings mutation.
//
// Asset-warn dialog: opened when Generate is pressed AND g_assets_hash
// differs from kVanillaAssetsHash AND no persisted decision is on file.
// The user picks Always allow / Allow once / Cancel; Always allow persists
// the choice keyed by the current g_assets_hash via the [RandoAssetDecisions]
// section of zelda3.ini.
// ===========================================================================

// Row identifiers — one per logical settings axis + action rows.
enum {
  kRow_Preset = 0,
  kRow_WorldState,
  kRow_Goal,
  kRow_CrystalsGanon,
  kRow_CrystalsTower,
  kRow_ItemPoolDifficulty,
  kRow_DungeonSmallKeys,
  kRow_DungeonBigKeys,
  kRow_DungeonMaps,
  kRow_DungeonCompasses,
  kRow_PiecesRequired,
  kRow_PiecesPlaced,
  // Spec-required Phase A axes. Both have exactly 2 supported Phase A
  // values per randomizer-core spec.
  kRow_ModeWeapons,        // Randomized / Assured (spec line 42)
  kRow_Accessibility,      // Items / Locations (spec line 43)
  kRow_PrizeShuffle,
  kRow_MedallionShuffle,
  kRow_RaceMode,
  kRow_Hints,              // Slice 5 — telepathic-tile hints (on/off)
  // Phase-B disabled rows (label-only; cursor skips over input but A
  // refuses with a tooltip-style refusal sound).
  kRow_EntranceShuffle_Disabled,
  kRow_EnemyShuffle_Disabled,
  kRow_BossShuffle_Disabled,
  kRow_Glitches_Disabled,
  // Action rows.
  kRow_Recommended,
  kRow_Seed,
  kRow_Generate,
  kRow__Count,
};

// Settings screen view-state — distinct from the row index (cursor).
enum {
  kSettingsView_Main = 0,
  kSettingsView_Recommended = 1,
  kSettingsView_AssetWarn = 2,
};

// Asset-warn dialog choices.
enum {
  kAssetWarnChoice_AlwaysAllow = 0,
  kAssetWarnChoice_AllowOnce = 1,
  kAssetWarnChoice_Cancel = 2,
  kAssetWarnChoice__Count = 3,
};

// Recommended-features panel row IDs (in panel order).
enum {
  kRecRow_SkipIntro = 0,
  kRecRow_ShowMaxItemsYellow,
  kRecRow_TurnWhileDashing,
  kRecRow_CollectItemsWithSword,
  kRecRow_BreakPotsWithSword,
  kRecRow_DisableLowHealthBeep,
  kRecRow_CarryMoreRupees,
  kRecRow_MiscBugFixes,
  kRecRow_GameChangingBugFixes,
  kRecRow_DimFlashes,
  kRecRow_ApplyAll,
  kRecRow__Count,
};

// Bitmask mapping each rec-row to the corresponding kFeatures0_* bit. The
// "recommended" set (the bits Apply-All flips ON) is the subset where
// kRecRecommendedOn[i] = 1. Per randomizer-ui spec.
static const uint32 kRecRowBits[] = {
  /*SkipIntro*/                kFeatures0_SkipIntroOnKeypress,
  /*ShowMaxItemsYellow*/       kFeatures0_ShowMaxItemsInYellow,
  /*TurnWhileDashing*/         kFeatures0_TurnWhileDashing,
  /*CollectItemsWithSword*/    kFeatures0_CollectItemsWithSword,
  /*BreakPotsWithSword*/       kFeatures0_BreakPotsWithSword,
  /*DisableLowHealthBeep*/     kFeatures0_DisableLowHealthBeep,
  /*CarryMoreRupees*/          kFeatures0_CarryMoreRupees,
  /*MiscBugFixes*/             kFeatures0_MiscBugFixes,
  /*GameChangingBugFixes*/     kFeatures0_GameChangingBugFixes,
  /*DimFlashes*/               kFeatures0_DimFlashes,
  /*ApplyAll*/                 0,  // synthetic
};
static const uint8 kRecRecommendedOn[] = {
  /*SkipIntro*/                1,
  /*ShowMaxItemsYellow*/       1,
  /*TurnWhileDashing*/         1,
  /*CollectItemsWithSword*/    1,
  /*BreakPotsWithSword*/       1,
  /*DisableLowHealthBeep*/     1,
  /*CarryMoreRupees*/          1,
  /*MiscBugFixes*/             1,
  /*GameChangingBugFixes*/     0,  // recommended OFF per spec
  /*DimFlashes*/               0,  // accessibility option — honor user pref
};
static const char *kRecRowLabels[] = {
  "SkipIntro",
  "MaxItemsYellow",
  "TurnDashing",
  "SwordCollect",
  "SwordPots",
  "NoBeep",
  "MoreRupees",
  "BugFixes",
  "GameBugFixes",
  "DimFlashes",
  "APPLY ALL",
};

// Settings screen state. Module-static; lives across frames.
static bool g_settings_active = false;
static uint8 g_settings_view = kSettingsView_Main;
static uint8 g_settings_target_slot = 0;
static uint8 g_settings_cursor = 0;
static uint8 g_settings_scroll_offset = 0;
static uint8 g_settings_preset_index = 0;
static RandoSettings g_settings_working;
static RandoTextField g_settings_seed_field;
static uint64 g_settings_seed_value = 0;
static bool g_settings_seed_parse_ok = true;
static uint64 g_settings_prepopulated_seed = 0;
static bool g_settings_seed_prepopulated = false;
// Recommended-features panel state.
static uint8 g_rec_cursor = 0;
static uint32 g_rec_working_features0 = 0;
// Asset-warn dialog state.
static uint8 g_asset_warn_cursor = 0;
static bool g_asset_warn_pending = false;
// One-shot bypass for the "Allow Once" choice. Without this, AllowOnce
// → recursive HandleGenerate → asset-warn gate fires again
// (AssetDecision_FindAllow is still false because AllowOnce
// intentionally doesn't persist), looping back to the dialog. The flag
// is set by AllowOnce, consumed (and cleared) on the next entry to
// HandleGenerate, and explicitly cleared by Deactivate so it can't
// leak across sessions.
static bool g_asset_warn_session_bypass = false;
// Generate state (re-fire guard).
static bool g_settings_generate_in_progress = false;
// Cached settings hash (8 hex chars displayable). Recomputed on mutation.
static uint8 g_settings_hash_short[16];

// ---------------------------------------------------------------------------
// Asset-warn persistence ([RandoAssetDecisions] in zelda3.ini).
//
// The decision is keyed by hex(g_assets_hash). When the user picks "Always
// allow", we record their hash + decision in a session-static lookup; on
// next launch the config parser populates the lookup from the INI. For
// Phase A the persistence is in-memory only (the INI write-back is deferred
// to a follow-up sprint — config.c is read-only today). The session-static
// lookup ensures the user is not re-prompted within a single session even
// without on-disk persistence.
// ---------------------------------------------------------------------------
typedef struct AssetDecision {
  uint8 hash[32];
  uint8 allow;  // 1 = always-allow recorded for this hash
} AssetDecision;
#define kAssetDecisions_Max 16
static AssetDecision g_asset_decisions[kAssetDecisions_Max];
static uint8 g_asset_decisions_count = 0;

static bool AssetDecision_FindAllow(const uint8 hash[32]) {
  for (uint8 i = 0; i < g_asset_decisions_count; ++i) {
    if (memcmp(g_asset_decisions[i].hash, hash, 32) == 0) {
      return g_asset_decisions[i].allow != 0;
    }
  }
  return false;
}

static void AssetDecision_Persist(const uint8 hash[32]) {
  // Update in-place if hash already recorded.
  for (uint8 i = 0; i < g_asset_decisions_count; ++i) {
    if (memcmp(g_asset_decisions[i].hash, hash, 32) == 0) {
      g_asset_decisions[i].allow = 1;
      return;
    }
  }
  if (g_asset_decisions_count >= kAssetDecisions_Max) return;
  AssetDecision *d = &g_asset_decisions[g_asset_decisions_count++];
  memcpy(d->hash, hash, 32);
  d->allow = 1;
  // Phase A: persistence is in-memory only. config.c's HandleIniConfig
  // already accepts the new [RandoAssetDecisions] section so future INI
  // edits by the user (or by a future INI-writer task) survive a launch.
  // Until the writer lands, the decision persists for the current session.
  fprintf(stderr, "[RandoAssetDecisions] recorded always-allow for hash ");
  for (int i = 0; i < 8; ++i) fprintf(stderr, "%02x", hash[i]);
  fprintf(stderr, "...\n");
}

// Called from config.c's [RandoAssetDecisions] parser to populate the
// session lookup from zelda3.ini at startup.
void Rando_RegisterAssetDecisionFromIni(const uint8 hash[32]) {
  if (g_asset_decisions_count >= kAssetDecisions_Max) return;
  AssetDecision *d = &g_asset_decisions[g_asset_decisions_count++];
  memcpy(d->hash, hash, 32);
  d->allow = 1;
}

// ---------------------------------------------------------------------------
// Seed-field parsing helpers.
// ---------------------------------------------------------------------------
static bool ParseSeedField(const char *s, uint64 *out_seed) {
  if (s == NULL || *s == 0) return false;
  uint64 v = 0;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    if (*s == 0) return false;
    while (*s) {
      char c = *s++;
      uint8 d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else return false;
      v = (v << 4) | d;
    }
  } else {
    while (*s) {
      char c = *s++;
      if (c < '0' || c > '9') return false;
      v = v * 10 + (uint64)(c - '0');
    }
  }
  *out_seed = v;
  return true;
}

// Derive a u64 from current state when the seed field is empty. Mixes the
// settings_hash with a session-incrementing counter so successive Generate
// presses without an explicit seed produce different seeds. Deterministic
// per-session: the seed_u64 is reproducible if the user notes its value
// (which is encoded into the share string).
static uint64 g_settings_generation_counter = 0;
static uint64 DeriveSeedFromState(void) {
  // SHA-256 of (settings_hash[16] + counter[8] + frame_counter[2]) → take
  // first 8 bytes as u64. Cheap and good enough.
  uint8 buf[32];
  memset(buf, 0, sizeof(buf));
  memcpy(buf, g_settings_hash_short, 16);
  g_settings_generation_counter++;
  for (int i = 0; i < 8; ++i) {
    buf[16 + i] = (uint8)((g_settings_generation_counter >> (i * 8)) & 0xff);
  }
  // frame_counter is uint8 — mix into a single byte; the generation
  // counter above already provides per-Generate-press variation.
  buf[24] = (uint8)frame_counter;
  uint8 digest[32];
  sha256_buffer(buf, 32, digest);
  uint64 r = 0;
  for (int i = 0; i < 8; ++i) r |= ((uint64)digest[i]) << (i * 8);
  return r;
}

// ---------------------------------------------------------------------------
// Settings-row helpers — get a label + a current-value string for any row.
// ---------------------------------------------------------------------------
static const char *RowLabel(int row) {
  switch (row) {
    case kRow_Preset:                    return "PRESET";
    case kRow_WorldState:                return "WORLD";
    case kRow_Goal:                      return "GOAL";
    case kRow_CrystalsGanon:             return "GANON";
    case kRow_CrystalsTower:             return "TOWER";
    case kRow_ItemPoolDifficulty:        return "POOL";
    case kRow_DungeonSmallKeys:          return "SMKEYS";
    case kRow_DungeonBigKeys:            return "BGKEYS";
    case kRow_DungeonMaps:               return "MAPS";
    case kRow_DungeonCompasses:          return "COMPS";
    case kRow_PiecesRequired:            return "PCS REQ";
    case kRow_PiecesPlaced:              return "PCS PLC";
    case kRow_ModeWeapons:               return "WEAPONS";
    case kRow_Accessibility:             return "ACCESS";
    case kRow_PrizeShuffle:              return "PRIZE";
    case kRow_MedallionShuffle:          return "MEDAL";
    case kRow_RaceMode:                  return "RACE";
    case kRow_Hints:                     return "HINTS";
    case kRow_EntranceShuffle_Disabled:  return "ENT B";
    case kRow_EnemyShuffle_Disabled:     return "ENEMY B";
    case kRow_BossShuffle_Disabled:      return "BOSS B";
    case kRow_Glitches_Disabled:         return "GLITCH B";
    case kRow_Recommended:               return "REC FX";
    case kRow_Seed:                      return "SEED";
    case kRow_Generate:                  return "GENERATE";
    default:                             return "ERR";
  }
}

static const char *RowValueText(int row, char *scratch, int scratch_len) {
  const RandoSettings *s = &g_settings_working;
  switch (row) {
    case kRow_Preset:
      return Settings_PresetName((SettingsPreset)g_settings_preset_index);
    case kRow_WorldState:
      return SelectFile_WorldStateAbbrev(s->world_state);
    case kRow_Goal:
      return SelectFile_GoalAbbrev(s->goal);
    case kRow_CrystalsGanon:
      snprintf(scratch, scratch_len, "%u OF 7", (unsigned)s->crystals_ganon);
      return scratch;
    case kRow_CrystalsTower:
      snprintf(scratch, scratch_len, "%u OF 7", (unsigned)s->crystals_tower);
      return scratch;
    case kRow_ItemPoolDifficulty:
      switch (s->item_pool_difficulty) {
        case kItemPoolDifficulty_Easy:   return "EASY";
        case kItemPoolDifficulty_Normal: return "NORM";
        case kItemPoolDifficulty_Hard:   return "HARD";
        case kItemPoolDifficulty_Expert: return "EXPRT";
        default:                         return "ERR";
      }
    case kRow_DungeonSmallKeys:
    case kRow_DungeonBigKeys:
    case kRow_DungeonMaps:
    case kRow_DungeonCompasses: {
      uint8 mode = (row == kRow_DungeonSmallKeys) ? s->dungeon_small_keys_mode
                 : (row == kRow_DungeonBigKeys) ? s->dungeon_big_keys_mode
                 : (row == kRow_DungeonMaps) ? s->dungeon_maps_mode
                 : s->dungeon_compasses_mode;
      switch (mode) {
        case kDungeonItemMode_Vanilla: return "VAN";
        case kDungeonItemMode_Dungeon: return "DNG";
        case kDungeonItemMode_Wild:    return "WLD";
        default:                       return "ERR";
      }
    }
    case kRow_PiecesRequired:
      // Show "-" sentinel when the goal doesn't use pieces, signalling
      // to the user that the field is inert.
      if (s->goal != kGoal_TriforceHunt && s->goal != kGoal_GanonHunt)
        return "-";
      snprintf(scratch, scratch_len, "%u", (unsigned)s->pieces_required);
      return scratch;
    case kRow_PiecesPlaced:
      if (s->goal != kGoal_TriforceHunt && s->goal != kGoal_GanonHunt)
        return "-";
      snprintf(scratch, scratch_len, "%u", (unsigned)s->pieces_placed);
      return scratch;
    case kRow_ModeWeapons:
      switch (s->mode_weapons) {
        case kModeWeapons_Randomized: return "RAND";
        case kModeWeapons_Assured:    return "ASUR";
        default:                      return "ERR";
      }
    case kRow_Accessibility:
      switch (s->accessibility) {
        case kAccessibility_Items:     return "ITEMS";
        case kAccessibility_Locations: return "LOCS";
        case kAccessibility_None:      return "NONE";
        default:                       return "ERR";
      }
    case kRow_PrizeShuffle:
      return s->prize_shuffle ? "ON" : "OFF";
    case kRow_MedallionShuffle:
      return s->medallion_shuffle ? "ON" : "OFF";
    case kRow_RaceMode:
      return s->race_mode ? "ON" : "OFF";
    case kRow_Hints:
      return s->hints ? "ON" : "OFF";
    case kRow_EntranceShuffle_Disabled:
    case kRow_EnemyShuffle_Disabled:
    case kRow_BossShuffle_Disabled:
    case kRow_Glitches_Disabled:
      return "OFF";
    case kRow_Recommended:
      return "GO";
    case kRow_Seed: {
      // Show "AUTO" when empty, else the buffer contents (truncated).
      // Parentheses dropped because the file-select font has no verified
      // glyph for them (TileForAscii would map them to blanks now).
      if (g_settings_seed_field.len == 0) return "AUTO";
      int n = g_settings_seed_field.len;
      if (n > scratch_len - 1) n = scratch_len - 1;
      memcpy(scratch, g_settings_seed_field.buf, n);
      scratch[n] = 0;
      return scratch;
    }
    case kRow_Generate:
      return g_settings_generate_in_progress ? "BUSY" : "GO";
    default:
      return "ERR";
  }
}

// Recompute the cached settings_hash whenever the working settings mutate.
static void SettingsHashRefresh(void) {
  Settings_HashShort(&g_settings_working, g_settings_hash_short);
}

// Validate piece-count invariants after any cycle that touches them.
static void SettingsValidatePieces(void) {
  RandoSettings *s = &g_settings_working;
  // Cap pieces_placed in a sane range so the UI doesn't allow nonsense.
  if (s->pieces_placed < 1) s->pieces_placed = 1;
  if (s->pieces_placed > 99) s->pieces_placed = 99;
  if (s->pieces_required < 1) s->pieces_required = 1;
  if (s->pieces_required > s->pieces_placed) s->pieces_required = s->pieces_placed;
}

// Cycle a row's value forward (delta=+1) or backward (delta=-1). Bool rows
// treat any nonzero delta as toggle.
static void CycleRow(int row, int delta) {
  RandoSettings *s = &g_settings_working;
  bool mutated = true;
  switch (row) {
    case kRow_Preset: {
      int n = (int)g_settings_preset_index + delta;
      if (n < 0) n = kPreset__Count - 1;
      if (n >= kPreset__Count) n = 0;
      g_settings_preset_index = (uint8)n;
      // HINTS is a UI-scoped axis (defaulted ON in SelectFile_Settings_Activate),
      // not a preset axis. Settings_ApplyPreset runs Settings_SetDefaults which
      // resets hints to OFF; preserve the user's HINTS choice across a preset
      // cycle so cycling PRESET doesn't silently turn hints off.
      uint8 saved_hints = s->hints;
      Settings_ApplyPreset((SettingsPreset)g_settings_preset_index, s);
      s->hints = saved_hints;
      break;
    }
    case kRow_WorldState: {
      // Phase A re-scope: Inverted (2) and Retro (3) require a Phase B logic
      // Phase B Slice 2 (2026-05-27): Inverted is now enabled — the
      // world-state-aware codegen + runtime VM merge lands in commits
      // c999af2/24e7987/86b0629. End-to-end CLI smoke shows 1/237
      // unreachable placements for a representative Inverted seed; small
      // graph gaps remain (task list #47-50) but the picker is functional.
      //
      // Retro un-gated as of Slice 3a (2026-05-27): 29 shop locations land
      // in the placement pool under world_state=Retro per
      // `openspec/changes/add-rando-retro-world-state/design.md` §1a.
      // The 22 TakeAny shops remain Slice 3b scope; players who select
      // Retro will see the regular-shop and Capacity-Upgrade randomization
      // but TakeAny caves stay vanilla until 3b ships sprite dispatch.
      //
      // The display label is preserved for all 4 world states so a slot
      // loaded from a Phase-B share string round-trips its label.
      int n = (int)s->world_state + delta;
      if (n < 0) n = kWorldState_Retro;          // wrap to last enabled
      if (n > kWorldState_Retro) n = 0;
      s->world_state = (uint8)n;
      break;
    }
    case kRow_Goal: {
      int n = (int)s->goal + delta;
      if (n < 0) n = kGoal_Completionist;
      if (n > kGoal_Completionist) n = 0;
      s->goal = (uint8)n;
      // Completionist auto-sets accessibility=Locations per the canonical
      // serializer rule.
      if (s->goal == kGoal_Completionist) s->accessibility = kAccessibility_Locations;
      break;
    }
    case kRow_CrystalsGanon: {
      int n = (int)s->crystals_ganon + delta;
      if (n < 0) n = 7;
      if (n > 7) n = 0;
      s->crystals_ganon = (uint8)n;
      break;
    }
    case kRow_CrystalsTower: {
      int n = (int)s->crystals_tower + delta;
      if (n < 0) n = 7;
      if (n > 7) n = 0;
      s->crystals_tower = (uint8)n;
      break;
    }
    case kRow_ItemPoolDifficulty: {
      int n = (int)s->item_pool_difficulty + delta;
      if (n < 0) n = kItemPoolDifficulty_Expert;
      if (n > kItemPoolDifficulty_Expert) n = 0;
      s->item_pool_difficulty = (uint8)n;
      break;
    }
    case kRow_DungeonSmallKeys:
    case kRow_DungeonBigKeys:
    case kRow_DungeonMaps:
    case kRow_DungeonCompasses: {
      uint8 *mode = (row == kRow_DungeonSmallKeys) ? &s->dungeon_small_keys_mode
                  : (row == kRow_DungeonBigKeys) ? &s->dungeon_big_keys_mode
                  : (row == kRow_DungeonMaps) ? &s->dungeon_maps_mode
                  : &s->dungeon_compasses_mode;
      int n = (int)*mode + delta;
      if (n < 0) n = kDungeonItemMode_Wild;
      if (n > kDungeonItemMode_Wild) n = 0;
      *mode = (uint8)n;
      break;
    }
    case kRow_PiecesRequired: {
      // Pieces fields are only meaningful for Triforce Hunt / Ganon Hunt
      // goals; refuse cycle otherwise so the user doesn't accidentally
      // mutate the field (which would change settings_hash without
      // visible effect on placement).
      if (s->goal != kGoal_TriforceHunt && s->goal != kGoal_GanonHunt) {
        mutated = false;
        sound_effect_1 = 0x3c;
        break;
      }
      int n = (int)s->pieces_required + delta;
      if (n < 1) n = (int)s->pieces_placed;  // wrap to max
      if (n > (int)s->pieces_placed) n = 1;
      s->pieces_required = (uint16)n;
      break;
    }
    case kRow_PiecesPlaced: {
      // Same gating as PiecesRequired.
      if (s->goal != kGoal_TriforceHunt && s->goal != kGoal_GanonHunt) {
        mutated = false;
        sound_effect_1 = 0x3c;
        break;
      }
      int n = (int)s->pieces_placed + delta;
      if (n < 1) n = 99;
      if (n > 99) n = 1;
      s->pieces_placed = (uint16)n;
      SettingsValidatePieces();
      break;
    }
    case kRow_ModeWeapons: {
      // Phase A supports only Randomized + Assured for mode.weapons.
      int n = (int)s->mode_weapons + delta;
      if (n < kModeWeapons_Randomized) n = kModeWeapons_Assured;
      if (n > kModeWeapons_Assured) n = kModeWeapons_Randomized;
      s->mode_weapons = (uint8)n;
      break;
    }
    case kRow_Accessibility: {
      // Items / Locations / None — Completionist goal auto-locks to Locations
      // (kRow_Goal sets it on cycle); cycling here while goal=Completionist
      // still allows the user to view but the goal-cycle will reassert.
      // Phase B Slice 4 added `None` (opt-in to possibly-unwinnable seeds).
      int n = (int)s->accessibility + delta;
      if (n < kAccessibility_Items) n = kAccessibility_None;
      if (n > kAccessibility_None) n = kAccessibility_Items;
      s->accessibility = (uint8)n;
      // Don't break the Completionist invariant.
      if (s->goal == kGoal_Completionist) s->accessibility = kAccessibility_Locations;
      break;
    }
    case kRow_PrizeShuffle: s->prize_shuffle ^= 1; break;
    case kRow_MedallionShuffle: s->medallion_shuffle ^= 1; break;
    case kRow_RaceMode: s->race_mode ^= 1; break;
    case kRow_Hints: s->hints ^= 1; break;
    default:
      mutated = false;
      break;
  }
  if (mutated) SettingsHashRefresh();
}

// Helpers for tile-stream emission.
static void emit_tile_pair(uint8 *cmd, int *o, uint8 tile, uint8 attr) {
  cmd[(*o)++] = tile;
  cmd[(*o)++] = attr;
}

// Map a printable ASCII char to a file-select font tile index. Falls back to
// the blank tile for chars not in the limited font (no lowercase support).
static uint8 TileForAscii(char c) {
  if (c >= 'A' && c <= 'Z') return SelectFile_TileForBase32(c);
  if (c >= 'a' && c <= 'z') return SelectFile_TileForBase32((char)(c - 'a' + 'A'));
  if (c >= '0' && c <= '9') {
    // Use the 16-px-tall digit pair from the slot-number font region:
    // 0xe6..0xef for top halves, 0xf6..0xff for bottom halves (verified
    // by the slot row tiles in kSelectFile_Func3_Data: slot 1's "1"
    // uses 0xe7 top + 0xf7 bot, slot 2's "2" uses 0xe8 + 0xf8, etc.).
    // The +0x10 top→bot offset that emit_text_run assumes holds for
    // this range. Previously TileForAscii returned 0x76+digit, the
    // 8-px-tall half-height digits from the name-entry font; those
    // have no matching bottom-half companions in the file-select
    // font, so emit_text_run rendered garbage glyphs in the bottom row.
    return (uint8)(0xe6 + (c - '0'));
  }
  // The file-select font has no verified glyphs for typical punctuation.
  // Render '-', '(', ')', '?', etc. as a blank rather than as random
  // letter-like glyphs from the 0x2a..0x2f range. Callers needing a
  // visible separator should use a space instead.
  if (c == ' ') return kFileSelectTile_Blank;
  if (c == 0) return kFileSelectTile_Blank;
  return kFileSelectTile_Blank;
}

// Emit one tilemap-row half (top OR bottom) of a horizontal text run as a
// tile-stream command at the given VRAM address. `attr` = palette/priority
// byte; HandleStripes14 count byte is (chars*2)-1.
// `tile_offset` is added to each glyph's base tile index — 0 for the top
// half, 0x10 for the bottom half (the file-select font stores top/bottom
// halves of each glyph 16 tiles apart). Caller's `cmd` buffer must have
// room for 4 header bytes + chars*2 payload bytes.
static int emit_text_run_half(uint8 *cmd, int o, uint16 vram_addr,
                              const char *text, int max_chars, uint8 attr,
                              uint8 tile_offset) {
  cmd[o++] = (uint8)(vram_addr >> 8);
  cmd[o++] = (uint8)(vram_addr & 0xff);
  cmd[o++] = 0;
  cmd[o++] = (uint8)((max_chars * 2) - 1);
  for (int i = 0; i < max_chars; ++i) {
    char c = text[i];
    if (c == 0) {
      for (int j = i; j < max_chars; ++j) {
        cmd[o++] = (uint8)(kFileSelectTile_Blank + tile_offset); cmd[o++] = attr;
      }
      break;
    }
    cmd[o++] = (uint8)(TileForAscii(c) + tile_offset); cmd[o++] = attr;
  }
  return o;
}

// Emit BOTH the top and bottom halves of a horizontal text run, since the
// file-select font is 16 pixels tall (each glyph spans two tilemap rows).
// The bottom half is written one tilemap row below (VRAM word offset +0x20)
// with tile indices shifted by +0x10. Callers spacing rows by 0x40 word
// units (= 16 px) get correctly stacked text with no overlap.
static int emit_text_run(uint8 *cmd, int o, uint16 vram_addr,
                         const char *text, int max_chars, uint8 attr) {
  o = emit_text_run_half(cmd, o, vram_addr, text, max_chars, attr, 0x00);
  o = emit_text_run_half(cmd, o, (uint16)(vram_addr + 0x20), text, max_chars,
                         attr, 0x10);
  return o;
}

// Stripes memset: write `num_words` copies of (tile=0xa9 attr=0x18) into
// consecutive tilemap entries from `vram_addr`. HandleStripes14 encodes the
// length as BYTES-MINUS-ONE then halves it (because memset writes one word
// per source-byte pair). So count_field = num_words*2 - 1, split across
// the attr byte's low 6 bits and the length byte.
static int emit_clear_area(uint8 *cmd, int o, uint16 vram_addr, int num_words) {
  int count = num_words * 2 - 1;
  cmd[o++] = (uint8)(vram_addr >> 8);
  cmd[o++] = (uint8)(vram_addr & 0xff);
  // Bit 6 = is_memset; low 6 bits = high 6 bits of count.
  cmd[o++] = (uint8)(0x40 | ((count >> 8) & 0x3f));
  cmd[o++] = (uint8)(count & 0xff);
  cmd[o++] = 0xa9;  // fill word low (tile = blank)
  cmd[o++] = 0x18;  // fill word high (palette 6, no flip/prio)
  return o;
}

// Visible row count (per-screen). File-select glyphs are 16 px tall (two
// tilemap rows per glyph), so a row's stride is 0x40 word units = 16 px.
// Budget: title (16) + 7 rows × 16 (112) + hash (16) + generate (16) =
// 160 px of content, well within the 224 px play region.
#define kSettingsVisibleRows 7

// ---------------------------------------------------------------------------
// Activate / deactivate.
// ---------------------------------------------------------------------------
static void SelectFile_Settings_Activate(uint8 target_slot,
                                          bool prepopulate_from_share) {
  g_settings_active = true;
  g_settings_view = kSettingsView_Main;
  g_settings_target_slot = target_slot;
  g_settings_cursor = kRow_Preset;
  g_settings_scroll_offset = 0;
  g_settings_preset_index = kPreset_OpenGanon;
  Settings_ApplyPreset(kPreset_OpenGanon, &g_settings_working);
  // Default the in-game HINTS row to ON (user preference). This is UI-scoped:
  // the global Settings_SetDefaults (used by CLI / corpus) stays hints=OFF, so
  // default-settings placement/spoiler stamps are unchanged. The player can
  // still toggle HINTS off in the menu before generating.
  g_settings_working.hints = 1;
  TextField_Init(&g_settings_seed_field, /*base32_only=*/false);
  g_settings_seed_field.active = false;  // not focused by default
  g_settings_seed_parse_ok = true;
  g_settings_seed_value = 0;
  g_settings_generate_in_progress = false;
  g_rec_cursor = 0;
  g_rec_working_features0 = g_config.features0;
  g_asset_warn_cursor = 0;
  g_asset_warn_pending = false;
  if (prepopulate_from_share) {
    // Use the decoded seed_u64 from the alphabet-picker submit path. The
    // user can still edit the seed (or settings) before pressing Generate.
    g_settings_seed_prepopulated = true;
    g_settings_prepopulated_seed = g_alphabet_decoded_seed;
    // Write the seed value into the text field as decimal.
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)g_alphabet_decoded_seed);
    TextField_PasteString(&g_settings_seed_field, buf);
  } else {
    g_settings_seed_prepopulated = false;
    g_settings_prepopulated_seed = 0;
  }
  SettingsHashRefresh();
}

static void SelectFile_Settings_Deactivate(void) {
  // Module_SelectFile_0 init calls this for state-reset purposes, BEFORE
  // submodules 1 and 2 have run (the slot-tile upload + triforce erase).
  // Unconditionally rewinding to submodule 3 here would bypass those,
  // leaving first-launch file-select without slot frames. Only restore
  // VRAM when we were ACTIVELY in the settings screen (i.e. our draw
  // clobbered vram_upload_data).
  bool was_active = g_settings_active;
  g_settings_active = false;
  g_settings_view = kSettingsView_Main;
  g_settings_generate_in_progress = false;
  g_settings_seed_field.active = false;
  g_asset_warn_session_bypass = false;  // don't leak past session
  if (was_active) {
    // Settings_DrawMain wrote text + blanks into BG2 tilemap rows 8-27.
    // The slot-rebuild flow below (submodule 3 -> 4) restores slot rows
    // 9-10, 13-14, 17-18, but the OTHER settings cells (title, label
    // columns, hash row, etc.) stay stale and bleed back into the file-
    // select screen. Push one final clear into vram_upload_data NOW so
    // the next NMI blanks those cells; the same clear pattern is used in
    // Settings_DrawMain (rows 8-27, all cols) and is known not to touch
    // PLAYER SELECT / COPY PLAYER / ERASE PLAYER (those live on a
    // different VRAM layer).
    uint8 *p = (uint8 *)vram_upload_data;
    int o = 0;
    int clear_count = 640 * 2 - 1;
    p[o++] = 0x61;
    p[o++] = 0x00;
    p[o++] = (uint8)(0x40 | ((clear_count >> 8) & 0x3f));
    p[o++] = (uint8)(clear_count & 0xff);
    p[o++] = 0xa9;
    p[o++] = 0x18;
    p[o++] = 0xff;  // stripes terminator
    nmi_load_bg_from_vram = 1;
    // Same VRAM-restore discipline as the alphabet picker — settings draw
    // memcpys into vram_upload_data, clobbering kSelectFile_Func3_Data.
    // Rewind to submodule 3 to reinstall it before the slot list resumes.
    submodule_index = 3;
    subsubmodule_index = 0;
  }
}

// ---------------------------------------------------------------------------
// Rendering.
// ---------------------------------------------------------------------------
static void SelectFile_Settings_DrawMain(void) {
  uint8 cmd[2048];
  int o = 0;
  // Clear the entire settings area first so stale VRAM from prior screens
  // (kind picker text, title-screen text, etc.) doesn't peek through the
  // gaps between label/value columns. Covers rows 8-27 (20 rows × 32 cols
  // = 640 word cells), which encompasses the title, all 7 settings rows
  // plus their bottom halves, the hash row, and the generate indicator row.
  o = emit_clear_area(cmd, o, 0x6100, 640);
  // Title at the very top: "RANDO SETTINGS" (14 chars).
  o = emit_text_run(cmd, o, 0x6108, "RANDO SETTINGS", 14, 0x18);

  // Render the visible window of rows. Each row occupies one tilemap row
  // (32 entries = 64 bytes; we use the leftmost ~24 cells).
  uint16 row_base_vram = 0x6148;  // ~4 tile rows below title
  for (int i = 0; i < kSettingsVisibleRows; ++i) {
    int row = (int)g_settings_scroll_offset + i;
    if (row >= kRow__Count) break;
    uint16 vram = (uint16)(row_base_vram + i * 0x40);
    bool selected = (row == (int)g_settings_cursor);
    uint8 attr = selected ? 0x38 : 0x18;
    // Disabled rows: the cluster author wanted a "greyed palette" for the
    // Phase-B coming-soon rows, but the chosen attr value 0x78 / 0x58
    // actually has bit 6 (x-flip) set, which mirrors the tiles —
    // "ENT B" rendered as "3HT 8" on screen. Until a verified greyed
    // palette index is sourced, render disabled rows with the same attr
    // as enabled rows. The "disabled" semantics are still enforced in
    // input handling (A-press plays the refusal sound), just not
    // visually differentiated for now.
    // Label (10 chars) at cols 8-17.
    const char *label = RowLabel(row);
    o = emit_text_run(cmd, o, vram, label, 10, attr);
    // Value (8 chars) on the SAME tilemap row at col 20 (= vram + 12 word
    // units past col 8). The original code used vram+24, which wraps past
    // col 32 in the 32-wide BG2 tilemap and dropped the value onto col 0
    // of the next row.
    char scratch[32];
    const char *val = RowValueText(row, scratch, sizeof(scratch));
    o = emit_text_run(cmd, o, (uint16)(vram + 12), val, 8, attr);
  }
  // Settings hash row (label + 16 hex chars), one 16-px row below the last
  // visible settings row. With row_base=0x6148 and 7 visible rows, the
  // bottom of row 6 is at 0x62e8; the hash row sits at 0x6308.
  o = emit_text_run(cmd, o, 0x6308, "HASH ", 5, 0x18);
  char hashbuf[32];
  static const char kHex[] = "0123456789ABCDEF";
  for (int i = 0; i < 8; ++i) {
    hashbuf[i * 2] = kHex[(g_settings_hash_short[i] >> 4) & 0xf];
    hashbuf[i * 2 + 1] = kHex[g_settings_hash_short[i] & 0xf];
  }
  hashbuf[16] = 0;
  o = emit_text_run(cmd, o, 0x6310, hashbuf, 16, 0x18);
  // Scroll indicators sit at column 0 of the title row (UP) and hash row
  // (DN), well clear of the centered settings rows that start at column 8.
  if (g_settings_scroll_offset > 0) {
    o = emit_text_run(cmd, o, 0x6100, "UP", 2, 0x18);
  }
  if (g_settings_scroll_offset + kSettingsVisibleRows < kRow__Count) {
    o = emit_text_run(cmd, o, 0x6300, "DN", 2, 0x18);
  }
  // Re-fire indicator below the hash row.
  if (g_settings_generate_in_progress) {
    o = emit_text_run(cmd, o, 0x6348, "GENERATING", 10, 0x18);
  }
  cmd[o++] = 0xff;
  memcpy(vram_upload_data, cmd, (size_t)o);

  // Cursor fairy at the currently-selected row. Visible-window index =
  // cursor - scroll_offset; each visible row is 16 screen pixels tall
  // starting at screen y ≈ 75 (row 0). Fairy sits at row_top + 7 to match
  // the slot-list fairy offset convention (kSelectFile_Faerie_Y[0]=0x4a
  // vs kSelectFile_Draw_Y[0]=0x43 → +7 px). X=0x18 places it just left
  // of the col-8 label column.
  int visible_idx = (int)g_settings_cursor - (int)g_settings_scroll_offset;
  if (visible_idx >= 0 && visible_idx < kSettingsVisibleRows) {
    uint8 fy = (uint8)(0x52 + visible_idx * 0x10);
    FileSelect_DrawFairy(0x18, fy);
  }
}

static void SelectFile_Settings_DrawRecommended(void) {
  uint8 cmd[2048];
  int o = 0;
  // Clear the panel area first so unwritten columns don't expose stale
  // VRAM from prior screens. Covers rows 7-28 (the title at row 7, 11 rec
  // rows starting at row 9, each consuming 2 tilemap rows = 22 rows of
  // tile cells × 32 cols ≈ 704 word cells).
  o = emit_clear_area(cmd, o, 0x60e0, 704);
  // 16-px-tall glyphs, no scrolling — fit 11 rows + title + B BACK hint in
  // the 224 px play region. Title is shifted up to 0x60e8 (row 7) so the
  // last row's bottom half lands at row 28 (y=219), within screen.
  o = emit_text_run(cmd, o, 0x60e8, "REC FEATURES", 12, 0x18);
  // B BACK hint sits inline with the title at column 22 — outside the
  // 14-column wide row-label area below, so it never collides with the
  // settings rows.
  o = emit_text_run(cmd, o, 0x60f6, "B BACK", 6, 0x18);
  uint16 row_base = 0x6128;
  for (int i = 0; i < kRecRow__Count; ++i) {
    uint16 vram = (uint16)(row_base + i * 0x40);
    bool selected = (i == (int)g_rec_cursor);
    uint8 attr = selected ? 0x38 : 0x18;
    o = emit_text_run(cmd, o, vram, kRecRowLabels[i], 14, attr);
    if (i < kRecRow_ApplyAll) {
      bool on = (g_rec_working_features0 & kRecRowBits[i]) != 0;
      // Place value on the SAME tilemap row as the label, at col 24 (label
      // spans cols 8-21). vram already targets col 8, so +16 → col 24. The
      // 32-wide BG2 tilemap wraps any offset past col 32 to the next row.
      o = emit_text_run(cmd, o, (uint16)(vram + 16), on ? "ON" : "OFF", 4, attr);
    }
  }
  cmd[o++] = 0xff;
  memcpy(vram_upload_data, cmd, (size_t)o);
}

static void SelectFile_Settings_DrawAssetWarn(void) {
  uint8 cmd[2048];
  int o = 0;
  // Clear before drawing — see Settings_DrawMain comment. Covers rows 8-25
  // (18 rows × 32 cols = 576 word cells).
  o = emit_clear_area(cmd, o, 0x6100, 576);
  o = emit_text_run(cmd, o, 0x6108, "ASSETS DIFFER", 13, 0x18);
  o = emit_text_run(cmd, o, 0x6148, "NON VANILLA", 11, 0x18);
  o = emit_text_run(cmd, o, 0x6188, "ASSETS DETECTED", 15, 0x18);
  static const char *kLabels[3] = { "ALWAYS ALLOW", "ALLOW ONCE", "CANCEL" };
  for (int i = 0; i < 3; ++i) {
    uint16 vram = (uint16)(0x6208 + i * 0x40);
    bool selected = (i == (int)g_asset_warn_cursor);
    uint8 attr = selected ? 0x38 : 0x18;
    o = emit_text_run(cmd, o, vram, kLabels[i], 14, attr);
  }
  cmd[o++] = 0xff;
  memcpy(vram_upload_data, cmd, (size_t)o);
}

static void SelectFile_Settings_Draw(void) {
  switch (g_settings_view) {
    case kSettingsView_Main:        SelectFile_Settings_DrawMain(); break;
    case kSettingsView_Recommended: SelectFile_Settings_DrawRecommended(); break;
    case kSettingsView_AssetWarn:   SelectFile_Settings_DrawAssetWarn(); break;
  }
}

// ---------------------------------------------------------------------------
// Input handling.
// ---------------------------------------------------------------------------
static void SettingsCursorMove(int delta) {
  int n = (int)g_settings_cursor + delta;
  if (n < 0) n = kRow__Count - 1;
  if (n >= kRow__Count) n = 0;
  g_settings_cursor = (uint8)n;
  // Scroll window so cursor stays in view.
  if (g_settings_cursor < g_settings_scroll_offset) {
    g_settings_scroll_offset = g_settings_cursor;
  } else if (g_settings_cursor >= g_settings_scroll_offset + kSettingsVisibleRows) {
    g_settings_scroll_offset =
        (uint8)((int)g_settings_cursor - kSettingsVisibleRows + 1);
  }
}

static bool SelectFile_Settings_HandleRecommendedInput(void) {
  // Cancel via B → back to main view.
  if (filtered_joypad_H & kJoypadH_B) {
    sound_effect_1 = 0x2c;
    g_settings_view = kSettingsView_Main;
    return true;
  }
  uint8 dir = filtered_joypad_H & 0xf;
  if (dir & kJoypadH_Up) {
    sound_effect_2 = 0x20;
    if (g_rec_cursor == 0) g_rec_cursor = kRecRow__Count - 1;
    else g_rec_cursor--;
    return true;
  }
  if (dir & kJoypadH_Down) {
    sound_effect_2 = 0x20;
    g_rec_cursor++;
    if (g_rec_cursor >= kRecRow__Count) g_rec_cursor = 0;
    return true;
  }
  if (filtered_joypad_L & kJoypadL_A) {
    sound_effect_1 = 0x2c;
    if (g_rec_cursor == kRecRow_ApplyAll) {
      // Flip every toggle to its recommended state.
      for (int i = 0; i < kRecRow_ApplyAll; ++i) {
        if (kRecRecommendedOn[i]) {
          g_rec_working_features0 |= kRecRowBits[i];
        } else {
          g_rec_working_features0 &= ~kRecRowBits[i];
        }
      }
    } else {
      // Toggle this row.
      g_rec_working_features0 ^= kRecRowBits[g_rec_cursor];
    }
    return true;
  }
  return true;  // swallow all input while in this view
}

static bool SelectFile_Settings_HandleAssetWarnInput(void) {
  uint8 dir = filtered_joypad_H & 0xf;
  if (dir & kJoypadH_Up) {
    sound_effect_2 = 0x20;
    if (g_asset_warn_cursor == 0) g_asset_warn_cursor = kAssetWarnChoice__Count - 1;
    else g_asset_warn_cursor--;
    return true;
  }
  if (dir & kJoypadH_Down) {
    sound_effect_2 = 0x20;
    g_asset_warn_cursor++;
    if (g_asset_warn_cursor >= kAssetWarnChoice__Count) g_asset_warn_cursor = 0;
    return true;
  }
  if ((filtered_joypad_L & kJoypadL_A) || (filtered_joypad_H & kJoypadH_Start)) {
    sound_effect_1 = 0x2c;
    switch (g_asset_warn_cursor) {
      case kAssetWarnChoice_AlwaysAllow:
        AssetDecision_Persist(g_assets_hash);
        g_asset_warn_pending = false;
        g_settings_view = kSettingsView_Main;
        // Proceed immediately to generation.
        SelectFile_Settings_HandleGenerate();
        break;
      case kAssetWarnChoice_AllowOnce:
        g_asset_warn_pending = false;
        g_asset_warn_session_bypass = true;  // consumed by next HandleGenerate call
        g_settings_view = kSettingsView_Main;
        SelectFile_Settings_HandleGenerate();
        break;
      case kAssetWarnChoice_Cancel:
      default:
        g_asset_warn_pending = false;
        g_settings_view = kSettingsView_Main;
        break;
    }
    return true;
  }
  if (filtered_joypad_H & kJoypadH_B) {
    sound_effect_1 = 0x2c;
    g_asset_warn_pending = false;
    g_settings_view = kSettingsView_Main;
    return true;
  }
  return true;
}

static bool SelectFile_Settings_Update(void) {
  if (!g_settings_active) return false;
  SelectFile_Settings_Draw();

  // Dispatch input by view.
  if (g_settings_view == kSettingsView_Recommended) {
    return SelectFile_Settings_HandleRecommendedInput();
  }
  if (g_settings_view == kSettingsView_AssetWarn) {
    return SelectFile_Settings_HandleAssetWarnInput();
  }

  // Cancel via B → back to kind picker on the target slot.
  if (filtered_joypad_H & kJoypadH_B) {
    sound_effect_1 = 0x3c;
    SelectFile_Settings_Deactivate();
    g_kind_picker_active = 1;
    g_kind_picker_cursor = 0;
    return true;
  }

  // Cursor up/down.
  uint8 dir = filtered_joypad_H & 0xf;
  if (dir & kJoypadH_Up) {
    sound_effect_2 = 0x20;
    SettingsCursorMove(-1);
    return true;
  }
  if (dir & kJoypadH_Down) {
    sound_effect_2 = 0x20;
    SettingsCursorMove(1);
    return true;
  }

  int row = (int)g_settings_cursor;
  // Left/Right cycle the current row's value (multi-option enums + sliders).
  if (dir & kJoypadH_Left) {
    sound_effect_2 = 0x20;
    CycleRow(row, -1);
    return true;
  }
  if (dir & kJoypadH_Right) {
    sound_effect_2 = 0x20;
    CycleRow(row, +1);
    return true;
  }

  // A button — context-dependent. While the seed text field is active
  // (focused for typing), swallow A so it doesn't re-trigger the row's
  // Clear-the-field action on every joypad poll.
  if ((filtered_joypad_L & kJoypadL_A) && !g_settings_seed_field.active) {
    sound_effect_1 = 0x2c;
    switch (row) {
      case kRow_Recommended:
        g_settings_view = kSettingsView_Recommended;
        g_rec_cursor = 0;
        break;
      case kRow_Generate:
        SelectFile_Settings_HandleGenerate();
        break;
      case kRow_Seed:
        // Seed-field activation is deferred — Phase A wires it to the text
        // input layer via a separate B+A combo or a tap of A on this row
        // that opens a numeric entry sub-prompt. For now, A on the seed row
        // just toggles base-prefix mode (decimal ↔ hex) for the next input.
        // The user can also paste via the alphabet picker (cluster 2).
        // Phase A simplification: A on the seed row clears the field so the
        // user can type a fresh value via the host SDL_TEXTINPUT path.
        // Don't clear if the field was prepopulated from an alphabet-
        // picker decoded share string — the user wants to view/edit,
        // not start over. Clear only when the field is empty (i.e.
        // fresh entry).
        if (!g_settings_seed_prepopulated)
          TextField_HandleKey(&g_settings_seed_field, kTextFieldKey_Clear);
        // Activate the field so the host routes SDL_TEXTINPUT here.
        // The g_rando_text_input_active edge in main.c clears
        // g_input1_state on activation so the A press that opened the
        // field doesn't strand as a held joypad bit.
        g_settings_seed_field.active = true;
        g_rando_active_textfield = &g_settings_seed_field;
        g_rando_text_input_active = true;
        g_rando_text_input_submit_pending = false;
        g_rando_text_input_cancel_pending = false;
        break;
      case kRow_Preset:
      case kRow_WorldState:
      case kRow_Goal:
      case kRow_ItemPoolDifficulty:
      case kRow_DungeonSmallKeys:
      case kRow_DungeonBigKeys:
      case kRow_DungeonMaps:
      case kRow_DungeonCompasses:
      case kRow_CrystalsGanon:
      case kRow_CrystalsTower:
      case kRow_PiecesRequired:
      case kRow_PiecesPlaced:
      case kRow_ModeWeapons:
      case kRow_Accessibility:
        CycleRow(row, +1);  // A = forward cycle, same as Right
        break;
      case kRow_PrizeShuffle:
      case kRow_MedallionShuffle:
      case kRow_RaceMode:
      case kRow_Hints:
        CycleRow(row, +1);  // bool toggle
        break;
      case kRow_EntranceShuffle_Disabled:
      case kRow_EnemyShuffle_Disabled:
      case kRow_BossShuffle_Disabled:
      case kRow_Glitches_Disabled:
        sound_effect_1 = 0x3c;  // refusal — Phase B feature
        break;
      default:
        break;
    }
    return true;
  }

  // Seed field text-input close: commit + parse on Start (joypad/gamepad
  // path) OR on the host-pending submit flag (PC keyboard Enter, suppressed
  // while text input is active). Cancel via the host-pending cancel flag
  // (PC keyboard Escape) restores the prior value.
  if (g_settings_seed_field.active) {
    bool submit = (filtered_joypad_H & kJoypadH_Start) != 0 ||
                  g_rando_text_input_submit_pending;
    bool cancel = g_rando_text_input_cancel_pending;
    if (submit || cancel) {
      g_rando_text_input_submit_pending = false;
      g_rando_text_input_cancel_pending = false;
      g_settings_seed_field.active = false;
      g_rando_text_input_active = false;
      g_rando_active_textfield = NULL;
      if (submit && g_settings_seed_field.len > 0) {
        uint64 v = 0;
        g_settings_seed_parse_ok = ParseSeedField(g_settings_seed_field.buf, &v);
        if (g_settings_seed_parse_ok) g_settings_seed_value = v;
        if (!g_settings_seed_parse_ok) sound_effect_1 = 0x3c;
      }
      return true;
    }
    // While typing, swallow all other input so cursor navigation doesn't
    // wander when the user types digit keys (which the keyboard→joypad
    // suppression would already block on PC, but gamepad players might
    // accidentally hit a direction).
    return true;
  }

  return true;  // swallow other input while active
}

// ---------------------------------------------------------------------------
// Generate action — §9.8.
//
// On press:
//   1. If asset-warn applies (non-vanilla assets + no persisted decision),
//      switch view to the asset-warn dialog and return. User confirms; this
//      function is re-invoked after their choice.
//   2. Parse seed input. Empty → derive from settings_hash + counter.
//   3. Run Place_AssumedFill.
//   4. Build the share string.
//   5. Write spoiler files (.json + .txt) to the spoiler directory.
//   6. Write the sidecar slot (kind=Randomizer, header populated).
//   7. Apply recommended-features panel choices to g_config.features0.
//   8. Reset sidecar cache so the rando banner renders next frame.
//   9. Transition back to file-select with the cursor on the target slot.
// ---------------------------------------------------------------------------
static void SelectFile_Settings_HandleGenerate(void) {
  // Re-fire guard — Generate is async-ish (placement takes a moment); a
  // double-press would otherwise re-enter mid-generation.
  if (g_settings_generate_in_progress) return;

  // Asset-warn gate. kVanillaAssetsHash / kVanillaAssetsHashKnown live in
  // src/rando/vanilla_assets_hash.h (included above; static linkage). The
  // session-bypass flag (set by Allow Once) consumes here so a subsequent
  // Generate within the same session continues to gate normally.
  bool consumed_bypass = g_asset_warn_session_bypass;
  g_asset_warn_session_bypass = false;
  if (kVanillaAssetsHashKnown &&
      memcmp(g_assets_hash, kVanillaAssetsHash, 32) != 0 &&
      !AssetDecision_FindAllow(g_assets_hash) &&
      !consumed_bypass &&
      g_settings_view != kSettingsView_AssetWarn) {
    // Show the dialog; user's choice re-enters Generate (Always/Allow Once)
    // or returns to settings (Cancel).
    g_settings_view = kSettingsView_AssetWarn;
    g_asset_warn_cursor = kAssetWarnChoice_Cancel;  // default-safe
    g_asset_warn_pending = true;
    return;
  }

  g_settings_generate_in_progress = true;
  SettingsValidatePieces();

  // Resolve seed.
  uint64 seed_u64 = 0;
  if (g_settings_seed_field.len > 0) {
    if (!ParseSeedField(g_settings_seed_field.buf, &seed_u64)) {
      fprintf(stderr, "[settings] bad seed input — refusing to generate\n");
      g_settings_generate_in_progress = false;
      sound_effect_1 = 0x3c;
      return;
    }
  } else if (g_settings_seed_prepopulated) {
    seed_u64 = g_settings_prepopulated_seed;
  } else {
    seed_u64 = DeriveSeedFromState();
  }

  // Compute settings_hash (already cached as short).
  uint8 settings_hash_full[32];
  Settings_ComputeHash(&g_settings_working, settings_hash_full);

  // Run placement.
  extern const uint32 kRandoLocationsCount;
  RandoPlacement *entries = (RandoPlacement *)calloc(kRandoLocationsCount,
                                                     sizeof(RandoPlacement));
  if (entries == NULL) {
    fprintf(stderr, "[settings] OOM allocating placement table\n");
    g_settings_generate_in_progress = false;
    sound_effect_1 = 0x3c;
    return;
  }
  RandoPlacementTable table = { entries, 0 };
  // Use a generous budget so even Triforce-Hunt configurations succeed.
  // Phase B Slice 6 audit H1 — race-mode generation must pass
  // budget_seconds=0 (no wall-clock cutoff) so the placer runs to its
  // deterministic kAssumedFillMaxAttempts cap. Reveal also passes 0; this
  // matches both sides so the stamp is reproducible across machines.
  int budget = (g_settings_working.race_mode != 0) ? 0 : 10;
  bool placed = Place_AssumedFill(&g_settings_working, seed_u64, budget, &table);
  if (!placed) {
    fprintf(stderr, "[settings] placement failed\n");
    free(entries);
    g_settings_generate_in_progress = false;
    sound_effect_1 = 0x3c;
    return;
  }

  // Build share string.
  ShareString ss;
  memset(&ss, 0, sizeof(ss));
  ss.version = (uint8)kGeneratorVersion;
  memcpy(ss.settings_hash, settings_hash_full, 16);
  ss.seed_u64 = seed_u64;
  char share_string[kShareStringBase32MaxLen];
  int share_len = Share_Encode(&ss, share_string, sizeof(share_string));
  if (share_len <= 0) {
    fprintf(stderr, "[settings] share string encode failed\n");
    free(entries);
    g_settings_generate_in_progress = false;
    sound_effect_1 = 0x3c;
    return;
  }

  // Pack the 31-byte raw binary blob via the public Share_PackBinary
  // helper so the trailing CRC is correct. Previously the code rebuilt
  // the blob inline and left CRC = 0, which meant the slot's stored
  // share_string base32-re-encoded to a string
  // DIFFERENT from the one Share_Encode emitted to the user — friends
  // who tried Share_Decode on the banner-displayed string would get
  // BadChecksum. The slot header reserves 32 bytes for share_string; we
  // zero-pad bytes 31..32 here for cleanliness.
  uint8 raw_binary[32];
  memset(raw_binary, 0, sizeof(raw_binary));
  Share_PackBinary(&ss, raw_binary);

  // Compute spoiler path + write spoiler files.
  char spoiler_json_path[512];
  char spoiler_txt_path[512];
  int n = Spoiler_ResolvePath(share_string, ".json", spoiler_json_path,
                              sizeof(spoiler_json_path));
  int m = Spoiler_ResolvePath(share_string, ".txt", spoiler_txt_path,
                              sizeof(spoiler_txt_path));
  if (n > 0 && m > 0) {
    RandoSpheres spheres;
    bool spheres_ok = Logic_ComputeSpheres(&g_settings_working, &table, &spheres);
    (void)spheres_ok;
    RandoSpoiler spoiler;
    memset(&spoiler, 0, sizeof(spoiler));
    spoiler.share_string = share_string;
    spoiler.seed_u64 = seed_u64;
    spoiler.generator_version = kGeneratorVersion;
    spoiler.settings = &g_settings_working;
    spoiler.placements = &table;
    spoiler.spheres = &spheres;
    spoiler.goal_completable = Goal_IsCompletable(&g_settings_working, &table);
    {
      const PlacementStats *st = Placement_GetLastStats();
      spoiler.forward_fill_fallback_count = st->forward_fill_fallback_count;
      spoiler.retry_attempts = st->attempts_used;
    }
    // Phase B Slice 6 — Spoiler_Write branches on race_mode (full vs suppressed).
    if (!Spoiler_Write(&spoiler, spoiler_json_path, spoiler_txt_path)) {
      fprintf(stderr, "[settings] spoiler write failed: %s\n", spoiler_json_path);
    }
  }

  // Build & write the sidecar slot. Slot kind = Randomizer.
  RandoSidecarSlot slot;
  memset(&slot, 0, sizeof(slot));
  slot.header.slot_kind = kSlotKind_Randomizer;
  slot.header.generator_version = (uint16)kGeneratorVersion;
  memcpy(slot.header.settings_hash, settings_hash_full, 16);
  memcpy(slot.header.share_string, raw_binary, kRandoSidecar_ShareStringLength);
  // Phase B hints: carry the `hints` and `goal` axes in the slot's reserved
  // tail (rando_save.h settings extension) so telepathic-tile hints can be
  // regenerated at slot load. The generator reads only these two axes.
  slot.header.settings_ext_present = 1;
  slot.header.hints_setting = g_settings_working.hints;
  slot.header.goal = g_settings_working.goal;
  // Phase B Inverted runtime: persist world_state so slot-load knows whether
  // this is an Inverted seed (Moon Pearl + Magic Mirror starting inventory,
  // Dark-World start state). Carried additively at slot-header @68; older
  // binaries ignore it, and 0 (== kWorldState_Open) is the safe default.
  slot.header.world_state = g_settings_working.world_state;
  // Flags: set the forward-fill bit if the placer used the fallback.
  {
    const PlacementStats *st = Placement_GetLastStats();
    if (st->forward_fill_fallback_count > 0) {
      slot.header.flags |= kRandoSlotFlag_ForwardFillUsed;
    }
  }
  // Copy placements + compute placement_table_size (BYTES = 2 * max_loc_id + 2).
  if (table.count > (uint16)(sizeof(slot.placements) / sizeof(slot.placements[0]))) {
    fprintf(stderr, "[settings] placement count %u exceeds sidecar slot capacity\n",
            (unsigned)table.count);
    free(entries);
    g_settings_generate_in_progress = false;
    sound_effect_1 = 0x3c;
    return;
  }
  memcpy(slot.placements, entries, sizeof(RandoPlacement) * table.count);
  slot.placement_count = table.count;
  uint16 max_loc = 0;
  for (uint16 i = 0; i < table.count; ++i) {
    if (entries[i].location_id > max_loc) max_loc = entries[i].location_id;
  }
  slot.header.placement_table_size = (uint16)((max_loc + 1) * 2);

  // Initialize the target sram.dat slot with the same "new file" defaults
  // that NameFile_DoTheNaming applies for vanilla saves so the slot is
  // valid when the player picks it. The actual rando-specific runtime
  // bookkeeping (starting-inventory injection, etc.) happens at game-start
  // time via Rando_TryGrantStartingInventory etc.
  uint8 *target_sram = g_zenv.sram + g_settings_target_slot * 0x500;
  memset(target_sram, 0, 0x500);
  // Pre-name the file "RANDO " to match the rando-banner convention.
  uint16 *name = (uint16 *)(target_sram + kSrmOffs_Name);
  name[0] = 0x21;  // R
  name[1] = 0x00;  // A
  name[2] = 0x0d;  // N
  name[3] = 0x03;  // D
  name[4] = 0x0e;  // O
  name[5] = 0xa9;  // blank
  WORD(target_sram[0x3e5]) = 0x55aa;
  WORD(target_sram[0x20c]) = 0xf000;
  WORD(target_sram[0x20e]) = 0xf000;
  // 0x3e3 is name[5] (already blank above); DiedCounter lives at
  // kSrmOffs_DiedCounter = 0x405.
  WORD(target_sram[kSrmOffs_DiedCounter]) = 0xffff;
  // Replicate the new-file init from `NameFile_DoTheNaming` so the slot
  // has the canonical starting state — without this, health bytes
  // stayed zero, so the rando slot loaded as instant-death (0 hearts
  // / 0 max). The 60-byte block initializes health, magic, gloves, etc.
  // The bytes
  // 0x18,0x18 at offsets +44/+45 are the starting health + max health
  // (0x18 = 3 hearts in quarter-heart units); 0xf8 at offset +57 is the
  // boomerang/item slot baseline.
  static const uint8 kSramInit_Normal[60] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0,    0,    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0,    0,    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0x18, 0x18, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf8, 0, 0,
  };
  memcpy(target_sram + 0x340, kSramInit_Normal, 60);

  // === Phase B non-Standard world-state runtime: post-escape starting state ===
  // The placer pre-grants RescuedZelda (item 122) and skips the sphere-0
  // weapon/lamp guarantee for every non-Standard world_state (see
  // Rando_BuildPlacement / build_starting_inventory in rando_placement.c):
  // the logic graph assumes the HC escape is already done and the overworld
  // is free-roam. The runtime MUST match that assumption, or a fresh save
  // boots into the vanilla rain/uncle/escape intro with no guaranteed weapon
  // and hard-softlocks at the 4 weaponless sphere-0 checks.
  //
  // We bake the post-escape state into the freshly-created SRAM image,
  // mirroring ALTTPR's seed-generator SRAM init table (z3randomizer
  // initsramtable.asm — a SINGLE shared table, not per-mode):
  //   InitProgressIndicator (0x1833C5 -> sram_progress_indicator 0xF3C5) = 2
  //       Past the rain/escape intro. Module05_LoadFile gates the intro path
  //       on `sram_progress_indicator < 2`, and the rain at OW screen 0x70
  //       (overworld.c) clears at `>= 2`, so 2 yields free-roam, no rain.
  //   InitProgressFlags (0x1833C6 -> sram_progress_flags 0xF3C6) = 0x14
  //       (0x10 | 0x04: uncle/escape + Sanctuary milestone bits — ALTTPR's
  //       non-Standard default.)
  // target_sram[X] maps to g_ram[0xF000 + X] once CopySaveToWRAM runs, so
  // these offsets are (RAM address - 0xF000). See variables.h.
  //
  // World flag differs by state and is NOT in the shared table — the asm's
  // InitCurrentWorld (0x1833CA) is left 0 (Light World); the Inverted runtime
  // overrides it to 0x40 (Dark World) plus grants Moon Pearl + Magic Mirror.
  //   - Open / Retro: Light World free-roam (world flag stays 0, no pearl/mirror).
  //   - Inverted: Dark World start with pearl (no bunny) + mirror (DW->LW exit).
  // Standard is intentionally left untouched: the vanilla rain/uncle/escape
  // intro IS the Standard start, and Standard's placer keeps the sphere-0
  // weapon/lamp guarantee.
  //
  // NOTE (Inverted scope): the Inverted branch gives the correct DW world flag
  // + pearl + mirror + skipped intro, but does NOT yet swap the overworld TILE
  // SOURCES (the LW<->DW topology inversion lives in a large per-screen
  // tilemap-overlay subsystem, z3randomizer invertedmaps.asm, not ported here).
  // The opening screen renders Light-World Link's-House geometry with the DW
  // flag set ("fake DW") until that subsystem lands. See design.md task #82.
  switch (g_settings_working.world_state) {
    case kWorldState_Open:
    case kWorldState_Retro:
      // Light-World post-escape free-roam. World flag stays 0 (Light World),
      // no Moon Pearl / Mirror grant.
      target_sram[0x3C5] = 0x02;  // sram_progress_indicator (skip intro)
      target_sram[0x3C6] = 0x14;  // sram_progress_flags
      break;
    case kWorldState_Inverted:
      target_sram[0x3CA] = 0x40;  // savegame_is_darkworld (DW)
      target_sram[0x3C5] = 0x02;  // sram_progress_indicator (skip intro)
      target_sram[0x3C6] = 0x14;  // sram_progress_flags
      target_sram[0x357] = 0x01;  // link_item_moon_pearl (held; no bunny in DW)
      target_sram[0x353] = 0x02;  // link_item_mirror (Magic Mirror)
      // which_starting_point = 1 (Sanctuary), matching ALTTPR's
      // initsramtable.asm InitStartingEntrance = $01 for non-Standard modes.
      // The DW flag above sends Module05_LoadFile down the post-escape
      // spawn-select prompt for an active Inverted slot (see misc.c
      // Module05_LoadFile), and this is the sane fallback / default spawn if
      // the prompt is ever bypassed. (Index 0 = Link's House bed, which would
      // mis-spawn under the DW flag; 1 = Sanctuary is the safe post-escape
      // spawn.)
      target_sram[0x3C8] = 0x01;  // which_starting_point (Sanctuary)
      break;
    case kWorldState_Standard:
    default:
      // Standard: vanilla rain/uncle/escape intro — leave SRAM at fresh defaults.
      break;
  }
  // === Phase B non-Standard world-state runtime: end ===

  Intro_FixCksum(target_sram);

  if (!Rando_WriteSidecarSlot((int)g_settings_target_slot, &slot, target_sram, 0x500)) {
    fprintf(stderr, "[settings] sidecar write failed for slot %u\n",
            (unsigned)g_settings_target_slot);
    free(entries);
    g_settings_generate_in_progress = false;
    sound_effect_1 = 0x3c;
    return;
  }
  // Commit the vanilla SRAM image too (sidecar first by spec; then sram.dat).
  ZeldaWriteSram();

  // Apply recommended-features panel choices (if user toggled). Per spec
  // the user must opt in explicitly; we honor whatever state the panel
  // reflects (g_rec_working_features0 vs g_config.features0). The user
  // changed bits — that's the explicit opt-in.
  if (g_rec_working_features0 != g_config.features0) {
    g_config.features0 = g_rec_working_features0;
  }

  free(entries);

  // Reset sidecar cache + flag the slot active so the next file-select
  // render picks up the rando banner.
  SelectFile_ResetSidecarCache();
  selectfile_arr1[g_settings_target_slot] = 1;

  // Transition back to file-select with cursor on the target slot.
  uint8 target = g_settings_target_slot;
  SelectFile_Settings_Deactivate();
  // Position cursor; ReturnToFileSelect was called via Deactivate's
  // submodule rewind, but we want the cursor specifically on the new slot.
  selectfile_R16 = target;

  sound_effect_1 = 0x2c;
  fprintf(stderr, "[settings] generated slot %u: share=%s seed=0x%016llx\n",
          (unsigned)target, share_string, (unsigned long long)seed_u64);
}

