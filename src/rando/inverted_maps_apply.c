// Inverted overworld tile-overlay applier (hand-written; the data table lives in
// inverted_maps.c, generated from z3randomizer/invertedmaps.asm).
//
// This is the C reimplementation of z3randomizer's Overworld_LoadNewTiles: it
// walks the per-screen RLE command stream and writes map16 tiles into the live
// overworld map16 buffer ($7E2000 = dung_bg2 = g_ram+0x2000) right after the
// screen's quadrants are decompressed and before Map16ToMap8 converts them. The
// caller (Overworld_DrawQuadrantsAndOverlays in overworld.c) gates this on the
// Inverted world-state, so the engine here is unconditionally "inverted" — the
// asm's InvertedMode-conditional commands were already resolved at data-gen time.
//
// Wire format (per .map block in invertedmaps.asm):
//   word pairs  <tile>, <pos>              place tile at $7E0000+pos
//   command words (bit15 set):
//     0x8000|dir            Stripe:       <start>, <tile..>  (STOP=bit15, SKIP=0xFFFF)
//     0x8001|dir|size<<8    StripeRLE:    <tile>, <start>
//     0x8002|dir|size<<8    StripeRLEINC: <tile>, <start>
//     0x8003                ArbTileCopy:  <tile>, <pos..>    (last pos has bit15)
//     0x8011                OWW_CMD_CUSTOM <custom-id> [+ id-specific operands]
//     0xFFFF                OWW_END
//   dir bit (0x0080) selects vertical (+0x80) vs horizontal (+0x02) increment.
#include "inverted_maps.h"
#include "../variables.h"  // g_ram, overworld_screen_index, save_ow_event_info, etc.

static inline void OWW_WriteTile(uint16 pos, uint16 tile) {
  // pos is the WRAM low address ($7E0000+pos); the map16 buffer (dung_bg2) is
  // g_ram[0x2000..0x5FFF]. CONFINE the write to that region: every legitimate
  // overlay position is in 0x2xxx-0x3Cxx, but pos is attacker-uncontrolled data
  // and a uint16 reaches into SRAM (g_ram[0xF000+]) and other live state. A
  // transcription bug or a stripe/RLE entry missing its terminator could
  // otherwise corrupt the save. Clamping makes the worst case a recoverable
  // visual glitch (wrong map16 tile) instead of state loss.
  if (pos < 0x2000 || pos >= 0x6000)
    return;
  *(uint16 *)(g_ram + pos) = tile;
}

void Overworld_ApplyInvertedTiles(void) {
  uint8 scr = (uint8)overworld_screen_index;
  if ((uint16)overworld_screen_index >= 0x80)
    return;
  uint16 off = kInvertedMapOffsets[scr];
  if (off == 0xFFFF)
    return;  // no overlay for this screen

  const uint16 *p = &kInvertedMapData[off];

  for (;;) {
    uint16 cmd = *p++;
    if (cmd == OWW_END)
      return;

    if (!(cmd & 0x8000)) {
      // plain single tile-write: cmd = tile, next = pos
      uint16 pos = *p++;
      OWW_WriteTile(pos, cmd);
      continue;
    }

    if (cmd == OWW_CMD_CUSTOM) {
      uint16 custom_id = *p++;
      switch (custom_id) {
      case OWW_CUSTOM_MAP1B_AGA: {
        // Pyramid-of-power hole on screen 0x1B appears only after Agahnim is
        // defeated (save_ow_event_info[0x5B] & 0x20). The generator emitted the
        // hole-data word count next; if Agahnim is NOT defeated, skip it so we
        // fall through to the no-hole (tower-entry sign) data.
        uint16 hole_words = *p++;
        if (!(save_ow_event_info[0x5B] & 0x20))
          p += hole_words;
        break;
      }
      case OWW_CUSTOM_MAP5B_WARP:
        // Screen 0x5B warp tile: 0x0034 before the relevant progress milestone,
        // 0x0212 once progress >= 3, written to fixed pos $3BBE.
        OWW_WriteTile(0x3BBE, (sram_progress_indicator < 3) ? 0x0034 : 0x0212);
        break;
      default:
        break;
      }
      continue;
    }

    // stripe-family command
    int opid = cmd & 0x00FF;
    int inc = (cmd & 0x0080) ? 0x80 : 0x02;  // vertical vs horizontal

    if (opid == 0x00) {
      // Stripe: <start>, then tiles until a terminator.
      uint16 x = *p++;
      bool first = true;
      for (;;) {
        if (!first)
          x = (uint16)(x + inc);
        first = false;
        uint16 t = *p++;
        if (t == OWW_SKIP)
          continue;  // skip this position, keep advancing
        if (t & 0x8000) {
          OWW_WriteTile(x, (uint16)(t & 0x7FFF));
          break;  // STOP
        }
        OWW_WriteTile(x, t);
      }
    } else if (opid == 0x01 || opid == 0x02) {
      // StripeRLE / StripeRLEINC: <tile>, <start>; size in high byte of cmd.
      int size = (cmd >> 8) & 0x7F;
      uint16 tile = *p++;
      uint16 x = *p++;
      bool first = true;
      while (size-- > 0) {
        if (!first) {
          x = (uint16)(x + inc);
          if (opid == 0x02)
            tile = (uint16)(tile + 1);  // RLEINC increments the tile
        }
        first = false;
        OWW_WriteTile(x, tile);
      }
    } else if (opid == 0x03) {
      // ArbTileCopy: <tile>, then positions until one has bit15 set (last).
      uint16 tile = *p++;
      for (;;) {
        uint16 pos = *p++;
        if (pos & 0x8000) {
          OWW_WriteTile((uint16)(pos & 0x7FFF), tile);
          break;
        }
        OWW_WriteTile(pos, tile);
      }
    }
    // opid 0x04..0x0C, 0x11..0x19 are .nothing in the asm; never emitted.
  }
}
