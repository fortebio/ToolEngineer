---
name: run-fbt-rapid
description: Build, launch, run, screenshot, and test the FBT_RAPID Flutter app — Windows desktop AND the web build. Use when asked to run/start/build/test/screenshot the app, verify a UI change, or see a screen render.
---

# Run FBT_RAPID (Flutter Windows desktop + web)

FBT_RAPID is a **Flutter Windows desktop GUI** companion app for the FBT_RAPID / Forte
Rapid+ test device (login → test history + CT charts, realtime temperature over COM). Verified
here on the native platform — Flutter 3.44.2 stable, Windows 11. Since 2026-07 it also builds
for **web** (feature subset: login + cloud history + charts + JSON files; no COM/flash) — see
"Web build" below.

A GUI has no `curl`/Playwright handle, so the harness is
[driver.ps1](driver.ps1): it launches the built `.exe`, polls for the `FBT_RAPID` window,
and screenshots **that window** via Win32 `PrintWindow` (works without bringing it to the
foreground). **Monorepo (2026-09-15): the app lives in `apps/fbt_rapid/`** — run every
`flutter` command from there (`cd apps/fbt_rapid`); `driver.ps1`/`webshot.ps1` default to that
folder (override with `-AppRoot` / `-WebRoot`). Paths below are relative to the app root
(`apps/fbt_rapid/` = the folder with `pubspec.yaml`). The exe is named `fbt_dxd_app.exe` — **not** `RapidPlusApp` (pubspec name)
nor `FBT_RAPID` (display name); all three differ.

## Prerequisites

- **Flutter SDK** (stable) on PATH and **Visual Studio** with the *"Desktop development with
  C++"* workload (required to build the Windows runner). Verify:
  ```powershell
  flutter --version
  ```
- No package installs needed on a configured Windows dev box. This is a desktop GUI: the
  machine must have a real **interactive desktop session** — a window has to be able to
  appear for the screenshot to capture pixels.

## Build

```powershell
flutter pub get
flutter build windows --debug
```

- Output: `build\windows\x64\runner\Debug\fbt_dxd_app.exe`. Incremental rebuild ~30s here.
- Release build (needed before packaging the installer): `flutter build windows --release`.

## Run (agent path — driver + screenshot)

This is the path to use. Build first (above), then:

```powershell
& .claude\skills\run-fbt-rapid\driver.ps1
```

Launches the Debug exe, waits for the window, writes `_smoke.png` to the current dir, then
closes the app. Output line: `OK title='FBT_RAPID' pid=... -> ..._smoke.png`. **Open the PNG
and actually look at it.** Useful flags:

```powershell
# Screenshot to a chosen path and leave the app running (prints the pid)
& .claude\skills\run-fbt-rapid\driver.ps1 -KeepOpen -Out shot.png

# Re-screenshot an app that is ALREADY running (e.g. one you left with -KeepOpen,
# or one started by `flutter run`) — does not launch a new instance
& .claude\skills\run-fbt-rapid\driver.ps1 -Attach -Out after.png

# Screenshot the Release build instead of Debug
& .claude\skills\run-fbt-rapid\driver.ps1 -Release
```

Other params: `-Exe <path>` (custom exe), `-Settle <sec>` (wait after the window appears
before capturing — default 6, raise it if the screen still shows a spinner), `-Timeout <sec>`
(how long to wait for the window — default 30).

To iterate on a UI change: edit `lib/...`, `flutter build windows --debug`, re-run the
driver, look at the new PNG.

## Run (human / dev path — hot reload)

```powershell
flutter run -d windows
```

Builds and opens the window (~12s here), then attaches with hot reload (`r` = reload, `R` =
restart, `q` = quit). It **blocks the terminal** and waits for keypresses, so it is awkward
to drive headlessly — prefer the driver above for one-shot screenshots. To screenshot a
`flutter run` session, leave it running and call the driver with `-Attach` from another shell.

## Web build (screenshot qua trình duyệt)

```powershell
flutter build web --release                                  # → build\web
& .claude\skills\run-fbt-rapid\webshot.ps1 -Out web.png      # serve + mở + chụp + dọn
```

[webshot.ps1](webshot.ps1) serves `build\web` with [web-server.js](web-server.js) (plain node,
no deps), opens Chrome/Edge in `--app` mode (own window, title = `FBT_RAPID`), captures via
PrintWindow, then kills both. **GOTCHA: it passes `--disable-gpu`** — without it Chromium's
GPU compositing makes PrintWindow return a solid gray image (Flutter still renders via
SwiftShader). Params: `-WebRoot` (default `build\web`), `-Port` (8177), `-Settle` (12s boot
wait). Fresh browser profile lives in `%TEMP%\fbtrapid_webshot_profile` — sessions do NOT
carry over from the desktop app, so you land on the login screen.

## Test

```powershell
flutter test                                   # whole suite
flutter test test/curve_processing_test.dart   # logic only — all green
```

`test/curve_processing_test.dart` (Savitzky–Golay / baseline / calibrate math) passes.
**`test/widget_test.dart` fails and that failure is pre-existing, not your change** — it
expects the old nav tabs (`'Cloud'`, `'Cài đặt'`) but the app now boots through `_AuthGate`
into `LoginScreen` (or a restored session), so those texts aren't present. So `flutter test`
exits 1 with `5 passing, 1 failing` on a clean tree.

## Gotchas

- **The screenshot may show the history screen, not the login screen.** `_AuthGate` calls
  `SessionStore.load()`; if a session was saved in `shared_preferences` from a prior run, the
  app skips `LoginScreen` and lands in `HomeShell` (and fetches live cloud data — you'll see
  real machine lists). To force the login screen, clear the saved session
  (`shared_preferences` keys in `session_store.dart`) before launching.
- **`PrintWindow` must use flag `2` (`PW_RENDERFULLCONTENT`).** Flutter renders via DWM
  composition; flag `0` captures black/blank. The driver already uses `2` — keep it.
- **Capture is by window handle, not screen grab**, so it works even if the FBT_RAPID window
  is behind others or not focused. But the window must actually exist first — the driver
  polls `MainWindowHandle != 0` rather than sleeping a fixed time.
- **Three different names.** Process/exe = `fbt_dxd_app(.exe)`; `Get-Process fbt_dxd_app`,
  not `RapidPlusApp` or `FBT_RAPID`.
- **Vietnamese console output looks garbled** (`ÄÃ³ng...`) in the PowerShell tool — that's
  the console code page, not a failure. The PNG and exit status are correct.
- **Stale `.exe` after a build.** `flutter build windows --debug` sometimes leaves the old
  binary if it thinks nothing changed; if a UI edit doesn't show up, delete
  `build\windows\x64\runner\Debug\fbt_dxd_app.exe` (or `flutter clean`) and rebuild.
- **The app allows multiple concurrent instances** (no single-instance guard), so repeated
  driver runs / `flutter run` leave orphan windows that all share the title `FBT_RAPID`. The
  default driver closes only the instance it launched; `-Attach` grabs the *first* windowed
  instance it finds, which may not be the one you just started. Sweep strays before iterating:
  `Get-Process fbt_dxd_app | Stop-Process -Force`.

## Troubleshooting

- **`Chưa có exe: ...fbt_dxd_app.exe`** → you haven't built. Run `flutter build windows
  --debug` (or pass `-Release` after a release build).
- **`Hết 30 s mà cửa sổ chưa xuất hiện` / `-Attach` finds nothing** → no interactive desktop
  session, or the app crashed on launch. Run the exe directly to see the error; raise
  `-Timeout` if the machine is just slow.
- **`App tự thoát (exit N) trước khi mở cửa sổ`** → the runner crashed before showing UI
  (often a missing VC++ runtime or a broken plugin symlink). `flutter clean && flutter pub
  get` fixes the symlink case; otherwise read the printed exit code.
- **Left a window open?** `Get-Process fbt_dxd_app | Stop-Process -Force`.
