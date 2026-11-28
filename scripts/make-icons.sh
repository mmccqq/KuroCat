#!/usr/bin/env bash
# Remakes Whiskerkey's app icons from the app's own cat (see scripts/icon/main.swift):
#   Resources/AppIcon.icns   (macOS app icon)
#   windows/app.ico          (Windows exe icon, embedded through windows/app.rc)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

SOURCES=()
while IFS= read -r f; do SOURCES+=("$ROOT/$f"); done < <(sed -n '/^SWIFT_SOURCES=(/,/^)/p' "$ROOT/build.sh" | grep -o 'Sources/[A-Za-z]*\.swift' | grep -v 'main.swift')
swiftc -O -swift-version 5 -target "$(uname -m)-apple-macos13.0" "${SOURCES[@]}" "$ROOT/scripts/icon/main.swift" -o "$WORK/make-icons"
# Run it under its own name in a work folder, so it leaves no settings behind in the real app's domain.
"$WORK/make-icons" "$WORK"
iconutil -c icns "$WORK/AppIcon.iconset" -o "$ROOT/Resources/AppIcon.icns"
cp "$WORK/app.ico" "$ROOT/windows/app.ico"
defaults delete make-icons 2>/dev/null || true
rm -f "$HOME/Library/Preferences/make-icons.plist"
echo "==> Wrote Resources/AppIcon.icns and windows/app.ico"
