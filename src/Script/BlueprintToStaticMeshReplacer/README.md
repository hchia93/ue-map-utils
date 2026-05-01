# BlueprintToStaticMeshReplacer Scripts

Tooling that drives the `MapUtilsBlueprintToStaticMeshReplacer` commandlet:
classify candidate wrapper Blueprints, snapshot the levels they live in, run the
replacement, and audit the Before/After diff.

## Files

| File | Role |
|---|---|
| `classify.py` | Read `BlueprintEdGraphExport` JSONs, emit candidates.csv (Status,Name,...) |
| `discover_levels.py` | Read candidates.csv, emit deduplicated list of level packages referenced by candidates |
| `snapshot.sh` | Snapshot driver: SVN pre-flight -> re-export BPs -> classify -> discover levels -> LevelExport -> copy to `Before/` -> manifest |
| `audit.py` | Compare Before/After LevelExport JSONs and produce per-level diff reports |

## Snapshot phase

Run from project root **after LDs have saved and committed all maps**:

```bash
bash Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/snapshot.sh
```

Pre-conditions:

- SVN working copy is clean (snapshot.sh refuses to run otherwise; set `SKIP_SVN_CHECK=1` to override)
- The project's editor target is built and up-to-date
- `UAssetJsonExporter` plugin compiled with the matching schema version

Env overrides: `UE_PATH`, `UPROJECT`, `PYTHON`.

Outputs (under `Intermediate/BlueprintToStaticMeshReplacer/`):

- `candidates.csv` — full classification of all `/Game/Asset/**/BP_*.uasset`
- `levels.txt` — deduped level package paths to snapshot
- `Before/<level_path>.json` — LevelExport output, frozen as the diff baseline for audit
- `manifest.json` — SVN revision + counts + timestamp

## Standalone usage

Re-classify without re-running the commandlet:

```bash
python Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/classify.py --pretty   # human-readable to stderr
python Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/classify.py            # CSV to stdout
```

Get the level list without the full snapshot pipeline:

```bash
python Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/discover_levels.py \
    --candidates Intermediate/BlueprintToStaticMeshReplacer/candidates.csv
```

Audit a completed run:

```bash
python Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/audit.py
```

## Conventions

- Run from project root
- Python 3.10+; auto-detected from PATH (`python3` / `py` / `python`)
- Scripts never write to `Content/` or invoke SVN mutations
- Every artifact lives under `Intermediate/` (gitignored / svn-ignored)
