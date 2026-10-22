# How it works

```
 phone / PC browser                     PS4
┌──────────────────┐    WebRTC     ┌───────────────────────────────────────────────┐
│ controller page  │ ────────────► │ Control4Free service (GoldHEN payload, :4264)  │
│ touch · keys ·   │  JSON, UDP    │   ├─ scePadVirtualDevice*  ─► virtual DS4 ×4   │
│ gamepads         │ ◄──────────── │   └─ /dev/klog (only while one signs in)       │
└──────────────────┘    status     │                                               │
                                   │ Control4Free app (home screen)                │
                                   │   └─ /api/status · /api/stop · AutoRun setup   │
                                   └───────────────────────────────────────────────┘
```

## Why a payload, not a plugin

GoldHEN loads plugins into game processes only. The home screen and the sign-in screen belong to the system's
own processes, which never load plugins, so a plugin can't reach them. Control4Free is a payload instead, and
uses Sony's virtual-device API: the one Remote Play goes through. A device made with it is an MBus device
that the system reads as a virtual DualShock 4, including on the home and sign-in screens. The individual
controller values and Control4Free's access to them are listed below.

## Virtual DS4 controls and values

These tables describe the virtual-controller values Control4Free exposes. Input goes into the
[`ScePadData` sample](../include/c4f_sce.h) passed to `scePadVirtualDeviceInsertData`; motor and light-bar
feedback comes back through `scePadVirtualDeviceGetRemoteSetting`.

The ranges below are accepted by Control4Free's [input parser](../src/web.c). Names such as `i.lx`,
`m.ax` and `v.large` identify positions in message arrays, not JSON object properties. See
[Protocol](protocol.md) for the exact array order.

### Buttons and analog controls

`0 / 1` means released / pressed. A pressed button sets its bit in `i.buttons`; several bits can be set
together. Sticks use `128` as neutral in Control4Free; low X is left and low Y is up.

| Individual value | Values | `ScePadData.buttons` bit, sent in `i.buttons` |
|---|---|---|
| D-pad Up | 0 / 1 | `0x0010` |
| D-pad Right | 0 / 1 | `0x0020` |
| D-pad Down | 0 / 1 | `0x0040` |
| D-pad Left | 0 / 1 | `0x0080` |
| Square | 0 / 1 | `0x8000` |
| Cross | 0 / 1 | `0x4000` |
| Circle | 0 / 1 | `0x2000` |
| Triangle | 0 / 1 | `0x1000` |
| L1 | 0 / 1 | `0x0400` |
| R1 | 0 / 1 | `0x0800` |
| L2 digital press | 0 / 1 | `0x0100` |
| R2 digital press | 0 / 1 | `0x0200` |
| L3, left-stick click | 0 / 1 | `0x0002` |
| R3, right-stick click | 0 / 1 | `0x0004` |
| Share | 0 / 1 | `0x0001` |
| Options | 0 / 1 | `0x0008` |
| PS | 0 / 1 | `0x10000` |
| Touchpad click | 0 / 1 | `0x100000` |

| Individual value | Accepted values | Native sample field / page field |
|---|---|---|
| Left stick X | 0–255 | `lx` / `i.lx` |
| Left stick Y | 0–255 | `ly` / `i.ly` |
| Right stick X | 0–255 | `rx` / `i.rx` |
| Right stick Y | 0–255 | `ry` / `i.ry` |
| L2 analog pressure | 0–255; 0 released, 255 fully pulled | `l2` / `i.l2` |
| R2 analog pressure | 0–255; 0 released, 255 fully pulled | `r2` / `i.r2` |

L2 and R2 each have a digital button bit and a separate analog-pressure field. A diagonal D-pad direction
sets two direction bits together.

### Touch and motion values

`touchData.fingers` selects how many contacts are active, up to two. Each contact has its own ID and
coordinates; touchpad click remains the separate button above.

| Individual value | Native sample field | Page field and accepted values |
|---|---|---|
| Active finger count | `touchData.fingers` | `i.fingers`: 0–2 |
| Finger 1 ID | `touchData.touch[0].finger` | First touch `id`: 0–127 |
| Finger 1 X | `touchData.touch[0].x` | First touch `x`: 0–1919 |
| Finger 1 Y | `touchData.touch[0].y` | First touch `y`: 0–941 |
| Finger 2 ID | `touchData.touch[1].finger` | Second touch `id`: 0–127 |
| Finger 2 X | `touchData.touch[1].x` | Second touch `x`: 0–1919 |
| Finger 2 Y | `touchData.touch[1].y` | Second touch `y`: 0–941 |

Motion fields in `ScePadData` are floating-point values. The service divides the message's quaternion
components by 10000 and its acceleration and angular-velocity components by 1000 before inserting them.

| Individual value | Native sample field and units | Page field and accepted values |
|---|---|---|
| Acceleration X | `accel[0]`, G | `m.ax`: −16000…16000 |
| Acceleration Y | `accel[1]`, G | `m.ay`: −16000…16000 |
| Acceleration Z | `accel[2]`, G | `m.az`: −16000…16000 |
| Angular velocity X | `gyro[0]`, rad/s | `m.gx`: −35000…35000 |
| Angular velocity Y | `gyro[1]`, rad/s | `m.gy`: −35000…35000 |
| Angular velocity Z | `gyro[2]`, rad/s | `m.gz`: −35000…35000 |
| Orientation quaternion X | `quat[0]` | `m.qx`: −10000…10000 |
| Orientation quaternion Y | `quat[1]` | `m.qy`: −10000…10000 |
| Orientation quaternion Z | `quat[2]` | `m.qz`: −10000…10000 |
| Orientation quaternion W | `quat[3]` | `m.qw`: −10000…10000 |

The neutral sample has orientation `[0, 0, 0, 1]` and zero acceleration and angular velocity.

### Motor and light-bar feedback

The service [decodes five feedback values](../src/feedback.c) requested by the PS4 game or system.
These travel from the PS4 to the page; the page protocol has no request for setting arbitrary feedback.
The native app's test tools can exercise the selected user's motors and light bar separately.

| Individual value | Decoded field and range | Page output |
|---|---|---|
| Large motor | `C4fPadFeedback.large`: 0–255; 0 stops it | `v.large`: 0–255 |
| Small motor | `C4fPadFeedback.small`: 0–255; 0 stops it | `v.small`: 0–255 |
| Light-bar red | `C4fPadFeedback.r`: 0–255 | Status `color[0]` |
| Light-bar green | `C4fPadFeedback.g`: 0–255 | Status `color[1]` |
| Light-bar blue | `C4fPadFeedback.b`: 0–255 | Status `color[2]` |

The `v` message preserves both 0–255 strengths. For a browser gamepad, the page divides each by 255
to use the [browser API's 0–1 scale](https://w3c.github.io/gamepad/); for example, 128 becomes about 0.502.
Linked Sony controllers receive the original strength bytes directly.

Red, green and blue are independent colour channels; for example, `[255, 255, 0]` combines red and green
to make yellow. The native tester's RGB selector adjusts each channel independently from 0 to 255.
The status colour preserves the RGB ratio but brightens its strongest channel to 255 for display.
When the feedback colour is all zero, the page keeps that controller's default colour.

### Other sample fields and current limits

The [native insertion code](../src/vda.c) sets `connected` and generates `timestamp` and `count`.
These are sample bookkeeping, not page controls. Extension, padding and unknown fields are zero-filled;
Control4Free assigns no controls to them.

Battery/charging status and headset detection are not fields in the virtual pad sample used here.
Control4Free also creates no audio device for speaker/headset playback or microphone capture, so
connecting a headset to a linked physical controller does not provide PS4 audio through Control4Free.

## Where it runs

GoldHEN's PayLoader doesn't start a process of its own: it runs the ELF inside an existing system process
(ScePartyDaemon on the development console). Control4Free:

1. takes an exclusive lock on `/data/control4free/instance.lock`, so a second copy (AutoRun plus the app, say)
   leaves without touching anything;
2. saves the host process's credentials, then raises them to the authority the virtual-device calls need. It
   restores exactly what it saved before it exits, and refuses to change them at all if it couldn't save them;
3. loads `libSceMbus` with `dlopen` before the first pad call. `libScePad`'s imports from it are unresolved in a
   payload, and calling into `libScePad` first can kill the process;
4. calls `scePadInit()`, then `scePadSetProcessPrivilege(1)`, in that order: the other way round the privilege
   call fails with *not initialised*;
5. serves the page and the WebRTC handshake on TCP port 4264, and handles the resulting controller
   connections on UDP port 4264 until it's stopped.

## Browser connection

The page creates two negotiated WebRTC data channels and posts its SDP offer to `POST /rtc`; the service
returns an SDP answer over HTTP. ICE checks establish the local UDP path, and DTLS encrypts the SCTP
connection. Channel 0 carries reliable, ordered controller selection, status and errors. Channel 1 carries
unordered input without retransmissions, rumble and link probes. Both channels share the connection's
congestion limits. The page reconnects with a new HTTP offer when the connection fails.

The service advertises its local address only, with no relay or internet service. Both TCP and UDP port
4264 must be reachable. The PS4 app still uses HTTP for its local status and stop requests. See
[Protocol](protocol.md) for the handshake and messages, and [Security](../SECURITY.md) for the limits of an
HTTP page with encrypted WebRTC channels.

## Creating a controller

`scePadVirtualDeviceAddDevice` returns a status, not a handle; on this console it returns `0x803b0006` even
when the device is created. The service obtains the handle, the device's MBus *DeviceId*, from the kernel
log, where the login manager announces every new device:

```
#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x7030d][type:1][subType:2]
```

So the service reads `/dev/klog` while it adds a device and accepts only that line, `subType:2` being the
Remote Play pad. Anything looser could hand it the DeviceId of a real controller plugged in at the same moment.
If no line arrives, it stops creating controllers until it's restarted, rather than leave devices behind that
it can no longer address.

The device is created for user 1, so it arrives with no user, and the PS4 asks *"Who's using this
controller?"*. The service never presses anything itself: the choice is made on the PS4. When a user is picked,
the login manager logs `DEVICE_OWNER_CHANGED [DeviceId:…][UserId:…]`, which is how the page learns the
controller is signed in.

UserService logout events and the current local login list are checked independently of the assignment
log. When a bound user signs out, their controllers are neutralized and removed, rumble is stopped, and
status clears the user and ownership. A failed login-list read leaves controllers alone; a newly assigned
user is first observed in the list before absence counts as logout. The browser clears affected gamepad
selections and returns to its home screen, requiring an explicit choice to reconnect.

Creating a controller is a small state machine the main loop advances: drain the log's
backlog, write a marker and wait for it to come back (proof that the reader really delivers), call
`AddDevice`, wait for the line. The other players keep playing and keep being answered meanwhile.

## The kernel log

`/dev/klog` has a single reader. GoldHEN's log server on port 3232 opens it only while it has a client, serves
one client at a time, and can go minutes without serving the next one after a client leaves. So the service
never uses the log server: it opens the device itself, only while a controller is signing in, and closes it
once every controller has a user, leaving the log to GoldHEN for the rest of the session.

## Input

The page sends the whole controller state as a compact array (see [protocol.md](protocol.md)). The service
turns it into a `ScePadData` sample and hands it to `scePadVirtualDeviceInsertData`:

- as soon as it arrives, unless a report went out in the last 4 ms;
- every 4 ms while input changed in the last half second;
- every 16 ms otherwise, to keep the controller alive;
- each sample carries a rising timestamp and counter, as a real pad's do;
- a bounded queue holds received button transitions between reports, while unchanged-button samples
  coalesce to the latest stick position.

Input includes a per-controller sequence number and up to four recent button changes. The service ignores
state older than the newest it applied and can recover missed button changes still present in that history.
Motion has its own sequence. Finite history cannot recover every press during a long loss burst, and
sequence numbers do not establish the age of packets held up by the network. A radio outage still delays
input, and recovered events cannot restore their original timing in a running game.

When a connection closes, its buttons are released at once. With no new controller-state input for 3 seconds
the controller goes neutral, and after 15 it's removed. A page reconnects automatically and tries to reclaim
controllers the service still retains, still signed in. Controllers removed after a timeout or PS4 logout
require a new selection.

Gamepad mapping happens on the page, before anything is sent. Each gamepad's inputs go through its profile's
bindings and calibration. Standard gamepads have a default layout; other devices can be mapped in the
editor, with profiles stored in the browser and imported or exported as JSON. Everything driving one
controller is combined: buttons are pressed if any source presses them, triggers take the strongest pull,
and sticks take the input pushed furthest. Linked Sony HID input keeps its analog trigger pressures
separate from its digital trigger flags. See
[Gamepad mapping](gamepad-mapping.md).

WebRTC connectivity checks and the page's probes keep the connection active. A peer from which nothing
arrives for 10 seconds is closed, so a device that left the network cannot hold a connection indefinitely.
At most eight WebRTC peers are connected at once; HTTP requests use separate bounded connection slots.

## Rumble and light bar

A game talks to a virtual controller the way it talks to a real one, and the system keeps what it asked for.
`scePadVirtualDeviceGetRemoteSetting` reads back the two motor strengths and light-bar red, green and blue
values decoded by Control4Free. The service reads it every 16 ms for each controller,
because games pulse rumble for only tens of milliseconds:

- a change in rumble is sent to the controller's owner, whose page buzzes the phone and rumbles any gamepad
  playing as that controller. Active rumble is repeated periodically to repair a missed message. Someone
  who takes a controller over is told its current state at once. The page stops rumble after 650 ms without
  a renewal, so a lost stop message cannot leave it running indefinitely;
- the light bar goes into the status every page sees. The PS4 sets player colours at a quarter of full
  brightness, so the page brightens them to full and keeps the hue.

## Rest mode and failures

The main loop watches both the monotonic clock and the wall clock. A gap of more than 5 seconds means the
console slept: the service closes every connection, releases the kernel log, rebuilds its listening socket,
and carries on. The same happens on a network failure. Users are signed out by rest mode anyway, so pages pick
their controllers again.

## The app

The app on the home screen is an OpenOrbis homebrew application that draws its own screen in software, in the
same style as the page. Before it does anything else it leaves the application sandbox through GoldHEN's SDK
call (syscall 500, command 2), because the sandbox refuses connections to the console itself, including
`127.0.0.1`. Then it:

- checks the service with `GET /api/status` every 2 seconds, and stops it with `POST /api/stop`;
- starts it by sending its own bundled copy of the payload to GoldHEN's PayLoader on port 9090;
- sets up auto-start by copying that payload to `/data/payloads/control4free.elf` and adding one line to the
  `[AutoRun]` section of `/data/GoldHEN/payloads.ini`, written to a temporary file and renamed over the
  original so a power cut can't leave it half written.

The native controller test reads signed-in users' controllers through `scePadReadState`. It samples input
on a separate worker with a 4 ms target interval, while the display requests 60 Hz. Static controller
artwork and the tools grid are cached; drawing does not block the input worker. The grid offers user
switching, orientation reset, motor tests and light-bar tests. Those output actions use the native pad
APIs for the selected user's controller.

The test's **Controller page RTT** line comes from the browser connection, independently of these local
pad reads. Every two seconds the page times an echoed input-channel probe and sends the result back to
the service. The app reads it through `/api/status`, matches virtual-controller slots to the selected
PS4 user and labels them C1 to C4. Reports expire after six seconds, and unavailable data is shown without
a number. RTT measures the browser-to-service round trip, including scheduling on that path; it does not
measure physical-controller input delay or the TV's display delay.

## Security

Being open on the home network is deliberate. What that does and doesn't protect is in
[SECURITY.md](../SECURITY.md).
