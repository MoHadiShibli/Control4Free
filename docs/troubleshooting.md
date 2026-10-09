# Troubleshooting

Find the message you see, or the symptom, below. If nothing here helps,
[open an issue](https://github.com/MoHadiShibli/Control4Free/issues/new/choose) with a diagnostic report, as
described at the [end of this page](#reporting-a-problem).

## On the controller page

**"Signing a controller in needs the PS4's kernel log, and something else has it."**
A klog viewer is connected to GoldHEN's log server (port 3232). Close it and pick the controller again. A
diagnostic app from a test build before 1.1.1 can also hold the log while it sits in the background: close it
(press **PS**, highlight the app, press **OPTIONS**, choose **Close Application**) and update. Only signing a
controller in needs the log, so this never interrupts play.

**"Another controller is being connected; try again in a moment."**
Controllers are added one at a time. Wait a second and pick yours again. Everyone already playing carries on
undisturbed.

**"Control4Free cannot add controllers until it is restarted."** or
**"The PS4 did not report the new controller."**
The PS4 didn't confirm a new controller, so one may exist that Control4Free can't address. Rather than leave
more of those behind, it stops adding controllers. Stop it in the app (**Square**, then **Cross**) and start
it again with **Cross**, or restart the PS4.

**"Controller is in use on another device."**
Someone else has that controller. Pick a free one. If it's yours from a phone that has gone, it frees itself
15 seconds after that phone stopped sending input.

**"That controller was disconnected after sitting unused."**
Nothing drove it for 15 seconds, for example because the phone's screen locked. Pick it again. To keep the
phone awake while you play, leave **Keep the screen on** turned on in the settings.

**"Someone else is still using a controller."**
The page's **Stop Control4Free** won't cut other players off. Ask them to disconnect, or stop it from the app
on the PS4.

**"Control4Free on the PS4 is older than this page."**
The page is newer than the service it's talking to, typically a saved copy of the page. Update the PS4 side:
see [Updating](installation.md#updating).

## The page doesn't open

- Use the address the app or the notification shows, including `:4264`.
- Type `http://`, not `https://`. If the browser turns the address into a search, put `http://` in front.
- The phone or computer must be on the same network as the PS4: guest Wi-Fi networks often keep devices
  apart.
- Check the app on the PS4 says **Running**. If it says **Not running**, press **Cross**.
- If the address worked before and doesn't now, the router may have given the PS4 a new one. Reserving an
  address for the PS4 in the router's settings keeps it the same.

## Picking a user

**The *"Who's using this controller?"* screen doesn't appear.** Wait a few seconds after picking the
controller. If it still doesn't, disconnect the controller from the page's menu and pick it again.

**The screen went away without a user.** PS cancels it. Pick the controller again and use only the D-pad and
Cross until you're signed in.

**The page keeps saying "Choose user on PS4" after signing in.** The page learns about sign-ins from the PS4's
kernel log. If a klog viewer was connected at that moment, it missed it. Play on: the controller works either
way.

## In games

**A game ignores the virtual controller.** Turn off other GoldHEN controller plugins for that game. A plugin
that takes over a signed-in user's controller can stop games from reading Control4Free's.

**Buttons feel slow.** The number next to the version at the bottom of the home screen is the round trip to
the console. Above about 30 ms, your Wi-Fi is the slow part: move closer to the router or use 5 GHz.

**No vibration from the game.** Check **Vibration** is on in the settings. Only Android phones can vibrate:
Safari on iPhone doesn't let pages do it. Not every controller supports vibration in the browser, and one
whose row says *no rumble in this browser* can't be rumbled from the page at all. Keep the page's window in
front. And not every game rumbles every player.

## Gamepads

**"This browser only allows gamepads on secure pages."** Save the page to the device, open the saved file, and
enter the console's address in its connection box. See [Gamepads](playing.md#gamepads).

**The buttons are mixed up.** The page says *"buttons may be mixed up"* when the browser doesn't recognise the
gamepad's layout. Press **Mapping** on its row and change each control that's wrong: see
[Gamepad mapping](gamepad-mapping.md). Another browser, or connecting the gamepad another way (cable instead
of Bluetooth, or the other way round), can also help.

**A gamepad button does nothing after changing its mapping.** Let go of it and press it again: a button held
down while a gamepad starts playing waits until it's released.

**The Xbox button presses PS for another controller in Chrome or Edge on Windows.** The browser's
Windows.Gaming.Input backend combines Xbox/Guide presses from several controllers and reports them on one
gamepad. Other buttons can still belong to the correct gamepad. Check **Mapping → Gamepad Controls** on
each device: if the wrong device's **B16** lights up, the browser has already lost the physical source.
Control4Free cannot recover that source from the reported input. See the
[Chromium implementation](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/device/gamepad/wgi_data_fetcher_win.cc#419).

On Windows 11, newer Chrome builds offer an experimental **Windows GameInput** backend with
per-device Guide-button callbacks. If your browser offers it, open
`chrome://flags/#enable-windows-gameinput-data-fetcher` (or search `edge://flags` in Edge), set it to
**Enabled**, and relaunch. Reconnect the gamepads and repeat the **Gamepad Controls** check: each Xbox
button should light **B16** only on its own gamepad. Confirm both PS buttons after assigning controllers.
This resolved combined Xbox/Guide reporting in a two-controller user test on Chrome 154 and Windows 11.
Controller models and connection modes were not recorded, and other input and haptic capabilities were
not revalidated. The option remains experimental; input counts and haptics may change. Compare
**Gamepad Controls**, including any extra paddles, and review saved profiles after changing the backend.
If gamepads stop appearing, return the flag to **Default**.
See Chromium's [backend selection](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/device/gamepad/gamepad_platform_data_fetcher.h)
and [per-device Guide handling](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/device/gamepad/gameinput_data_fetcher.cc).

GameInput is a workaround for this browser's multi-controller Guide reporting, not a general
Control4Free requirement. The controller page cannot select a Windows input backend or change browser
flags through the [Gamepad API](https://w3c.github.io/gamepad/). A permanent correction to this browser
path requires a browser fix; a future PC app could choose its own per-device reader. The existing mapper
can also use an independently reported button for PS.

In older browser builds that offer **Enable Windows.Gaming.Input**, open
`chrome://flags/#enable-windows-gaming-input-data-fetcher` (or search `edge://flags` in Edge), set it to
**Disabled**, and relaunch the browser. Reconnect the gamepads, select their controllers again, and check
both Xbox buttons independently. This switches those builds to per-device XInput; extra paddles, input
counts and haptic capabilities may change, so review saved mappings. The flag is unavailable in some
newer builds.

If these options are unavailable or the raw Guide input still crosses devices, try Firefox on Windows
or map an independently reported button to **PS button**
using **DS4 Mapping → PS button → Change**. That replaces the Xbox/Guide binding. Remapping or swapping
controller numbers alone cannot separate the combined Xbox-button signal.

**The gamepad does nothing.** Keep the page's window in front, press a button on the gamepad after the page
has loaded, and check it has a controller under **Gamepads on this device**.

## In the app on the PS4

**"PayLoader did not answer."** Turn on GoldHEN's PayLoader and press **Cross** again, or restart the PS4 and
run the jailbreak: with auto-start on, Control4Free starts by itself.

**"Control4Free is not responding."** Wait a few seconds after the PS4 wakes from rest mode, then press
**Cross** again. If it stays stuck, restart the PS4 and run the jailbreak.

**"Sent, but Control4Free did not start."** or **"The transfer to PayLoader broke off."** Press **Cross** to
try again. Two copies can't end up running: a second one quits at once. If it never starts, open an issue
with your firmware and GoldHEN versions.

**"Something answers on port 4264 but not the way this app expects."** or **"No answer the app
understands."** Something else is answering on port 4264, often an older version of Control4Free. Stop it
from its own page, or restart the PS4.

**"The app cannot reach Control4Free."** The app couldn't connect to the service from its own sandbox. Close
the app and open it again; if that doesn't help, restart the PS4 and run the jailbreak.

**"The bundled payload is damaged."** Reinstall the package.

**Control4Free is listed under Games instead of Applications.** That's a package from before 1.0.0. Delete the
app and install the current package.

## Reporting a problem

Every release comes with a diagnostic package, `Control4Free-<version>-diag.pkg`. It's the same Control4Free,
except that its app shows on screen what the PS4 reports, so you can send screenshots instead of digging out
log files. Please use it before you open an issue:

1. Download `Control4Free-<version>-diag.pkg` from the
   [latest release](https://github.com/MoHadiShibli/Control4Free/releases/latest) and install it over the app.
   It replaces the app and shows up as **Control4Free (diagnostic)**.
2. Open it. If it says RUNNING, the normal version is still the one running: press **Square**, then **Cross**
   to stop it. Then press **Cross** to start the diagnostic version, and wait until the top stops saying
   "WORKING" (up to 30 seconds).
3. Make the problem happen again, for example open the page on your phone or pick a controller. Then go back
   to the app.
4. Press **R1** to go through its 5 pages, and take a screenshot of each one: press **SHARE**, then
   **Triangle**. To copy them to a USB stick, open **Capture Gallery**, press **OPTIONS** and choose **Copy
   to USB Storage Device**.
5. [Open an issue](https://github.com/MoHadiShibli/Control4Free/issues/new/choose) and attach the
   screenshots.

The app also attempts to save the same report as text: `control4free-diag.txt` on a plugged-in USB stick, and
`/data/control4free/diag-report.txt` on the PS4. Its first page reports which saves succeeded. You can attach
a saved report instead of the screenshots. The report
shows your console's local network address. It only works inside your home network, but you can blur it.

When you're done, install the normal package again over the diagnostic one, open it, press **Square**, then
**Cross** to stop the diagnostic version, and **Cross** to start the normal one. That also puts the normal
one back on auto-start.

The service's own log is `/data/control4free/control4free.log` on the PS4, readable over GoldHEN's FTP
server (port 2121). Each active segment is bounded to 1 MiB: rollover moves it to `control4free.log.1` and
starts a new one, replacing the older segment. The last segment of the previous run is in
`control4free.log.previous`. The diagnostic app shows the end of each, and its first page says
whether it managed to save the report.
