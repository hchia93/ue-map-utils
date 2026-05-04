#!/usr/bin/env bash
#
# Snapshot phase of the BlueprintToStaticMeshReplacer workflow.
#
# Sequence:
#   1. Pre-flight: assert SVN working copy is clean (no M / A / D / ?)
#   2. Re-export every BP_*.uasset under "$BP_ROOT" via BlueprintEdGraphExport
#   3. Classify into CANDIDATE / SKIP / EXCLUDE -> candidates.csv
#   4. Aggregate level packages referenced by candidates -> levels.txt
#   5. Run LevelExport on those levels
#   6. Copy LevelExport JSONs to Intermediate/BlueprintToStaticMeshReplacer/Before/
#   7. Record SVN revision + timestamps to manifest.json
#
# Run from project root:
#   bash Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/snapshot.sh
#
# Env overrides:
#   UE_PATH    Default "C:/Program Files/Epic Games/UE_5.7"
#   UPROJECT   Default: first *.uproject found in CWD
#   BP_ROOT    Content sub-tree to scan for BP_*.uasset. Default "Content"
#   PYTHON     Default first available of: python3, py, python
#   SKIP_SVN_CHECK=1  Bypass SVN cleanliness pre-flight (use only if you accept the risk)

set -euo pipefail

# Resolve project root in a form Windows-native UnrealEditor-Cmd.exe understands.
# MSYS / git-bash report $PWD as /<drive>/path; UE needs <Drive>:/path.
if command -v cygpath >/dev/null 2>&1; then
    PROJECT_ROOT="$(cygpath -m "$PWD")"
elif WIN_PWD="$(pwd -W 2>/dev/null)"; then
    PROJECT_ROOT="$WIN_PWD"
else
    PROJECT_ROOT="$PWD"
fi

UE_PATH="${UE_PATH:-C:/Program Files/Epic Games/UE_5.7}"
if [ -z "${UPROJECT:-}" ]; then
    UPROJECT="$(find "$PROJECT_ROOT" -maxdepth 2 -name '*.uproject' -type f 2>/dev/null | head -n 1)"
fi
BP_ROOT="${BP_ROOT:-Content}"

WRAPPER="$PWD/Plugins/UAssetJsonExporter/scripts/run_commandlet.sh"
EXPORT_ROOT="$PWD/Intermediate/UAssetExport"
WORK_ROOT="$PWD/Intermediate/BlueprintToStaticMeshReplacer"
BEFORE_DIR="$WORK_ROOT/Before"
MANIFEST="$WORK_ROOT/manifest.json"

if [ -z "$UPROJECT" ] || [ ! -f "$UPROJECT" ]; then
    echo "[snapshot] uproject not found (looked for *.uproject under $PROJECT_ROOT); set UPROJECT env var" >&2
    exit 2
fi
if [ ! -d "$BP_ROOT" ]; then
    echo "[snapshot] BP_ROOT not found: $BP_ROOT" >&2
    exit 2
fi
if [ ! -f "$WRAPPER" ]; then
    echo "[snapshot] commandlet wrapper not found: $WRAPPER" >&2
    exit 2
fi

resolve_python() {
    # Try in order, prefer `py` (Windows official launcher) before `python3`
    # because on Windows `python3` often resolves to a Microsoft Store stub
    # that prints "Python was not found" instead of running. We verify each
    # candidate by actually running it with -V.
    for p in py python python3; do
        if command -v "$p" >/dev/null 2>&1; then
            if "$p" -V >/dev/null 2>&1; then
                echo "$p"
                return 0
            fi
        fi
    done
    return 1
}
PYTHON="${PYTHON:-$(resolve_python || true)}"
if [ -z "$PYTHON" ]; then
    echo "[snapshot] python not found in PATH; set PYTHON env var" >&2
    exit 2
fi

# 1. SVN cleanliness pre-flight
if [ "${SKIP_SVN_CHECK:-0}" != "1" ]; then
    echo "[snapshot] checking svn status..."
    if ! command -v svn >/dev/null 2>&1; then
        echo "[snapshot] svn CLI not found; set SKIP_SVN_CHECK=1 to bypass" >&2
        exit 2
    fi
    DIRTY=$(svn status --depth=infinity Content Source Plugins 2>/dev/null \
        | grep -E '^[MAD?!]' || true)
    if [ -n "$DIRTY" ]; then
        echo "[snapshot] svn working copy has uncommitted changes:" >&2
        echo "$DIRTY" >&2
        echo "[snapshot] commit / revert / clean, or SKIP_SVN_CHECK=1 to override" >&2
        exit 3
    fi
    echo "[snapshot] svn working copy clean"
fi

mkdir -p "$WORK_ROOT" "$BEFORE_DIR"

# 2. Re-export every BP_*.uasset under BP_ROOT.
echo "[snapshot] discovering BPs under $BP_ROOT..."
BP_LIST=$(find "$BP_ROOT" -name 'BP_*.uasset' -type f \
    | sed 's|^Content|/Game|; s|\.uasset$||' \
    | sort | tr '\n' ',' | sed 's/,$//')
BP_COUNT=$(echo "$BP_LIST" | tr ',' '\n' | wc -l | tr -d ' ')
echo "[snapshot] found $BP_COUNT BPs"
if [ "$BP_COUNT" -eq 0 ]; then
    echo "[snapshot] no BPs discovered; aborting" >&2
    exit 1
fi

echo "[snapshot] running BlueprintEdGraphExport..."
bash "$WRAPPER" "$UE_PATH" "$UPROJECT" BlueprintEdGraphExport "$BP_LIST"

# 3. Classify
echo "[snapshot] classifying..."
"$PYTHON" Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/classify.py > "$WORK_ROOT/candidates.csv"
"$PYTHON" Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/classify.py --pretty > /dev/null  # echoes summary to stderr
CANDIDATE_COUNT=$(awk -F, 'NR>1 && $1=="CANDIDATE"' "$WORK_ROOT/candidates.csv" | wc -l | tr -d ' ')
SKIP_COUNT=$(awk -F, 'NR>1 && $1=="SKIP"' "$WORK_ROOT/candidates.csv" | wc -l | tr -d ' ')
EXCLUDE_COUNT=$(awk -F, 'NR>1 && $1=="EXCLUDE"' "$WORK_ROOT/candidates.csv" | wc -l | tr -d ' ')
echo "[snapshot] CANDIDATE=$CANDIDATE_COUNT SKIP=$SKIP_COUNT EXCLUDE=$EXCLUDE_COUNT"

if [ "$CANDIDATE_COUNT" -eq 0 ]; then
    echo "[snapshot] zero candidates; nothing to snapshot" >&2
    exit 0
fi

# 4. Discover level packages from candidates
echo "[snapshot] aggregating level package list..."
"$PYTHON" Plugins/MapUtils/Script/BlueprintToStaticMeshReplacer/discover_levels.py \
    --candidates "$WORK_ROOT/candidates.csv" \
    > "$WORK_ROOT/levels.txt"
LEVEL_COUNT=$(wc -l < "$WORK_ROOT/levels.txt" | tr -d ' ')
echo "[snapshot] $LEVEL_COUNT levels to snapshot"
if [ "$LEVEL_COUNT" -eq 0 ]; then
    echo "[snapshot] no levels referenced; aborting" >&2
    exit 1
fi

# 5. LevelExport on the discovered levels
LEVEL_ASSETS=$(paste -sd, "$WORK_ROOT/levels.txt")
echo "[snapshot] running LevelExport..."
bash "$WRAPPER" "$UE_PATH" "$UPROJECT" LevelExport "$LEVEL_ASSETS"

# 6. Copy outputs to Before/ (preserve relative path)
echo "[snapshot] copying LevelExport outputs to Before/..."
COPIED=0
MISSING=0
while IFS= read -r LVL; do
    SRC="$EXPORT_ROOT/${LVL#/}.json"
    DST="$BEFORE_DIR/${LVL#/}.json"
    mkdir -p "$(dirname "$DST")"
    if [ -f "$SRC" ]; then
        cp "$SRC" "$DST"
        COPIED=$((COPIED + 1))
    else
        echo "[snapshot] missing LevelExport output: $SRC" >&2
        MISSING=$((MISSING + 1))
    fi
done < "$WORK_ROOT/levels.txt"
echo "[snapshot] copied=$COPIED missing=$MISSING"

# 7. Manifest with SVN revision
SVN_REV="unknown"
if command -v svn >/dev/null 2>&1; then
    SVN_REV=$(svn info --show-item revision 2>/dev/null || echo "unknown")
fi
NOW=$(date -u +%Y-%m-%dT%H:%M:%SZ)
cat > "$MANIFEST" <<EOF
{
    "phase": "snapshot",
    "timestamp_utc": "$NOW",
    "svn_revision": "$SVN_REV",
    "bp_total": $BP_COUNT,
    "candidate_count": $CANDIDATE_COUNT,
    "skip_count": $SKIP_COUNT,
    "exclude_count": $EXCLUDE_COUNT,
    "level_count": $LEVEL_COUNT,
    "levels_copied": $COPIED,
    "levels_missing": $MISSING
}
EOF
echo "[snapshot] manifest written: $MANIFEST"

if [ "$MISSING" -gt 0 ]; then
    echo "[snapshot] WARNING: $MISSING level(s) missing from LevelExport output" >&2
    exit 1
fi
echo "[snapshot] OK"
