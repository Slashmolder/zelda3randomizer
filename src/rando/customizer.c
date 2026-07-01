// customizer.c — customizer-mode manifest parsing + name resolution + install.
// See customizer.h for the design + manifest format.

#include "customizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rando_logic.h"  // Rando_GetLocationName, Rando_GetItemName
#include "rando_settings.h"   // RandoSettings, Settings_SetDefaults (placement selfcheck)
#include "rando_placement.h"  // Place_AssumedFill, PlacementTable_ComputeDigest
#include "item_ids.h"         // ITEM_Prize_GreenPendant .. ITEM_DefeatAgahnim
#include "third_party/sha256/sha256.h"  // sha256_buffer

// MSVC has no strtok_r; strtok_s is signature-identical (C11 Annex K aside).
#ifdef _MSC_VER
#define strtok_r strtok_s
#endif

// Highest location id we probe when reverse-resolving a name. The registry is
// append-only and currently tops out well under this; Rando_GetLocationName
// returns "(unknown)" for ids that aren't in the registry, so an over-estimate
// is harmless (just skipped).
// Probe the whole location id space (sized by the module-wide ceiling in
// rando_logic.h). Ids past the registry return a sentinel name and are skipped
// (is_sentinel_name), so the wider range costs only cheap sentinel checks.
#define kCustomizerLocIdProbeMax kRandoLocationCapacity
#define kCustomizerItemIdProbeMax 256

// ---------------------------------------------------------------------------
// Name normalization + reverse resolution
// ---------------------------------------------------------------------------

// Lowercase, alphanumeric-only. "Eastern_Palace_Boss", "Eastern Palace - Boss",
// and "easternpalaceboss" all normalize identically. Truncates at outlen-1.
static void normalize_name(const char *in, char *out, size_t outlen) {
  size_t o = 0;
  for (const char *p = in; *p && o + 1 < outlen; p++) {
    unsigned char c = (unsigned char)*p;
    if (c >= 'A' && c <= 'Z') out[o++] = (char)(c - 'A' + 'a');
    else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[o++] = (char)c;
    // everything else (space, '-', '_', '(', ')', ...) is dropped
  }
  out[o] = '\0';
}

static bool is_sentinel_name(const char *n) {
  return strcmp(n, "(unknown)") == 0 || strcmp(n, "(unnamed)") == 0;
}

uint16 Customizer_ResolveLocation(const char *name) {
  char want[160];
  normalize_name(name, want, sizeof want);
  if (want[0] == '\0') return 0xFFFF;
  for (uint32 id = 0; id < kCustomizerLocIdProbeMax; id++) {
    const char *n = Rando_GetLocationName((uint16)id);
    if (is_sentinel_name(n)) continue;
    char have[160];
    normalize_name(n, have, sizeof have);
    if (strcmp(have, want) == 0) return (uint16)id;
  }
  return 0xFFFF;
}

uint16 Customizer_ResolveItem(const char *name) {
  char want[160];
  normalize_name(name, want, sizeof want);
  if (want[0] == '\0') return 0xFFFF;
  for (uint32 id = 0; id < kCustomizerItemIdProbeMax; id++) {
    const char *n = Rando_GetItemName((uint16)id);
    if (is_sentinel_name(n)) continue;
    char have[160];
    normalize_name(n, have, sizeof have);
    if (strcmp(have, want) == 0) return (uint16)id;
  }
  return 0xFFFF;
}

// ---------------------------------------------------------------------------
// Manifest parsing (strict line-based YAML subset)
// ---------------------------------------------------------------------------

static void rtrim(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}
static const char *ltrim(const char *s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

enum { SEC_NONE = 0, SEC_PLACEMENTS, SEC_POOL };

bool Customizer_IsNonGrantableItem(uint16 item_id) {
  return (item_id >= ITEM_Prize_GreenPendant && item_id <= ITEM_DefeatAgahnim) ||
         item_id == ITEM_HyruleCastleBigKey;
}

// Parse a pool_overrides list value: "[A, B, C]" or a bare "A". Resolves each
// item name and appends its id to out[*count] (capped at cap). Rejects unknown
// items; when reject_non_grantable is set, also rejects prize/event/virtual
// items that have no pool/grant path. Returns 0 on success.
static int parse_pool_list(const char *value, int line, const char *which,
                           uint16 *out, uint16 *count, uint16 cap,
                           bool reject_non_grantable, char *err, size_t errlen) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s", value);
  char *s = (char *)ltrim(buf);
  rtrim(s);
  size_t n = strlen(s);
  if (n > 0 && s[0] == '[') {
    if (s[n - 1] != ']') {
      snprintf(err, errlen, "line %d: %s list missing closing ']'", line, which);
      return 1;
    }
    s[n - 1] = '\0';
    s++;
  }
  char *save = NULL;
  for (char *tok = strtok_r(s, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
    char item[160];
    snprintf(item, sizeof item, "%s", tok);
    char *t = (char *)ltrim(item);
    rtrim(t);
    if (t[0] == '\0') continue;  // tolerate trailing comma / empty list
    uint16 id = Customizer_ResolveItem(t);
    if (id == 0xFFFF) {
      snprintf(err, errlen, "line %d: unknown item '%s' in %s list", line, t, which);
      return 1;
    }
    if (reject_non_grantable && Customizer_IsNonGrantableItem(id)) {
      snprintf(err, errlen, "line %d: item '%s' is not grantable and cannot be added to the pool",
               line, t);
      return 1;
    }
    if (*count >= cap) {
      snprintf(err, errlen, "line %d: too many %s items (max %u)", line, which, (unsigned)cap);
      return 1;
    }
    out[(*count)++] = id;
  }
  return 0;
}

int Customizer_Parse(const char *text, size_t len, CustomizerManifest *out,
                     char *err, size_t errlen) {
  memset(out, 0, sizeof *out);
  if (errlen) err[0] = '\0';

  // customizer_seed = sha256(manifest_bytes)[0..8] — reproducible across users
  // who share the same manifest file.
  uint8 h[32];
  sha256_buffer((const uint8 *)text, len, h);
  memcpy(out->seed8, h, 8);

  int section = SEC_NONE;
  size_t i = 0;
  int line = 0;
  while (i < len) {
    size_t start = i;
    while (i < len && text[i] != '\n') i++;
    size_t end = i;          // exclusive (points at '\n' or len)
    if (i < len) i++;        // consume '\n'
    line++;

    size_t blen = end - start;
    if (blen > 0 && text[start + blen - 1] == '\r') blen--;  // strip CR
    char buf[256];
    if (blen >= sizeof buf) { snprintf(err, errlen, "line %d: line too long", line); return 1; }
    memcpy(buf, text + start, blen);
    buf[blen] = '\0';

    // Strip a trailing comment (first unquoted '#'). Manifest values are bare
    // tokens with no '#', so a plain first-'#' rule is sufficient.
    char *hash = strchr(buf, '#');
    if (hash) *hash = '\0';

    bool indented = (buf[0] == ' ' || buf[0] == '\t');
    const char *s = ltrim(buf);
    char body[256];
    snprintf(body, sizeof body, "%s", s);
    rtrim(body);
    if (body[0] == '\0') continue;  // blank / comment-only line

    if (!indented) {
      // Top-level section header: "<name>:" with no value.
      char *colon = strchr(body, ':');
      if (colon == NULL || *(colon + 1) != '\0') {
        snprintf(err, errlen, "line %d: expected a section header (e.g. 'placements:')", line);
        return 1;
      }
      *colon = '\0';
      rtrim(body);
      if (strcmp(body, "placements") == 0) {
        section = SEC_PLACEMENTS;
      } else if (strcmp(body, "pool_overrides") == 0) {
        section = SEC_POOL;
      } else {
        snprintf(err, errlen, "line %d: unknown section '%s'", line, body);
        return 1;
      }
      continue;
    }

    // Indented entry.
    if (section == SEC_NONE) {
      snprintf(err, errlen, "line %d: entry before any section header", line);
      return 1;
    }
    if (section == SEC_POOL) {
      // "add: [A, B]" / "remove: [C]".
      char *colon = strchr(body, ':');
      if (colon == NULL) {
        snprintf(err, errlen, "line %d: expected 'add: [...]' or 'remove: [...]'", line);
        return 1;
      }
      *colon = '\0';
      char key[64];
      snprintf(key, sizeof key, "%s", body);
      rtrim(key);
      const char *val = ltrim(colon + 1);
      if (strcmp(key, "add") == 0) {
        if (parse_pool_list(val, line, "add", out->pool_add, &out->pool_add_count,
                            kCustomizerMaxPoolOps, /*reject_non_grantable=*/true, err, errlen) != 0)
          return 1;
      } else if (strcmp(key, "remove") == 0) {
        if (parse_pool_list(val, line, "remove", out->pool_remove, &out->pool_remove_count,
                            kCustomizerMaxPoolOps, /*reject_non_grantable=*/false, err, errlen) != 0)
          return 1;
      } else {
        snprintf(err, errlen, "line %d: unknown pool_overrides key '%s' (expected add/remove)",
                 line, key);
        return 1;
      }
      continue;
    }

    // section == SEC_PLACEMENTS: "<location>: <item>".
    char *colon = strchr(body, ':');
    if (colon == NULL) {
      snprintf(err, errlen, "line %d: expected '<location>: <item>'", line);
      return 1;
    }
    *colon = '\0';
    char loc_name[256], item_name[256];
    snprintf(loc_name, sizeof loc_name, "%s", body);
    rtrim(loc_name);
    snprintf(item_name, sizeof item_name, "%s", ltrim(colon + 1));
    rtrim(item_name);
    if (loc_name[0] == '\0') { snprintf(err, errlen, "line %d: empty location", line); return 1; }
    if (item_name[0] == '\0') { snprintf(err, errlen, "line %d: empty item for '%s'", line, loc_name); return 1; }

    uint16 loc_id = Customizer_ResolveLocation(loc_name);
    if (loc_id == 0xFFFF) {
      snprintf(err, errlen, "line %d: unknown location '%s'", line, loc_name);
      return 1;
    }
    uint16 item_id = Customizer_ResolveItem(item_name);
    if (item_id == 0xFFFF) {
      snprintf(err, errlen, "line %d: unknown item '%s'", line, item_name);
      return 1;
    }
    // Reject a double-pinned location (ambiguous).
    for (uint16 p = 0; p < out->pin_count; p++) {
      if (out->pins[p].location_id == loc_id) {
        snprintf(err, errlen, "line %d: location '%s' pinned more than once", line, loc_name);
        return 1;
      }
    }
    if (out->pin_count >= kCustomizerMaxPins) {
      snprintf(err, errlen, "line %d: too many placements (max %d)", line, kCustomizerMaxPins);
      return 1;
    }
    out->pins[out->pin_count].location_id = loc_id;
    out->pins[out->pin_count].item_id = item_id;
    out->pin_count++;
  }

  return 0;
}

int Customizer_LoadFile(const char *path, CustomizerManifest *out,
                        char *err, size_t errlen) {
  if (errlen) err[0] = '\0';
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    snprintf(err, errlen, "cannot open manifest '%s'", path);
    return 1;
  }
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); snprintf(err, errlen, "seek failed on '%s'", path); return 1; }
  long sz = ftell(f);
  if (sz < 0) { fclose(f); snprintf(err, errlen, "tell failed on '%s'", path); return 1; }
  if (sz > (1L << 20)) { fclose(f); snprintf(err, errlen, "manifest '%s' too large (> 1 MiB)", path); return 1; }
  rewind(f);
  char *text = (char *)malloc((size_t)sz + 1);
  if (text == NULL) { fclose(f); snprintf(err, errlen, "out of memory reading '%s'", path); return 1; }
  size_t got = fread(text, 1, (size_t)sz, f);
  fclose(f);
  text[got] = '\0';
  int rc = Customizer_Parse(text, got, out, err, errlen);
  free(text);
  return rc;
}

// ---------------------------------------------------------------------------
// Active manifest + hard-error channel (read by the placer)
// ---------------------------------------------------------------------------

static const CustomizerManifest *g_active_customizer = NULL;
static char g_customizer_error[160] = {0};

void Customizer_Install(const CustomizerManifest *m) { g_active_customizer = m; }
const CustomizerManifest *Customizer_GetActive(void) { return g_active_customizer; }

const char *Customizer_LastError(void) { return g_customizer_error; }
void Customizer__SetError(const char *msg) {
  snprintf(g_customizer_error, sizeof g_customizer_error, "%s", msg ? msg : "");
}
void Customizer__ClearError(void) { g_customizer_error[0] = '\0'; }

// ---------------------------------------------------------------------------
// Self-check
// ---------------------------------------------------------------------------

static void customizer_selfcheck_die(const char *msg) {
  fprintf(stderr, "Customizer_SelfCheck: %s\n", msg);
  exit(2);
}

void Customizer_SelfCheck(void) {
  // 1. Normalized location names must be unique — the reverse resolver returns
  //    the FIRST match, so a collision would silently resolve to the wrong id.
  //    O(N^2) over the location id space; one-time, fine.
  for (uint32 a = 0; a < kCustomizerLocIdProbeMax; a++) {
    const char *na = Rando_GetLocationName((uint16)a);
    if (is_sentinel_name(na)) continue;
    char norm_a[160];
    normalize_name(na, norm_a, sizeof norm_a);
    if (norm_a[0] == '\0') customizer_selfcheck_die("a location name normalizes to empty");
    for (uint32 b = a + 1; b < kCustomizerLocIdProbeMax; b++) {
      const char *nb = Rando_GetLocationName((uint16)b);
      if (is_sentinel_name(nb)) continue;
      char norm_b[160];
      normalize_name(nb, norm_b, sizeof norm_b);
      if (strcmp(norm_a, norm_b) == 0) {
        fprintf(stderr, "Customizer_SelfCheck: location names '%s' (id %u) and '%s' (id %u) "
                        "normalize identically\n", na, a, nb, b);
        exit(2);
      }
    }
  }
  // 2. Item names likewise unique.
  for (uint32 a = 0; a < kCustomizerItemIdProbeMax; a++) {
    const char *na = Rando_GetItemName((uint16)a);
    if (is_sentinel_name(na)) continue;
    char norm_a[160];
    normalize_name(na, norm_a, sizeof norm_a);
    if (norm_a[0] == '\0') customizer_selfcheck_die("an item name normalizes to empty");
    for (uint32 b = a + 1; b < kCustomizerItemIdProbeMax; b++) {
      const char *nb = Rando_GetItemName((uint16)b);
      if (is_sentinel_name(nb)) continue;
      char norm_b[160];
      normalize_name(nb, norm_b, sizeof norm_b);
      if (strcmp(norm_a, norm_b) == 0) {
        fprintf(stderr, "Customizer_SelfCheck: item names '%s' (id %u) and '%s' (id %u) "
                        "normalize identically\n", na, a, nb, b);
        exit(2);
      }
    }
  }

  // 3. Resolution sanity: a known location + item resolve, and both the symbol
  //    and human forms hit the same id.
  uint16 ep_boss_sym = Customizer_ResolveLocation("Eastern_Palace_Boss");
  uint16 ep_boss_hum = Customizer_ResolveLocation("Eastern Palace - Boss");
  if (ep_boss_sym == 0xFFFF || ep_boss_sym != ep_boss_hum)
    customizer_selfcheck_die("Eastern Palace - Boss must resolve in both name forms to one id");
  if (Customizer_ResolveItem("Hookshot") == 0xFFFF)
    customizer_selfcheck_die("item 'Hookshot' must resolve");
  if (Customizer_ResolveLocation("Definitely Not A Real Location") != 0xFFFF)
    customizer_selfcheck_die("a bogus location must NOT resolve");

  // 4. Parse round-trip: a small manifest parses to the expected pins, and a
  //    duplicate location is rejected.
  {
    const char *ok_text =
        "# sample\n"
        "placements:\n"
        "  Eastern_Palace_Boss: Hookshot\n"
        "  Master Sword Pedestal: TitanMitt\n";
    CustomizerManifest m;
    char err[160];
    if (Customizer_Parse(ok_text, strlen(ok_text), &m, err, sizeof err) != 0)
      customizer_selfcheck_die(err);
    if (m.pin_count != 2) customizer_selfcheck_die("expected 2 pins");
    if (m.pins[0].location_id != ep_boss_sym ||
        m.pins[0].item_id != Customizer_ResolveItem("Hookshot"))
      customizer_selfcheck_die("first pin mismatch");
  }
  {
    const char *dup_text =
        "placements:\n"
        "  Eastern_Palace_Boss: Hookshot\n"
        "  Eastern Palace - Boss: Bow\n";
    CustomizerManifest m;
    char err[160];
    if (Customizer_Parse(dup_text, strlen(dup_text), &m, err, sizeof err) == 0)
      customizer_selfcheck_die("a double-pinned location must be rejected");
  }
  {
    const char *bad_text = "placements:\n  Not A Place: Hookshot\n";
    CustomizerManifest m;
    char err[160];
    if (Customizer_Parse(bad_text, strlen(bad_text), &m, err, sizeof err) == 0)
      customizer_selfcheck_die("an unknown location must be rejected");
  }
  {
    const char *bad_pool_text =
        "placements:\n"
        "  Eastern_Palace_Boss: Hookshot\n"
        "pool_overrides:\n"
        "  add: [HyruleCastleBigKey]\n";
    CustomizerManifest m;
    char err[160];
    if (Customizer_Parse(bad_pool_text, strlen(bad_pool_text), &m, err, sizeof err) == 0)
      customizer_selfcheck_die("a virtual non-grantable pool add must be rejected");
  }

  fprintf(stderr, "[Customizer_SelfCheck] OK\n");
}

// Placement-path regression guard (CI via --rando-selftest). The corpus only
// covers customizer-OFF seeds; this exercises the §3c pin block end-to-end:
// install a manifest, run the real placer, and assert (a) every pin is honored
// in the output table and (b) two runs of the same (manifest, seed, settings)
// produce an identical placement digest. A future placer refactor that breaks
// customizer placement fails here on every platform.
void Customizer_PlacementSelfCheck(void) {
  extern const uint32 kRandoLocationsCount;

  CustomizerManifest m;
  memset(&m, 0, sizeof m);
  static const struct { const char *loc; const char *item; } kPins[] = {
    {"Eastern Palace - Big Chest", "Hookshot"},
    {"Link's Uncle", "ProgressiveSword"},
    {"Desert Palace - Big Chest", "ProgressiveBow"},
  };
  for (unsigned i = 0; i < sizeof kPins / sizeof kPins[0]; i++) {
    uint16 lid = Customizer_ResolveLocation(kPins[i].loc);
    uint16 iid = Customizer_ResolveItem(kPins[i].item);
    if (lid == 0xFFFF || iid == 0xFFFF)
      customizer_selfcheck_die("placement selfcheck: a pin name failed to resolve");
    m.pins[m.pin_count].location_id = lid;
    m.pins[m.pin_count].item_id = iid;
    m.pin_count++;
  }

  RandoSettings s;
  Settings_SetDefaults(&s);  // Open / Fast Ganon — these pins are reachable
  s.customizer_active = 1;

  RandoPlacement *e1 = (RandoPlacement *)calloc(kRandoLocationsCount, sizeof(RandoPlacement));
  RandoPlacement *e2 = (RandoPlacement *)calloc(kRandoLocationsCount, sizeof(RandoPlacement));
  if (e1 == NULL || e2 == NULL) customizer_selfcheck_die("placement selfcheck: out of memory");
  RandoPlacementTable t1 = { e1, 0 };
  RandoPlacementTable t2 = { e2, 0 };

  Customizer_Install(&m);
  bool ok1 = Place_AssumedFill(&s, 0x00C0FFEEull, 0, &t1);
  bool ok2 = Place_AssumedFill(&s, 0x00C0FFEEull, 0, &t2);
  Customizer_Install(NULL);
  if (!ok1 || !ok2) {
    const char *e = Customizer_LastError();
    fprintf(stderr, "Customizer_PlacementSelfCheck: Place_AssumedFill failed%s%s\n",
            e[0] ? ": " : "", e);
    free(e1); free(e2);
    exit(2);
  }

  // (a) every pin honored in the output table.
  for (uint16 i = 0; i < m.pin_count; i++) {
    uint16 got = 0xFFFF;
    for (uint16 k = 0; k < t1.count; k++) {
      if (t1.entries[k].location_id == m.pins[i].location_id) { got = t1.entries[k].item_id; break; }
    }
    if (got != m.pins[i].item_id) {
      fprintf(stderr, "Customizer_PlacementSelfCheck: pin %u (loc %u) resolved to %u, want %u\n",
              i, m.pins[i].location_id, got, m.pins[i].item_id);
      free(e1); free(e2);
      exit(2);
    }
  }

  // (b) determinism: identical digest across two runs.
  uint8 d1[32], d2[32];
  PlacementTable_ComputeDigest(&t1, d1);
  PlacementTable_ComputeDigest(&t2, d2);
  free(e1); free(e2);
  if (memcmp(d1, d2, 32) != 0)
    customizer_selfcheck_die("placement selfcheck: non-deterministic digest for a fixed (manifest, seed)");

  {
    CustomizerManifest bad;
    memset(&bad, 0, sizeof bad);
    bad.pins[0].location_id = Customizer_ResolveLocation("Eastern_Palace_Boss");
    bad.pins[0].item_id = Customizer_ResolveItem("HyruleCastleBigKey");
    bad.pin_count = 1;
    if (bad.pins[0].location_id == 0xFFFF || bad.pins[0].item_id == 0xFFFF)
      customizer_selfcheck_die("placement selfcheck: virtual item names failed to resolve");
    RandoPlacement *bad_entries =
        (RandoPlacement *)calloc(kRandoLocationsCount, sizeof(RandoPlacement));
    if (bad_entries == NULL)
      customizer_selfcheck_die("placement selfcheck: out of memory for bad pin");
    RandoPlacementTable bad_table = { bad_entries, 0 };
    Customizer_Install(&bad);
    bool bad_ok = Place_AssumedFill(&s, 0x00C0FFEEull, 0, &bad_table);
    Customizer_Install(NULL);
    free(bad_entries);
    if (bad_ok || strstr(Customizer_LastError(), "not grantable") == NULL)
      customizer_selfcheck_die("placement selfcheck: virtual non-grantable pin must be rejected");
  }

  fprintf(stderr, "[Customizer_PlacementSelfCheck] OK\n");
}
