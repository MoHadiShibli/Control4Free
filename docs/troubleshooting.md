# Troubleshooting

Find the message you see, or the symptom, below. If nothing here helps,
[open an issue](https://github.com/MoHadiShibli/Control4Free/issues/new/choose) with the log described at the
end of this page.

## On the controller page

**"Signing a controller in needs the PS4's kernel log, and something else has it."**
A klog viewer is connected to GoldHEN's log server (port 3232). Close it and pick the controller again. Only
signing a controller in needs the log, so this never interrupts play.

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

**The page keeps saying "Choose user on TV" after signing in.** The page learns about sign-ins from the PS4's
kernel log. If a klog viewer was connected at that moment, it missed it. Play on: the controller works either
way.

## In games

**A game ignores the virtual controller.** Turn off other GoldHEN controller plugins for that game. A plugin
that takes over a signed-in user's controller can stop games from reading Control4Free's.

**Buttons feel slow.** The number next to the version at the bottom of the home screen is the round trip to
the console. Above about 30 ms, your Wi-Fi is the slow part: move closer to the router or use 5 GHz.

**No vibration from the game.** Rumble isn't passed to the phone or gamepad yet. It's planned for the next
version.

## Gamepads

**"This browser only allows gamepads on secure pages."** Save the page to the device, open the saved file, and
enter the console's address in its connection box. See [Gamepads](playing.md#gamepads).

**The buttons are mixed up.** The page says *"buttons may be mixed up"* when the browser doesn't recognise the
gamepad's layout. Try another browser, or connect the gamepad another way (cable instead of Bluetooth, or the
other way round).

**The gamepad does nothing.** Keep the page's window in front, press a button on the gamepad after the page
has loaded, and check it has a controller under **Gamepads on this device**.

## In the app on the PS4

**"PayLoader did not answer."** Turn on GoldHEN's PayLoader and press **Cross** again, or restart the PS4 and
run the jailbreak: with auto-start on, Control4Free starts by itself.

**"Control4Free is not responding."** Wait a few seconds after the PS4 wakes from rest mode, then press
**Cross** again. If it stays stuck, restart the PS4 and run the jailbreak.

**"Sent, but Control4Free never answered."** or **"The transfer to PayLoader broke off."** Restart the PS4
before trying again. The app won't send a second copy until then, so two can't end up running.

**"Something answers on port 4264 but not the way this app expects."** or **"No answer the app
understands."** Something else is answering on port 4264, often an older version of Control4Free. Stop it
from its own page, or restart the PS4.

**"The app cannot reach Control4Free."** The app couldn't connect to the service from its own sandbox. Close
the app and open it again; if that doesn't help, restart the PS4 and run the jailbreak.

**"The bundled payload is damaged."** Reinstall the package.

**Control4Free is listed under Games instead of Applications.** That's a package from before 1.0.0. Delete the
app and install the current package.

## The log

The service writes a log to `/data/control4free/control4free.log` on the PS4, and keeps the previous run in
`control4free.log.previous`. Turn on GoldHEN's FTP server and download them from port 2121, or connect to
GoldHEN's log server on port 3232 and look for lines starting with `[c4f]`.

The log contains your console's network address. Remove it before posting the log if you prefer.
