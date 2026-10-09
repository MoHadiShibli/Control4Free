# HDMI-CEC navigation — development build

The development app includes a TV-remote input reader and an **Actions** menu.
PS4 hardware testing is still pending; this is not part of the already-published release.
It addresses [issue #3](https://github.com/MoHadiShibli/Control4Free/issues/3), where a Samsung
TV remote navigates the PS4 home screen but stops responding inside Control4Free.

## Using the remote

Enable **Settings → System → Enable HDMI Device Link** on the PS4 and HDMI-CEC on the TV.
The setting's name depends on the TV model. First check that the
remote's arrows and OK can navigate the PS4 home screen. TV model and connection-chain
compatibility varies; see [Sony's HDMI Device Link guide](https://manuals.playstation.net/document/en/ps4/settings/devicelink.html).

In the development app:

1. Press **OK** to open **Actions**.
2. Use the arrows to choose an action, then press **OK** again. The menu includes setup/start,
   auto-start, Stop and Close when those actions are available.
3. **Stop** opens a separate confirmation with **Cancel** selected. Choose **Stop**, release
   OK, then press it again to confirm. Stopping disconnects every virtual controller.
4. **Back**, when the PS4 exposes it, returns from the menu or cancels confirmation. **Resume**
   also returns without needing a Back button. **Close app** leaves the service running.

Existing DualShock 4 shortcuts work outside the menu. Its D-pad can also open and navigate
Actions in the normal app. Service operations finish before another operation or Close is
available. Release held buttons after opening the app, reconnecting or returning to it.

In the diagnostic app, **Left/Right** change pages and **Up/Down** scroll while the menu is
closed. **OK** opens Actions, which also includes **Capture kernel log**. Menu arrows move
the selection without changing the page underneath it. The Summary page reports DS4 and
TV-remote input status; a ready source shows its received button mask, and a failed source
shows the native error code.

## Test before release

Use the locally built normal and diagnostic packages. Record the PS4 firmware, GoldHEN
version, TV model and any receiver/switch between the TV and console. Verify:

- App launch and arrows/OK with physical DS4s disconnected, then with a DS4 connected.
- Setup/start, auto-start on/off, updating an older auto-start copy, Stop confirmation,
  Resume and Close. Holding OK must not activate an action twice.
- Diagnostic page navigation, scrolling and action selection.
- Switching the TV's HDMI input away and back, returning from the PS4 home screen, and rest/wake.
- Recovery after the remote becomes unavailable; a held button must be released before it acts again.

TV reception remains unverified until these checks pass on a PS4. The app reads the system's
remote-control stream independently of DS4 input; it does not change the console's HDMI
settings, create virtual controllers, or forward TV keys to games or browser controllers.
Media/color keys and TV power/volume control are outside this implementation.
