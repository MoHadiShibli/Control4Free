# Changelog

Versions follow [Semantic Versioning](https://semver.org): PATCH for fixes, MINOR for features that break
nothing, MAJOR for anything people rely on that changes.

## 1.2.0 — 2026-10-08

Gamepad mapping.

- Map a gamepad on a picture of a DualShock 4: each control shows what drives it and lights up when pressed.
  Click a control, then press the gamepad button for it. It works for gamepads the browser doesn't know, and
  for wheels, guitars, pedals and arcade sticks, as DualShock 4 buttons, sticks and triggers. Sticks and
  triggers show their position with a dead zone and a curve. **Everything it sends** shows every button and
  axis live, and sets an axis's range a step at a time. See [Gamepad mapping](docs/gamepad-mapping.md).
- Keyboard keys are chosen on the same picture.
- Mappings are saved as profiles in the browser, used for every gamepad of the same kind, and can be exported
  to a file and imported on another device.
- Several gamepads can play as the same controller, for example a wheel and its pedals.
- A button still held when a gamepad starts playing does nothing until it's let go.
- A phone that drops off the network no longer keeps a connection to the PS4 open for ever. Before, enough of
  those could stop new phones and computers from opening the page.
- When two people pick controllers at the same moment, one is asked to try again, instead of one taking the
  other's controller.
- A controller the console refused input for recovers once it accepts input again.
- The page only asks for the PS4's address when it was saved to the device and opened from there.
- The address box takes a full address, such as `http://192.168.1.20:4264/`, and adds `:4264` when it's
  missing.
- A touch on the touchpad that the browser cancels no longer clicks it.
- Keys with Ctrl, Alt or the Windows key can't be chosen any more: they never worked in play.
- The app on the PS4 says when the Control4Free running is a different version from the app, and how to switch.
  It tells a missing auto-start copy apart from auto-start being off.
- The service's log stays under 2 MB, however long it runs.
- The diagnostic app says whether it managed to save its report.

## 1.1.1 — 2026-10-07

Fixes.

- Control4Free starts on firmware 13.52. Before, it stopped before doing anything on that firmware:
  GoldHEN said the payload launched, but the app said it never answered. The payload's start-up code now
  comes from a newer ps4-payload-sdk that knows 13.52's kernel.
- When a start fails, press **Cross** to try again. The app no longer asks you to restart the PS4: a second
  copy of Control4Free quits on its own, so two can't end up running.
- Each release now includes a diagnostic package, `Control4Free-<version>-diag.pkg`. Its app shows what the
  PS4 reports (firmware, GoldHEN, the logs, each connection from a phone or PC) on pages you can screenshot
  for a bug report. See [Reporting a problem](docs/troubleshooting.md#reporting-a-problem).

## 1.1.0 — 2026-10-06

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

Tested on firmware 10.01 with GoldHEN v2.4b18.10. Rumble, light bar and motion are not passed on.
