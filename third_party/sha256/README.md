# SHA-256

Vendored from <https://github.com/B-Con/crypto-algorithms> by Brad Conte.
Original disclaimer: "presented as-is without any guarantees" — public domain.

## Local modifications

- Renamed `BYTE` / `WORD` typedefs to `SHA256_BYTE` / `SHA256_WORD` to avoid
  collision with `src/types.h`'s `BYTE(x)` / `WORD(x)` cast macros.
- Switched `#include <stddef.h>` to `#include <stddef.h>` + `<stdint.h>`
  and replaced `unsigned int` with `uint32_t`.
- Added `sha256_buffer()` convenience wrapper for one-shot hashing.

## Use in this repo

- `src/rando/rando.c` (when it lands): computes `g_assets_hash` once after
  `LoadAssets` returns (tasks.md §1.1a).
- `src/rando/rando_share.c` (when it lands): settings-hash and share-string
  checksum (tasks.md §2.5).
- `src/rando/rando_save.c` (when it lands): sidecar slot header SHA fields.
- `assets/restool.py` (post-build step): SHA-256 of `zelda3_assets.dat` →
  `src/rando/vanilla_assets_hash.h` (tasks.md §1.1b).

## Verification

The implementation is the textbook FIPS 180-2 SHA-256. Known test vectors:

| Input | Expected SHA-256 |
|---|---|
| (empty) | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `abc` | `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` |

A self-test runs once at startup if the binary is built with `RANDO_SELFCHECK`
(see `src/rando/rando.c` for the runner).
