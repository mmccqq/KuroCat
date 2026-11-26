# Releasing KuroCat (this fork)

The apps update themselves from this repository's **latest GitHub release** (never from upstream):

| | Looks for | Installs it only if |
|---|---|---|
| macOS | `KuroCat-macOS-universal.zip` | it has exactly the listed size, and `KuroCat.app` inside is signed with the same certificate as the running app |
| Windows | `KuroCat-Windows-x64.exe` | it has exactly the listed size and is a Windows program (`MZ`) |

A release is "newer" when its tag has larger numbers: `v1.3.3-mmccqq.6` > `v1.3.3-mmccqq.5`.

## Making a release

1. Bump the version in **both** places, to the same value:
   - `Resources/Info.plist` → `KuroCatForkVersion`
   - `windows/src/main.cpp` → `APP_VERSION`
2. Add a `## v<version> — <date>` section at the top of **both** `CHANGELOG.md` (中文) and
   `CHANGELOG.en.md` (English). It becomes the release notes, which people see when they click
   「打开下载页」 in the update prompt. The README lists only the key features, so put the details here.
3. Commit, merge to `main` and push.
4. On the Mac that holds the "KuroCat Code Signing" certificate, run:

   ```bash
   scripts/release.sh
   ```

   It builds and checks the Mac app, pushes the tag `v<version>`, waits for
   `.github/workflows/release.yml` to build and test the Windows exe, and then publishes the release.

The signing certificate (backup in `~/KuroCat-signing`) must never be uploaded. That's why the Mac
build is made locally: an update signed any other way would make macOS forget the Input Monitoring
permission, and the app refuses to install it.
