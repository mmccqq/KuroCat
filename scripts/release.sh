#!/usr/bin/env bash
# Publish a Whiskerkey release that the apps' auto-update picks up (macOS + Windows).
#
#   scripts/release.sh             publish
#   scripts/release.sh --dry-run   run every check and the macOS build, but don't tag or publish
#
# Publishes the version in Resources/Info.plist (CFBundleShortVersionString), which must equal APP_VERSION
# in windows/src/main.cpp and have a "## v<version>" section in CHANGELOG.md and CHANGELOG.en.md
# (used as the release notes). Run it on main, with everything committed and pushed. It:
#   1. builds Whiskerkey.app here, signed with "Whiskerkey Code Signing" (the certificate never leaves
#      this Mac; it is what lets updates keep macOS's Input Monitoring permission),
#   2. checks that the installed Whiskerkey would accept this build as an update,
#   3. tags and pushes v<version>; .github/workflows/release.yml then builds and tests the Windows .exe,
#   4. downloads that .exe and publishes the release with both files (only this script creates releases),
#   5. moves the Whiskerkey.app it built to the Trash: a newer copy left here would make the installed
#      app offer "switch to the copy you already downloaded" instead of downloading the update.
set -euo pipefail
cd "$(dirname "$0")/.."

DRY_RUN=0
[ "${1:-}" = "--dry-run" ] && DRY_RUN=1
REPO="whiskerkey/whisku"
APP="Whiskerkey.app"
MAC_ZIP="dist/Whiskerkey-macOS-universal.zip"
WIN_ASSET="Whiskerkey-Windows-x64.exe"
die() { echo "release: $*" >&2; exit 1; }
# A dry run reports these problems instead of stopping, so it can still check the rest.
must() { if [ "$DRY_RUN" = 1 ]; then echo "release (dry run would stop here): $*" >&2; else die "$@"; fi; }

VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' Resources/Info.plist)"
WIN_VERSION="$(sed -n 's/.*APP_VERSION = L"\([^"]*\)".*/\1/p' windows/src/main.cpp)"
TAG="v$VERSION"
[ "$WIN_VERSION" = "$VERSION" ] || die "windows/src/main.cpp APP_VERSION ($WIN_VERSION) differs from Info.plist ($VERSION)"
[ -z "$(git status --porcelain)" ] || must "commit or stash your changes first"
[ "$(git rev-parse --abbrev-ref HEAD)" = "main" ] || die "release from the main branch"
git fetch -q origin main --tags
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || must "push main first (local and origin/main differ)"
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then die "tag $TAG already exists: bump the version first"; fi

echo "==> Building $TAG for macOS"
./build.sh
# Read codesign's output first, then search it: piping into `grep -q` makes codesign die of a broken
# pipe once grep has its match, which `set -o pipefail` reports as a failure.
SIGN_INFO="$(codesign -dvv "$APP" 2>&1 || true)"
case "$SIGN_INFO" in
    *"Authority=Whiskerkey Code Signing"*) ;;
    *) die "$APP is not signed with \"Whiskerkey Code Signing\" (updates would lose Input Monitoring)" ;;
esac
# The installed app accepts an update only if it meets the installed app's designated requirement
# (same identifier and certificate). Check that with codesign, which works for any installed version.
if [ -d "/Applications/$APP" ]; then
    REQ="$(codesign -d -r- "/Applications/$APP" 2>/dev/null | sed -n 's/^designated => //p')"
    [ -n "$REQ" ] || die "could not read the installed Whiskerkey's signature requirement"
    codesign --verify --deep --strict -R="$REQ" "$APP" \
        || die "the installed Whiskerkey would refuse this build as an update"
fi
mkdir -p dist
rm -f "$MAC_ZIP"
ditto -c -k --keepParent "$APP" "$MAC_ZIP"

# Release notes: this version's section of CHANGELOG.md (中文) and CHANGELOG.en.md (English).
section() {   # print the body of "## <tag>..." up to the next "## " heading
    awk -v tag="$2" '$0 ~ "^## " { if (found) exit; if ($0 == "## " tag || index($0, "## " tag " ") == 1) { found = 1; next } } found' "$1"
}
NOTES_ZH="$(section CHANGELOG.md "$TAG")"
NOTES_EN="$(section CHANGELOG.en.md "$TAG")"
[ -n "$(echo "$NOTES_ZH" | tr -d '[:space:]')" ] || die "CHANGELOG.md has no section for $TAG"
[ -n "$(echo "$NOTES_EN" | tr -d '[:space:]')" ] || die "CHANGELOG.en.md has no section for $TAG"
NOTES="$(printf '%s\n\n---\n\n**English**\n%s\n' "$NOTES_ZH" "$NOTES_EN")"

if [ "$DRY_RUN" = 1 ]; then
    echo "==> Dry run OK: $TAG would be published with $MAC_ZIP ($(du -h "$MAC_ZIP" | cut -f1)) and these notes:"
    printf "%s\n" "$NOTES" | sed -n "1,12p"
    exit 0
fi

echo "==> Tagging $TAG"
git tag -a "$TAG" -m "Whiskerkey $TAG"
git push -q origin "$TAG"

echo "==> Waiting for the Windows build"
SHA="$(git rev-parse HEAD)"
RUN=""
for _ in $(seq 1 30); do
    RUN="$(gh run list -R "$REPO" --workflow release.yml --limit 10 --json databaseId,headSha \
        -q "map(select(.headSha == \"$SHA\")) | .[0].databaseId // empty")"
    [ -n "$RUN" ] && break
    sleep 10
done
[ -n "$RUN" ] || die "the Windows release build did not start (check GitHub Actions)"
gh run watch "$RUN" -R "$REPO" --exit-status --interval 20 >/dev/null || die "the Windows release build failed: gh run view $RUN -R $REPO --log-failed"

WIN_DIR="dist/windows"
rm -rf "$WIN_DIR"
gh run download "$RUN" -R "$REPO" -n Whiskerkey-Windows-x64 -D "$WIN_DIR"
WIN_EXE="$WIN_DIR/$WIN_ASSET"
[ "$(head -c 2 "$WIN_EXE")" = "MZ" ] || die "the downloaded Windows build is not a Windows program"
python3 -I -c 'import sys; sys.exit(0 if sys.argv[2].encode("utf-16-le") in open(sys.argv[1], "rb").read() else 1)' "$WIN_EXE" "$VERSION" \
    || die "the Windows build does not contain version $VERSION"

echo "==> Publishing the release"
if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then die "a release for $TAG already exists"; fi
gh release create "$TAG" -R "$REPO" --verify-tag --latest --title "Whiskerkey $TAG" --notes "$NOTES" "$MAC_ZIP" "$WIN_EXE"
ASSETS="$(gh release view "$TAG" -R "$REPO" --json assets -q '.assets[].name')"
grep -qx "$(basename "$MAC_ZIP")" <<<"$ASSETS" || die "the release has no macOS zip"
grep -qx "$WIN_ASSET" <<<"$ASSETS" || die "the release has no Windows exe"
echo "==> Published $TAG: https://github.com/$REPO/releases/tag/$TAG"
mv "$APP" "$HOME/.Trash/Whiskerkey-$VERSION-build-$(date +%H%M%S).app" && echo "==> Moved the local $APP build to the Trash"
