# Releasing Whiskerkey

The apps update themselves from this repository's **latest GitHub release** (never from upstream):

| | Looks for | Installs it only if |
|---|---|---|
| macOS | `Whiskerkey-macOS-universal.zip` | it has exactly the listed size, and `Whiskerkey.app` inside is signed as `com.whiskerkey.app` with the same certificate as the running app |
| Windows | `Whiskerkey-Windows-x64.exe` | it has exactly the listed size and is a Windows program (`MZ`) |

A release is "newer" when its tag has larger numbers: `v1.0.1` > `v1.0.0`. KuroCat's old releases
(`v1.3.3-mmccqq.N`) are never offered to Whiskerkey, even though their numbers are higher.

## Making a release

1. Bump the version in **both** places, to the same value:
   - `Resources/Info.plist` → `CFBundleShortVersionString` (edit the text; `PlistBuddy -c Set` reformats the file)
   - `windows/src/main.cpp` → `APP_VERSION`
2. Add a `## v<version> — <date>` section at the top of **both** `CHANGELOG.md` (中文) and
   `CHANGELOG.en.md` (English). It becomes the release notes, which people see when they click
   「打开下载页」 in the update prompt. The README lists only the key features, so put the details here.
   If the section is already there as `— unreleased` (`— 未发布`), replace that with the date.
3. Commit, merge to `main` and push.
4. On the Mac that holds the "Whiskerkey Code Signing" certificate, run:

   ```bash
   scripts/release.sh
   ```

   It builds and checks the Mac app, pushes the tag `v<version>`, waits for
   `.github/workflows/release.yml` to build and test the Windows exe, downloads it, and publishes the
   release with both files. Then it moves the `Whiskerkey.app` it built to the Trash, so the installed
   app downloads the update instead of offering to switch to that copy. Try `scripts/release.sh --dry-run`
   first: it runs every check and the Mac build without tagging or publishing.

The signing certificate (created by `scripts/create-macos-signing-cert.sh`, backup in
`~/Whiskerkey-signing`) must never be uploaded. That's why the Mac build is made locally: an update
signed any other way would make macOS forget the Input Monitoring permission, and the app refuses to
install it.
