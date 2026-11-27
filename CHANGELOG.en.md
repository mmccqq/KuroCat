# Changelog

English | [中文](CHANGELOG.md)

What changed in each version of Whiskerkey, newest first. Whiskerkey started from [KuroCat](https://github.com/chanke17/KuroCat) v1.3.3 by chanke17 and was called KuroCat until v1.3.3-mmccqq.8: those versions are listed below Whiskerkey 1.0, with version numbers like `v1.3.3-mmccqq.5` (the upstream version, then this project's release number).

## v1.0.0 — 2026-11-27

macOS and Windows. **KuroCat is now Whiskerkey**, and its cat is called **Whisku** (WHIS-koo).

- **Install it by hand once**: download Whiskerkey from the [release page](https://github.com/whiskerkey/whiskerkey/releases/latest). KuroCat won't offer it as an update, because the numbers start again at 1.0. From now on Whiskerkey updates itself.
- **Everything comes along**: on its first launch Whiskerkey copies KuroCat's settings (your cats, Cat Link pairing code, nickname, positions and options), quits KuroCat if it's still running, and moves "start at login" over.
- **Cat Link has its own Whiskerkey protocol**, so both of you need Whiskerkey: it doesn't link with KuroCat. Your pairing code stays the same and connects again by itself once your friend has Whiskerkey too. Codes are now shown as `WK-…`; old `KC-…` codes still work.
- **Mac**: Whiskerkey offers to move the old KuroCat to the Trash and to point Claude Code's hooks at Whiskerkey (the old settings file is kept as `~/.claude/settings.json.before-whiskerkey`). It's a new app for macOS, so it asks once for Input Monitoring again. It's signed with its own "Whiskerkey Code Signing" certificate.
- **Windows**: the program is now `Whiskerkey.exe`; you can delete `KuroCat.exe`.

## v1.3.3-mmccqq.8 — 2026-11-27

macOS and Windows.

- **KuroCat is becoming Whiskerkey**: the project now lives at [github.com/whiskerkey/whiskerkey](https://github.com/whiskerkey/whiskerkey) (old links still work). This version gets ready for the new name: it checks for updates at the new address, and its updater can install the renamed app (Whiskerkey.app on Mac, Whiskerkey.exe on Windows) in place of KuroCat. The next version will be Whiskerkey 2.0, and it will bring your settings and Cat Link along.
- **New license**: the project is now licensed under the [GNU GPL v3.0](LICENSE). This covers every change made since KuroCat v1.3.3, in all versions from v1.3.3-mmccqq.1 on. The code it started from (KuroCat v1.3.3 by chanke17) keeps its MIT notice in [LICENSE-MIT](LICENSE-MIT). Older versions are no longer offered for download.

## v1.3.3-mmccqq.7 — 2026-11-27

macOS and Windows.

- **☕ Break reminder**: after you've worked for an hour, the cat waves for a few seconds and a bubble says "You've worked for 1 hour. Time for a break ☕". Choose how long in **Break reminder ☕** (Off, 30 min, 1 h, 2 h, 3 h), and try it with **Preview the reminder**.
  - **OK**: you have 30 seconds to save your work, then 2 minutes away counts as a break. If you keep working instead, the cat keeps reminding you.
  - **Later**, or ignoring the bubble (it closes after 30 seconds): the cat reminds you again every 30 minutes.
  - **I've rested** (from the second reminder on): for a break the cat couldn't see. The work timer starts again.
  - **What counts as a break**: 15 minutes without the mouse or keyboard (shorter pauses could just be thinking), 2 minutes after OK, or 2 minutes with the screen locked or the computer asleep. Tiny mouse movements, like bumping the desk, don't count as input. After a break, the timer starts again when you come back.
  - It stays on this computer: nothing about it goes over Cat Link.
- **Fixed (Mac): a paw stuck on the keyboard**: after the screen was locked, or while typing in a password field, macOS sometimes didn't tell KuroCat that a key was released, so a paw stayed pressed on that key until you pressed it again. Caps Lock also kept a paw down while it was on. The cat now checks the keyboard four times a second and lets go of keys that aren't held anymore, as the Windows version already did. Keys you really hold, like WASD in a game, stay down.

## v1.3.3-mmccqq.6 — 2026-11-26

macOS and Windows.

- **⏸ Pause sharing in Cat Link**: **🔗 Cat Link → ⏸ Pause sharing my cat** keeps you linked but stops sending your activity. Your cat disappears from your friend's screen at once, and you still see theirs. Their menu says you paused sharing (not that you went offline): while paused, the only thing sent is a tiny "paused" signal every 10 seconds, with no keys, mouse movement, clicks, scrolling, petting, headphones or nickname. Uncheck it and your cat comes back. The switch stays on after a restart. Both of you need this version: an older version keeps showing a paused friend's cat, standing still.
- **Friend's name tag sits lower**: the tag above your friend's cat ("…'s cat · online") now sits just above its ears instead of floating high above its head.

## v1.3.3-mmccqq.5 — 2026-11-26

macOS and Windows. From this version on, both platforms have the same version number and features. It's also the first fork version for Windows: the changes in v1.3.3-mmccqq.1 to .4 below were Mac-only, and Windows gets all of them here (see "Windows catches up" at the end of this section).

### New

- **🔗 Cat Link (see a faraway friend's cat)**: one person creates a pairing code in the **🔗 Cat Link** menu and sends it to a friend, who enters it (on Windows: copy it, then click **Paste a friend's pairing code and link**). Each friend's cat then appears next to the other's, with their skin, nickname and online status, typing, moving the mouse, scrolling and wearing headphones along with them. Pet your friend's cat and theirs closes its eyes on their screen, with floating ♥. Macs and Windows PCs can link with each other.
  - **Privacy**: only "left paw / right paw is typing" is sent, never which key (the friend's cat presses random keys on that side, and its key bubbles show those random keys). Keyboard activity pauses while a password field is focused. Everything is encrypted with the pairing code and relayed through EMQX's free public server (Shanghai or US), so the server can't read it. It's opt-in, and nothing is sent after you disconnect.
  - The **Cats to show** submenu shows or hides your cat and your friend's cat separately, or all at once.
  - **🎬 Demo: pretend friend** in the menu previews it locally without sending anything. The wire format is in [docs/cat-link-protocol.md](docs/cat-link-protocol.md).
- **🤖 Claude Code alerts**: when you code with [Claude Code](https://claude.com/claude-code), a bubble pops up over the cat when a session finishes ("✓ project done"), and the cat hops twice with happy ^^ eyes. When Claude is waiting for you to approve something, an orange bubble says "project needs you", and the cat looks at you and waves until you deal with it. Clicking a bubble closes it and brings back the window the session runs in (VS Code, a terminal, ...). Several sessions can show at once: a new bubble pops in at the bottom and older ones move up.
  - **Setup**: in the menu, choose **Claude Code alerts 🤖 → Copy Claude Code hook settings**, merge what it copies into `~/.claude/settings.json` (Windows: `%USERPROFILE%\.claude\settings.json`), then reopen Claude Code. It uses Claude Code hooks (`Stop`, `Notification`, `PermissionRequest`) that run `KuroCat --claude-hook`. The same menu can show a test bubble and turn off the sound or the whole feature.
  - **Local only**: only the project folder name, the event type and the window to bring back are passed. KuroCat never reads the conversation, and nothing goes over Cat Link.
- **🦟 A mosquito while it sleeps**: after the cat has been asleep for about 3 minutes, a cartoon mosquito buzzes in from the left and circles its head. The cat wakes up, follows it with half-open eyes and swats at it once on each side, but the mosquito dodges both times and flies off to the right, and the cat dozes off again. While it stays asleep, another one comes every 5–9 minutes; if you touch the keyboard or mouse, the mosquito leaves at once. **🎬 Demo: here comes a mosquito** in the menu shows it right away, and **Mosquito visits while asleep 🦟** turns it off.
- **🔄 Updates from this fork only**: auto-update checks only this repository's [GitHub Releases](https://github.com/whiskerkey/whiskerkey/releases) and never contacts upstream, so it can't replace this fork with the official release. It checks at launch and every 6 hours. When there's a new version, it asks you once per version (after that, the menu keeps showing **🆕 New version**); **Update now** downloads it, swaps it in and restarts. The Mac app installs only builds signed with the same certificate (which also keeps the Input Monitoring permission), and both platforms check the download's exact size. GitHub downloads can be slow or fail from mainland China; the download page is then offered instead.

- **🌐 English and Chinese**: the whole app (menus, alerts, bubbles, the Cat Link name tag) is now in both languages. It follows the system language (Chinese on a Chinese system, English otherwise); **语言 / Language** in the menu switches any time. Nicknames and project names show as typed, and a Chinese cat and an English cat can still link.

### Windows catches up (v1.3.3-mmccqq.5)

The features that were Mac-only before (v1.3.3-mmccqq.1 to .4) are now on Windows too: deliberate petting, the keyboard not counting as the cat's body, delayed hunting pupils, resting paws, the scroll-wheel paw, and AirPods for headphones (read from Windows' sound output device).

## v1.3.3-mmccqq.4 — 2026-11-25

macOS only (Windows got these in v1.3.3-mmccqq.5).

- **More realistic AirPods**: they show the outer side you see when AirPods are in someone's ears, with the stem pointing down. When petting folds the ears back, the AirPods stay in the ears instead of floating off.

## v1.3.3-mmccqq.3 — 2026-11-25

macOS only (Windows got these in v1.3.3-mmccqq.5).

- **AirPods for headphones**: when the sound output switches to headphones (Bluetooth, USB, or the headphone jack), the cat lifts both paws and puts a pair of AirPods in its ears, and ♪ notes drift out while something is playing. When the output switches back to the computer's own speakers (or a monitor's speakers), it takes the AirPods out and tucks them away. Bluetooth speakers may count as headphones too, because the system reports them the same way. KuroCat reads only the output device's type, never any audio. It can be turned off in the menu.

## v1.3.3-mmccqq.2 — 2026-11-24

macOS only (Windows got these in v1.3.3-mmccqq.5).

- **Scroll-wheel paw**: while you scroll, the paw resting on the mini mouse moves onto its wheel and strokes it back and forth in rhythm. The wheel's ridges roll, and a wheel badge pops up like the click badges.
- **No update check**: the app stopped checking upstream's update server, so it never offers to replace this fork with the official release. (v1.3.3-mmccqq.5 replaced this with updates from this fork only.)
- **Stable signing certificate** (from upstream v1.3.4): builds are signed with the self-signed certificate that `scripts/create-macos-signing-cert.sh` creates, so the Input Monitoring permission survives rebuilds.

## v1.3.3-mmccqq.1 — 2026-11-23

macOS only (Windows got these in v1.3.3-mmccqq.5). The first fork version, based on upstream v1.3.3.

- **Petting must be deliberate**: the eyes-closed reaction starts only after about **1 second** of stroking the head or back, so a cursor passing over the cat no longer triggers it. Slow strokes count, and short pauses (up to 0.6 s, e.g. lifting a finger off the trackpad) don't reset the progress.
- **The keyboard isn't part of the cat**: moving the cursor over the mini keyboard no longer counts as petting. You can still drag the cat by its keyboard.
- **Hunting eyes need sustained motion**: the big round pupils appear only after about **0.8 seconds** of continuous fast mouse movement. Ordinary cursor moves across the screen no longer trigger them; waving the mouse back and forth quickly still does.
- **Resting paws**: when idle, both front paws rest on the edge of the keyboard, with toe lines, in the coat's colors.

The timings can be tuned in `Sources/CatPhysics.swift` (Windows: `windows/src/CatPhysics.hpp`): `petEngageDuration`, `petStrokeGap`, `huntArmDuration`, `huntGap` (later versions add `scrollHoldDuration`, `earbudPutOnDuration`, `earbudTakeOffDuration`, `mosquitoFirstDelay`, `mosquitoRepeatDelay`).
