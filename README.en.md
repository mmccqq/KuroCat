# 🐱 KuroCat — Full-State Keyboard & Mouse Desktop Cat Pet (macOS / Windows / Linux / UOS)

<p align="center">
  <a href="README.md">简体中文</a> | <b>English</b>
</p>

<p align="center">
  <img src="assets/demo_typing.gif" width="265" alt="Typing on keyboard"/>
  <img src="assets/demo_hunting.gif" width="265" alt="Hunting boba eyes on fast mouse movement"/>
  <img src="assets/demo_combo.gif" width="265" alt="Typing + dragging mouse + 3rd key with hind paw"/>
</p>

<p align="center">
  <b>Ultra-lightweight native desktop pet · Hardware-level full-state capture for typing &amp; gaming · 2.5D spherical head tracking · Hunting boba eyes · 4 switchable skins</b><br/>
  Native macOS (Swift + CoreGraphics + IOHIDManager) · Windows (C++17 + GDI+ + Raw Input) · Linux / UOS (C++17 + GTK3/Cairo + XInput2/evdev)
</p>

---

## 🍴 About This Fork

This is a fork of [chanke17/KuroCat](https://github.com/chanke17/KuroCat), based on upstream **v1.3.3**. The changes below are in **both the macOS and Windows** versions, which are kept feature-for-feature the same; the Linux version is unchanged from upstream.

- **Deliberate petting**: the cat reacts after about **1 second** of stroking its head or back, not the instant the cursor passes over it. Slow strokes count, and short pauses (up to 0.6 s, such as lifting a finger off the trackpad) keep your progress.
- **The keyboard isn't the cat**: moving the cursor over the mini keyboard no longer counts as petting. You can still drag the cat by its keyboard.
- **Calmer hunting eyes**: the round boba pupils appear only after about **0.8 seconds** of continuous fast mouse movement, so ordinary cursor moves across the screen don't trigger them. Waving the mouse back and forth still does.
- **Visible resting paws**: when idle, the cat's front paws rest on the keyboard's edge, with toe lines and each skin's paw color.
- **Paw on the scroll wheel**: when you scroll, the paw on the mini mouse reaches onto the wheel and rubs it back and forth in rhythm, the wheel's stripes roll, and a "滚轮" (scroll wheel) badge pops up like the click badges.
- **AirPods for headphones**: when the sound output switches to headphones (Bluetooth, USB, or the headphone jack), the cat lifts both paws and puts a pair of AirPods in its ears, and ♪ notes drift out while something is playing. When the output switches back to the computer's own speakers (or a monitor's speakers), it takes the AirPods out and tucks them away. Bluetooth speakers may count as headphones too, because the system reports them the same way. KuroCat reads only the output device's type, never any audio. Turn it off with **连接耳机时戴上 AirPods 🎧** in the menu.
- **🔗 Cat Link (see a faraway friend's cat)**: one person creates a pairing code in the **🔗 猫咪连线** menu and sends it to a friend, who enters it (on Windows: copy it, then click **粘贴朋友的配对码并连线**, "paste a friend's code and connect"). Each friend's cat then appears next to the other's, with their skin, nickname and online status, typing, moving the mouse, scrolling and wearing headphones along with them. Pet your friend's cat and theirs closes its eyes on their screen, with floating ♥. Macs and Windows PCs can link with each other.
  - **Privacy**: only "left paw / right paw is typing" is sent, never which key (the friend's cat presses random keys on that side, and its key bubbles show those random keys). Keyboard activity pauses while a password field is focused. Everything is encrypted with the pairing code and relayed through EMQX's free public server (Shanghai or US), so the server can't read it. It's opt-in, and nothing is sent after you disconnect.
  - **🎬 演示：模拟一位朋友** ("Demo: simulate a friend") in the menu previews it locally without sending anything. The wire format is in [docs/cat-link-protocol.md](docs/cat-link-protocol.md).
- **No update check**: the app no longer checks upstream's update server, so it never offers to replace this fork with the official release. To update, pull this repository and rebuild.

The timings can be tuned in `Sources/CatPhysics.swift` (Windows: `windows/src/CatPhysics.hpp`) (`petEngageDuration`, `petStrokeGap`, `huntArmDuration`, `huntGap`, `scrollHoldDuration`, `earbudPutOnDuration`, `earbudTakeOffDuration`).

This fork doesn't publish pre-built downloads. To use it, build from source with `./build.sh` (see [Download & Run](#-download--run)).

macOS remembers the Input Monitoring permission by the app's code signature. To keep it across rebuilds, run this once before building:

```bash
bash scripts/create-macos-signing-cert.sh
```

It creates a free self-signed "KuroCat Code Signing" certificate in your login keychain (with a private backup in `~/KuroCat-signing`; never upload it), and `./build.sh` signs every build with it. Without it, builds are ad-hoc signed, and after each rebuild you must remove and re-add KuroCat in **System Settings → Privacy & Security → Input Monitoring**, then restart it.

---

## ✨ Highlights

- **🎮 Full-State Input Capture for Typing & Gaming (True NKRO)**
  - **macOS**: Dual-layer `IOHIDManager` (kernel HID hardware layer) + `CGEventTap`, unaffected by IME switching. Supports holding `WASD` in games, key combinations, standalone modifier keys (`⌘ / ⇧ / ⌥ / ⌃ / Fn`), arrow keys, and `F1–F12`.
  - **Windows**: Native `Raw Input API (RIDEV_INPUTSINK)` hardware scan-code listener. Captures global keystrokes and mouse micro-movements even inside DirectX/Vulkan games with locked cursor.
  - **Linux & UOS (统信 UOS)**: Dual-layer `XInput2 (XI_RawKeyPress/Motion)` + kernel `evdev (/dev/input/event*)`. **Works out of the box without `sudo` on UOS / Deepin / Ubuntu X11 desktops**; on Wayland, add your user to the `input` group for hardware-level full-state capture.
- **🌀 2.5D Spherical Head Tracking & Natural Perspective**
  - Uses a Live2D-style spherical projection algorithm: the cat's head, ears, eyes, and whiskers rotate with realistic 2.5D perspective and parallax toward your cursor. The keyboard and mouse are oriented 180° facing the cat, matching your left-hand keyboard / right-hand mouse gaming posture.
- **⚡ Hunting Boba Eyes Mode**
  - Shake your mouse quickly and the cat's pupils dilate from slit pupils into huge round boba eyes with bright highlights—crouching low, wagging its tail faster, and locking onto the cursor without blinking.
- **🐾 Fun Interactive Details**
  - **3-Key "Hind Paw Press"**: Hold 3 or more keys simultaneously and the cat lifts its hind paw to press the 3rd key!
  - **Head / Back Petting**: Gently stroke the cat's head with your cursor to trigger cozy closed eyes `— —` and shimmer particles; leave it idle and it falls asleep with floating `z`s.
  - **Smart Click-Through, Launch at Login & Auto Update Check**: Transparent areas pass clicks through automatically. Toggle **Full Click-Through (Gaming Anti-Misclick)** anytime, enable **Launch at Login** (Windows Registry / macOS LaunchAgent / Linux XDG Autostart), and get automatic update notifications in the menu bar / tray.
- **🎨 4 Skins & Custom Color Combinations**
  - **Cat Skins**: Classic Black Cat (neon yellow-green eyes) / Siamese Cat (warm oat cream + 2.5D caramel mask + chocolate socks + sapphire blue eyes) / American Shorthair (silver tabby & white) / Orange & White Cat (orange crown + white chest & mittens)
  - **Keyboard Themes**: Classic Warm Cream / Midnight Geek Black / Retro Cream Gray (toggle key labels, key bubbles, and mouse click bubbles)
  - **Eye Colors & Scaling**: 6 eye colors, 5 size levels, plus daily keystroke counter and live KPM stats.

### 6-State Preview of All 4 Skins

| Classic Black Cat | Siamese Cat |
| :---: | :---: |
| <img src="assets/preview_black.jpg" width="420" alt="Classic Black Cat preview"/> | <img src="assets/preview_siamese.jpg" width="420" alt="Siamese Cat preview"/> |

| American Shorthair | Orange & White Cat |
| :---: | :---: |
| <img src="assets/preview_shorthair.jpg" width="420" alt="American Shorthair preview"/> | <img src="assets/preview_orangeWhite.jpg" width="420" alt="Orange & White Cat preview"/> |

The 6-state previews are natively rendered by the app: Idle, Top-Left Look, Bottom-Right Look, Hunting, Keyboard/Mouse + 3rd Paw, and Petting Closed Eyes.

---

## 📦 Download & Run

Download pre-built binaries from **[Releases](../../releases)** (built automatically via GitHub Actions):

### 🍎 macOS (Universal: Apple Silicon & Intel · macOS 13+)
1. Download `KuroCat-macOS-universal.zip`, unzip it, and drag `KuroCat.app` into **Applications** (lives in the top menu bar, no Dock icon).
2. On first launch, click the bottom prompt pill to open **System Settings → Privacy & Security → Input Monitoring**, enable `KuroCat`, then click **Restart KuroCat** from the menu bar to enable global typing & gaming capture.
3. Supports one-click **Launch at Login**. When **Full Click-Through** is enabled, use the menu bar icon or **hold `⌥ Option` and right-click / drag the cat**.

**Build from source:**
```bash
chmod +x build.sh
./build.sh
open KuroCat.app
```

### 🪟 Windows (Windows 10 / 11 x64)
1. Download the single-file `KuroCat-Windows-x64.exe` (~220 KB, **zero external runtime dependencies**). Double-click to run; lives in the system tray.
2. Right-click the cat or click the tray icon to switch skins, keyboard themes, scale, **Launch at Login**, or **Full Click-Through**.
3. **4 ways to exit or open the menu while Full Click-Through is active**:
   - Global shortcut: **`Ctrl + Alt + Shift + P`** to toggle click-through;
   - Temporary unlock: hold **`Alt`** and right-click the cat to uncheck or left-drag to move;
   - System tray: click the black cat icon in the **`^` hidden icons area** on the taskbar;
   - Fallback: **double-click `KuroCat.exe` again** to disable click-through and open the settings menu at your cursor.
   > **Cat follows the mouse but not the keyboard in a game?** The game (or its launcher) runs as Administrator, and Windows withholds its keystrokes from normal apps. Right-click the cat → **Restart as administrator**, or tick **Always run as administrator**. KuroCat shows a tray tip when it detects such a game.
   >
   > **Cat disappears in games?** KuroCat now re-asserts always-on-top when a fullscreen/borderless game comes to the front and moves back on-screen after resolution changes. True *exclusive fullscreen* cannot be overlaid by any desktop window — switch the game to borderless/windowed fullscreen.
   >
   > **Still told to update after downloading the new version?** An old copy was still running. A newer KuroCat now shuts down older running copies on launch, and the update prompt shows which exe is running.

**Build from source (MSVC / MinGW):**
```bat
cd windows
build.bat
```
Or with CMake:
```bat
cd windows
cmake -B build && cmake --build build --config Release
```

### 🐧 Linux & UOS / Kylin (x86_64 & ARM64)
Supports **Ubuntu 20.04+ / Debian 11+ / Deepin / UOS (x86_64 & ARM64) / Kylin / Arch / Fedora**:

1. **No-password install (recommended on UOS / Kylin)**: extract `KuroCat-Linux-x86_64.tar.gz` or `KuroCat-Linux-arm64.tar.gz` and run `bash install.sh` in that folder. It installs into `~/.local` for the current user only (no sudo); `bash uninstall.sh` removes it.
2. **`.deb` package (UOS / Deepin / Ubuntu / Debian / Kylin)**:
   - Download `KuroCat-Linux-amd64.deb` or `KuroCat-Linux-arm64.deb`, double-click it or run:
     ```bash
     sudo apt install ./KuroCat-Linux-amd64.deb
     ```
   - Packages are built on Debian 10 (glibc 2.28), so older UOS 20 / Kylin V10 systems can run them. In-app updates install per-user and no longer ask for a password.
   - Kylin refuses to install? Set *Security Center → App Protection / Execution Control* to Off or Warn, or use the no-password install.
3. **Other Linux distros (Arch / Fedora, etc.)**:
   - Extract the tar.gz and run `bash install.sh`, or simply run `./kurocat`.
   - Desktop lagging? KuroCat uses an adaptive 30/20/10 fps by default; the **60fps** option in the menu should stay off on low-end machines.
   - On pure **Wayland** sessions, run `sudo usermod -aG input $USER` once and re-login to enable kernel-level `evdev` capture.
4. **Build from source (including LoongArch)**:
   ```bash
   sudo apt install g++ pkg-config libgtk-3-dev libcairo2-dev libx11-dev libxi-dev
   cd linux && chmod +x build.sh build_deb.sh
   ./build.sh        # Compile binary
   ./build_deb.sh    # Package .deb installer
   ```

---

## 📁 Project Structure

```text
KuroCat/
├── Sources/                   # macOS native Swift source (Universal: Apple Silicon + Intel)
├── Resources/                 # macOS Info.plist & AppIcon.icns
├── build.sh                   # macOS universal binary build script
├── windows/                   # Windows native C++17 (Win32 + GDI+ + Raw Input)
├── linux/                     # Linux & UOS native C++17 (GTK3 + Cairo + XInput2 + evdev)
│   ├── src/                   # KeyMap / InputEngine / CatPhysics / CatRenderer / main.cpp
│   ├── build.sh               # Local build script
│   └── build_deb.sh           # Debian / UOS .deb packager
└── .github/workflows/build.yml # Automated multi-platform release workflow
```

---

## 🔒 Privacy

All keyboard and mouse hardware signals in KuroCat are processed **strictly in local memory in real time** to drive animations and counters—**no keystrokes are ever logged or saved**. Aside from checking the official release endpoint for new versions on startup or when manually clicking "Check for Updates", KuroCat makes no network requests. The entire codebase is open source and auditable.

## 📄 License

[MIT License](LICENSE)
