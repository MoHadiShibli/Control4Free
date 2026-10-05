# Changelog

Versions follow [Semantic Versioning](https://semver.org): PATCH for fixes, MINOR for features that break
nothing, MAJOR for anything people rely on that changes.

## 1.1.0 — 2026-10-05

Rumble and the light bar.

- When a game rumbles a controller, the device driving it feels it: an Android phone buzzes, and a gamepad
  playing as that controller rumbles too, though not every controller supports vibration in the browser. A
  gamepad that can't be rumbled says so on its row.
- The page glows in the controller's light-bar colour, the player colour the PS4 gives each user or whatever
  the game sets, brightened from the quarter strength the PS4 uses.
- Each controller shows the name of the PS4 user signed in on it.
- An **Invite** button shows the page's address as a QR code, so friends can scan it and join. The console
  makes the code, so the page still needs no libraries.
- The page says "Choose user on PS4" rather than "on TV": not everyone plays on a TV.
- Protocol: the status reports the light bar's colour in `color` and the signed-in user in `user`; the
  service sends rumble as `{"method": "v", "params": [pad, large, small]}`; a new `invite` method returns the
  QR code. See [docs/protocol.md](docs/protocol.md).

## 1.0.0 — 2026-10-05

First release.

Control4Free is a GoldHEN payload that turns a phone or PC into a PS4 controller that works everywhere: the
home screen, the sign-in screen and games. Up to four of them, through the PS4's own virtual-device API, with
the PS4's own *"Who's using this controller?"* screen deciding who each one is.

- A controller page the service serves itself on port 4264: a DualShock 4 on the screen, keyboard keys, or any
  gamepad the browser can see. A layout editor, key remapping and a per-gamepad controller picker, saved on
  each device.
- An app for the PS4's home screen, under Applications, that starts and stops Control4Free, shows its address
  and a QR code and the number of connected controllers, and sets up GoldHEN's AutoRun so the console starts
  it by itself after every restart.
- The page can be kept on a phone's home screen.
- Input is reported the moment it arrives, at a DualShock 4's own 4 ms rate while a player is doing something,
  and at a slow keepalive while nobody is.
- Connecting a controller never stops the other players being served.
- Recovers by itself after rest mode or a network failure. Controllers have to be picked again after rest
  mode, which signs every user out.
- Open on the local network by design, with no pairing. What that does and does not protect is written down in
  [SECURITY.md](SECURITY.md).

Tested on firmware 10.01 with GoldHEN v2.4b18.10. Rumble, light bar and motion are not passed on yet.
