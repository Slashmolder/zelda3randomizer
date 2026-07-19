#!/usr/bin/env python3
"""Mirror local, gitignored runtime/build inputs into the current git worktree.

The project's ROM (`zelda3.smc` / `zelda3.sfc`), extracted asset blob
(`zelda3_assets.dat`), and ROM-derived randomizer metadata are gitignored, so
freshly-created git worktrees lack them. Asset-loading CLI paths need those
files before they can run.

On Windows/MSBuild, SDL2 is also gitignored under `third_party/SDL2-2.32.10/`.
Mirror that directory from the main worktree so a fresh worktree can build
without manually re-extracting the SDL2-devel zip.

We also mirror `zelda3.ini` (optional, best-effort). The exe searches parent
directories for `zelda3.ini`, so a worktree run with no local ini falls back
to the *main checkout's* ini and writes its F12 state dumps + SRAM saves into
the main checkout root — polluting it and risking save collisions. A local ini
keeps dumps/saves inside the worktree. Missing-source-ini is non-fatal: the exe
still runs via the parent-dir fallback.

Run this script after creating a new worktree to mirror the ROM, assets, chest
table, optional pot-shuffle and enemy-check artifacts, Windows SDL2, and
optional ini from the main worktree. If assets are present but
`src/rando/vanilla_assets_hash.h` is missing, the script generates the header
from the local `zelda3_assets.dat`. The script is idempotent: if files already
exist locally it does nothing.

Usage:
    python assets/scripts/setup_worktree.py              # auto-detect main worktree
    python assets/scripts/setup_worktree.py --from PATH  # explicit source dir
    python assets/scripts/setup_worktree.py --verify     # check, don't copy
    python assets/scripts/setup_worktree.py --verify --require-pot-artifacts
    python assets/scripts/setup_worktree.py --verify --require-enemy-artifacts
    python assets/scripts/setup_worktree.py --verify --require-terrain-artifacts
    python assets/scripts/setup_worktree.py --verify --require-all-artifacts

Resolution order for the source:
    1. --from PATH command-line override
    2. ZELDA3_MAIN_WORKTREE environment variable
    3. `git worktree list` -- prefer the checkout with a real `.git`
       directory, then fall back to the shortest path

Exits 0 if required inputs are present (or were successfully mirrored), 1
otherwise. On Windows, vendored SDL2 is required; on other platforms it is not.
Pot-shuffle artifacts are optional by default because public/assetless builds
intentionally omit them; use `--require-pot-artifacts` for a pot-ready worktree.
Enemy-check artifacts follow the same rule; use `--require-enemy-artifacts` for
an enemy-drop-check-ready worktree. Terrain has a matching flag. The
`--require-all-artifacts` profile requires every local registry used by the
full randomizer validation flow, including soul-room and bonk registries.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

from check_assets_signature import AssetSignatureError
from check_assets_signature import check_assets_signature

ROM_NAMES = ("zelda3.sfc", "zelda3.smc")
ASSETS_NAME = "zelda3_assets.dat"
INI_NAME = "zelda3.ini"  # optional, best-effort (keeps dumps/saves in the worktree)
# Generated/gitignored ROM-extracted chest table, read by rando_logic_gen.py at
# codegen time to build src/rando/chest_lookup.h (the (room,ordinal)->LOC map the
# runtime chest dispatch uses). If it is ABSENT when the codegen runs, chest_lookup.h
# is emitted EMPTY and *every chest grants its vanilla item* — a silent, very
# confusing playtest failure (placement is correct, but no chest resolves to its
# placed item). Mirror it so worktree builds dispatch chests correctly.
CHEST_TABLE_REL = os.path.join("assets", "rando", "chest_table.gen.bin")
POT_ARTIFACT_RELS = (
    os.path.join("assets", "rando", "pot_dump.gen.txt"),
    os.path.join("assets", "rando", "pots.gen.yaml"),
    os.path.join("assets", "rando", "pot_key_depth.gen.yaml"),
)
# add-rando-enemy-drop-sanity: forced-drop and ordinary-enemy registries. They
# are generated together from zelda3_assets.dat plus a key-depth dump from a
# built binary. The build can intentionally omit them, but every
# enemy_drop_checks tier then fails closed. Mirror the complete pair so a fresh
# worktree does not need a warning-producing bootstrap build followed by local
# prepare and a second build.
ENEMY_ARTIFACT_RELS = (
    os.path.join("assets", "rando", "enemy_drops.gen.yaml"),
    os.path.join("assets", "rando", "enemy_checks.gen.yaml"),
)
# add-rando-grass-rock-shuffle: overworld terrain dump + registry (gitignored,
# ROM-derived). Complete-set rule like pots: the dump regenerates the registry,
# the registry feeds rando codegen (terrain_lookup.h); absent => no terrain
# locations and grass/rock tiers fail closed via the registry digest guard.
TERRAIN_ARTIFACT_RELS = (
    os.path.join("assets", "rando", "terrain_dump.gen.txt"),
    os.path.join("assets", "rando", "terrain.gen.yaml"),
)
# add-rando-bonk-sanity: its own group, NOT part of the terrain complete-set
# (external-review P2: riding the terrain tuple made a missing bonk artifact
# veto the whole terrain mirror). Regenerable from the mirrored
# zelda3_assets.dat alone (gen_bonk_tables.py), so mirror-else-regenerate
# like soul_rooms. Absent => bonk tiers fail closed via the registry guard.
BONK_REGISTRY_REL = os.path.join("assets", "rando", "bonk.gen.yaml")
# add-enemy-souls: kill-gated-room soul requirements. Local/gitignored; read by
# rando_logic_gen.py. Absent => kRandoSoulRoomsBaked=0 and souls_shuffle=all
# seeds fail closed in BuildItemPool (loud, not silent), but a worktree that
# can regenerate it should. Regenerable from the mirrored zelda3_assets.dat +
# the COMMITTED door tables (no key_depth.txt needed), so we mirror it and fall
# back to running the generator.
SOUL_ROOMS_REL = os.path.join("assets", "rando", "soul_rooms.gen.yaml")
VANILLA_ASSETS_HASH_REL = os.path.join("src", "rando", "vanilla_assets_hash.h")
SDL2_REL = os.path.join("third_party", "SDL2-2.32.10")
SDL2_REQUIRED_RELS = (
    os.path.join(SDL2_REL, "include", "SDL.h"),
    os.path.join(SDL2_REL, "lib", "x64", "SDL2.lib"),
    os.path.join(SDL2_REL, "lib", "x64", "SDL2.dll"),
    os.path.join(SDL2_REL, "lib", "x86", "SDL2.lib"),
    os.path.join(SDL2_REL, "lib", "x86", "SDL2.dll"),
)
SDL2_REQUIRED_ON_THIS_PLATFORM = os.name == "nt"


def find_main_worktree() -> Path | None:
    """Run `git worktree list --porcelain` and return the main worktree's path.

    Heuristic: linked worktrees normally have a `.git` file that points back
    to the main checkout's metadata, while the main checkout has a `.git`
    directory. If that signal is unavailable, prefer the shortest path.
    """
    try:
        out = subprocess.check_output(
            ["git", "worktree", "list", "--porcelain"],
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        print(f"setup_worktree: git worktree list failed: {exc}", file=sys.stderr)
        return None

    candidates: list[Path] = []
    for line in out.splitlines():
        if line.startswith("worktree "):
            candidates.append(Path(line[len("worktree "):].strip()))

    main_candidates = [p for p in candidates if (p / ".git").is_dir()]
    if not main_candidates:
        main_candidates = candidates
    if not main_candidates:
        return None
    # Prefer the shortest path — if multiple main-style worktrees exist,
    # the canonical checkout will typically be the shortest.
    main_candidates.sort(key=lambda p: len(p.as_posix()))
    return main_candidates[0]


def find_existing_rom(d: Path) -> Path | None:
    for name in ROM_NAMES:
        p = d / name
        if p.is_file():
            return p
    return None


def has_vendored_sdl2(d: Path) -> bool:
    return all((d / rel).is_file() for rel in SDL2_REQUIRED_RELS)


def missing_pot_artifacts(d: Path) -> list[str]:
    return [rel for rel in POT_ARTIFACT_RELS if not (d / rel).is_file()]


def has_pot_artifacts(d: Path) -> bool:
    return not missing_pot_artifacts(d)


def missing_enemy_artifacts(d: Path) -> list[str]:
    return [rel for rel in ENEMY_ARTIFACT_RELS if not (d / rel).is_file()]


def has_enemy_artifacts(d: Path) -> bool:
    return not missing_enemy_artifacts(d)


def missing_terrain_artifacts(d: Path) -> list[str]:
    return [rel for rel in TERRAIN_ARTIFACT_RELS if not (d / rel).is_file()]


def has_terrain_artifacts(d: Path) -> bool:
    return not missing_terrain_artifacts(d)


def print_missing_sdl2(prefix: str, d: Path) -> None:
    missing = [rel for rel in SDL2_REQUIRED_RELS if not (d / rel).is_file()]
    print(f"{prefix} missing vendored SDL2 under {d / SDL2_REL}", file=sys.stderr)
    for rel in missing:
        print(f"  missing: {rel}", file=sys.stderr)


def print_missing_pot_artifacts(prefix: str, d: Path, *, required: bool) -> None:
    missing = missing_pot_artifacts(d)
    if not missing:
        return
    label = "required" if required else "optional"
    print(f"{prefix} missing {label} local pot-shuffle artifacts under "
          f"{d / 'assets' / 'rando'}", file=sys.stderr)
    for rel in missing:
        print(f"  missing: {rel}", file=sys.stderr)
    print("  prepare with: python assets/scripts/run_rando_local_checks.py "
          "--binary=<built zelda3.exe> --prepare-only", file=sys.stderr)


def print_missing_enemy_artifacts(prefix: str, d: Path, *, required: bool) -> None:
    missing = missing_enemy_artifacts(d)
    if not missing:
        return
    label = "required" if required else "optional"
    print(f"{prefix} missing {label} local enemy-check artifacts under "
          f"{d / 'assets' / 'rando'}", file=sys.stderr)
    for rel in missing:
        print(f"  missing: {rel}", file=sys.stderr)
    print("  prepare after a bootstrap build with: python "
          "assets/scripts/run_rando_local_checks.py "
          "--binary=<built zelda3.exe> --prepare-only", file=sys.stderr)


def print_missing_terrain_artifacts(prefix: str, d: Path, *, required: bool) -> None:
    missing = missing_terrain_artifacts(d)
    if not missing:
        return
    label = "required" if required else "optional"
    print(f"{prefix} missing {label} local terrain-shuffle artifacts under "
          f"{d / 'assets' / 'rando'}", file=sys.stderr)
    for rel in missing:
        print(f"  missing: {rel}", file=sys.stderr)
    print("  prepare with: python assets/scripts/run_rando_local_checks.py "
          "--binary=<built zelda3.exe> --prepare-only", file=sys.stderr)


def requested_local_artifacts_ok(d: Path, *, require_pots: bool,
                                 require_enemies: bool,
                                 require_terrain: bool,
                                 require_all: bool,
                                 prefix: str) -> bool:
    ok = True
    if (require_pots or require_all) and not has_pot_artifacts(d):
        print_missing_pot_artifacts(prefix, d, required=True)
        ok = False
    if (require_enemies or require_all) and not has_enemy_artifacts(d):
        print_missing_enemy_artifacts(prefix, d, required=True)
        ok = False
    if (require_terrain or require_all) and not has_terrain_artifacts(d):
        print_missing_terrain_artifacts(prefix, d, required=True)
        ok = False
    if require_all:
        for rel, label in ((SOUL_ROOMS_REL, "soul-room registry"),
                           (BONK_REGISTRY_REL, "bonk registry")):
            if not (d / rel).is_file():
                print(f"{prefix} missing required {label}: {rel}",
                      file=sys.stderr)
                ok = False
    return ok


def asset_signature_ok(asset_file: Path, repo: Path, *, label: str) -> bool:
    try:
        check_assets_signature(asset_file, repo / "src" / "assets.h")
        return True
    except AssetSignatureError as exc:
        print(f"setup_worktree: {label} {exc}", file=sys.stderr)
        return False


def ensure_vanilla_assets_hash(d: Path) -> int:
    """Generate the gitignored vanilla asset hash header from zelda3_assets.dat."""
    asset_file = d / ASSETS_NAME
    if not asset_file.is_file():
        print(f"setup_worktree: cannot generate {VANILLA_ASSETS_HASH_REL}: "
              f"{ASSETS_NAME} is missing.", file=sys.stderr)
        return 1
    if not asset_signature_ok(asset_file, d, label="local"):
        print("setup_worktree: cannot generate vanilla asset hash from a stale "
              "asset blob; rerun `python assets/restool.py --extract-from-rom`.",
              file=sys.stderr)
        return 1
    print(f"setup_worktree: generate {VANILLA_ASSETS_HASH_REL} from {ASSETS_NAME}")
    try:
        subprocess.check_call(
            [sys.executable, "assets/scripts/dump_vanilla_assets_hash.py"],
            cwd=d,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        print(f"setup_worktree: vanilla asset hash generation failed: {exc}",
              file=sys.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--from", dest="source", default=None,
                        help="explicit source directory (overrides auto-detect)")
    parser.add_argument("--verify", action="store_true",
                        help="check whether assets are present, don't copy")
    parser.add_argument("--require-pot-artifacts", action="store_true",
                        help="fail if local pot-shuffle artifacts are absent")
    parser.add_argument("--require-enemy-artifacts", action="store_true",
                        help="fail if local enemy-check artifacts are absent")
    parser.add_argument("--require-terrain-artifacts", action="store_true",
                        help="fail if local terrain-shuffle artifacts are absent")
    parser.add_argument("--require-all-artifacts", action="store_true",
                        help="fail unless every local randomizer artifact is present")
    args = parser.parse_args()

    cwd = Path.cwd().resolve()

    have_rom = find_existing_rom(cwd) is not None
    have_assets = (cwd / ASSETS_NAME).is_file()
    assets_ok = (have_assets and
                 asset_signature_ok(cwd / ASSETS_NAME, cwd, label="local"))
    have_ini = (cwd / INI_NAME).is_file()
    have_chest = (cwd / CHEST_TABLE_REL).is_file()
    have_hash = (cwd / VANILLA_ASSETS_HASH_REL).is_file()
    have_pots = has_pot_artifacts(cwd)
    have_enemies = has_enemy_artifacts(cwd)
    have_terrain = has_terrain_artifacts(cwd)
    # add-enemy-souls: soul_rooms.gen.yaml is gitignored/ROM-derived. Like pots,
    # it must gate the "nothing to do" early-exit below — otherwise a worktree
    # that already has rom+assets+hash+chest+pots bails BEFORE the soul block
    # ever runs, leaving souls_shuffle=all fail-closed with no attempt to
    # produce it. (npc_souls.yaml is COMMITTED, so it needs no mirroring.)
    have_souls = (cwd / SOUL_ROOMS_REL).is_file()
    # add-rando-bonk-sanity: same early-exit-gating rationale as souls — the
    # bonk mirror-else-regenerate block must stay reachable (external-review
    # round 2: it was unreachable in otherwise-prepared worktrees).
    have_bonk = (cwd / BONK_REGISTRY_REL).is_file()
    have_sdl2 = has_vendored_sdl2(cwd)
    need_sdl2 = SDL2_REQUIRED_ON_THIS_PLATFORM
    required_ready = (have_rom and assets_ok and have_chest and have_hash and
                      (have_sdl2 or not need_sdl2))

    if args.verify:
        if required_ready:
            if not requested_local_artifacts_ok(
                    cwd,
                    require_pots=args.require_pot_artifacts,
                    require_enemies=args.require_enemy_artifacts,
                    require_terrain=args.require_terrain_artifacts,
                    require_all=args.require_all_artifacts,
                    prefix="setup_worktree:"):
                return 1
            pot_note = " + pot artifacts" if have_pots else ""
            enemy_note = " + enemy artifacts" if have_enemies else ""
            terrain_note = " + terrain artifacts" if have_terrain else ""
            if need_sdl2:
                print(f"setup_worktree: OK (rom + assets + chest table + hash + SDL2{pot_note}{enemy_note}{terrain_note} present)")
            else:
                print(f"setup_worktree: OK (rom + assets + chest table + hash{pot_note}{enemy_note}{terrain_note} present)")
            if not have_pots:
                print_missing_pot_artifacts("setup_worktree:", cwd, required=False)
            if not have_enemies:
                print_missing_enemy_artifacts("setup_worktree:", cwd, required=False)
            if not have_terrain:
                print_missing_terrain_artifacts("setup_worktree:", cwd, required=False)
            if not have_souls:
                print(f"setup_worktree: {SOUL_ROOMS_REL} absent -- souls_shuffle=all "
                      f"fails closed; run this without --verify to produce it.",
                      file=sys.stderr)
            if not have_bonk:
                print(f"setup_worktree: {BONK_REGISTRY_REL} absent -- bonk_shuffle "
                      f"fails closed; run this without --verify to produce it.",
                      file=sys.stderr)
            return 0
        if not have_rom:
            print(f"setup_worktree: MISSING {ROM_NAMES} in {cwd}")
        if not have_assets:
            print(f"setup_worktree: MISSING {ASSETS_NAME} in {cwd}")
        if not have_chest:
            print(f"setup_worktree: MISSING {CHEST_TABLE_REL} in {cwd}")
        if not have_hash:
            print(f"setup_worktree: MISSING {VANILLA_ASSETS_HASH_REL} in {cwd}")
        if need_sdl2 and not have_sdl2:
            print_missing_sdl2("setup_worktree:", cwd)
        if not have_pots:
            print_missing_pot_artifacts("setup_worktree:", cwd,
                                        required=args.require_pot_artifacts)
        if not have_enemies:
            print_missing_enemy_artifacts("setup_worktree:", cwd,
                                          required=args.require_enemy_artifacts)
        if not have_terrain:
            print_missing_terrain_artifacts(
                "setup_worktree:", cwd,
                required=(args.require_terrain_artifacts or args.require_all_artifacts))
        return 1

    if (have_rom and assets_ok and have_ini and have_chest and have_hash and
            have_pots and have_enemies and have_terrain and have_souls and have_bonk and
            (have_sdl2 or not need_sdl2)):
        if need_sdl2:
            print("setup_worktree: nothing to do (rom + assets + hash + ini + "
                  "chest table + pot artifacts + enemy artifacts + terrain + soul rooms + "
                  "SDL2 already present)")
        else:
            print("setup_worktree: nothing to do (rom + assets + hash + ini + "
                  "chest table + pot artifacts + enemy artifacts + terrain + soul rooms "
                  "already present)")
        return 0

    # Resolve the source.
    source: Path | None = None
    if args.source:
        source = Path(args.source).resolve()
    elif "ZELDA3_MAIN_WORKTREE" in os.environ:
        source = Path(os.environ["ZELDA3_MAIN_WORKTREE"]).resolve()
    else:
        source = find_main_worktree()

    if source is None:
        # The ini is optional; only a missing ROM/assets is fatal.
        if have_rom and assets_ok and (have_sdl2 or not need_sdl2):
            if not have_hash:
                rc = ensure_vanilla_assets_hash(cwd)
                if rc:
                    return rc
            print("setup_worktree: rom + assets present; could not locate main "
                  "worktree to mirror optional zelda3.ini (skipping -- the exe "
                  "falls back to a parent-dir ini).", file=sys.stderr)
            if not have_chest:
                # Not optional: an absent chest table → empty chest_lookup.h → all
                # chests grant vanilla. We can't mirror it (no source), so warn loudly.
                print(f"setup_worktree: WARNING {CHEST_TABLE_REL} is MISSING and no "
                      f"source was found to mirror it -- worktree chest_lookup.h will "
                      f"be EMPTY and ALL CHESTS will grant their vanilla item. Pass "
                      f"--from <main-worktree>, set ZELDA3_MAIN_WORKTREE, or run "
                      f"`python assets/restool.py --extract-from-rom`.", file=sys.stderr)
            if not have_pots:
                print_missing_pot_artifacts("setup_worktree:", cwd,
                                            required=args.require_pot_artifacts)
                if args.require_pot_artifacts:
                    return 1
            if not have_enemies:
                print_missing_enemy_artifacts(
                    "setup_worktree:", cwd,
                    required=args.require_enemy_artifacts)
                if args.require_enemy_artifacts:
                    return 1
            if not requested_local_artifacts_ok(
                    cwd,
                    require_pots=args.require_pot_artifacts,
                    require_enemies=args.require_enemy_artifacts,
                    require_terrain=args.require_terrain_artifacts,
                    require_all=args.require_all_artifacts,
                    prefix="setup_worktree:"):
                return 1
            return 0
        if need_sdl2 and not have_sdl2:
            print_missing_sdl2("setup_worktree:", cwd)
        print("setup_worktree: could not locate main worktree. Pass --from PATH,",
              file=sys.stderr)
        print("                set ZELDA3_MAIN_WORKTREE, or ensure git knows about",
              file=sys.stderr)
        print("                the main checkout.", file=sys.stderr)
        return 1

    if source.resolve() == cwd:
        if have_rom and assets_ok and have_chest and (have_sdl2 or not need_sdl2):
            if not have_hash:
                rc = ensure_vanilla_assets_hash(cwd)
                if rc:
                    return rc
            if not have_pots:
                print_missing_pot_artifacts("setup_worktree:", cwd,
                                            required=args.require_pot_artifacts)
                if args.require_pot_artifacts:
                    return 1
            if not have_enemies:
                print_missing_enemy_artifacts(
                    "setup_worktree:", cwd,
                    required=args.require_enemy_artifacts)
                if args.require_enemy_artifacts:
                    return 1
            if not have_bonk:
                gen = cwd / "assets" / "scripts" / "gen_bonk_tables.py"
                rc = subprocess.call([sys.executable, str(gen)], cwd=str(cwd))
                if rc or not (cwd / BONK_REGISTRY_REL).is_file():
                    print(f"setup_worktree: WARNING could not produce "
                          f"{BONK_REGISTRY_REL} -- bonk_shuffle seeds will fail "
                          f"closed.", file=sys.stderr)
            if not requested_local_artifacts_ok(
                    cwd,
                    require_pots=args.require_pot_artifacts,
                    require_enemies=args.require_enemy_artifacts,
                    require_terrain=args.require_terrain_artifacts,
                    require_all=args.require_all_artifacts,
                    prefix="setup_worktree:"):
                return 1
            print(f"setup_worktree: source == cwd ({source}); required inputs "
                  f"are present, nothing to mirror.")
            return 0
        print(f"setup_worktree: source == cwd ({source}); nothing to mirror.",
              file=sys.stderr)
        return 1

    print(f"setup_worktree: source = {source}")

    if not have_rom:
        src_rom = find_existing_rom(source)
        if src_rom is None:
            print(f"setup_worktree: source has no ROM ({ROM_NAMES}) -- cannot mirror.",
                  file=sys.stderr)
            print(f"                Place a copy in {source} first, or pass --from.",
                  file=sys.stderr)
            return 1
        dst_rom = cwd / src_rom.name
        print(f"setup_worktree: copy {src_rom} -> {dst_rom}")
        shutil.copy2(src_rom, dst_rom)

    copied_assets = False
    if not assets_ok:
        src_assets = source / ASSETS_NAME
        if not src_assets.is_file():
            print(f"setup_worktree: source has no {ASSETS_NAME} -- cannot mirror.",
                  file=sys.stderr)
            print(f"                Run `python assets/restool.py --extract-from-rom`",
                  file=sys.stderr)
            print(f"                in {source} first, or pass --from to a populated dir.",
                  file=sys.stderr)
            return 1
        if not asset_signature_ok(src_assets, cwd, label="source"):
            print("setup_worktree: source asset blob is stale for this checkout; "
                  "run `python assets/restool.py --extract-from-rom` in the "
                  "source checkout first.", file=sys.stderr)
            return 1
        dst_assets = cwd / ASSETS_NAME
        print(f"setup_worktree: copy {src_assets} -> {dst_assets}")
        shutil.copy2(src_assets, dst_assets)
        have_assets = True
        assets_ok = True
        copied_assets = True

    # Windows/MSBuild requires the gitignored SDL2-devel VC package. Copy the
    # whole directory so both x64 and Win32 project platforms stay usable.
    if need_sdl2 and not have_sdl2:
        src_sdl2 = source / SDL2_REL
        if not has_vendored_sdl2(source):
            print_missing_sdl2("setup_worktree: source", source)
            print("setup_worktree: re-extract third_party/SDL2-2.32.10/ from "
                  "libsdl.org's SDL2-devel-2.32.10-VC.zip, then re-run this "
                  "script.", file=sys.stderr)
            return 1
        dst_sdl2 = cwd / SDL2_REL
        if dst_sdl2.exists() and not dst_sdl2.is_dir():
            print(f"setup_worktree: {dst_sdl2} exists but is not a directory.",
                  file=sys.stderr)
            return 1
        print(f"setup_worktree: copy {src_sdl2} -> {dst_sdl2}")
        shutil.copytree(src_sdl2, dst_sdl2, dirs_exist_ok=True)

    # Optional: mirror zelda3.ini so F12 dumps + SRAM saves stay in the worktree
    # instead of falling back to the main checkout's ini (and its directory).
    # Best-effort: a missing source ini is non-fatal.
    if not have_ini:
        src_ini = source / INI_NAME
        if src_ini.is_file():
            dst_ini = cwd / INI_NAME
            print(f"setup_worktree: copy {src_ini} -> {dst_ini}")
            shutil.copy2(src_ini, dst_ini)
        else:
            print(f"setup_worktree: source has no {INI_NAME} (optional) -- skipping; "
                  f"the exe will fall back to a parent-dir ini.", file=sys.stderr)

    # Mirror the generated chest table so the worktree's rando codegen can build a
    # populated chest_lookup.h. Without it, chest_lookup.h is emitted EMPTY and
    # every chest grants vanilla (silent runtime breakage). Best-effort: a missing
    # source table is non-fatal here but loudly warned, since the symptom is
    # baffling at playtest time.
    if not have_chest:
        src_chest = source / CHEST_TABLE_REL
        if src_chest.is_file():
            dst_chest = cwd / CHEST_TABLE_REL
            dst_chest.parent.mkdir(parents=True, exist_ok=True)
            print(f"setup_worktree: copy {src_chest} -> {dst_chest}")
            shutil.copy2(src_chest, dst_chest)
        else:
            print(f"setup_worktree: WARNING source has no {CHEST_TABLE_REL} -- "
                  f"worktree chest_lookup.h will be EMPTY and ALL CHESTS will grant "
                  f"their vanilla item. Run `python assets/restool.py --extract-from-rom` "
                  f"in {source} to generate it, then re-run this script + rebuild.",
                  file=sys.stderr)

    # Optional local pot-shuffle artifacts. Mirror them only as a complete set:
    # pots.gen.yaml without pot_key_depth.gen.yaml makes rando codegen fail closed,
    # so a partial source set is not useful bootstrap material.
    if not have_pots:
        src_missing_pots = missing_pot_artifacts(source)
        if not src_missing_pots:
            for rel in POT_ARTIFACT_RELS:
                dst_pot = cwd / rel
                if dst_pot.is_file():
                    continue
                src_pot = source / rel
                dst_pot.parent.mkdir(parents=True, exist_ok=True)
                print(f"setup_worktree: copy {src_pot} -> {dst_pot}")
                shutil.copy2(src_pot, dst_pot)
            have_pots = has_pot_artifacts(cwd)
        else:
            print_missing_pot_artifacts("setup_worktree: source", source,
                                        required=args.require_pot_artifacts)
        if not have_pots:
            print_missing_pot_artifacts("setup_worktree:", cwd,
                                        required=args.require_pot_artifacts)
            if args.require_pot_artifacts:
                return 1

    # Optional local enemy-check artifacts. Mirror them only as a complete set:
    # both are generated from the same key-depth dump, and a partial set enables
    # only some enemy_drop_checks tiers. If the source is not populated, keep
    # the assetless/public-build behavior but explain the one-time two-build
    # bootstrap needed to generate the pair locally.
    if not have_enemies:
        src_missing_enemies = missing_enemy_artifacts(source)
        if not src_missing_enemies:
            for rel in ENEMY_ARTIFACT_RELS:
                dst_enemy = cwd / rel
                if dst_enemy.is_file():
                    continue
                src_enemy = source / rel
                dst_enemy.parent.mkdir(parents=True, exist_ok=True)
                print(f"setup_worktree: copy {src_enemy} -> {dst_enemy}")
                shutil.copy2(src_enemy, dst_enemy)
            have_enemies = has_enemy_artifacts(cwd)
        else:
            print_missing_enemy_artifacts(
                "setup_worktree: source", source,
                required=args.require_enemy_artifacts)
        if not have_enemies:
            print_missing_enemy_artifacts(
                "setup_worktree:", cwd,
                required=args.require_enemy_artifacts)
            if args.require_enemy_artifacts:
                return 1

    # add-rando-grass-rock-shuffle: overworld terrain artifacts — mirror as a
    # complete set (dump + registry), like pots. Absence is fail-closed for the
    # grass/rock tiers (registry digest guard), so best-effort + loud warning.
    terrain_missing_src = missing_terrain_artifacts(source)
    terrain_missing_dst = missing_terrain_artifacts(cwd)
    if terrain_missing_dst:
        if not terrain_missing_src:
            for rel in TERRAIN_ARTIFACT_RELS:
                dst_t = cwd / rel
                if dst_t.is_file():
                    continue
                src_t = source / rel
                dst_t.parent.mkdir(parents=True, exist_ok=True)
                print(f"setup_worktree: copy {src_t} -> {dst_t}")
                shutil.copy2(src_t, dst_t)
        else:
            print_missing_terrain_artifacts(
                "setup_worktree: source", source,
                required=(args.require_terrain_artifacts or args.require_all_artifacts))
            if args.require_terrain_artifacts or args.require_all_artifacts:
                return 1

    # add-enemy-souls: soul_rooms.gen.yaml — mirror, else regenerate from the
    # just-mirrored assets (needs only zelda3_assets.dat + committed door
    # tables). Absence is fail-closed for souls_shuffle=all, so this is
    # best-effort with a loud warning.
    dst_souls = cwd / SOUL_ROOMS_REL
    if not dst_souls.is_file():
        src_souls = source / SOUL_ROOMS_REL
        if src_souls.is_file():
            dst_souls.parent.mkdir(parents=True, exist_ok=True)
            print(f"setup_worktree: copy {src_souls} -> {dst_souls}")
            shutil.copy2(src_souls, dst_souls)
        else:
            gen = cwd / "assets" / "scripts" / "gen_soul_room_tables.py"
            rc = subprocess.call([sys.executable, str(gen)], cwd=str(cwd))
            if rc or not dst_souls.is_file():
                print(f"setup_worktree: WARNING could not produce {SOUL_ROOMS_REL} "
                      f"-- souls_shuffle=all seeds will fail closed in this "
                      f"worktree build (bosses tier unaffected).", file=sys.stderr)

    # add-rando-bonk-sanity: bonk.gen.yaml — mirror, else regenerate from the
    # just-mirrored zelda3_assets.dat (gen_bonk_tables.py needs nothing else).
    # Absence is fail-closed for the bonk tiers, so best-effort + loud warning.
    dst_bonk = cwd / BONK_REGISTRY_REL
    if not dst_bonk.is_file():
        src_bonk = source / BONK_REGISTRY_REL
        if src_bonk.is_file():
            dst_bonk.parent.mkdir(parents=True, exist_ok=True)
            print(f"setup_worktree: copy {src_bonk} -> {dst_bonk}")
            shutil.copy2(src_bonk, dst_bonk)
        else:
            gen = cwd / "assets" / "scripts" / "gen_bonk_tables.py"
            rc = subprocess.call([sys.executable, str(gen)], cwd=str(cwd))
            if rc or not dst_bonk.is_file():
                print(f"setup_worktree: WARNING could not produce {BONK_REGISTRY_REL} "
                      f"-- bonk_shuffle seeds will fail closed in this worktree "
                      f"build.", file=sys.stderr)

    if copied_assets or not (cwd / VANILLA_ASSETS_HASH_REL).is_file():
        rc = ensure_vanilla_assets_hash(cwd)
        if rc:
            return rc

    if not requested_local_artifacts_ok(
            cwd,
            require_pots=args.require_pot_artifacts,
            require_enemies=args.require_enemy_artifacts,
            require_terrain=args.require_terrain_artifacts,
            require_all=args.require_all_artifacts,
            prefix="setup_worktree:"):
        return 1

    print("setup_worktree: done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
