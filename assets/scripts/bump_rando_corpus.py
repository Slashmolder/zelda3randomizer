#!/usr/bin/env python3
"""Bump-corpus tool (tasks.md §12.5).

When `kGeneratorVersion` advances (per `docs/randomizer.md` "Generator version
bump policy"), the existing regression-corpus digests no longer match — the
placement algorithm or RNG behavior changed. This tool:

  1. Regenerates the placement-table digest for every entry in
     ``tests/rando_corpus/manifest.yaml`` against the current binary.
  2. Updates each entry's ``expected_digest`` to the new value.
  3. Updates the manifest's ``generator_version`` field to the current
     ``kGeneratorVersion``.
  4. Prints a summary of changed digests for inclusion in the commit message.

**A0 status (scaffold)**: until task 1.6a's ``--generate-seed`` produces real
spoiler JSON (Phase A1 single-seed path is wired but reads default settings),
this script's "regenerate" step is a stub that reports which entries WOULD
change. Once the generator stabilizes (task 14.1b acceptance), flip the
``--apply`` flag on in CI to write the updates.

Usage:
  python assets/scripts/bump_rando_corpus.py            # dry-run: report
  python assets/scripts/bump_rando_corpus.py --apply    # write updated manifest
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent.parent
MANIFEST = REPO / "tests" / "rando_corpus" / "manifest.yaml"
RANDO_H = REPO / "src" / "rando" / "rando.h"
BINARY = REPO / "zelda3"


def current_generator_version() -> int:
    """Read kGeneratorVersion from src/rando/rando.h."""
    text = RANDO_H.read_text(encoding="utf-8")
    match = re.search(r"#define\s+kGeneratorVersion\s+(\d+)u?\b", text)
    if not match:
        print("ERROR: kGeneratorVersion not found in src/rando/rando.h")
        sys.exit(1)
    return int(match.group(1))


def version_line(version: int, note: str = "") -> str:
    """Render the generator_version line with a provenance comment.

    The comment is REGENERATED from facts on every bump rather than preserved,
    so it can never go stale (a prior bug left a "Phase B Slice 3b — Retro TakeAny"
    comment on an unrelated later bump). The bump's rationale belongs in the git
    commit + the kGeneratorVersion comment in src/rando/rando.h; the manifest
    comment just records that this field is tool-managed. `--note` appends context.
    """
    comment = "bump_rando_corpus.py --apply"
    if note:
        comment += f" - {note}"  # ASCII separator — the manifest is hand-editable; avoid non-ASCII
    return f"generator_version: {version}  # {comment}"


# Matches the whole generator_version line (incl. any trailing comment) so the
# comment is rewritten, not preserved. The capture-free [^\n]* consumes the
# stale comment that the old `^generator_version:\s*(?:\d+|null)`-only sub left behind.
VERSION_LINE_RE = re.compile(r"^generator_version:[^\n]*", re.MULTILINE)


def manifest_version(manifest_text: str) -> int | None:
    """Parse generator_version from the YAML manifest (very simple parser)."""
    match = re.search(r"^generator_version:\s*(\d+|null)\s*(?:#.*)?$",
                      manifest_text, re.MULTILINE)
    if not match:
        return None
    if match.group(1) == "null":
        return None
    return int(match.group(1))


def regenerate_entry(binary: Path, settings: dict, seed: str) -> tuple[str | None, str | None]:
    """Run --generate-seed for one entry, return (placement_digest, sphere_digest)."""
    if not binary.exists():
        return None, None
    settings_csv = ",".join(f"{k}={v}" for k, v in settings.items())
    with tempfile.TemporaryDirectory() as td:
        out_json = Path(td) / "out.json"
        try:
            subprocess.run(
                [str(binary), "--generate-seed",
                 f"--settings={settings_csv}",
                 f"--seed={seed}",
                 f"--out-spoiler={out_json}"],
                check=True, capture_output=True, timeout=60,
            )
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
            print(f"  generator failed for seed={seed}: {e}", file=sys.stderr)
            return None, None
        if not out_json.exists():
            return None, None
        # Phase B Slice 6 — race-mode entries emit a 138-byte ZRSR binary
        # instead of JSON. Invoke --reveal-spoiler on the suppressed file
        # (it overwrites the file in place with full JSON) and parse the
        # revealed digest. This matches the steady-state corpus runner's
        # behavior and ensures a kGeneratorVersion bump regenerates the
        # ZRSR-path digest the same way it regenerates the CSV-path
        # digest. Prior implementation
        # returned ("<ZRSR>", "<ZRSR>") and the caller skipped comparison,
        # leaving stale expected_digest values in the manifest with no
        # proof the new binary's reveal-spoiler path produced the same
        # placement.
        head = out_json.read_bytes()[:4]
        if head == b"ZRSR":
            try:
                subprocess.run(
                    [str(binary), f"--reveal-spoiler={out_json}"],
                    check=True, capture_output=True, timeout=60,
                )
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
                print(f"  reveal failed for seed={seed}: {e}", file=sys.stderr)
                return None, None
        spoiler = json.loads(out_json.read_text(encoding="utf-8"))
        meta = spoiler.get("meta", {})
        return meta.get("placement_digest_hex"), meta.get("sphere_digest")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true",
                        help="Write the updated manifest (default: dry-run report).")
    parser.add_argument("--binary", type=Path, default=BINARY,
                        help="Path to the zelda3 binary (default: ./zelda3)")
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--note", default="",
                        help="Short reason appended to the regenerated "
                             "generator_version provenance comment "
                             "(e.g. --note 'Inverted Ganon override').")
    args = parser.parse_args(argv)

    if not args.manifest.exists():
        print(f"ERROR: {args.manifest} not found")
        return 1

    # Use PyYAML if available; fall back to manual parse for the simple fields.
    try:
        import yaml  # type: ignore
    except ImportError:
        print("ERROR: PyYAML not installed (pip install -r requirements.txt).")
        return 2

    text = args.manifest.read_text(encoding="utf-8")
    data = yaml.safe_load(text) or {}
    entries = data.get("entries", [])

    current = current_generator_version()
    in_manifest = manifest_version(text)

    print(f"bump_rando_corpus: kGeneratorVersion = {current}")
    print(f"  manifest generator_version = {in_manifest}")
    print(f"  manifest entries = {len(entries)}")

    if in_manifest == current:
        if entries:
            print("\nNo bump needed — manifest is already at current version.")
        else:
            print("\nNo bump needed — manifest version matches and is empty.")
        return 0

    if not entries:
        # Version mismatch but no entries to regenerate. Just bump the manifest version.
        if args.apply:
            new_text = VERSION_LINE_RE.sub(
                lambda m: version_line(current, args.note), text, count=1)
            args.manifest.write_text(new_text, encoding="utf-8")
            print(f"\nWrote generator_version: {current} to {args.manifest}")
        else:
            print(f"\n[dry-run] Would write generator_version: {current}")
        return 0

    # Regenerate digests.
    print("\nRegenerating placement digests...")
    changed: list[tuple[int, str, str, str]] = []  # (idx, label, old, new) — placement digest
    sphere_changes: list[tuple[int, str, str]] = []  # (idx, old, new) — sphere digest
    for i, entry in enumerate(entries):
        old = entry.get("expected_digest", "<absent>")
        old_sphere = entry.get("expected_sphere_digest", "")
        new, new_sphere = regenerate_entry(args.binary, entry.get("settings", {}),
                                            str(entry.get("seed", "")))
        if new is None:
            print(f"  [{i}] {entry.get('label', '?')}: SKIP (generator unavailable / failed)")
            continue
        # NOTE: prior versions returned ("<ZRSR>", "<ZRSR>") for race-mode
        # entries and skipped here. The M3 fix at regenerate_entry now
        # invokes --reveal-spoiler and returns the revealed digest, so this
        # branch is no longer reachable. Kept as a defensive assert.
        assert new != "<ZRSR>", "regenerate_entry returned sentinel for race-mode entry — M3 regression?"
        if new != old:
            changed.append((i, entry.get("label", "?"), old, new))
            entry["expected_digest"] = new
            print(f"  [{i}] {entry.get('label', '?')}: {old[:8]}... -> {new[:8]}...")
        else:
            print(f"  [{i}] {entry.get('label', '?')}: unchanged")
        # Sphere digest is optional — only update entries whose manifest row
        # already carries an expected_sphere_digest field.
        if old_sphere and new_sphere and new_sphere != old_sphere:
            sphere_changes.append((i, old_sphere, new_sphere))
            entry["expected_sphere_digest"] = new_sphere

    if args.apply:
        # Update generator_version + serialize back. The version line's comment is
        # regenerated (never preserved → never stale); entry digests are updated
        # in place below, which DOES preserve their surrounding comments/layout.
        new_text = VERSION_LINE_RE.sub(
            lambda m: version_line(current, args.note), text, count=1)
        # In-place digest update preserves the manifest's comments + layout.
        # We match each entry by its label and replace its expected_digest.
        for (idx, label, old_d, new_d) in changed:
            if not old_d or old_d == "<absent>":
                continue
            # Replace exactly: expected_digest: "<old>" → "<new>"
            pattern = (r'(\bexpected_digest:\s*[\'"])' + re.escape(old_d) + r'([\'"])')
            new_text, count = re.subn(pattern,
                                       lambda m, n=new_d: f"{m.group(1)}{n}{m.group(2)}",
                                       new_text, count=1)
            if count == 0:
                print(f"  WARNING: could not find expected_digest {old_d[:8]}... in manifest text",
                      file=sys.stderr)
        # Same in-place update for sphere digests.
        for (idx, old_s, new_s) in sphere_changes:
            pattern = (r'(\bexpected_sphere_digest:\s*[\'"])' + re.escape(old_s) + r'([\'"])')
            new_text, count = re.subn(pattern,
                                       lambda m, n=new_s: f"{m.group(1)}{n}{m.group(2)}",
                                       new_text, count=1)
            if count == 0:
                print(f"  WARNING: could not find expected_sphere_digest {old_s[:8]}... in manifest text",
                      file=sys.stderr)
        args.manifest.write_text(new_text, encoding="utf-8")
        print(f"\nWrote updated manifest to {args.manifest}")
        print(f"  generator_version: {in_manifest} -> {current}")
        print(f"  {len(changed)} digest(s) updated")
    else:
        print(f"\n[dry-run] Would update generator_version: {in_manifest} -> {current}")
        print(f"[dry-run] Would update {len(changed)} digest(s)")
        print("\nRe-run with --apply to write the changes.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
