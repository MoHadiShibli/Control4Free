# Installation

Control4Free comes in two pieces:

- **The service** (`control4free.elf`), a GoldHEN payload that runs in the background, creates the virtual
  controllers, and serves the controller page on port 4264.
- **The app** (`Control4Free-<version>.pkg`), which goes on the PS4's home screen. It starts and stops the
  service, shows the address and a QR code, and sets up auto-start. The package carries its own copy of the
  service, so the package is all you need.

## Requirements

- A PS4 with [GoldHEN](https://github.com/GoldHEN/GoldHEN) v2.4b18.10 or later. Older GoldHEN versions don't
  run ELF payloads like Control4Free reliably, and have no auto-start. See [Compatibility](#compatibility) for
  each firmware.
- **Debug Settings** turned on in GoldHEN, for the Package Installer.
- A phone, tablet or computer with a modern browser, on the same network as the PS4.

## Compatibility

Every firmware GoldHEN supports has a GoldHEN version of v2.4b18.10 or later, so updating GoldHEN is all it
takes. **Tested** means someone confirmed it on a console; the other rows have GoldHEN support and support in
Control4Free's start-up code, but nobody has tried them yet.

| Firmware | GoldHEN | Status |
|---|---|---|
| 10.01 | v2.4b18.10 or later | **Tested** (v2.4b18.10) |
| 13.02, 13.04 | v2.4b18.12 or later | **Tested** (v2.4b18.12) |
| 13.52 | v2.4b18.11 or later | **Tested** (v2.4b18.12), with Control4Free 1.1.1 or later |
| 13.50 | v2.4b18.12 or later | Not tested |
| 13.00 | v2.4b18.10 or later | Not tested |
| 12.00, 12.02, 12.50, 12.52 | v2.4b18.10 or later | Not tested |
| 5.05, 6.71, 6.72, 7.0x, 7.5x, 8.0x, 8.5x, 9.0x, 9.5x, 9.60, 10.00, 10.50, 10.70, 10.71, 11.0x, 11.5x | v2.4b18.10 or later | Not tested |

The older a firmware, the less certain it is that the PS4's virtual-controller API behaves as on the tested
ones. If you try an untested firmware, an issue saying whether it worked helps others, with a
[diagnostic report](troubleshooting.md#reporting-a-problem) if it didn't.

## Install the app

1. Download `Control4Free-<version>.pkg` from the
   [latest release](https://github.com/MoHadiShibli/Control4Free/releases/latest).
2. Get it onto the PS4, either way:
   - copy it to a USB drive formatted exFAT or FAT32 and plug it in; or
   - turn on GoldHEN's FTP server and upload it to `/data/pkg/`.
3. On the PS4, open **Settings → Debug Settings → Game → Package Installer**, choose the USB drive or the
   internal storage, and install **Control4Free**.

The app appears in **Library → Applications**.

## First start

Open **Control4Free** and press **Cross**. The app:

1. copies the service to `/data/payloads/control4free.elf`;
2. adds it to GoldHEN's AutoRun list, `/data/GoldHEN/payloads.ini`, so GoldHEN starts it every time it
   loads;
3. starts it straight away if GoldHEN's **PayLoader** is on. If it isn't, turn PayLoader on in GoldHEN's
   settings and press **Cross** again. AutoRun also starts Control4Free each time GoldHEN loads.

Once it's running, the app shows the address to open, for example `http://192.168.1.20:4264`, and a QR code
for your phone's camera. The PS4 also shows the address in a notification when the service starts.

AutoRun keeps other entries in `payloads.ini` as they are.

## After a restart

Run the GoldHEN jailbreak as usual. With auto-start on, Control4Free starts by itself and shows its address in
a notification, so you don't need to open the app. If you do open it and press **PS** to go back to the home
screen, that's fine too: the app doesn't get in the way of Control4Free.

## The app's buttons

| Button | What it does |
|---|---|
| **Cross** | Starts Control4Free when it isn't running. Until auto-start is on with this app's version, it sets that up first. |
| **Triangle** | Turns auto-start on or off. After you install a newer package, it updates the auto-start copy. |
| **Square**, then **Cross** | Stops Control4Free and disconnects every controller. |
| **Circle** | Closes the app. Control4Free keeps running without it. |

## Updating

1. Install the new package over the old one. Delete the old app first if the installer refuses.
   You don't need to stop Control4Free first: deleting or replacing the app leaves the running service and
   its auto-start copy alone.
2. Open Control4Free. It says the auto-start copy is older than the one in the app: press **Triangle** to
   update it.
3. The old version is still the one running. Press **Square**, then **Cross** to stop it, and **Cross**
   again to start the new one (this needs GoldHEN's PayLoader). Or restart the PS4 and run the jailbreak:
   auto-start then starts the new one.

Your controller layouts, keys and gamepad mappings live in each phone's or computer's browser, so they survive updates.

## Without the app

The service is an ordinary GoldHEN payload, so the app is optional.

**With GoldHEN's Payloader LaunchPad** (under **Utilities** in GoldHEN's menu):

1. Put `control4free.elf` from the release in `/data/payloads/` on the PS4, for example over GoldHEN's FTP
   server (port 2121).
2. In the LaunchPad, select `control4free.elf` to start it once, or press **Square** on it to add it to the
   AutoRun queue. The queue lives in `/data/GoldHEN/payloads.ini`:

   ```ini
   [AutoRun]
   /user/data/payloads/control4free.elf = 1
   ```

**From a computer**: send `control4free.elf` to GoldHEN's PayLoader on port 9090 with any payload sender.
It runs until the PS4 restarts.

## Uninstalling

1. In the app, press **Triangle** to turn auto-start off, then **Square** and **Cross** to stop the service.
2. Delete the app from the Library.
3. Delete `/data/payloads/control4free.elf`, and `/data/control4free/` if you want the logs gone too.

Do step 1 before deleting the app. Deleting the app alone doesn't stop the service, and GoldHEN keeps
starting it after every restart. If the app is already gone, either install it again and start from step 1,
or remove the `control4free.elf` line from `/data/GoldHEN/payloads.ini` by hand and restart the PS4.

## Files on the console

| Path | What it is |
|---|---|
| `/data/payloads/control4free.elf` | The copy of the service that AutoRun starts. |
| `/data/GoldHEN/payloads.ini` | GoldHEN's AutoRun list. The app adds or removes one line. |
| `/data/control4free/control4free.log` | The service's log, with the previous run in `control4free.log.previous`. |
| `/data/control4free/control4free.log.1` | The older part of the log, once it passes 1 MB. |
| `/data/control4free/instance.lock` | Keeps a second copy from starting next to the first. |
