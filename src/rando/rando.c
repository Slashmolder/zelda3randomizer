// rando.c — randomizer master module (tasks.md §1.1, §1.1a). Phase A0 stub.
//
// Phase A0 deliverables landed here:
//   - g_assets_hash[32] global (computed by main.c::LoadAssets via sha256_buffer)
//   - Rando_OnLocationCheck() stub — currently pass-through (returns vanilla_item_id);
//     real placement-table dispatch lands with task 6.1
//   - Rando_BumpReachabilityCounter() stub — no-op; activates with task 3.8's
//     Logic_ComputeReachability memoization
//   - Optional SHA-256 self-test (RANDO_SELFCHECK build flag)

#include "rando.h"
#include "rando_rng.h"
#include "rando_share.h"
#include "rando_settings.h"
#include "rando_logic.h"
#include "rando_placement.h"
#include "rando_shuffles.h"
#include "rando_save.h"
#include "rando_textfield.h"
#include "item_ids.h"
#include "location_ids.h"
#include "../types.h"
#include "third_party/sha256/sha256.h"

// ---------------------------------------------------------------------------
// g_assets_hash — populated by LoadAssets() in src/main.c after the asset
// blob is read and validated. See task 1.1a.
// ---------------------------------------------------------------------------
uint8 g_assets_hash[32];

// ---------------------------------------------------------------------------
// Reachability state counter — bumped by Rando_BumpReachabilityCounter()
// when a story-progress event flag changes. The tracker overlay queries this
// to know when to invalidate its memoized Logic_ComputeReachability cache.
// (Heap-resident per design — NOT in g_ram. See proposal.md "Impact".)
// ---------------------------------------------------------------------------
static uint32 g_reachability_state_counter;

// ---------------------------------------------------------------------------
// Rando_OnLocationCheck — universal dispatcher (tasks.md §6.1).
// Phase A0 stub: pass-through. Phase A1 wires the placement_table lookup.
// ---------------------------------------------------------------------------
uint16 Rando_OnLocationCheck(uint16 location_id, uint16 vanilla_item_id) {
  // Placement_Lookup returns vanilla_item_id when no active placement table
  // is installed (rando mode inactive), or when location_id is not in the
  // active table. See rando_placement.c.
  return Placement_Lookup(location_id, vanilla_item_id);
}

uint8 Rando_DispatchVanillaGrant(uint16 location_id,
                                 uint16 vanilla_registry_id,
                                 uint8 vanilla_lttp_code) {
  uint16 placed = Rando_OnLocationCheck(location_id, vanilla_registry_id);
  if (placed == vanilla_registry_id) return vanilla_lttp_code;
  uint8 lttp = Rando_VanillaItemForRegistryId(placed);
  if (lttp == 0xFF) {
    // Placed item has no vanilla LttP dispatch path (progressive / dungeon
    // item / prize / virtual). §6.2 introduces per-class receive helpers
    // for these. Until then: fall back to the vanilla LttP code so the
    // game keeps running with the vanilla grant. This is detectable in
    // the spoiler (placement says X, in-game you got Y).
    return vanilla_lttp_code;
  }
  return lttp;
}

// ---------------------------------------------------------------------------
// Chest universal-dispatch lookup table (Phase A1 stub).
//
// Phase A2 work: populate from assets/rando/chest_lookup.yaml authored from
// audit.md §0.3.5 catalog cross-referenced with vanilla LttP room data.
// Until then this returns 0xFFFF (no rando substitute) and chests grant
// their vanilla items. The dispatch wrapper is in place so the codegen
// flip is a one-file change.
// ---------------------------------------------------------------------------
static uint16 chest_lookup(uint16 dungeon_room, uint8 chest_ordinal) {
  (void)dungeon_room;
  (void)chest_ordinal;
  // Phase A2: replace with generated lookup table.
  return 0xFFFFu;
}

uint8 Rando_ChestDispatch(uint16 dungeon_room, uint8 chest_ordinal,
                          uint8 vanilla_lttp_code) {
  uint16 loc_id = chest_lookup(dungeon_room, chest_ordinal);
  if (loc_id == 0xFFFFu) return vanilla_lttp_code;  // not mapped — vanilla
  // We don't know the vanilla registry id for unmapped chests; pass 0xFFFF
  // and let Rando_DispatchVanillaGrant's fall-back handle it.
  return Rando_DispatchVanillaGrant(loc_id, 0xFFFFu, vanilla_lttp_code);
}

// ---------------------------------------------------------------------------
// Rando_BumpReachabilityCounter — invalidates the tracker's cached
// reachability. Phase A0 stub: increment the counter. The tracker (task 10.2)
// will consume the value.
// ---------------------------------------------------------------------------
void Rando_BumpReachabilityCounter(void) {
  g_reachability_state_counter++;
}

// ---------------------------------------------------------------------------
// Per-seed shuffle-assignment globals consumed by Logic_ComputeReachability.
// ---------------------------------------------------------------------------
static const uint8 *g_dungeon_prize_assignment = NULL;
static const uint8 *g_medallion_assignment = NULL;

void Rando_SetDungeonPrizeAssignment(const uint8 *assignment) {
  g_dungeon_prize_assignment = assignment;
}
void Rando_SetMedallionAssignment(const uint8 *assignment) {
  g_medallion_assignment = assignment;
}
const uint8 *Rando_GetDungeonPrizeAssignment(void) { return g_dungeon_prize_assignment; }
const uint8 *Rando_GetMedallionAssignment(void) { return g_medallion_assignment; }

// ---------------------------------------------------------------------------
// Self-tests. Always linked; invoked from --rando-selftest CLI flag.
//
// Rando_SelfCheck validates the SHA-256 impl against the two NIST FIPS 180-2
// known vectors. Rando_RunAllSelfChecks invokes every subsystem's self-test
// in sequence; any failure exits with code 2.
//
// Cost: ~tens of microseconds per binary launch when invoked; not run during
// normal game frames.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rando_rng.h"

static int hex_eq(const uint8 *hash, const char *hex) {
  for (int i = 0; i < 32; ++i) {
    unsigned hi = (unsigned char)hex[i * 2], lo = (unsigned char)hex[i * 2 + 1];
    unsigned hv = (hi <= '9' ? hi - '0' : (hi & 0x5f) - 'A' + 10);
    unsigned lv = (lo <= '9' ? lo - '0' : (lo & 0x5f) - 'A' + 10);
    if (hash[i] != (uint8)((hv << 4) | lv)) return 0;
  }
  return 1;
}

void Rando_SelfCheck(void) {
  uint8 out[32];
  static const char kExpectedEmpty[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  static const char kExpectedAbc[]   = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

  sha256_buffer((const uint8 *)"", 0, out);
  if (!hex_eq(out, kExpectedEmpty)) {
    fprintf(stderr, "Rando_SelfCheck: SHA-256 of empty input FAILED\n");
    exit(2);
  }
  sha256_buffer((const uint8 *)"abc", 3, out);
  if (!hex_eq(out, kExpectedAbc)) {
    fprintf(stderr, "Rando_SelfCheck: SHA-256 of 'abc' FAILED\n");
    exit(2);
  }

  // Dispatch wrapper coverage (§6.1).
  // When no placement table is installed, Rando_OnLocationCheck returns
  // vanilla_item_id unchanged → Rando_DispatchVanillaGrant returns the
  // vanilla LttP code unchanged.
  Placement_Install(NULL);
  if (Rando_OnLocationCheck(8, 5) != 5) {
    fprintf(stderr, "Rando_SelfCheck: pass-through OnLocationCheck failed\n");
    exit(2);
  }
  if (Rando_DispatchVanillaGrant(8, 5, 0x00) != 0x00) {
    fprintf(stderr, "Rando_SelfCheck: pass-through DispatchVanillaGrant failed\n");
    exit(2);
  }

  // Build a synthetic placement table and verify dispatch routes to the
  // new LttP code. ITEM_BugCatchingNet=31 has dispatch:vanilla:0x21.
  static RandoPlacement entries[2];
  entries[0].location_id = 8;   // LOC_Link_s_Uncle
  entries[0].item_id = 31;      // ITEM_BugCatchingNet
  entries[1].location_id = 166; // LOC_Bottle_Merchant
  entries[1].item_id = 43;      // ITEM_BottleEmpty (vanilla:0x16) — identity
  RandoPlacementTable t = { entries, 2 };
  Placement_Install(&t);
  // Uncle now holds BugCatchingNet: vanilla code for that is 0x21
  if (Rando_DispatchVanillaGrant(8, 5, 0x00) != 0x21) {
    fprintf(stderr,
      "Rando_SelfCheck: dispatch did not translate Uncle→BugCatchingNet "
      "(expected 0x21, got 0x%02x)\n",
      (unsigned)Rando_DispatchVanillaGrant(8, 5, 0x00));
    exit(2);
  }
  // Bottle Merchant identity placement still grants 0x16
  if (Rando_DispatchVanillaGrant(166, 43, 0x16) != 0x16) {
    fprintf(stderr, "Rando_SelfCheck: identity dispatch failed\n");
    exit(2);
  }
  // Unknown location → vanilla fall-back
  if (Rando_DispatchVanillaGrant(0xFFFF, 5, 0x00) != 0x00) {
    fprintf(stderr, "Rando_SelfCheck: unknown-location fall-back failed\n");
    exit(2);
  }
  Placement_Install(NULL);

  // Vanilla-dispatch table boundaries (must return 0xFF for progressive items
  // and 0xFF for ids beyond the table).
  if (Rando_VanillaItemForRegistryId(0) != 0xFF) {  // ITEM_ProgressiveSword
    fprintf(stderr, "Rando_SelfCheck: ProgressiveSword should have no vanilla dispatch\n");
    exit(2);
  }
  if (Rando_VanillaItemForRegistryId(31) != 0x21) {  // ITEM_BugCatchingNet
    fprintf(stderr, "Rando_SelfCheck: BugCatchingNet vanilla dispatch wrong\n");
    exit(2);
  }
  if (Rando_VanillaItemForRegistryId(0xFFFFu) != 0xFF) {
    fprintf(stderr, "Rando_SelfCheck: out-of-range vanilla dispatch should be 0xFF\n");
    exit(2);
  }

  // Chest dispatch stub returns vanilla unchanged (lookup table is empty).
  if (Rando_ChestDispatch(0x12, 0, 0x05) != 0x05) {
    fprintf(stderr, "Rando_SelfCheck: chest dispatch stub did not fall back\n");
    exit(2);
  }
}

void Rando_RunAllSelfChecks(void) {
  Rando_SelfCheck();
  Rando_Rng_SelfCheck();
  Share_SelfCheck();
  Settings_SelfCheck();
  Logic_SelfCheck();
  Placement_SelfCheck();
  Shuffles_SelfCheck();
  RandoSave_SelfCheck();
  TextField_SelfCheck();
  fprintf(stderr, "Rando_RunAllSelfChecks: all subsystems OK.\n");
}
