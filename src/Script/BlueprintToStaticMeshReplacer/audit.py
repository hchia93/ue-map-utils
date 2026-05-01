"""Audit BlueprintToStaticMeshReplacer replacement by comparing Before / After LevelExport JSONs.

Independent verification of the modify phase: does NOT read the commandlet's
manifest. Reconstructs the expected outcome purely from Before vs After level
state.

Inputs:
    --before   Directory with LevelExport JSONs taken before the modify run
               (default: Intermediate/BlueprintToStaticMeshReplacer/Before)
    --after    Directory with LevelExport JSONs taken after the modify run
               (default: Intermediate/BlueprintToStaticMeshReplacer/After)
    --candidates  candidates.csv from the snapshot phase (used to filter
                  which BP classes were intended targets)
    --bp-export-root  Directory holding BlueprintEdGraphExport JSONs for the
                  candidate BPs. Used to resolve Before-side OverrideMaterials
                  from the BP CDO when level instance has no delta override.
                  (default: Intermediate/UAssetExport)
    --diff-dir  Where to write per-level diff text reports
                (default: Intermediate/BlueprintToStaticMeshReplacer/Diff)
    --epsilon-cm  World-position match tolerance in cm. Default 0.01

Per-level checks:
    1. Total actor count is unchanged
    2. Every CANDIDATE BP class has 0 instances after
    3. For each Before candidate instance, an SMA exists at its SMC's expected
       world location with the same StaticMesh
    4. Matched pair has the same Mobility, CollisionProfile, Folder, and
       resolved OverrideMaterials (BP CDO + instance delta vs SMA instance delta)
    5. Other (non-candidate) actors are unchanged in count

Outputs:
    - <diff-dir>/<level_path>.txt  per-level human-readable diff
    - <diff-dir>/audit_summary.txt overall PASS/FAIL summary
    - Exit 0 iff every level GREEN, else 1

Known limitations:
    - SMC at non-identity RelativeTransform with parent rotation != identity
      requires full quaternion composition. Such cases are reported as
      "complex" and excluded from exact-location pairing; counts still
      checked. Real BPs we've inspected have SMC at identity, so this is
      typically empty.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(newline="\n")


SMC_CLASS = "/Script/Engine.StaticMeshComponent"
SMA_CLASS = "/Script/Engine.StaticMeshActor"


@dataclass
class FieldDiff:
    field: str
    before: str
    after: str


@dataclass
class LevelAudit:
    level: str
    before_actor_count: int = 0
    after_actor_count: int = 0
    before_candidate_count: int = 0
    after_candidate_count: int = 0
    before_other_count: int = 0
    after_other_count: int = 0
    matched_pairs: int = 0
    unmatched_before: list[str] = field(default_factory=list)
    unmatched_after: list[str] = field(default_factory=list)
    known_skipped: list[str] = field(default_factory=list)
    complex_pairs: int = 0
    field_mismatches: list[tuple[str, list[FieldDiff]]] = field(default_factory=list)
    fail_reasons: list[str] = field(default_factory=list)

    @property
    def passed(self) -> bool:
        return not self.fail_reasons

    def add_fail(self, reason: str) -> None:
        self.fail_reasons.append(reason)


# ----- transform parsing -----

def _parse_loc(s: str) -> tuple[float, float, float]:
    s = s.strip("()")
    parts = [float(p) for p in s.split(",")]
    return (parts[0], parts[1], parts[2])


def _parse_rot(s: str) -> tuple[float, float, float]:
    """Returns (Pitch, Yaw, Roll)."""
    s = s.strip("()")
    d = {}
    for p in s.split(","):
        k, v = p.split("=")
        d[k.strip()] = float(v)
    return (d.get("P", 0.0), d.get("Y", 0.0), d.get("R", 0.0))


def _is_identity_loc(loc: tuple[float, float, float]) -> bool:
    return all(abs(c) < 1e-4 for c in loc)


def _is_identity_rot(rot: tuple[float, float, float]) -> bool:
    return all(abs(c) < 1e-4 for c in rot)


def _rotator_to_matrix(pitch_deg: float, yaw_deg: float, roll_deg: float) -> list[list[float]]:
    """UE FRotator (Pitch, Yaw, Roll degrees) -> 3x3 row-major rotation matrix.

    Mirrors Engine/Source/Runtime/Core/Public/Math/RotationMatrix.h such that
    M * v applies UE's standard Roll * Pitch * Yaw composition for an FRotator.
    """
    p = math.radians(pitch_deg)
    y = math.radians(yaw_deg)
    r = math.radians(roll_deg)
    cp, sp = math.cos(p), math.sin(p)
    cy, sy = math.cos(y), math.sin(y)
    cr, sr = math.cos(r), math.sin(r)
    return [
        [cp * cy,                        cp * sy,                        sp],
        [sr * sp * cy - cr * sy,         sr * sp * sy + cr * cy,         -sr * cp],
        [-(cr * sp * cy + sr * sy),      cy * sr - cr * sp * sy,         cr * cp],
    ]


def _mat_mul_vec(M: list[list[float]], v: tuple[float, float, float]) -> tuple[float, float, float]:
    """Apply 3x3 matrix to vector with v interpreted as a column.

    UE convention: world_vector = local_vector * Matrix (row-vector convention).
    We mirror this by treating M as (row-major) and pre-multiplying as a column,
    which is equivalent for rotation matrices stored this way.
    """
    return (
        v[0] * M[0][0] + v[1] * M[1][0] + v[2] * M[2][0],
        v[0] * M[0][1] + v[1] * M[1][1] + v[2] * M[2][1],
        v[0] * M[0][2] + v[1] * M[1][2] + v[2] * M[2][2],
    )


# ----- helpers on JSON structure -----

def _non_editor_smcs(actor: dict) -> list[dict]:
    return [
        c for c in actor.get("Components", [])
        if c.get("Class") == SMC_CLASS and not c.get("IsEditorOnly", False)
    ]


def _actor_world_loc(actor: dict) -> tuple[float, float, float]:
    return _parse_loc(actor["Transform"]["Loc"])


def _actor_world_rot(actor: dict) -> tuple[float, float, float]:
    return _parse_rot(actor["Transform"]["Rot"])


def _smc_effective_world_loc(actor: dict, smc: dict) -> tuple[float, float, float]:
    """Compose actor world transform with SMC RelativeTransform.

    SRT order: rel_loc is scaled by actor scale, rotated by actor rotation,
    then translated by actor location. Matches UE's SceneComponent attachment.
    """
    actor_loc = _actor_world_loc(actor)
    rel = smc.get("RelativeTransform")
    if rel is None:
        return actor_loc
    rel_loc = _parse_loc(rel.get("Loc", "(0,0,0)"))
    if _is_identity_loc(rel_loc):
        return actor_loc
    actor_rot = _actor_world_rot(actor)
    actor_scale = _parse_loc(actor["Transform"]["Scale"])
    scaled = (rel_loc[0] * actor_scale[0], rel_loc[1] * actor_scale[1], rel_loc[2] * actor_scale[2])
    if _is_identity_rot(actor_rot):
        return (actor_loc[0] + scaled[0], actor_loc[1] + scaled[1], actor_loc[2] + scaled[2])
    M = _rotator_to_matrix(*actor_rot)
    rotated = _mat_mul_vec(M, scaled)
    return (actor_loc[0] + rotated[0], actor_loc[1] + rotated[1], actor_loc[2] + rotated[2])


def _smc_is_root(actor: dict, smc: dict) -> bool:
    """For root SMCs, LevelExport's RelativeTransform == world transform (no parent
    to compose against). Detect via Class == StaticMeshActor (single SMC = root) OR
    by checking the SMC.RelTransform fields equal Actor.Transform field-wise.
    """
    if actor.get("Class") == SMA_CLASS:
        return True
    rel = smc.get("RelativeTransform")
    if rel is None:
        return False
    return (rel.get("Loc")   == actor.get("Transform", {}).get("Loc")
        and rel.get("Rot")   == actor.get("Transform", {}).get("Rot")
        and rel.get("Scale") == actor.get("Transform", {}).get("Scale"))


def _smc_effective_world_scale(actor: dict, smc: dict) -> tuple[float, float, float]:
    """Composed world scale of the mesh = Actor.Scale * SMC.RelScale (per-axis).
    For root SMCs the RelativeTransform already IS the world transform; don't compose.
    """
    a = _parse_loc(actor["Transform"]["Scale"])
    rel = smc.get("RelativeTransform")
    if rel is None or _smc_is_root(actor, smc):
        return a
    r = _parse_loc(rel.get("Scale", "(1,1,1)"))
    return (a[0] * r[0], a[1] * r[1], a[2] * r[2])


def _smc_effective_world_rot(actor: dict, smc: dict) -> tuple[float, float, float]:
    """Composed world rotation as (Pitch, Yaw, Roll). Actor rotation summed with
    SMC relative rotation; for the axis-aligned cases we see in env props this is
    accurate. For root SMCs the RelativeTransform already IS the world transform;
    don't compose. Yaw normalised to [-180, 180].
    """
    a = _actor_world_rot(actor)
    rel = smc.get("RelativeTransform")
    if rel is None or _smc_is_root(actor, smc):
        return tuple(((v + 180.0) % 360.0) - 180.0 for v in a)
    r = _parse_rot(rel.get("Rot", "(P=0,Y=0,R=0)"))
    raw = (a[0] + r[0], a[1] + r[1], a[2] + r[2])
    return tuple(((v + 180.0) % 360.0) - 180.0 for v in raw)


def _strip_class_path(cls: str) -> str:
    """'/Game/.../BP_Foo.BP_Foo_C' -> '/Game/.../BP_Foo'.

    candidates.csv stores AssetPath without the asset object name suffix.
    """
    if "." in cls:
        return cls.rsplit(".", 1)[0]
    return cls


def _smc_delta_field(smc: dict, name: str) -> Optional[str]:
    """Return DeltaProperties[name] if present (LevelExport surfaces it as a dict)."""
    dp = smc.get("DeltaProperties")
    if isinstance(dp, dict):
        v = dp.get(name)
        return v if isinstance(v, str) else None
    if isinstance(dp, list):
        for entry in dp:
            if entry.get("Name") == name:
                v = entry.get("Value")
                return v if isinstance(v, str) else None
    return None


def _load_bp_cdo_smc_overrides(asset_path: str, bp_export_root: Path) -> dict[str, str]:
    """Read the BP's first non-editor StaticMeshComponent CDO PropertyOverrides
    (Name -> Value). Empty dict if export missing or no SMC.
    """
    rel = asset_path.lstrip("/") + ".json"
    fp = bp_export_root / rel
    if not fp.exists():
        return {}
    try:
        with fp.open(encoding="utf-8") as f:
            d = json.load(f)
    except (OSError, json.JSONDecodeError):
        return {}
    for c in d.get("Components", []):
        if c.get("Class") == "StaticMeshComponent" and not c.get("IsEditorOnly", False):
            return {ov["Name"]: ov["Value"] for ov in c.get("PropertyOverrides", []) if "Name" in ov and "Value" in ov}
    return {}


# ----- audit core -----

def _bucket_actor_loc(loc: tuple[float, float, float], eps_cm: float) -> tuple[int, int, int]:
    """Quantize world location for fast equality lookups within tolerance."""
    return tuple(round(c / eps_cm) for c in loc)


def audit_level(level_path: str, before: dict, after: dict,
                candidate_asset_paths: set[str], eps_cm: float,
                bp_cdo_overrides: dict[str, dict[str, str]] | None = None,
                known_skip_labels: set[str] | None = None) -> LevelAudit:
    a = LevelAudit(level=level_path)
    a.before_actor_count = len(before.get("Actors", []))
    a.after_actor_count  = len(after.get("Actors",  []))

    if a.before_actor_count != a.after_actor_count:
        a.add_fail(
            f"actor count delta != 0 (before={a.before_actor_count}, after={a.after_actor_count})"
        )

    # Partition Before
    before_candidates: list[dict] = []
    before_smas: list[dict] = []
    before_others: list[dict] = []
    for actor in before.get("Actors", []):
        cls_path = _strip_class_path(actor.get("Class", ""))
        if cls_path in candidate_asset_paths:
            before_candidates.append(actor)
        elif actor.get("Class") == SMA_CLASS:
            before_smas.append(actor)
        else:
            before_others.append(actor)

    # Partition After
    after_candidates: list[dict] = []
    after_smas: list[dict] = []
    after_others: list[dict] = []
    for actor in after.get("Actors", []):
        cls_path = _strip_class_path(actor.get("Class", ""))
        if cls_path in candidate_asset_paths:
            after_candidates.append(actor)
        elif actor.get("Class") == SMA_CLASS:
            after_smas.append(actor)
        else:
            after_others.append(actor)

    a.before_candidate_count = len(before_candidates)
    a.after_candidate_count  = len(after_candidates)
    a.before_other_count = len(before_others)
    a.after_other_count  = len(after_others)

    # Pre-count Before candidates whose label is known-skipped; those remain as BP in After.
    skip_labels_set = known_skip_labels or set()
    expected_remaining_bp = sum(
        1 for ac in before_candidates if ac.get("Label", "") in skip_labels_set
    )
    if a.after_candidate_count != expected_remaining_bp:
        a.add_fail(
            f"{a.after_candidate_count} candidate BP instance(s) still in After "
            f"(expected {expected_remaining_bp} known-skipped)"
        )

    if a.before_other_count != a.after_other_count:
        a.add_fail(
            f"non-candidate non-SMA actor count changed "
            f"({a.before_other_count} -> {a.after_other_count})"
        )

    expected_new_smas = a.before_candidate_count - expected_remaining_bp
    actual_sma_delta = len(after_smas) - len(before_smas)
    if actual_sma_delta != expected_new_smas:
        a.add_fail(
            f"SMA count delta {actual_sma_delta} != expected new {expected_new_smas}"
        )

    # Build After SMA index by location bucket -> list of actors at that loc
    after_sma_by_loc: dict[tuple[int, int, int], list[dict]] = {}
    for sma in after_smas:
        key = _bucket_actor_loc(_actor_world_loc(sma), eps_cm)
        after_sma_by_loc.setdefault(key, []).append(sma)

    # To avoid double-matching, track consumed After SMAs
    consumed: set[int] = set()

    # Pre-build a Before SMA loc set so we can isolate "new" SMAs (those not in Before)
    before_sma_locs: set[tuple[int, int, int]] = set()
    for sma in before_smas:
        before_sma_locs.add(_bucket_actor_loc(_actor_world_loc(sma), eps_cm))

    skip_labels = known_skip_labels or set()
    for bp_actor in before_candidates:
        label = bp_actor.get("Label", "")
        if label in skip_labels:
            # User pre-acknowledged this instance is structurally non-flattenable; will be hand-handled.
            a.known_skipped.append(f"{label} (level-instance shape != BP CDO)")
            continue
        smcs = _non_editor_smcs(bp_actor)
        if len(smcs) != 1:
            a.unmatched_before.append(
                f"{bp_actor.get('Label','?')} (label) -- expected 1 SMC, found {len(smcs)}"
            )
            continue
        smc = smcs[0]
        eff_loc = _smc_effective_world_loc(bp_actor, smc)
        if smc.get("RelativeTransform") is not None:
            a.complex_pairs += 1
        # Search the bucket + 26 neighbor buckets (3x3x3) so two actors that fall
        # within epsilon but straddle a round() bucket boundary still pair up.
        key = _bucket_actor_loc(eff_loc, eps_cm)
        cands: list[dict] = []
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    cands.extend(after_sma_by_loc.get(
                        (key[0] + dx, key[1] + dy, key[2] + dz), []
                    ))
        # Prefer an unconsumed SMA whose mesh matches Before BP's SMC mesh
        bp_mesh = (smc.get("StaticMesh") or "").rstrip(".'\"")
        match_idx = None
        for idx, cand in enumerate(cands):
            if id(cand) in consumed:
                continue
            cand_smcs = _non_editor_smcs(cand)
            if len(cand_smcs) != 1:
                continue
            cand_mesh = (cand_smcs[0].get("StaticMesh") or "").rstrip(".'\"")
            if cand_mesh and bp_mesh and cand_mesh.split(".")[0] == bp_mesh.split(".")[0]:
                match_idx = idx
                break
        if match_idx is None:
            a.unmatched_before.append(
                f"{bp_actor.get('Label','?')} @ {eff_loc} mesh={bp_mesh}"
            )
            continue
        matched_sma = cands[match_idx]
        consumed.add(id(matched_sma))
        a.matched_pairs += 1

        # Field-by-field check (passes BP CDO overrides for resolved materials check)
        bp_cls = _strip_class_path(bp_actor.get("Class", ""))
        cdo_overrides = (bp_cdo_overrides or {}).get(bp_cls, {})
        diffs = _diff_pair(bp_actor, smc, matched_sma, cdo_overrides)
        if diffs:
            a.field_mismatches.append((bp_actor.get("Label", "?"), diffs))

    # Anything in After SMAs not previously in Before SMAs and not consumed
    for sma in after_smas:
        if id(sma) in consumed:
            continue
        key = _bucket_actor_loc(_actor_world_loc(sma), eps_cm)
        if key not in before_sma_locs:
            a.unmatched_after.append(
                f"{sma.get('Label','?')} @ {_actor_world_loc(sma)}"
            )

    if a.unmatched_before:
        a.add_fail(f"{len(a.unmatched_before)} Before candidate(s) without After match")
    if a.unmatched_after:
        a.add_fail(f"{len(a.unmatched_after)} After SMA(s) not pairing to a Before candidate")
    if a.field_mismatches:
        a.add_fail(f"{len(a.field_mismatches)} field-level mismatch(es)")

    return a


def _diff_pair(bp_actor: dict, bp_smc: dict, after_sma: dict,
               bp_cdo_overrides: dict[str, str]) -> list[FieldDiff]:
    """Compare resolved fields between Before BP+SMC and After SMA."""
    diffs: list[FieldDiff] = []
    after_smcs = _non_editor_smcs(after_sma)
    if len(after_smcs) != 1:
        diffs.append(FieldDiff("non_editor_smc_count", "1", str(len(after_smcs))))
        return diffs
    after_smc = after_smcs[0]

    pairs = [
        ("StaticMesh",       bp_smc.get("StaticMesh"),       after_smc.get("StaticMesh")),
        ("Mobility",         bp_smc.get("Mobility"),         after_smc.get("Mobility")),
        ("CollisionProfile", bp_smc.get("CollisionProfile"), after_smc.get("CollisionProfile")),
        ("CollisionEnabled", bp_smc.get("CollisionEnabled"), after_smc.get("CollisionEnabled")),
        ("Folder",           bp_actor.get("Folder"),         after_sma.get("Folder")),
    ]
    for name, b, af in pairs:
        if (b or "") != (af or ""):
            diffs.append(FieldDiff(name, str(b), str(af)))

    # Composed world scale of the mesh: Actor.Scale * SMC.RelScale
    bs = _smc_effective_world_scale(bp_actor, bp_smc)
    as_ = _smc_effective_world_scale(after_sma, after_smc)
    if any(abs(bs[i] - as_[i]) > 1e-3 for i in range(3)):
        diffs.append(FieldDiff(
            "EffectiveWorldScale",
            f"({bs[0]:.4f},{bs[1]:.4f},{bs[2]:.4f})",
            f"({as_[0]:.4f},{as_[1]:.4f},{as_[2]:.4f})",
        ))

    # Composed world rotation of the mesh: Actor.Rot + SMC.RelRot (axis-aligned approx).
    br = _smc_effective_world_rot(bp_actor, bp_smc)
    ar = _smc_effective_world_rot(after_sma, after_smc)
    if any(abs(((br[i] - ar[i] + 180.0) % 360.0) - 180.0) > 1e-2 for i in range(3)):
        diffs.append(FieldDiff(
            "EffectiveWorldRot",
            f"(P={br[0]:.3f},Y={br[1]:.3f},R={br[2]:.3f})",
            f"(P={ar[0]:.3f},Y={ar[1]:.3f},R={ar[2]:.3f})",
        ))

    # OverrideMaterials resolved comparison.
    # Before resolved = level instance delta if present, else BP CDO override, else "" (mesh defaults).
    # After resolved = level instance delta if present, else "" (SMA CDO has no overrides).
    before_mat = _smc_delta_field(bp_smc, "OverrideMaterials") or bp_cdo_overrides.get("OverrideMaterials", "")
    after_mat  = _smc_delta_field(after_smc, "OverrideMaterials") or ""
    if before_mat != after_mat:
        diffs.append(FieldDiff("OverrideMaterials", before_mat, after_mat))

    # Vertex paint: VertexPaintLODs array surfaced by LevelExport via non-UProperty path.
    # Compare the per-LOD (NumVertices, ColorCRC32) tuples; order matters.
    before_paint = _paint_signature(bp_smc.get("VertexPaintLODs"))
    after_paint  = _paint_signature(after_smc.get("VertexPaintLODs"))
    if before_paint != after_paint:
        diffs.append(FieldDiff("VertexPaintLODs", before_paint, after_paint))

    # Custom Primitive Data: per-instance material parameter overrides.
    before_cpd = _cpd_signature(bp_smc.get("CustomPrimitiveData"))
    after_cpd  = _cpd_signature(after_smc.get("CustomPrimitiveData"))
    if before_cpd != after_cpd:
        diffs.append(FieldDiff("CustomPrimitiveData", before_cpd, after_cpd))

    return diffs


def _paint_signature(lods: object) -> str:
    if not isinstance(lods, list) or not lods:
        return ""
    return ";".join(
        f"LOD{e.get('LOD','?')}:{e.get('NumVertices','?')}:{e.get('ColorCRC32','?')}"
        for e in lods if isinstance(e, dict)
    )


def _cpd_signature(cpd: object) -> str:
    if not isinstance(cpd, list) or not cpd:
        return ""
    return ",".join(f"{float(v):.6f}" for v in cpd)


# ----- io -----

def _load_jsons(root: Path) -> dict[str, dict]:
    """Map {/Game/.../LevelName: parsed json} indexed by AssetPath."""
    result: dict[str, dict] = {}
    for jf in root.rglob("*.json"):
        d = json.load(jf.open(encoding="utf-8"))
        if d.get("ExportType") != "Level":
            continue
        result[d["AssetPath"]] = d
    return result


def _read_candidate_asset_paths(csv_path: Path) -> set[str]:
    paths: set[str] = set()
    with csv_path.open(encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row["Status"] == "CANDIDATE":
                paths.add(row["AssetPath"])
    return paths


def _write_level_diff(diff_dir: Path, audit: LevelAudit) -> None:
    diff_dir.mkdir(parents=True, exist_ok=True)
    rel_name = audit.level.lstrip("/").replace("/", "_") + ".txt"
    out = diff_dir / rel_name
    lines: list[str] = []
    status = "GREEN" if audit.passed else "RED"
    lines.append(f"[{status}] {audit.level}")
    lines.append(f"  before_actor_count={audit.before_actor_count} after_actor_count={audit.after_actor_count}")
    lines.append(f"  before_candidates={audit.before_candidate_count} after_candidates={audit.after_candidate_count}")
    lines.append(f"  before_other={audit.before_other_count} after_other={audit.after_other_count}")
    lines.append(f"  matched_pairs={audit.matched_pairs} complex_pairs={audit.complex_pairs}")
    if audit.fail_reasons:
        lines.append("  fail_reasons:")
        for r in audit.fail_reasons:
            lines.append(f"    - {r}")
    if audit.known_skipped:
        lines.append(f"  known_skipped ({len(audit.known_skipped)}, accepted by --known-skip):")
        for u in audit.known_skipped[:25]:
            lines.append(f"    - {u}")
        if len(audit.known_skipped) > 25:
            lines.append(f"    ... +{len(audit.known_skipped) - 25} more")
    if audit.unmatched_before:
        lines.append(f"  unmatched_before ({len(audit.unmatched_before)}):")
        for u in audit.unmatched_before[:25]:
            lines.append(f"    - {u}")
        if len(audit.unmatched_before) > 25:
            lines.append(f"    ... +{len(audit.unmatched_before) - 25} more")
    if audit.unmatched_after:
        lines.append(f"  unmatched_after ({len(audit.unmatched_after)}):")
        for u in audit.unmatched_after[:25]:
            lines.append(f"    - {u}")
        if len(audit.unmatched_after) > 25:
            lines.append(f"    ... +{len(audit.unmatched_after) - 25} more")
    if audit.field_mismatches:
        lines.append(f"  field_mismatches ({len(audit.field_mismatches)}):")
        for label, diffs in audit.field_mismatches[:25]:
            lines.append(f"    - {label}")
            for d in diffs:
                lines.append(f"        {d.field}: '{d.before}' -> '{d.after}'")
        if len(audit.field_mismatches) > 25:
            lines.append(f"    ... +{len(audit.field_mismatches) - 25} more")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument(
        "--before",
        default=Path.cwd() / "Intermediate" / "BlueprintToStaticMeshReplacer" / "Before",
        type=Path,
    )
    parser.add_argument(
        "--after",
        default=Path.cwd() / "Intermediate" / "BlueprintToStaticMeshReplacer" / "After",
        type=Path,
    )
    parser.add_argument(
        "--candidates",
        default=Path.cwd() / "Intermediate" / "BlueprintToStaticMeshReplacer" / "candidates.csv",
        type=Path,
    )
    parser.add_argument(
        "--bp-export-root",
        default=Path.cwd() / "Intermediate" / "UAssetExport",
        type=Path,
    )
    parser.add_argument(
        "--diff-dir",
        default=Path.cwd() / "Intermediate" / "BlueprintToStaticMeshReplacer" / "Diff",
        type=Path,
    )
    parser.add_argument("--epsilon-cm", default=0.01, type=float)
    parser.add_argument(
        "--known-skip",
        default="",
        help="Comma-separated actor labels that are intentionally NOT flattened "
             "(commandlet's per-instance soft-skip). Audit accepts these as-is "
             "instead of reporting RED.",
    )
    args = parser.parse_args()
    known_skip_labels = {s.strip() for s in args.known_skip.split(",") if s.strip()}

    if not args.before.exists():
        print(f"[audit] before dir not found: {args.before}", file=sys.stderr)
        return 1
    if not args.after.exists():
        print(f"[audit] after dir not found: {args.after}", file=sys.stderr)
        return 1
    if not args.candidates.exists():
        print(f"[audit] candidates.csv not found: {args.candidates}", file=sys.stderr)
        return 1

    candidate_paths = _read_candidate_asset_paths(args.candidates)
    print(f"[audit] {len(candidate_paths)} candidate BP class(es)", file=sys.stderr)

    bp_cdo_overrides: dict[str, dict[str, str]] = {}
    if args.bp_export_root.exists():
        for cp in candidate_paths:
            bp_cdo_overrides[cp] = _load_bp_cdo_smc_overrides(cp, args.bp_export_root)
        with_overrides = sum(1 for v in bp_cdo_overrides.values() if v)
        print(f"[audit] loaded BP CDO overrides for {with_overrides}/{len(candidate_paths)} candidates", file=sys.stderr)
    else:
        print(f"[audit] WARN: bp-export-root not found: {args.bp_export_root}", file=sys.stderr)

    before_jsons = _load_jsons(args.before)
    after_jsons  = _load_jsons(args.after)
    print(f"[audit] before levels={len(before_jsons)} after levels={len(after_jsons)}", file=sys.stderr)

    common = sorted(set(before_jsons) & set(after_jsons))
    only_before = sorted(set(before_jsons) - set(after_jsons))
    only_after  = sorted(set(after_jsons) - set(before_jsons))

    if only_before:
        print(f"[audit] WARN: {len(only_before)} level(s) in Before but not After:", file=sys.stderr)
        for p in only_before:
            print(f"  {p}", file=sys.stderr)
    if only_after:
        print(f"[audit] WARN: {len(only_after)} level(s) in After but not Before:", file=sys.stderr)
        for p in only_after:
            print(f"  {p}", file=sys.stderr)

    args.diff_dir.mkdir(parents=True, exist_ok=True)
    audits: list[LevelAudit] = []
    for level_path in common:
        a = audit_level(
            level_path,
            before_jsons[level_path],
            after_jsons[level_path],
            candidate_paths,
            args.epsilon_cm,
            bp_cdo_overrides,
            known_skip_labels,
        )
        audits.append(a)
        _write_level_diff(args.diff_dir, a)

    summary = args.diff_dir / "audit_summary.txt"
    green = [a for a in audits if a.passed]
    red   = [a for a in audits if not a.passed]
    summary_lines = [
        f"Audit summary @ epsilon={args.epsilon_cm} cm",
        f"  levels: {len(audits)} (GREEN={len(green)}, RED={len(red)})",
        f"  total matched_pairs: {sum(a.matched_pairs for a in audits)}",
        f"  total complex_pairs: {sum(a.complex_pairs for a in audits)}",
        f"  total before_candidates: {sum(a.before_candidate_count for a in audits)}",
        f"  total after_candidates:  {sum(a.after_candidate_count for a in audits)}",
        "",
    ]
    if red:
        summary_lines.append("RED levels:")
        for a in red:
            summary_lines.append(f"  - {a.level}")
            for r in a.fail_reasons:
                summary_lines.append(f"      {r}")
    summary.write_text("\n".join(summary_lines) + "\n", encoding="utf-8")
    print("\n".join(summary_lines))

    return 0 if not red else 1


if __name__ == "__main__":
    sys.exit(main())
