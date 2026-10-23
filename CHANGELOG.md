# Changelog

## Unreleased

- **Brazilian Portuguese.** The controller page follows the browser's language, with a picker at the
  bottom of the home screen and in **Settings → This device**. The PS4 app follows the console's system
  language; **L1** on its main screen switches between automatic, English and Português (Brasil). The
  service's error messages are shown translated on the page.
- The PS4 app now draws accented letters (Latin-1).
- Updated the Xbox-button workaround for Chrome and Edge on Windows: from version 156 the GameInput option
  is hidden on the flags page. [Gamepad troubleshooting](docs/troubleshooting.md#gamepads) shows how to
  bring it back, and a launch option that works in any version.

## 1.3.0 — 2026-10-22

WebRTC connections, controller testing on the PS4, and new experimental controller features.

- **Rebuilt controller connections around WebRTC.** The old WebSocket connection delivered everything
  in order, so recovering a lost packet could hold up newer input. Input now uses an unordered channel
  without retransmissions, while controller selection and status use a separate reliable channel.
  This reduces delayed and bunched-up inputs during packet loss. After a brief disconnection, the page
  reconnects and restores its controller selection.
- **Controller tester in the PS4 app:** see buttons, sticks, analog triggers, touchpad, motion and
  connection latency; test rumble, choose light-bar colours with RGB sliders and reset orientation.
  Supports controllers and HDMI-CEC remotes. See [Testing controllers](docs/playing.md#testing-controllers-on-the-ps4).
- **DualShock 4 and DualSense touchpads now work when linked:** both finger positions and the click.
  Experimental Sony controller support also adds direct rumble and PS4 light-bar colours.
- **Experimental motion controls** use a phone's sensors or a linked DualShock 4 / DualSense.
- **Experimental trigger rumble** adds vibration to the trigger you're pulling on supported Xbox
  controllers and a linked DualSense. It follows the game's rumble; when both triggers are held, the
  last one pulled vibrates. See [Experimental features](docs/playing.md#experimental-features) for setup
  and how it works.
- Corrected the large and small rumble motors being swapped on connected gamepads.

Experimental features are off by default. Sony linking and motion need the
[Chrome / Edge setup](docs/troubleshooting.md#experimental-features).

**Updating:** update the PS4 service, then reload the controller page. Replace any saved copy with a
fresh one from the updated service.

## 1.2.1 — 2026-10-11

HDMI-CEC navigation for the PS4 app.

- Use your TV remote to control the PS4 app through HDMI-CEC. Press **OK** to open **Actions**,
  use the arrows to choose, and press **OK** again. **Back** returns; Stop has a separate confirmation.
  The diagnostic app also supports remote navigation and retains the last received remote key.
  See [HDMI-CEC navigation](docs/hdmi-cec.md).
- If the app cannot reach PayLoader, it tells you to enable **GoldHEN → Server Settings → Enable
  PayLoader Server** and retry Start. The setup message now fits on screen, and the installation
  guide explains when the server is needed.

Thanks to [NotDev-Kike](https://github.com/MoHadiShibli/Control4Free/issues/3#issuecomment-6103983733)
for testing and confirming OK, arrows, Back and service startup with a Samsung TV remote on firmware 11.02.

## 1.2.0 — 2026-10-09

Gamepad mapping.

![DS4 Mapping with an example gamepad](https://raw.githubusercontent.com/MoHadiShibli/Control4Free/v1.2.0/docs/images/mapping.png)

- Map a gamepad on a DualShock 4: each control shows what drives it and lights up when pressed.
  Click a control, then press the gamepad button for it. Map browser-exposed inputs from gamepads, wheels,
  guitars, pedals and arcade sticks to DualShock 4 buttons, sticks and triggers. Wheel and guitar model
  compatibility remains unverified, and native accessory identity is not provided. Sticks and
  triggers show their position with a dead zone and a curve. **Gamepad Controls** shows every button and
  axis live, and sets an axis's range a step at a time. See [Gamepad mapping](docs/gamepad-mapping.md).
- Keyboard keys are chosen on the same screen.
- Mappings are saved as named profiles in the browser. Save updates every connected gamepad using that
  profile; Save as new gives the selected gamepad a separate profile. Profiles can be exported to a file
  and imported on another device for review before saving.
- Several gamepads can play as the same controller, for example a wheel and its pedals.
- A phone that drops off the network no longer keeps a connection to the PS4 open for ever. Before, enough of
  those could stop new phones and computers from opening the page.
- When two people pick controllers at the same moment, one is asked to try again, instead of one taking the
  other's controller.
- A controller the console refused input for recovers once it accepts input again.
- Signing out of a PS4 user disconnects their virtual controllers and clears their names and browser
  selections. Other signed-in players stay connected.
- The mapping header aligns its controls, and gamepad rows group the device name, controller choice and
  Mapping button. The PS button uses the PlayStation mark and shows press feedback.
- Documented a Windows GameInput workaround for Xbox buttons crossing gamepads in affected Chrome/Edge
  builds. A two-controller Chrome/Windows 11 test confirmed it; it is not a general requirement. See
  [Gamepad troubleshooting](docs/troubleshooting.md#gamepads).
- A touch on the touchpad that the browser cancels no longer clicks it.
- Keys with Ctrl, Alt or the Windows key can't be chosen any more: they never worked in play.
- The app on the PS4 says when the Control4Free running is a different version from the app, and how to switch.
  It tells a missing auto-start copy apart from auto-start being off.
- Active service logs are bounded to two 1 MiB files. The last segment of the previous run is kept separately.
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
