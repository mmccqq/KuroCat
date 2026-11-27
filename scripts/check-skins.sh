#!/usr/bin/env bash
# Native renderer regression and action sheets; never launches the app or changes settings.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK_DIR="$(mktemp -d)"
trap 'rm -rf "$CHECK_DIR"' EXIT
python3 "$ROOT/scripts/generate-coat-patterns.py" --check
python3 - "$ROOT" "$CHECK_DIR" <<'PY'
from pathlib import Path
import re,subprocess,sys
root,out=map(Path,sys.argv[1:])
# Rename every whole-word CatRenderer: the renderer refers to itself, and CatPhysics calls the current one.
p=re.sub(r'\bCatRenderer\b','CurrentRenderer',(root/'Sources/CatPhysics.swift').read_text().replace('private(set) ',''))
(out/'CatPhysics.swift').write_text(p)
for name,source in [('LegacyRenderer',subprocess.check_output(['git','-C',str(root),'show','be51bdcd8fd43fb5ac07ab330a5ac98241fad3ef:Sources/CatRenderer.swift'],text=True)),('CurrentRenderer',(root/'Sources/CatRenderer.swift').read_text())]:
 source=re.sub(r'\bCatRenderer\b',name,source).replace('CatSettings.shared','RenderSettings.shared')
 if name=='LegacyRenderer':source=source.replace('CatHitZone','LegacyCatHitZone')
 (out/(name+'.swift')).write_text(source)
PY
swiftc -module-cache-path "$CHECK_DIR/cache" \
 "$ROOT/Sources/Localization.swift" "$ROOT/Sources/KeyMap.swift" "$ROOT/Sources/InputEngine.swift" "$ROOT/Sources/CoatPatterns.swift" \
 "$CHECK_DIR/CatPhysics.swift" "$CHECK_DIR/LegacyRenderer.swift" "$CHECK_DIR/CurrentRenderer.swift" \
 "$ROOT/tests/skins/main.swift" -o "$CHECK_DIR/check" \
 -framework AppKit -framework CoreGraphics -framework QuartzCore -framework IOKit -framework ApplicationServices
"$CHECK_DIR/check" "${1:-$CHECK_DIR}"
