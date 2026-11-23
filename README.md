# StadiaPad

A small Windows tray app that makes a Google Stadia controller (USB or Bluetooth) show up as an Xbox 360 controller, so games can use it.

## Download

Get the latest version from [Releases](https://github.com/Daniil9Ver/stadiapad/releases/latest):

- [StadiaPad-Setup.exe](https://github.com/Daniil9Ver/stadiapad/releases/latest/download/StadiaPad-Setup.exe) - installer (Start menu and desktop shortcuts, uninstall from Windows Settings)
- [StadiaPad.exe](https://github.com/Daniil9Ver/stadiapad/releases/latest/download/StadiaPad.exe) - portable, runs from any folder

Requires Windows 10 or 11, 64-bit. The files are not code-signed, so SmartScreen may show "Windows protected your PC" the first time. Click "More info", then "Run anyway".

## Why

After Stadia shut down, Google released a firmware update that lets the controller connect to a PC. Windows sees it as a generic HID game controller, and most games don't support those - they expect an Xbox (XInput) controller.

The existing workaround is to combine three tools yourself: [stadia-vigem](https://github.com/walkco/stadia-vigem) to translate the controller, the [ViGEmBus](https://github.com/nefarius/ViGEmBus) driver for the virtual Xbox controller, and [HidHide](https://github.com/nefarius/HidHide) to hide the real controller so games don't register every button press twice. HidHide has to be configured by hand, and once it is, the controller stops working whenever stadia-vigem isn't running.

StadiaPad puts this into one program:

- It includes the official ViGEmBus 1.22.0 and HidHide 1.5.230 installers and offers to install them on first run.
- It configures HidHide itself and hides the controller over both USB and Bluetooth.
- When you exit it, the controller is un-hidden, so it still works as a normal device.
- Vibration works over USB. Up to 4 controllers. The Stadia button acts as the Xbox Guide button.

## Usage

1. Install StadiaPad and start it.
2. If the drivers aren't installed it will ask to install them. Accept the administrator prompt and restart Windows.
3. Start StadiaPad again and connect the controller.

StadiaPad doesn't start with Windows. Launch it from the shortcut before playing.

Right-click the tray icon for:

- the list of connected controllers and the driver status
- Hide original controller (on by default)
- Reconnect
- Install drivers / Uninstall drivers
- Exit

## Uninstalling

Use Windows Settings > Apps > StadiaPad. The uninstaller asks whether to remove the drivers as well. Say no if another program (for example DS4Windows) uses ViGEmBus or HidHide, or if you plan to reinstall StadiaPad. StadiaPad's own settings, log and HidHide entries are removed either way.

## Known issues

- No vibration over Bluetooth.
- The Assistant and Capture buttons aren't mapped.

If something doesn't work, open an [issue](https://github.com/Daniil9Ver/stadiapad/issues) and attach the log from `%LOCALAPPDATA%\StadiaPad\StadiaPad.log`.

## Building

Needs Visual Studio Build Tools 2022 (C++), and [Inno Setup 6](https://jrsoftware.org/isinfo.php) for the installer.

```
powershell -ExecutionPolicy Bypass -File build.ps1
ISCC.exe installer.iss
```

This produces `bin\StadiaPad.exe` and `bin\StadiaPad-Setup.exe`.

Source layout:

- `src/main.c` - tray menu and the Stadia to Xbox mapping
- `src/stadia.c`, `src/hid.c` - reading the controller (from stadia-vigem)
- `src/hidhide.c` - talks to the HidHide driver
- `src/drivers.c` - driver install and uninstall
- `installer.iss` - installer script
- `drivers/` - the official driver installers that get embedded in the exe

## Credits

- [stadia-vigem](https://github.com/walkco/stadia-vigem) by walkco, which StadiaPad is based on, and [Mi-ViGEm](https://github.com/grayver/Mi-ViGEm) by grayver, which stadia-vigem was forked from (MIT)
- [ViGEmBus](https://github.com/nefarius/ViGEmBus) and [ViGEmClient](https://github.com/nefarius/ViGEmClient) by Benjamin Höglinger-Stelzer / Nefarius Software Solutions (BSD-3-Clause, MIT)
- [HidHide](https://github.com/nefarius/HidHide) by Eric Korff de Gidts and Benjamin Höglinger-Stelzer / Nefarius Software Solutions (MIT)
- [hidapi](https://github.com/libusb/hidapi) and [zserge/tray](https://github.com/zserge/tray), credited by stadia-vigem
- Installer made with [Inno Setup](https://jrsoftware.org/isinfo.php)
- Icon by [Freepik](https://www.flaticon.com/authors/freepik) from flaticon.com

## License

MIT, see [LICENSE](LICENSE). Third-party licenses are in [licenses/](licenses).

Not affiliated with Google, Microsoft or Nefarius Software Solutions.
