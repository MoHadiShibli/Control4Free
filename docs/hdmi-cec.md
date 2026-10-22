# HDMI-CEC navigation

Use your TV remote to start and stop Control4Free, manage auto-start, and navigate the PS4 app.
The normal and diagnostic packages include an **Actions** menu for remotes.

## Set up

Enable **Settings → System → Enable HDMI Device Link** on the PS4 and HDMI-CEC on the TV.
The setting's name depends on the TV model. First check that the
remote's arrows and OK can navigate the PS4 home screen. TV and receiver/switch compatibility
varies; see [Sony's HDMI Device Link guide](https://manuals.playstation.net/document/en/ps4/settings/devicelink.html).

To start the service directly from the app, enable **GoldHEN → Server Settings → Enable
PayLoader Server**. If the app says PayLoader did not answer, enable it and retry Start.
The app cannot enable this GoldHEN setting automatically. AutoRun can start the service
after a restart without the server; see [Installation](installation.md).

## Using the remote

1. Press **OK** to open **Actions**. A Menu key can also open it if exposed by the PS4.
2. Use the arrows to choose an action, then press **OK** again. The menu includes setup/start,
   auto-start, **Test controllers**, Stop and Close when those actions are available.
3. **Stop** opens a separate confirmation with **Cancel** selected. Choose **Stop**, release
   OK, then press it again to confirm. Stopping disconnects every virtual controller.
4. **Back**, when the PS4 exposes it, returns from the menu or cancels confirmation. **Resume**
   also returns without needing a Back button. **Close app** leaves the service running.

Existing DualShock 4 shortcuts work outside the menu. Its D-pad can also open and navigate
Actions in the normal app. Service operations finish before another operation or Close is
available. Release held buttons after opening the app, reconnecting or returning to it.

## Controller test

Choose **Actions → Test controllers**. The live picture and values show input received for the selected
PS4 user. Press **OK** for the controller-tools grid. Arrows move between its icons and wrap at the
edges; **OK** runs the selected tool. **Switch user** cycles through signed-in users. Tools include
rumble, light-bar colors and orientation reset; unavailable ones are dimmed and explain why.

**Resume** or **Back** returns to live input. **Leave test** returns to the launcher; Back from the live
test does the same. **Controller page RTT** shows the connection reading for that user's virtual
controllers when available. See [Testing controllers](playing.md#testing-controllers-on-the-ps4).

## Diagnostic app

**Left/Right** change pages and **Up/Down** scroll while the menu is
closed. **OK** opens Actions, which also includes **Capture kernel log**. Menu arrows move
the selection without changing the page underneath it. The Summary page reports DS4 and
TV-remote input status; a ready source shows its navigation button mask, and a failed source
shows the native error code. It also retains the last active remote sample's key code and raw
button mask, so a short press remains visible after you release it. These values describe
the PS4's input data; they are not HDMI-CEC wire command numbers.

If a remote fails inside the app, press and release the affected button, then photograph
Summary or save its report. See [Reporting a problem](troubleshooting.md#reporting-a-problem).

## Confirmed setup and limits

| TV | Remote | PS4 firmware | GoldHEN | Confirmed behavior |
|---|---|---|---|---|
| Samsung UN55NU7090P | AA59-00818A (TM1240) | 11.02 | v2.4b18.12 | OK, arrows, RETURN/Back and service startup with PayLoader Server enabled |

This setup was [confirmed by NotDev-Kike](https://github.com/MoHadiShibli/Control4Free/issues/3#issuecomment-6103983733)
with no physical DS4 connected. Other TV/remote models, simultaneous DS4 use, HDMI input
switching and rest/wake behavior still need hardware reports. Held-button handling, recovery
and Stop confirmation have automated tests; their hardware behavior was not separately reported.

The app reads the PS4's remote-control stream independently of DS4 input. TV keys control
the app only. Media/color keys and TV power/volume control are outside this implementation.

For another setup, report the TV and remote models, PS4 firmware, GoldHEN version, any
receiver/switch between TV and console, and which buttons work. Diagnostic screenshots help
identify inputs the PS4 exposes.
