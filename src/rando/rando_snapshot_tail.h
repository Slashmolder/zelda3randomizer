// rando_snapshot_tail.h — snapshot TLV-chain save/load (tasks.md §8.8, §8.8a).
//
// Extends the existing `StateRecorder_Save/Load` (src/zelda_rtl.c) with a
// forward-compatible Type-Length-Value chain appended after the standard
// 4-chunk write. Per randomizer-save spec § "Snapshot interoperability":
//
//   TLV entry = magic[8] + type[4] LE + length[4] LE + payload[length]
//
// Phase A emits a single TLV of type TAIL_RANDO_STATE with payload:
//
//   generator_version[2] LE
//   settings_hash[16]
//   share_string[32]
//   placement_table_size[2] LE        // bytes; = 2 × #locations
//   placement_table[placement_table_size]  // flat uint16 LE by location_id
//
// where 0xFFFF is the "no placement" sentinel (same convention as the
// sidecar — randomizer-placement spec).
//
// Older binaries reading a Phase-A snapshot stop after the standard chunks
// and silently ignore the trailing TLV bytes (graceful degradation). Newer
// binaries reading older snapshots terminate the TLV loop on EOF or
// non-matching magic — both are valid steady states.
//
// Across generator versions the embedded `generator_version` is the payload-
// schema discriminator (spec scenario "Newer binary reads older rando
// snapshot (cross-version TLV)"). Phase A has one schema; future schema
// changes dispatch on this field.

#ifndef ZELDA3_RANDO_SNAPSHOT_TAIL_H_
#define ZELDA3_RANDO_SNAPSHOT_TAIL_H_

#include "../types.h"
#include <stdio.h>

// TLV constants. ZRSNAP01 is 8 raw ASCII bytes (no NUL); matches what the
// pre-rewrite stub emitted and what the spec implicitly pins for Phase A.
#define kRandoSnapshotTail_Magic   "ZRSNAP01"
#define kRandoSnapshotTail_MagicLen 8

// Type discriminators.
#define kRandoSnapshotTail_Type_RandoState 1u
// per-slot canonical settings + prize_attempt + the 4 process-static
// ownership bytes (mushroom/flute-shovel/boomerang/bow). Carried as a SEPARATE
// TLV (not folded into RandoState) so older binaries — which expect RandoState's
// length to be exactly 52 + placement_table_bytes — skip it as an unknown type
// and still read the placement table, per the format's append-a-new-type rule.
#define kRandoSnapshotTail_Type_RandoSettings 2u

// Upper bound on a single TLV payload's claimed length. The largest legal
// payload is the RandoState body (52 + 512*2 = 1076 bytes); the loader rejects
// any TLV claiming more than this before seeking, so an untrusted/corrupt
// length can't drive a negative (LLP64) or wild fseek. Generous headroom.
#define kRandoSnapshotTail_MaxPayloadBytes 0x10000u

// ---------------------------------------------------------------------------
// Snapshot context. The active rando slot's metadata that the TLV payload
// needs (generator_version + settings_hash + share_string). The placer
// installs this when a rando slot becomes active; the snapshot saver reads
// it; clearing it (e.g., on slot exit) suppresses TLV emission.
//
// Borrowed pointers; caller retains ownership. Pass NULL to clear.
// ---------------------------------------------------------------------------
void Rando_SetSnapshotContext(uint16 generator_version,
                              const uint8 settings_hash[16],
                              const uint8 share_string[32]);
void Rando_ClearSnapshotContext(void);
bool Rando_HasSnapshotContext(void);
uint16 Rando_GetSnapshotGeneratorVersion(void);
const uint8 *Rando_GetSnapshotSettingsHash(void);  // [16]
const uint8 *Rando_GetSnapshotShareString(void);   // [32]

// settings sub-context for the type-2 RandoSettings TLV. The active slot
// installs its canonical settings blob (kSettingsCanonicalLen bytes) + the
// accepted prize_attempt here so RandoSnapshotTail_Save can emit the type-2 TLV.
// Pass NULL to clear (a v1/no-blob slot): the type-2 TLV is then suppressed and a
// cold replay degrades to placement-only. Independent of the type-1 context above
// — Rando_ClearSnapshotContext clears both.
void Rando_SetSnapshotSettingsContext(const uint8 *settings_canonical_or_null,
                                      uint8 prize_attempt);

// ---------------------------------------------------------------------------
// Save: append the TLV chain to `f` (already at end of the 4-chunk write).
// Emits nothing when no placement is installed OR no context is set — the
// snapshot then has a vanilla tail and older readers see no rando state.
//
// Returns true on success (including the no-emit case); false on I/O error.
// ---------------------------------------------------------------------------
bool RandoSnapshotTail_Save(FILE *f);

// ---------------------------------------------------------------------------
// Load: iterate trailing TLV entries until EOF or a non-matching magic.
// Reinstalls the placement table on a recognized TAIL_RANDO_STATE TLV;
// unknown types are seeked past per their length. Returns the number of
// recognized TLVs consumed (0 is normal — vanilla snapshot or older
// snapshot). On I/O error mid-TLV, the function terminates the loop without
// reinstalling partial state.
// ---------------------------------------------------------------------------
int RandoSnapshotTail_Load(FILE *f);

// ---------------------------------------------------------------------------
// Ordering-invariant tripwire (tasks.md §8.8a).
//
// Rando_OnLocationCheck increments g_rando_oncheck_call_count at its top.
// StateRecorder_Load snapshots the counter immediately before LoadSnesState
// and asserts it didn't change before the TLV reinstall (debug builds only).
// If it changed, a game frame ran between SnesState restore and TLV
// reinstall — which would corrupt rando state because `kRam_RandoSlotActive`
// is true from g_ram restore but the placement table is stale/empty.
//
// The counter is process-lifetime monotonic, not per-load. The assertion
// compares snapshot-before vs snapshot-after across exactly the load.
// ---------------------------------------------------------------------------
extern uint64 g_rando_oncheck_call_count;

// Test helper: synthetic 4-chunk-equivalent + TLV round-trip, exits with
// code 2 on failure. Invoked from Rando_RunAllSelfChecks.
void RandoSnapshotTail_SelfCheck(void);

#endif  // ZELDA3_RANDO_SNAPSHOT_TAIL_H_
