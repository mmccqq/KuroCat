#!/usr/bin/env bash
# Publish a KuroCat release that the apps' auto-update picks up (macOS + Windows).
#
#   scripts/release.sh             publish
#   scripts/release.sh --dry-run   run every check and the macOS build, but don't tag or publish
#
# Publishes the version in Resources/Info.plist (KuroCatForkVersion), which must equal APP_VERSION
# in windows/src/main.cpp and have a "## v<version>" section in CHANGELOG.md and CHANGELOG.en.md
# (used as the release notes). Run it on main, with everything committed and pushed. It:
#   1. builds KuroCat.app here, signed with "KuroCat Code Signing" (the certificate never leaves this
#      Mac; it is what lets updates keep macOS's Input Monitoring permission),
#   2. checks that the installed KuroCat would accept this build as an update,
#   3. tags and pushes v<version>; .github/workflows/release.yml then builds the Windows .exe,
#   4. creates a draft release with the macOS zip, waits for the .exe, then publishes the release.
set -euo pipefail
cd "$(dirname "$0")/.."

DRY_RUN=0
[ "${1:-}" = "--dry-run" ] && DRY_RUN=1
REPO="mmccqq/KuroCat-vibecoding"
MAC_ZIP="dist/KuroCat-macOS-universal.zip"
die() { echo "release: $*" >&2; exit 1; }
# A dry run reports these problems instead of stopping, so it can still check the rest.
must() { if [ "$DRY_RUN" = 1 ]; then echo "release (dry run would stop here): $*" >&2; else die "$@"; fi; }

VERSION="$(/usr/libexec/PlistBuddy -c 'Print :KuroCatForkVersion' Resources/Info.plist)"
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
SIGN_INFO="$(codesign -dvv KuroCat.app 2>&1 || true)"
case "$SIGN_INFO" in
    *"Authority=KuroCat Code Signing"*) ;;
    *) die "KuroCat.app is not signed with \"KuroCat Code Signing\" (updates would lose Input Monitoring)" ;;
esac
# The installed app accepts an update only if it meets the installed app's designated requirement
# (same identifier and certificate). Check that with codesign, which works for any installed version.
if [ -d /Applications/KuroCat.app ]; then
    REQ="$(codesign -d -r- /Applications/KuroCat.app 2>/dev/null | sed -n 's/^designated => //p')"
    [ -n "$REQ" ] || die "could not read the installed KuroCat's signature requirement"
    codesign --verify --deep --strict -R="$REQ" KuroCat.app \
        || die "the installed KuroCat would refuse this build as an update"
fi
mkdir -p dist
rm -f "$MAC_ZIP"
ditto -c -k --keepParent KuroCat.app "$MAC_ZIP"

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
git tag -a "$TAG" -m "KuroCat $TAG"
git push -q origin "$TAG"

echo "==> Draft release with the macOS build"
gh release create "$TAG" -R "$REPO" --draft --title "KuroCat $TAG" --notes "$NOTES" "$MAC_ZIP" \
    || gh release upload "$TAG" -R "$REPO" "$MAC_ZIP" --clobber
gh release edit "$TAG" -R "$REPO" --notes "$NOTES" >/dev/null

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

ASSETS="$(gh release view "$TAG" -R "$REPO" --json assets -q '.assets[].name')"
grep -qx "KuroCat-macOS-universal.zip" <<<"$ASSETS" || die "the release has no macOS zip"
grep -qx "KuroCat-Windows-x64.exe" <<<"$ASSETS" || die "the release has no Windows exe"

gh release edit "$TAG" -R "$REPO" --draft=false --latest >/dev/null
echo "==> Published $TAG: https://github.com/$REPO/releases/tag/$TAG"
