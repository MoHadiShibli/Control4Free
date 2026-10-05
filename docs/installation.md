# Installation

Control4Free comes in two pieces:

- **The service** (`control4free.elf`), a GoldHEN payload that runs in the background, creates the virtual
  controllers, and serves the controller page on port 4264.
- **The app** (`Control4Free-<version>.pkg`), which goes on the PS4's home screen. It starts and stops the
  service, shows the address and a QR code, and sets up auto-start. The package carries its own copy of the
  service, so the package is all you need.

## Requirements

- A PS4 with [GoldHEN](https://github.com/GoldHEN/GoldHEN). Tested on firmware 10.01 with GoldHEN v2.4b18.10.
  Auto-start and GoldHEN's Payloader LaunchPad need v2.4b18.10 or later.
- **Debug Settings** turned on in GoldHEN, for the Package Installer.
- A phone, tablet or computer with a modern browser, on the same network as the PS4.

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
3. starts it straight away if GoldHEN's **PayLoader** is on. If it isn't, restart the PS4 and run the
   jailbreak: AutoRun starts Control4Free from then on.

Once it's running, the app shows the address to open, for example `http://192.168.1.20:4264`, and a QR code
for your phone's camera. The PS4 also shows the address in a notification when the service starts.

AutoRun keeps other entries in `payloads.ini` as they are.

## The app's buttons

| Button | What it does |
|---|---|
| **Cross** | Starts Control4Free when it isn't running. Until auto-start is on, it sets that up first. |
| **Triangle** | Turns auto-start on or off. After you install a newer package, it updates the auto-start copy. |
| **Square**, then **Cross** | Stops Control4Free and disconnects every controller. |
| **Circle** | Closes the app. Control4Free keeps running without it. |

## Updating

1. Install the new package over the old one. Delete the old app first if the installer refuses.
2. Open Control4Free. It says the auto-start copy is older than the one in the app: press **Triangle** to
   update it.
3. The old version is still the one running. Press **Square**, then **Cross** to stop it, and **Cross**
   again to start the new one (this needs GoldHEN's PayLoader). Or restart the PS4 and run the jailbreak.

Your controller layouts and keys live in each phone's browser, so they survive updates.

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

## Files on the console

| Path | What it is |
|---|---|
| `/data/payloads/control4free.elf` | The copy of the service that AutoRun starts. |
| `/data/GoldHEN/payloads.ini` | GoldHEN's AutoRun list. The app adds or removes one line. |
| `/data/control4free/control4free.log` | The service's log, with the previous run in `control4free.log.previous`. |
| `/data/control4free/instance.lock` | Keeps a second copy from starting next to the first. |
