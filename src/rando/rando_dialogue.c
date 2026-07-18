#include "rando_dialogue.h"

#include "item_ids.h"
#include "location_ids.h"
#include "rando.h"
#include "rando_logic.h"
#include "rando_placement.h"
#include "../features.h"
#include "../variables.h"
#include "../zelda_rtl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kDialogueBufferCapacity = 0xE00,  // g_ram[0x11200..0x11fff]
  kCmdChoose = 0x68,
  kCmdScroll = 0x73,
  kCmdLine2 = 0x75,
  kCmdLine3 = 0x76,
  kCmdWaitKey = 0x7E,
  kCmdEnd = 0x7F,
  kMaxSafeLineChars = 13,
};

static uint8 dialogue_ascii_to_font(char ch) {
  if (ch >= 'A' && ch <= 'Z') return (uint8)(ch - 'A');
  if (ch >= 'a' && ch <= 'z') return (uint8)(26 + ch - 'a');
  if (ch >= '0' && ch <= '9') return (uint8)(52 + ch - '0');
  switch (ch) {
  case '!': return 62;
  case '?': return 63;
  case '-': return 64;
  case '.': return 65;
  case ',': return 66;
  case '>': return 68;
  case '\'': return 81;
  default: return 89;  // space (also the safe replacement for unsupported glyphs)
  }
}

static int write_ascii(uint8 *out, int pos, const char *text) {
  while (*text && pos < kDialogueBufferCapacity - 1)
    out[pos++] = dialogue_ascii_to_font(*text++);
  return pos;
}

// Three-row, word-wrapped US dialogue. Reward templates are deliberately short
// enough to fit one box; a pathological future item name is clipped rather than
// corrupting the following RAM region.
static void write_wrapped_message(uint8 *out, const char *text) {
  int w = 0, col = 0, row = 0;
  const char *p = text;
  while (*p && w < kDialogueBufferCapacity - 2) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *end = p;
    while (*end && *end != ' ') end++;
    int n = (int)(end - p);
    if (col && col + 1 + n > kMaxSafeLineChars) {
      if (row == 2) break;
      out[w++] = (++row == 1) ? kCmdLine2 : kCmdLine3;
      col = 0;
    } else if (col) {
      out[w++] = dialogue_ascii_to_font(' ');
      col++;
    }
    while (p < end) {
      if (col == kMaxSafeLineChars) {
        if (row == 2) goto done;
        out[w++] = (++row == 1) ? kCmdLine2 : kCmdLine3;
        col = 0;
      }
      out[w++] = dialogue_ascii_to_font(*p++);
      col++;
    }
  }
done:
  out[w] = kCmdEnd;
}

static void write_choice_message(uint8 *out, const char *top,
                                 const char *yes, const char *no) {
  int w = write_ascii(out, 0, top);
  out[w++] = kCmdLine2;
  w = write_ascii(out, w, "> ");
  w = write_ascii(out, w, yes);
  out[w++] = kCmdLine3;
  w = write_ascii(out, w, "  ");
  w = write_ascii(out, w, no);
  out[w++] = kCmdChoose;
  out[w] = kCmdEnd;
}

static void split_registry_token(const char *in, char *out, size_t cap) {
  size_t w = 0;
  for (size_t i = 0; in[i] && w + 1 < cap; i++) {
    char c = in[i];
    if (c == '_') {
      if (w && out[w - 1] != ' ') out[w++] = ' ';
      continue;
    }
    bool boundary = i && ((c >= 'A' && c <= 'Z' && in[i - 1] >= 'a' && in[i - 1] <= 'z') ||
                          (c >= '0' && c <= '9' && !(in[i - 1] >= '0' && in[i - 1] <= '9')));
    if (boundary && w && out[w - 1] != ' ' && w + 1 < cap) out[w++] = ' ';
    out[w++] = c;
  }
  out[w] = 0;
}

static const char *short_dungeon_name(const char *token) {
  if (!strcmp(token, "HyruleCastleEscape")) return "HC";
  if (!strcmp(token, "EasternPalace")) return "Eastern";
  if (!strcmp(token, "DesertPalace")) return "Desert";
  if (!strcmp(token, "TowerOfHera")) return "Hera";
  if (!strcmp(token, "HyruleCastleTower")) return "Castle Tower";
  if (!strcmp(token, "PalaceOfDarkness")) return "PoD";
  if (!strcmp(token, "SwampPalace")) return "Swamp";
  if (!strcmp(token, "SkullWoods")) return "Skull";
  if (!strcmp(token, "ThievesTown")) return "Thieves";
  if (!strcmp(token, "IcePalace")) return "Ice";
  if (!strcmp(token, "MiseryMire")) return "Mire";
  if (!strcmp(token, "TurtleRock")) return "TR";
  if (!strcmp(token, "GanonsTower")) return "GT";
  return token;
}

static void format_reward_item(uint16 item_id, char *out, size_t cap) {
  const char *tok = Rando_GetItemName(item_id);
  struct NameOverride { const char *token, *display; };
  static const struct NameOverride kOverrides[] = {
    {"L1Sword", "Fighter Sword"}, {"L2Sword", "Master Sword"},
    {"L3Sword", "Tempered Sword"}, {"L4Sword", "Gold Sword"},
    {"OcarinaInactive", "Flute"}, {"BugCatchingNet", "Bug Net"},
    {"BottleEmpty", "Bottle"}, {"PieceOfHeart", "Heart Piece"},
    {"BossHeartContainer", "Heart Container"},
    {"SilverArrowUpgrade", "Silver Arrows"},
    {"Rupee1", "1 Rupee"}, {"Rupee5", "5 Rupees"},
    {"Rupee20", "20 Rupees"}, {"Rupee100", "100 Rupees"},
    {"Rupee300", "300 Rupees"}, {"Arrow1", "1 Arrow"},
    {"Arrow10", "10 Arrows"}, {"Bombs1", "1 Bomb"},
    {"Bombs3", "3 Bombs"}, {"Bombs10", "10 Bombs"},
  };
  for (size_t i = 0; i < sizeof(kOverrides) / sizeof(kOverrides[0]); i++) {
    if (!strcmp(tok, kOverrides[i].token)) {
      snprintf(out, cap, "%s", kOverrides[i].display);
      return;
    }
  }

  static const struct { const char *prefix, *kind; } kDungeonKinds[] = {
    {"SmallKey_", "Small Key"}, {"BigKey_", "Big Key"},
    {"Map_", "Map"}, {"Compass_", "Compass"},
  };
  for (size_t i = 0; i < sizeof(kDungeonKinds) / sizeof(kDungeonKinds[0]); i++) {
    size_t n = strlen(kDungeonKinds[i].prefix);
    if (!strncmp(tok, kDungeonKinds[i].prefix, n)) {
      snprintf(out, cap, "%s %s", short_dungeon_name(tok + n), kDungeonKinds[i].kind);
      return;
    }
  }
  if (!strncmp(tok, "Prize_Crystal", 13)) {
    snprintf(out, cap, "Crystal %s", tok + 13);
    return;
  }
  if (!strncmp(tok, "Prize_", 6)) tok += 6;
  if (!strncmp(tok, "Soul_Npc_", 9)) {
    char tmp[40];  // 39 chars + " Soul" must fit the smallest 48-byte reward buffer
    split_registry_token(tok + 9, tmp, sizeof(tmp));
    snprintf(out, cap, "%s Soul", tmp);
    return;
  }
  if (!strncmp(tok, "Soul_", 5)) {
    char tmp[40];  // 39 chars + " Soul" must fit the smallest 48-byte reward buffer
    split_registry_token(tok + 5, tmp, sizeof(tmp));
    snprintf(out, cap, "%s Soul", tmp);
    return;
  }
  split_registry_token(tok, out, cap);
}

static int dialogue_len(const uint8 *buf) {
  for (int i = 0; i < kDialogueBufferCapacity; i++)
    if (buf[i] == kCmdEnd) return i;
  return -1;
}

static int encode_search(const char *text, uint8 *out) {
  int n = 0;
  while (*text) out[n++] = dialogue_ascii_to_font(*text++);
  return n;
}

static int find_ascii_run(const uint8 *buf, const char *text) {
  uint8 needle[96];
  int nn = encode_search(text, needle);
  int bn = dialogue_len(buf);
  if (bn < 0) return -1;
  for (int i = 0; i + nn <= bn; i++)
    if (!memcmp(buf + i, needle, (size_t)nn)) return i;
  return -1;
}

static bool replace_ascii_run(uint8 *buf, const char *find, const char *replace) {
  int at = find_ascii_run(buf, find);
  int len = dialogue_len(buf);
  if (at < 0 || len < 0) return false;
  uint8 repl[96];
  int fn = (int)strlen(find), rn = encode_search(replace, repl);
  if (len - fn + rn >= kDialogueBufferCapacity) return false;
  memmove(buf + at + rn, buf + at + fn, (size_t)(len - at - fn + 1));
  memcpy(buf + at, repl, (size_t)rn);
  return true;
}

static uint16 lookup_reward(uint16 loc, uint16 vanilla_item) {
  return Placement_Lookup(loc, vanilla_item);  // deliberately side-effect free
}

static void write_item_message(uint8 *buf, const char *fmt,
                               uint16 loc, uint16 vanilla_item) {
  char item[48], text[96];
  format_reward_item(lookup_reward(loc, vanilla_item), item, sizeof(item));
  snprintf(text, sizeof(text), fmt, item);
  write_wrapped_message(buf, text);
}

static void rewrite_uncle(uint8 *buf) {
  char item[48], repl[64];
  format_reward_item(lookup_reward(LOC_Link_s_Uncle, ITEM_L1Sword), item, sizeof(item));
  snprintf(repl, sizeof(repl), "the %s", item);
  replace_ascii_run(buf, "my sword and shield", repl);
}

static void rewrite_zora_reward(uint8 *buf) {
  int tail = find_ascii_run(buf, "I will let you use the magic");
  if (tail < 0) return;
  int len = dialogue_len(buf);
  char item[48], text[80];
  format_reward_item(lookup_reward(LOC_King_Zora, ITEM_Flippers), item, sizeof(item));
  snprintf(text, sizeof(text), "%s is yours!", item);
  uint8 tmp[kDialogueBufferCapacity];
  write_wrapped_message(tmp, text);
  int head = dialogue_len(tmp);
  int tail_len = len - tail + 1;
  if (head < 0 || head + 2 + tail_len > kDialogueBufferCapacity) return;
  tmp[head++] = kCmdWaitKey;
  tmp[head++] = kCmdScroll;
  memcpy(tmp + head, buf + tail, (size_t)tail_len);
  memcpy(buf, tmp, (size_t)(head + tail_len));
}

static void write_ganon_crystal_warning(uint8 *buf, unsigned required) {
  char text[64];
  snprintf(text, sizeof(text), "You need %u crystal%s to harm me.",
           required, required == 1 ? "" : "s");
  write_wrapped_message(buf, text);
}

static void rewrite_ganon_crystal_warning(uint8 *buf) {
  // add-rando-random-crystals: reads the RESOLVED count — this dialogue is
  // the in-world reveal of a rolled ganon requirement.
  const RandoSettings *settings = Rando_GetActiveSettings();
  uint8 required = Rando_EffectiveCrystalsGanon();
  if (settings == NULL || required == 0 ||
      Rando_HasRequiredGanonCrystals())
    return;
  write_ganon_crystal_warning(buf, required);
}

static uint16 fairy_next_location(void) {
  if (!savegame_is_darkworld) {
    if (!Rando_IsLocationChecked(LOC_Waterfall_Fairy_Left)) return LOC_Waterfall_Fairy_Left;
    if (!Rando_IsLocationChecked(LOC_Waterfall_Fairy_Right)) return LOC_Waterfall_Fairy_Right;
  } else {
    if (!Rando_IsLocationChecked(LOC_Pyramid_Fairy_Left)) return LOC_Pyramid_Fairy_Left;
    if (!Rando_IsLocationChecked(LOC_Pyramid_Fairy_Right)) return LOC_Pyramid_Fairy_Right;
  }
  return 0xFFFFu;
}

void Rando_RewriteRewardDialogue(uint16 msg_id, uint8 *buf) {
  // The runtime buffer preserves the selected language's command grammar and
  // font alphabet. This renderer currently emits US commands/glyph indices;
  // bit 0 selects the incompatible EU/new command grammar and bit 1 marks the
  // translated original-format languages with different glyph tables. Leave
  // every non-US buffer untouched until it has a complete locale encoder.
  if (buf == NULL || !Rando_IsActive() || g_zenv.dialogue_flags != 0) return;

  switch (msg_id) {
  case 0x00E: rewrite_uncle(buf); return;
  case 0x0CD: write_item_message(buf, "You won %s!", LOC_Maze_Race, ITEM_PieceOfHeart); return;
  case 0x0D1: write_choice_message(buf, "Mystery item", "Buy for 100", "No thanks"); return;
  case 0x0D2: write_item_message(buf, "You bought %s!", LOC_Bottle_Merchant, ITEM_BottleEmpty); return;
  case 0x0D4: write_wrapped_message(buf, "I am sold out of special items."); return;
  case 0x0D8: write_choice_message(buf, "Want our prize?", "Yes", "No thanks"); return;
  case 0x0D9: write_choice_message(buf, "Only 10 Rupees", "Pay", "Wait"); return;
  case 0x0DA: write_choice_message(buf, "Are you sure?", "Yes", "No"); return;
  case 0x0DB: write_wrapped_message(buf, "Your prize is already made."); return;
  case 0x0DE: write_item_message(buf, "%s is ready!", LOC_Blacksmith, ITEM_L3Sword); return;
  case 0x0DF: write_wrapped_message(buf, "Find my partner and we can make a special item."); return;
  case 0x0E0: write_wrapped_message(buf, "Thanks! Return and we will make a special item."); return;
  case 0x0E6: write_item_message(buf, "I lend you %s.", LOC_Stumpy, ITEM_Shovel); return;
  case 0x105: write_item_message(buf, "Please take %s.", LOC_Sick_Kid, ITEM_BugCatchingNet); return;
  case 0x10E: write_item_message(buf, "You receive %s.", LOC_Ether_Tablet, ITEM_Ether); return;
  case 0x10F: write_item_message(buf, "You receive %s.", LOC_Bombos_Tablet, ITEM_Bombos); return;
  case 0x111: write_item_message(buf, "You got %s!", LOC_Magic_Bat, ITEM_HalfMagic); return;
  case 0x142: write_choice_message(buf, "Want my item?", "Ask price", "Just visiting"); return;
  case 0x143: write_choice_message(buf, "It costs 500", "Pay", "Quit"); return;
  case 0x144: rewrite_zora_reward(buf); return;
  case 0x16F: rewrite_ganon_crystal_warning(buf); return;
  case 0x176: {
    // 0x176 is shared with the Bottle Merchant's outdoor fish animation. Room
    // alone is unsafe because dungeon_room_index can remain stale outdoors.
    if (!player_is_indoors) return;
    uint16 loc = Rando_GiftThiefLocationForRoom(dungeon_room_index);
    if (loc == 0xFFFFu) return;
    write_item_message(buf, "Take %s.", loc,
                       loc == LOC_Mini_Moldorm_Cave_NPC ? ITEM_PieceOfHeart : ITEM_Rupee100);
    return;
  }
  case 0x0B7:
    replace_ascii_run(buf, "The Master Sword", "The forest treasure");
    return;
  case 0x14A:
    write_choice_message(buf, "Fairy gift?", "Accept", "Decline");
    return;
  case 0x14B:
    write_wrapped_message(buf, fairy_next_location() == 0xFFFFu
                                   ? "No gifts remain."
                                   : "Maybe next time.");
    return;
  default:
    return;
  }
}

static bool buffer_contains_ascii(const uint8 *buf, const char *text) {
  return find_ascii_run(buf, text) >= 0;
}

static void selfcheck_fail(const char *what) {
  fprintf(stderr, "RandoDialogue_SelfCheck: %s\n", what);
  abort();
}

void RandoDialogue_SelfCheck(void) {
  static RandoPlacement entries[] = {
    {LOC_Link_s_Uncle, ITEM_Hookshot},
    {LOC_Bottle_Merchant, ITEM_Boots},
    {LOC_Magic_Bat, ITEM_Hammer},
    {LOC_Sick_Kid, ITEM_BookOfMudora},
    {LOC_Bombos_Tablet, ITEM_IceRod},
    {LOC_Mini_Moldorm_Cave_NPC, ITEM_PieceOfHeart},
    {LOC_Maze_Race, ITEM_BugCatchingNet},
    {LOC_Ether_Tablet, ITEM_FireRod},
    {LOC_Blacksmith, ITEM_L3Sword},
    {LOC_Stumpy, ITEM_Shovel},
    {LOC_Hype_Cave_NPC, ITEM_Rupee100},
    {LOC_King_Zora, ITEM_Hookshot},
  };
  RandoPlacementTable table = {entries, (uint16)(sizeof(entries) / sizeof(entries[0]))};
  const RandoPlacementTable *saved_table = Placement_GetActive();
  uint8 saved_active = g_rando_slot_active;
  uint8 saved_indoors = player_is_indoors;
  uint16 saved_room = dungeon_room_index;
  uint8 saved_dialogue_flags = g_zenv.dialogue_flags;
  uint8 saved_checked[kRandoCheckedBitmapBytes];
  memcpy(saved_checked, g_rando_checked_bitmap, sizeof(saved_checked));
  memset(g_rando_checked_bitmap, 0, sizeof(saved_checked));
  Placement_Install(&table);
  g_rando_slot_active = 1;
  g_zenv.dialogue_flags = 0;  // exercise the supported US grammar

  uint8 buf[kDialogueBufferCapacity];
  memset(buf, kCmdEnd, sizeof(buf));
  Rando_RewriteRewardDialogue(0x0CD, buf);
  if (!buffer_contains_ascii(buf, "Bug") || !buffer_contains_ascii(buf, "Net"))
    selfcheck_fail("Maze Race item name");

  memset(buf, kCmdEnd, sizeof(buf));
  int w = write_ascii(buf, 0, "Take my sword and shield and listen");
  buf[w] = kCmdEnd;
  Rando_RewriteRewardDialogue(0x00E, buf);
  if (!buffer_contains_ascii(buf, "Hookshot") || buffer_contains_ascii(buf, "sword and shield"))
    selfcheck_fail("Uncle inline item rewrite");

  memset(buf, kCmdEnd, sizeof(buf));
  w = write_ascii(buf, 0, "old flippers prelude");
  buf[w++] = kCmdWaitKey; buf[w++] = kCmdScroll;
  w = write_ascii(buf, w, "I will let you use the magic water ways");
  buf[w] = kCmdEnd;
  Rando_RewriteRewardDialogue(0x144, buf);
  if (!buffer_contains_ascii(buf, "Hookshot") ||
      !buffer_contains_ascii(buf, "I will let you use the magic"))
    selfcheck_fail("King Zora item/tutorial rewrite");

  player_is_indoors = 1;
  dungeon_room_index = 0x11E;
  memset(buf, kCmdEnd, sizeof(buf));
  Rando_RewriteRewardDialogue(0x176, buf);
  if (!buffer_contains_ascii(buf, "100") || !buffer_contains_ascii(buf, "Rupees"))
    selfcheck_fail("Hype Cave gift mapping");
  dungeon_room_index = 0x123;
  memset(buf, kCmdEnd, sizeof(buf));
  Rando_RewriteRewardDialogue(0x176, buf);
  if (!buffer_contains_ascii(buf, "Heart") || !buffer_contains_ascii(buf, "Piece"))
    selfcheck_fail("MMC gift mapping");
  dungeon_room_index = 0x127;
  memset(buf, 0x55, sizeof(buf)); buf[0] = kCmdEnd;
  Rando_RewriteRewardDialogue(0x176, buf);
  if (buf[0] != kCmdEnd) selfcheck_fail("unmapped gift room must fall through");
  player_is_indoors = 0;
  dungeon_room_index = 0x11E;
  memset(buf, 0x55, sizeof(buf)); buf[0] = kCmdEnd;
  Rando_RewriteRewardDialogue(0x176, buf);
  if (buf[0] != kCmdEnd) selfcheck_fail("outdoor fish message must fall through");

  memset(buf, kCmdEnd, sizeof(buf));
  Rando_RewriteRewardDialogue(0x0D1, buf);
  bool has_choose = false;
  for (int i = 0; i < 80 && buf[i] != kCmdEnd; i++)
    if (buf[i] == kCmdChoose) has_choose = true;
  if (!has_choose) selfcheck_fail("generic choice command");

  memset(buf, kCmdEnd, sizeof(buf));
  write_ganon_crystal_warning(buf, 1);
  if (!buffer_contains_ascii(buf, "You need 1") ||
      !buffer_contains_ascii(buf, "crystal to") ||
      !buffer_contains_ascii(buf, "harm me"))
    selfcheck_fail("Ganon singular crystal warning");

  memset(buf, kCmdEnd, sizeof(buf));
  write_ganon_crystal_warning(buf, 2);
  if (!buffer_contains_ascii(buf, "You need 2") ||
      !buffer_contains_ascii(buf, "crystals to"))
    selfcheck_fail("Ganon plural crystal warning");

  // Both non-US modes must remain byte-for-byte untouched: bit 0 is the EU/new
  // command grammar; bit 1 is a translated original-format font alphabet.
  for (uint8 locale_flags = 1; locale_flags <= 2; locale_flags++) {
    g_zenv.dialogue_flags = locale_flags;
    memset(buf, 0x55, sizeof(buf)); buf[0] = kCmdEnd;
    Rando_RewriteRewardDialogue(0x0CD, buf);
    if (buf[0] != kCmdEnd) selfcheck_fail("non-US dialogue must not rewrite");
  }
  g_zenv.dialogue_flags = 0;

  g_rando_slot_active = 0;
  memset(buf, 0x55, sizeof(buf)); buf[0] = kCmdEnd;
  Rando_RewriteRewardDialogue(0x0CD, buf);
  if (buf[0] != kCmdEnd) selfcheck_fail("inactive slot must not rewrite");

  g_rando_slot_active = saved_active;
  player_is_indoors = saved_indoors;
  dungeon_room_index = saved_room;
  g_zenv.dialogue_flags = saved_dialogue_flags;
  memcpy(g_rando_checked_bitmap, saved_checked, sizeof(saved_checked));
  Placement_Install(saved_table);
  fprintf(stderr, "[RandoDialogue_SelfCheck] OK\n");
}
