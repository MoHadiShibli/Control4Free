# How it works

```
 phone / PC browser                     PS4
┌──────────────────┐   WebSocket   ┌───────────────────────────────────────────────┐
│ controller page  │ ────────────► │ Control4Free service (GoldHEN payload, :4264)  │
│ touch · keys ·   │   JSON, LAN   │   ├─ scePadVirtualDevice*  ─► virtual DS4 ×4   │
│ gamepads         │ ◄──────────── │   └─ /dev/klog (only while one signs in)       │
└──────────────────┘    status     │                                               │
                                   │ Control4Free app (home screen)                │
                                   │   └─ /api/status · /api/stop · AutoRun setup   │
                                   └───────────────────────────────────────────────┘
```

## Why a payload, not a plugin

GoldHEN loads plugins into game processes only. The home screen and the sign-in screen belong to the system's
own processes, which never load plugins, so a plugin can't reach them. Control4Free is a payload instead, and
uses Sony's virtual-device API: the one Remote Play goes through. A device made with it is a real MBus device,
and the whole system sees it like a DualShock 4 plugged in over USB.

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
5. serves the page and the WebSocket on port 4264 until it's stopped.

## Creating a controller

`scePadVirtualDeviceAddDevice` returns a status, not a handle; on this console it returns `0x803b0006` even
when the device is created. The handle is the device's MBus *DeviceId*, and the only place a payload can learn
it is the kernel log, where the login manager announces every new device:

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

None of this blocks. Creating a controller is a small state machine the main loop advances: drain the log's
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
- every 4 ms while input changed in the last half second, as fast as a real DualShock 4 reports;
- every 16 ms otherwise, to keep the controller alive;
- each sample carries a rising timestamp and counter, as a real pad's do;
- a short queue keeps every button press and release, so a quick tap can't fall between two reports, while
  stick movement only ever keeps the latest position.

When a page stops sending, its buttons are released at once; after 3 seconds the controller goes neutral, and
after 15 it's removed. A page that reconnects within 15 seconds gets its controller back.

## Rumble and light bar

A game talks to a virtual controller the way it talks to a real one, and the system keeps what it asked for.
`scePadVirtualDeviceGetRemoteSetting` reads it back, in the layout of a DualShock 4's own output report: the
two motors, then the light bar's red, green and blue. The service reads it every 16 ms for each controller,
because games pulse rumble for only tens of milliseconds:

- a change in rumble is sent to the controller's owner, whose page buzzes the phone and rumbles any gamepad
  playing as that controller. Someone who takes a controller over is told its current state at once;
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

## Security

Being open on the home network is deliberate. What that does and doesn't protect is in
[SECURITY.md](../SECURITY.md).
