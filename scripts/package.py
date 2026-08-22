#!/usr/bin/env python3
"""AKEIR Engine release packager.

    python scripts/package.py [--preset msvc-release] [--version 0.1.1] [--ref HEAD]

Produces dist/AKEIR-<version>.zip containing
  - every git-tracked file at <ref>  (git archive; no build/, .cpm-cache/, Cache/)
  - bin/akeir.exe from build/<preset>/bin/   (prebuilt CLI; the zip is usable without a compiler; no .pdb)
  - bin/<ProjectName>.exe (the game executable, Tools/Player — double-click to play the sample)
  - .mcp.json with RELATIVE paths (bin/akeir.exe mcp --project Game) so Claude Code can be pointed at the unpacked folder
  - RELEASE.md with version, git ref, sha256 of akeir.exe

Requires: a git commit to archive (run after `git commit`), and the preset built (`scripts\\build.cmd msvc-release all`).
"""
import re, argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import zipfile
from datetime import datetime, timezone

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True, text=True, **kw).stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default="msvc-release")
    ap.add_argument("--version", default="0.1.1")
    ap.add_argument("--ref", default="HEAD")
    a = ap.parse_args()

    exe = os.path.join(ROOT, "build", a.preset, "bin", "akeir.exe")
    if not os.path.exists(exe):
        sys.exit(f"missing {exe}: build it first (scripts\\build.cmd {a.preset} all)")
    name = f"AKEIR-{a.version}"
    dist = os.path.join(ROOT, "dist")
    stage = os.path.join(dist, name)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage, exist_ok=True)

    # 1. git archive -> stage
    archive = subprocess.run(["git", "archive", "--format=zip", a.ref], cwd=ROOT, check=True, capture_output=True).stdout
    with zipfile.ZipFile(io.BytesIO(archive)) as z:
        z.extractall(stage)
    sha = run(["git", "rev-parse", "--short", a.ref])
    tags = run(["git", "tag", "--points-at", a.ref]).split()

    # 2. binary
    os.makedirs(os.path.join(stage, "bin"), exist_ok=True)
    shutil.copy2(exe, os.path.join(stage, "bin", "akeir.exe"))
    # the game executable (Tools/Player): bin/<ProjectName>.exe — double-click to play the sample
    with open(os.path.join(ROOT, "Game", "project.json"), encoding="utf-8") as f:
        game_exe = re.sub(r"[^A-Za-z0-9_.-]", "", json.load(f).get("name", "Game")) or "Game"
    game_path = os.path.join(ROOT, "build", a.preset, "bin", game_exe + ".exe")
    if os.path.exists(game_path):
        shutil.copy2(game_path, os.path.join(stage, "bin", game_exe + ".exe"))
    # akeir.pdb(64MB 심볼)는 넣지 않는다 — 같은 태그에서 재빌드하면 재생성된다 (QUICKSTART §5)
    with open(exe, "rb") as f:
        exe_sha = hashlib.sha256(f.read()).hexdigest()

    # 3. relative .mcp.json (repo copy has absolute dev paths)
    with open(os.path.join(stage, ".mcp.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"mcpServers": {"akeir": {"command": "bin\\akeir.exe", "args": ["mcp", "--project", "Game"]}}}, f, indent=2)   # 백슬래시: cmd.exe 와 cwd 기준 spawn 양쪽에서 풀린다
        f.write("\n")

    # 4. release notes
    version_json = json.loads(subprocess.run([exe, "version", "--json"], cwd=ROOT, check=True, capture_output=True, text=True).stdout)["result"]
    notes = f"""# {name} — release

- engine: {version_json.get('engine')} {version_json.get('engineVersion')} (release {version_json.get('release')})
- git: {sha} {' '.join(tags)}
- built: {datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')} preset {a.preset}, compiler {version_json.get('compiler')}, fpFlagsHash {version_json.get('fpFlagsHash')}
- bin/akeir.exe sha256 {exe_sha}
- reference: `cd Game && ..\\bin\\akeir.exe run --headless --ticks 600 --json` → result.finalHash 0x404c60567ccb9e85

Start with QUICKSTART.md.
"""
    with open(os.path.join(stage, "RELEASE.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write(notes)

    # 5. zip
    out = os.path.join(dist, f"{name}.zip")
    if os.path.exists(out):
        os.remove(out)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for dirpath, _, files in os.walk(stage):
            for fn in files:
                full = os.path.join(dirpath, fn)
                z.write(full, os.path.relpath(full, dist))
    size = os.path.getsize(out)
    print(f"{out}  ({size/1e6:.1f} MB)  git {sha} {' '.join(tags)}  akeir.exe sha256 {exe_sha[:16]}…")


if __name__ == "__main__":
    main()
