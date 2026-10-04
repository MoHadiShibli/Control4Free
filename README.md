# Control4Free

A GoldHEN payload for controlling the PS4 from a phone or PC, using touch,
keyboard, or an Xbox/other controller exposed by the browser's Gamepad API.
Virtual controllers use the PS4's native user-selection screen, so they work on
the home screen, at sign-in and in games.

Browser control, native user sign-in, gameplay, the launcher app, GoldHEN
AutoRun and recovery after rest mode are all confirmed on the development
console. Four controllers at a time; the console's own device limits apply.

See [CHANGELOG.md](CHANGELOG.md) for what is in a release, and
[SECURITY.md](SECURITY.md) for what being open on your network does and does not
mean.

## Requirements

- A PS4 running GoldHEN. Development console: firmware 10.01, GoldHEN 2.4b18.10.
  Starting Control4Free from GoldHEN itself (LaunchPad or AutoRun) needs 2.4b18.10
  or later; the launcher app and PC sending need PayLoader.
- Nothing connected to GoldHEN's klog viewer while you add a controller.
  Control4Free reads the kernel log itself to recover a new device's handle, and
  the log has a single reader.
- A phone or PC on the same network.
- Turn off other GoldHEN controller plugins in games you play with Control4Free. A
  plugin that takes over a signed-in user's controller can stop games from reading
  the virtual controllers.

## Install

1. Copy `build/Control4Free-<version>.pkg` to a USB drive (or to `/data/pkg/` over
   GoldHEN's FTP server) and install it with GoldHEN's Package Installer.
2. Open **Control4Free** from the home screen and press **Cross** once. The app
   adds Control4Free to GoldHEN's AutoRun (GoldHEN 2.4b18.10 or later), so GoldHEN
   starts it every time it loads. It also starts it right away if GoldHEN's
   PayLoader is on; otherwise restart the PS4 and run the jailbreak again.
3. Scan the QR code or enter the address on your phone or PC. Select a controller
   there and sign in through the PS4 screen.

The app stays useful afterwards: it shows whether Control4Free is running, its
address and QR code, and how many controllers are connected.

- **Cross** starts Control4Free when it is not running (PayLoader must be on).
- **Triangle** turns GoldHEN's auto-start on or off, or updates it after you
  install a newer package.
- **Square**, then **Cross**, stops Control4Free and disconnects its controllers.
- **Circle** closes the app. Control4Free keeps running without it.

### Without the app

GoldHEN's own **Payloader LaunchPad** (under **Utilities**) does the same job:

1. Put `control4free.elf` in `/data/payloads/` on the PS4, for example through
   GoldHEN's FTP server (port 2121).
2. In the LaunchPad, select `control4free.elf` to start it, or press **Square** on
   it to add it to the AutoRun queue. The queue is `/data/GoldHEN/payloads.ini`:

   ```ini
   [AutoRun]
   /user/data/payloads/control4free.elf = 1
   ```

When you update Control4Free this way, replace `/data/payloads/control4free.elf`.

**Upgrading from the original test payload:** use **Stop Control4Free** in the
controller page's menu, or restart the PS4. That old version has no launcher API,
so the app will not start a second copy over it.

**Rest mode:** the service closes stale connections and rebuilds its
listener after socket failures or a long pause. Queued input is discarded, and
disconnected controllers report neutral input. Reopen the controller page and
select your controller after waking; an unused controller is removed after the
reconnect grace period. This can recover a surviving payload's network service;
it cannot revive a host process that the console terminated or stopped running.
When upgrading a stuck older instance, restart the PS4 and load GoldHEN again.

**Kernel logs:** controller sign-in is detected in the PS4's kernel log, which
has a single reader. Control4Free opens `/dev/klog` only while a controller is
waiting for sign-in and releases it afterwards, so GoldHEN's klog server works
the rest of the time. It does not use GoldHEN's klog stream: GoldHEN serves one
client at a time and, after one leaves, can go minutes without serving the next.
If a klog viewer is connected to GoldHEN when you add a controller, the page
says so; close the viewer and try again.

Diagnostics are saved at `/data/control4free/control4free.log`, with elapsed timestamps
such as `[c4f] [+00:01:23.456]`; the PS4's calendar setting is not used. Starting
a new instance preserves the last run in `control4free.log.previous`. A heartbeat every
minute and explicit network recovery messages help locate any remaining hang.

## Use

1. Stop any older Control4Free payload before loading another. They all use port
   4264.
2. Start Control4Free: from GoldHEN (above), from the launcher app, or by sending
   `build/control4free.elf` from a PC to GoldHEN's PayLoader on port 9090 with a
   payload sender. The payload serves its own page; no PC web server is needed.
3. Open `http://YOUR-PS4-IP:4264` in the phone or PC browser. Control4Free shows
   the exact address on the TV when it starts. On a phone you can add the page to
   the home screen (**Share → Add to Home Screen** on iOS, the browser menu on
   Android) and skip typing it next time.
4. Select a free controller. This creates it and opens native PS4 user selection.
   Use Left/Right and Cross to select a user or follow the PS4's guest flow.
   **Do not press PS while the initial user-selection screen is open:** in the
   hardware experiment this cancelled selection. PS works after sign-in.
5. Test the home screen and then a game before adding another controller.

For an Xbox or other physical controller, connect it to the phone/PC, press a
button so the browser detects it, then choose its virtual-controller slot under
connected controllers. Sign it in through the same PS4 screen. Add and sign in
controllers one at a time. Touch and keyboard can also operate the selected slot.

The corner menu provides local layout/keyboard preferences, **Disconnect
controller**, and **Stop Control4Free**. PS and Share send actual controller
buttons. The PS4 assigns users; there are no user settings on the page.

Some browsers restrict Gamepad API access on an HTTP page. If the page reports
that restriction, try opening a saved copy of `client/index.html` locally and
entering `YOUR-PS4-IP:4264` in its connection settings. Browser support for local
files varies. Keep the controller page in the foreground while playing.

## Troubleshooting

**"Signing a controller in needs the PS4's kernel log, and something else has
it."** A klog viewer is connected to GoldHEN. Close it and select the controller
again. Only controller sign-in needs the log, so this never interrupts play.

**"Control4Free cannot add controllers until it is restarted."** The PS4 did not
report a new device, so Control4Free stops adding more rather than leave devices
behind that it can no longer address. Stop it in the app (Square, then Cross) and
start it again with Cross, or restart the PS4.

**"Another controller is being connected; try again in a moment."** Controllers
are created one at a time. Wait a second and select yours again; everyone already
playing is unaffected.

**The page does not load.** Check the address on the TV, that the phone or PC is on
the same network, and that `http://` is used rather than `https://`. If the browser
corrects it to a search, type the address with `http://` in front.

**The browser says it cannot use controllers on this page.** Some browsers only
allow the Gamepad API on secure pages. Touch and keyboard still work. For a
physical pad, save a copy of the controller page to the device, open the saved
file, and enter the console address in its connection box.

**A game ignores the virtual controller.** Turn off other GoldHEN controller
plugins for that game. A plugin that takes over a signed-in user's controller can
stop games from reading Control4Free's.

**Nothing happens after waking the PS4 from rest mode.** Rest mode signs everyone
out, so controllers have to be selected again. Reopen the page and pick yours. If
the page will not connect at all, start Control4Free again from the app.

**The app says Control4Free is not responding.** Wait a few seconds after waking,
then press Cross again. If it stays stuck, restart the PS4 and run the jailbreak.

The payload's own log is at `/data/control4free/control4free.log` on the console,
readable over GoldHEN's FTP server, with the previous run kept beside it.

## Connection behavior

- Opening the page does not create a controller. Creation follows an explicit
  controller selection; the payload never presses sign-in buttons automatically.
- Each virtual slot has one browser owner. Multiple local inputs may share that
  browser's selected slot, or use separate slots.
- A lost connection releases held buttons. After 3 seconds without updates,
  inputs become neutral; after 15 seconds the controller is removed. A short
  disconnect leaves a 15-second window to select that controller again.
- Explicit disconnect removes the controller immediately. Stopping the payload
  removes all controllers and restores the saved host credentials. Browser Stop
  refuses while another browser owns a controller. The launcher's confirmed Stop
  can disconnect all controllers through its launcher API, which websites cannot use.
- If the kernel log is not available yet when Control4Free starts (AutoRun can
  start it early), it connects to it when the first controller is created.
- There is no time limit. Rebooting the console ends it; AutoRun, the launcher or
  a PC starts it again.

## Build

Everything builds in Docker, so nothing has to be installed on the machine. CI
(`.github/workflows/build.yml`) runs exactly these steps and attaches the payload,
the package and their SHA256 sums to each tagged release.

The [ps4-payload-sdk](https://github.com/ps4-payload-dev/sdk) toolchain is pinned
in the Docker image:

```sh
docker build -t control4free-build docker/
docker run --rm -v "$PWD:/src" -w /src control4free-build make
```

Output: `build/control4free.elf`, with the compressed page embedded. The version
comes from the `VERSION` file at the top of the repository.

To build the installable launcher after the payload image is available:

```sh
docker build -t control4free-launcher-build -f docker/Dockerfile.launcher docker/
docker run --rm -v "$PWD:/src" -w /src control4free-launcher-build bash -lc 'make && make -C launcher'
```

The host test suites build the real sources with the PS4 calls stubbed, so they
need no console:

```sh
docker run --rm --network none -v "$PWD:/src" -w /src control4free-launcher-build     python3 -B tests/run.py
```

Output: `build/Control4Free-<version>.pkg` (title ID `CFRE00001`). On Windows,
`tools/build-launcher.ps1` runs both image builds and the package build. It does
not send anything to a console. Packaging uses OpenOrbis and LibOrbisPkg. The
launcher draws its screen and its icon in software, in the controller page's style.

## Current limits

Rumble, lightbar feedback and motion input are not implemented. Multiple native
user sessions are unverified. User-assignment status comes from kernel-log events
and may lag or miss an event; check the TV.
Logs are written to `/data/control4free/control4free.log` on the console.

## Credits and license

The VDA code is ported from [seregonwar/SplashDown](https://github.com/seregonwar/SplashDown).
The build uses John Törnblom's [ps4-payload-sdk](https://github.com/ps4-payload-dev/sdk)
and the [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain).
[Ghostcontrol](https://github.com/srbraboo/Ghostcontrol-PS5-USB-Controller-Patcher)
was a research reference. JSON parsing uses [jsmn](https://github.com/zserge/jsmn);
the launcher uses [QR Code generator](https://github.com/nayuki/QR-Code-generator),
[stb_truetype](https://github.com/nothings/stb) and the
[Roboto](https://github.com/googlefonts/roboto) font.

GPL-3.0; see [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md).
