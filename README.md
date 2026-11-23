# StadiaPad

Use a **Google Stadia controller** (USB or Bluetooth) as an **Xbox 360 gamepad** in any Windows game — with one small app that lives in the system tray.

## ⬇️ Download

| | |
|---|---|
| **[StadiaPad-Setup.exe](https://github.com/Daniil9Ver/stadiapad/releases/latest/download/StadiaPad-Setup.exe)** | **Recommended.** Installer: choose a folder, get Start-menu and desktop shortcuts, uninstall from Windows Settings. |
| [StadiaPad.exe](https://github.com/Daniil9Ver/stadiapad/releases/latest/download/StadiaPad.exe) | Portable version: no installation, just run it from any folder. |

Windows 10/11 x64. All versions and release notes: [Releases](https://github.com/Daniil9Ver/stadiapad/releases).

> Windows SmartScreen may say *"Windows protected your PC"* because the files aren't code-signed yet. Click **More info → Run anyway**.

## The problem it solves

When Google shut Stadia down, it released a firmware update that turns the Stadia controller into a normal Bluetooth/USB controller. Windows sees it as a generic *"HID-compliant game controller"* — and **most PC games simply ignore it**, because they only support Xbox (XInput) controllers.

The usual fix meant setting up **three separate tools** by hand:

1. an emulator app that translates the Stadia controller into a virtual Xbox controller,
2. the **ViGEmBus** driver that provides that virtual controller,
3. **HidHide** plus its configuration client, to hide the real Stadia controller — otherwise games see *two* controllers and every button press counts twice (*double input*).

Then you had to whitelist the right app, tick the right devices, enable hiding, reboot — and repeat it all on every new PC. And with hiding enabled, the controller stopped working entirely whenever the emulator wasn't running.

**StadiaPad does all of that in one app:**

- 🎮 **Stadia → Xbox 360.** Games see a standard Xbox 360 controller: buttons, sticks, triggers, vibration (USB), Stadia button = Xbox Guide. Up to 4 controllers.
- 📦 **Drivers included.** The official, signed installers of **ViGEmBus** and **HidHide** are built in. StadiaPad offers to install them on first launch — one click, one admin prompt, one restart.
- 🙈 **No double input, no manual setup.** StadiaPad configures HidHide automatically and hides the real controller over **USB and Bluetooth**.
- ↩️ **Your controller is never "lost".** When you exit StadiaPad, the real Stadia controller is un-hidden again.
- 🧹 **Clean removal.** Uninstall the drivers from the tray menu, or uninstall StadiaPad from Windows Settings — it removes its settings, log and HidHide entries.

## How to use

1. **Download** [StadiaPad-Setup.exe](https://github.com/Daniil9Ver/stadiapad/releases/latest/download/StadiaPad-Setup.exe) and install it (pick a folder, keep *Create a desktop shortcut* ticked).
2. **Launch StadiaPad.** If the drivers are missing it asks to install them → click **Yes**, accept the administrator prompt, and **restart** Windows when asked.
3. **Launch StadiaPad again** from the desktop shortcut. A gamepad icon appears in the tray (bottom-right).
4. **Connect your Stadia controller** by USB cable or Bluetooth. That's it — start your game.

StadiaPad does **not** start with Windows; launch it from the shortcut when you want to play.

### Tray menu (right-click the icon)

| Item | What it does |
|---|---|
| *Controller 1: Stadia (USB) → Xbox 360* | Connected controllers |
| *Drivers: installed / not installed* | Driver status |
| *Hide original controller* | Hide the real Stadia from games (prevents double input). On by default. |
| *Reconnect* | Re-scan controllers and drivers |
| *Install drivers… / Uninstall drivers…* | Install or remove ViGEmBus + HidHide |
| *Exit* | Quit and give the real controller back to Windows |

### Uninstalling

**Windows Settings → Apps → StadiaPad → Uninstall.** You'll be asked whether to remove the drivers too:

- **No** (default) — keep the drivers; reinstalling StadiaPad later works instantly, no restart.
- **Yes** — remove ViGEmBus and HidHide as well (choose this only if no other program, e.g. DS4Windows, uses them).

### Troubleshooting

- **Game doesn't react to the controller** — make sure the tray menu says *Drivers: installed* and shows your controller; try *Reconnect*.
- **Restart required** shown in the menu — restart Windows after installing or removing drivers.
- **Log file** for bug reports: `%LOCALAPPDATA%\StadiaPad\StadiaPad.log` — please attach it when opening an [issue](https://github.com/Daniil9Ver/stadiapad/issues).

**Known limitations:** vibration works over USB only (not Bluetooth); the Assistant and Capture buttons are not mapped yet.

## Building from source

Only needed if you want to change the code. Requires Visual Studio Build Tools 2022 (C++ workload) and, for the installer, [Inno Setup 6](https://jrsoftware.org/isinfo.php):

```
powershell -ExecutionPolicy Bypass -File build.ps1
ISCC.exe installer.iss
```

Output: `bin\StadiaPad.exe` (x64, static CRT) and `bin\StadiaPad-Setup.exe`.

| Path | Purpose |
|---|---|
| `src/main.c` | App logic, tray menu, Stadia → Xbox 360 mapping |
| `src/stadia.c`, `src/hid.c` | Reading the Stadia controller (from stadia-vigem, with lifecycle fixes) |
| `src/hidhide.c` | Talks to the HidHide driver directly via `\\.\HidHide` |
| `src/drivers.c` | Detects, installs and uninstalls the drivers |
| `src/tray.c` | Tray icon and menu |
| `installer.iss` | Inno Setup installer script |
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
- Installer built with [Inno Setup](https://jrsoftware.org/isinfo.php) by Jordan Russell.
- Application icon by [Freepik](https://www.flaticon.com/authors/freepik) from flaticon.com.

Full license texts are in [`licenses/`](licenses). The embedded driver installers are unmodified official releases redistributed under their licenses.

Not affiliated with Google, Microsoft or Nefarius Software Solutions.
