# Changelog

Dates are the day the work landed. Versions follow [Semantic Versioning](https://semver.org):
PATCH for fixes, MINOR for features that break nothing, MAJOR for anything people
rely on that changes.

## 1.0.0 — unreleased

First release.

Control4Free is a GoldHEN payload that turns a phone or PC into a PS4 controller
that works everywhere: the home screen, the sign-in screen and games. Up to four
of them, through the PS4's own virtual-device API, with the PS4's own
"Who's using this controller?" screen deciding who each one is.

- A controller page the payload serves itself on port 4264. Touch controls shaped
  like a DualShock 4, keyboard keys, or any gamepad the browser exposes. A layout
  editor, key remapping and a per-controller picker, saved per device.
- An installable app for the PS4's home screen that starts and stops Control4Free,
  shows its address and a QR code, and sets up GoldHEN's AutoRun so the console
  starts it by itself after every restart.
- The page can be kept on a phone's home screen.
- Input is reported the moment it arrives, at a DualShock 4's own 4 ms cadence
  while a player is doing something, and at a slow keepalive while nobody is.
- Connecting a controller never stops the other players being served.
- Recovers by itself after rest mode or a network failure: controllers have to be
  picked again, which is what rest mode does to real ones too.
- Open on the local network by design, with no pairing. What that does and does
  not protect is written down in [SECURITY.md](SECURITY.md).

Tested on firmware 10.01 with GoldHEN 2.4b18.10. Rumble, light bar and motion are
not implemented.
