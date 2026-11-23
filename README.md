# StadiaPad

Use a **Google Stadia controller** (USB or Bluetooth) as an **Xbox 360 gamepad** on Windows — with one `.exe` that lives in the system tray.

## ⬇️ Download

**[Download StadiaPad.exe (latest release)](https://github.com/Daniil9Ver/stadiapad/releases/latest/download/StadiaPad.exe)** — ready to use, no building or extra installs needed. Windows 10/11 x64.

All versions and release notes: [Releases](https://github.com/Daniil9Ver/stadiapad/releases).

> Windows SmartScreen may say *"Windows protected your PC"* because the exe isn't code-signed. Click **More info → Run anyway**.

After Google shut Stadia down, the controller got a firmware update that lets it connect to any PC, but most games still don't recognise it. Until now that meant juggling three separate tools: an emulator app, the ViGEmBus driver and HidHide (plus its configuration client). StadiaPad combines them:

- **Drivers included.** The official, signed installers of **ViGEmBus 1.22.0** and **HidHide 1.5.230** are embedded in the exe. On first launch StadiaPad offers to install them silently (one UAC prompt, then a restart).
- **No manual HidHide setup.** StadiaPad whitelists itself and hides the real Stadia controller (USB *and* Bluetooth) from games, so there is no double input.
- **Controller is given back on exit.** When you quit StadiaPad it un-hides the Stadia controller, so it is never "lost" while the app isn't running.
- **Tray menu:** controller and driver status, *Hide original controller*, *Reconnect*, *Install drivers…* / *Uninstall drivers…*, *Exit*.
- Vibration over USB, up to 4 controllers, Stadia button → Xbox Guide.

## Usage

1. [Download `StadiaPad.exe`](https://github.com/Daniil9Ver/stadiapad/releases/latest/download/StadiaPad.exe) and put it anywhere (e.g. `C:\SOFT\StadiaPad\`).
2. Run it. If the drivers are missing, click **Yes**, accept the administrator prompt and restart Windows.
3. Run StadiaPad again (it does not autostart — launch it from a shortcut when you want to play).

Settings: `HKCU\Software\StadiaPad`. Log: `%LOCALAPPDATA%\StadiaPad\StadiaPad.log`.

## Building from source

Only needed if you want to change the code — otherwise just use the [release](https://github.com/Daniil9Ver/stadiapad/releases/latest). Requires Visual Studio Build Tools 2022 with the C++ workload:

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

Output: `bin\StadiaPad.exe` (x64, static CRT — nothing else needs to be installed on the target PC).

## Project layout

| Path | Purpose |
|---|---|
| `src/main.c` | App logic, tray menu, Stadia → Xbox 360 mapping |
| `src/stadia.c`, `src/hid.c` | Reading the Stadia controller (from stadia-vigem, with lifecycle fixes) |
| `src/hidhide.c` | Talks to the HidHide driver directly via `\\.\HidHide` |
| `src/drivers.c` | Detects, installs and uninstalls the drivers |
| `src/tray.c` | Tray icon and menu |
| `drivers/` | Official driver installers, embedded into the exe |
| `third_party/ViGEmClient` | ViGEm client library |

## Credits

StadiaPad stands on the shoulders of these projects — all the hard work is theirs:

- **[stadia-vigem](https://github.com/walkco/stadia-vigem)** by **walkco** — the Stadia controller → ViGEm bridge this app is based on (MIT).
- **[Mi-ViGEm](https://github.com/grayver/Mi-ViGEm)** by **grayver** — the original project stadia-vigem was forked from (MIT).
- **[ViGEmBus](https://github.com/nefarius/ViGEmBus)** and **[ViGEmClient](https://github.com/nefarius/ViGEmClient)** by **Benjamin Höglinger-Stelzer / Nefarius Software Solutions e.U.** — the virtual gamepad bus driver and its client library (BSD-3-Clause / MIT).
- **[HidHide](https://github.com/nefarius/HidHide)** by **Eric Korff de Gidts** and **Benjamin Höglinger-Stelzer / Nefarius Software Solutions e.U.** — the HID device hiding filter driver (MIT).
- Libraries credited by stadia-vigem: [hidapi](https://github.com/libusb/hidapi) (HID implementation ideas) and [zserge/tray](https://github.com/zserge/tray) (tray code).
- Inspired by [Xiaomi_gamepad](https://github.com/irungentoo/Xiaomi_gamepad) and [mi-360](https://github.com/dancol90/mi-360).
- Application icon by [Freepik](https://www.flaticon.com/authors/freepik) from flaticon.com.

Full license texts are in [`licenses/`](licenses). The embedded driver installers are unmodified official releases redistributed under their licenses.

Not affiliated with Google, Microsoft or Nefarius Software Solutions.
