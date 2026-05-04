"""Aggregate level packages referenced by CANDIDATE Blueprints.

Reads candidates.csv (output of classify.py) plus the underlying BlueprintExport
JSONs, walks the `Referencers_Levels` array of each candidate, and emits a
deduplicated list of level package paths.

Usage:
    python Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/discover_levels.py \
        --candidates Intermediate/BlueprintToStaticMeshReplacer/candidates.csv \
        > Intermediate/BlueprintToStaticMeshReplacer/levels.txt

Output format:
    One level package path per line, sorted, e.g.
        /Game/Maps/L_Persistent
        /Game/Maps/Sublevels/L_Sub_A
        ...

The list is intentionally a flat text list (not CSV) so it can be piped into
the commandlet wrapper as a comma-separated argument:
    paste -sd, < levels.txt
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

# Force LF line endings on stdout, even on Windows, so paste / FParse-style
# consumers don't trip over CRs embedded as whitespace separators.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(newline="\n")


def _asset_path_to_export_json(asset_path: str, export_root: Path) -> Path:
    # /Game/Foo/Bar -> <export_root>/Game/Foo/Bar.json
    rel = asset_path.lstrip("/")
    return export_root / f"{rel}.json"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument(
        "--candidates",
        default=Path.cwd() / "Intermediate" / "BlueprintToStaticMeshReplacer" / "candidates.csv",
        type=Path,
    )
    parser.add_argument(
        "--export-root",
        default=Path.cwd() / "Intermediate" / "UAssetExport",
        type=Path,
        help="Root containing BlueprintExport JSONs. AssetPath '/Game/Foo' resolves to <root>/Game/Foo.json.",
    )
    parser.add_argument(
        "--with-counts",
        action="store_true",
        help="Annotate each level with the number of candidate BPs that reference it.",
    )
    args = parser.parse_args()

    if not args.candidates.exists():
        print(f"[discover_levels] candidates.csv not found: {args.candidates}", file=sys.stderr)
        return 1

    counts: dict[str, int] = {}
    total_candidates = 0

    with args.candidates.open(encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row["Status"] != "CANDIDATE":
                continue
            total_candidates += 1
            json_path = _asset_path_to_export_json(row["AssetPath"], args.export_root)
            if not json_path.exists():
                print(
                    f"[discover_levels] missing export for {row['Name']}: {json_path}",
                    file=sys.stderr,
                )
                continue
            d = json.load(json_path.open(encoding="utf-8"))
            for ref in d.get("Referencers_Levels", []):
                pkg = ref.get("PackageName")
                if pkg:
                    counts[pkg] = counts.get(pkg, 0) + 1

    print(
        f"[discover_levels] {total_candidates} candidates -> {len(counts)} unique levels",
        file=sys.stderr,
    )

    for pkg in sorted(counts):
        if args.with_counts:
            print(f"{pkg}\t{counts[pkg]}")
        else:
            print(pkg)

    return 0


if __name__ == "__main__":
    sys.exit(main())
