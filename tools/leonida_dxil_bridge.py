#!/usr/bin/env python3
"""Optional bridge: convert Leonida-produced SPIR-V to DXIL for XValkyrie.

Nothing is fetched or run from the game. This tool reads the *already generated*
Leonida shader corpus, runs local spirv-cross and dxc, and writes a cache which
XValkyrie's XV_LEONIDA_DXIL_DIR can use for exact DXBC-container matches.

Shader validity in a driver is not guaranteed by successful conversion.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

STAGES = {"vs": "vs_6_6", "ps": "ps_6_6", "cs": "cs_6_6", "gs": "gs_6_6"}
OFFSET = 14695981039346656037
PRIME = 1099511628211


def fingerprint(data: bytes) -> str:
    value = OFFSET
    for b in data:
        value = ((value ^ b) * PRIME) & 0xffffffffffffffff
    return f"{value:016x}"


def keys(row: dict) -> set[str]:
    names = set()
    for field in ("name", "file", "filename", "path", "spv", "stem", "source", "shader"):
        v = row.get(field)
        if isinstance(v, str) and v:
            names.add(Path(v).stem.casefold())
            names.add(Path(v).name.casefold())
    return names


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--work", type=Path, required=True,
                   help="Leon's existing WORK directory (contains shaders/dxbc, shaders/spirv)")
    p.add_argument("--output", type=Path, required=True, help="output DXIL shader-cache directory")
    p.add_argument("--spirv-cross", default="spirv-cross")
    p.add_argument("--dxc", default="dxc")
    p.add_argument("--max", type=int, default=0, help="optional limit for a small trial")
    args = p.parse_args()
    dxbc = args.work / "shaders" / "dxbc"
    spirv = args.work / "shaders" / "spirv"
    source_index = dxbc / "index.json"
    if not source_index.is_file() or not spirv.is_dir():
        p.error("missing Leonida shaders/dxbc/index.json or shaders/spirv; run Leonida shader stage first")
    for executable in (args.spirv_cross, args.dxc):
        if not shutil.which(executable) and not Path(executable).is_file():
            p.error(f"missing {executable}; install spirv-cross and dxc on your PATH")
    with source_index.open(encoding="utf-8-sig") as f:
        entries = json.load(f)
    if not isinstance(entries, list):
        p.error("unexpected Leonida DXBC index format")
    # Index both basename and stem, so sanitized Leonida filenames work.
    by_key: dict[str, Path] = {}
    for path in spirv.rglob("*.spv"):
        by_key.setdefault(path.stem.casefold(), path)
        by_key.setdefault(path.name.casefold(), path)
    translated_index = spirv / "index.json"
    if translated_index.is_file():
        with translated_index.open(encoding="utf-8-sig") as f:
            translated = json.load(f)
        if isinstance(translated, list):
            for item in translated:
                if not isinstance(item, dict):
                    continue
                location = next((item[k] for k in ("spv", "file", "path", "output")
                                 if isinstance(item.get(k), str)), None)
                if not location:
                    continue
                spv_path = spirv / location
                if not spv_path.is_file():
                    spv_path = by_key.get(Path(location).stem.casefold())
                if spv_path and spv_path.is_file():
                    for key in keys(item):
                        by_key.setdefault(key, spv_path)
    args.output.mkdir(parents=True, exist_ok=True)
    counts = {"matched": 0, "missing_spv": 0, "converted": 0, "failed": 0, "already_cached": 0,
              "skipped_stage": 0}
    problems = []
    processed = 0
    for row in entries:
        if not isinstance(row, dict):
            continue
        stage = str(row.get("stage") or "").lower()
        if stage not in STAGES:
            counts["skipped_stage"] += 1
            continue
        raw = row.get("file")
        if not isinstance(raw, str) or Path(raw).name != raw:
            continue
        source = dxbc / raw
        if not source.is_file():
            continue
        processed += 1
        if args.max and processed > args.max:
            break
        spv = next((by_key[k] for k in sorted(keys(row)) if k in by_key), None)
        if spv is None:
            counts["missing_spv"] += 1
            continue
        counts["matched"] += 1
        dest = args.output / f"{stage}-{fingerprint(source.read_bytes())}.dxil"
        if dest.is_file() and dest.stat().st_size > 32:
            counts["already_cached"] += 1
            continue
        with tempfile.TemporaryDirectory(prefix="xv-leonida-") as tmp:
            hlsl = Path(tmp) / "shader.hlsl"
            dxil = Path(tmp) / "shader.dxil"
            try:
                subprocess.run([args.spirv_cross, str(spv), "--hlsl", "--shader-model", "66",
                                "--output", str(hlsl)], check=True, capture_output=True,
                               text=True, timeout=90)
                subprocess.run([args.dxc, "-T", STAGES[stage], "-E", "main", "-Fo",
                                str(dxil), str(hlsl)], check=True, capture_output=True,
                               text=True, timeout=120)
                blob = dxil.read_bytes()
                if len(blob) < 32 or blob[:4] != b"DXBC":
                    raise ValueError("DXC result has no DXIL container header")
                os.replace(dxil, dest)
                counts["converted"] += 1
            except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired,
                    ValueError) as exc:
                counts["failed"] += 1
                if len(problems) < 30:
                    detail = getattr(exc, "stderr", "") or str(exc)
                    problems.append({"shader": raw, "error": detail[-800:]})
    summary = {"counts": counts, "limits": {
        "shader_bindings": "not verified against the Xbox root signature",
        "game_boot": "not proven; XValkyrie's native translation remains fallback",
        "manifest_origin": str(source_index)},
        "sample_failures": problems}
    (args.output / "bridge-report.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(counts, indent=2))
    print("Cache:", args.output)
    return 0 if counts["converted"] + counts["already_cached"] > 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
