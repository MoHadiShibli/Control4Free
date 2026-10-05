# Playing

## Connect a controller

1. Make sure Control4Free is running: the app on the PS4 says **Running** and shows the address.
2. On a phone, tablet or computer on the same network, open that address, for example
   `http://192.168.1.20:4264`, or scan the app's QR code.
3. Pick a free controller. The PS4 shows its *"Who's using this controller?"* screen.
4. Choose a user with the D-pad (**Left** and **Right**) and press **Cross**, or follow the guest option.

Connect one controller at a time, and finish the user screen before the next person picks theirs. **Don't
press PS while that screen is up**: it cancels the choice. PS works normally once you're signed in.

The home screen shows all four controllers live:

| State | Meaning |
|---|---|
| **Free** | Nobody is using it. Pick it to play. |
| **Connecting** | The PS4 is setting it up. Takes a moment. |
| **Choose user on PS4** | Waiting for someone to pick a user on the PS4's screen. |
| **Connected** | Signed in and ready. |
| **Paused** | Its phone stopped sending input, for example because the screen locked. |
| **This device** / **+1 device** | Who's driving it: this browser, or other ones. |

## Touch controls

The controller screen is a DualShock 4 laid out for your screen, best held sideways:

- **Sticks** follow your thumb. With **Floating sticks** on, a stick starts wherever your thumb lands.
- **The touchpad** takes one or two fingers. A quick tap clicks it; hold still to keep it pressed.
- **L3 and R3**: tap a stick, then touch it again straight away to press it in. If you'd rather have separate
  buttons, turn off **Double-tap a stick for L3 / R3**.
- **PS, Share and Options** are real controller buttons, so PS takes you to the PS4's home screen.

## Your own layout

Open the menu (**☰**) and choose **Edit the button layout**. Drag a button to move it, change its size, or
hide it. **Reset to default** puts everything back; tap it twice, so a stray tap can't throw a layout away. Each device keeps its own layout, separately for portrait
and landscape.

## Keyboard

On a computer the keyboard drives the controller open on the page. Change any key in **All settings →
Keyboard**, and turn on **Show keyboard keys** to see them on the buttons.

| Controller | Key | Controller | Key |
|---|---|---|---|
| Cross | Space | L1 / R1 | Q / E |
| Circle | Esc | L2 / R2 | 1 / 3 |
| Square | Backspace | L3 / R3 | Z / C |
| Triangle | Enter | Options | O |
| D-pad | I J K L | Share | V |
| Left stick | W A S D | PS | P |
| Right stick | Arrow keys | Touchpad click | U |
| Touchpad corners | T Y G H | | |

## Gamepads

Connect an Xbox, DualSense, Switch Pro or other gamepad to the phone or computer and press a button on it. It
appears under **Gamepads on this device**:

- a gamepad that turns up while a controller is open on the page drives that controller;
- otherwise, pick the controller each gamepad plays as. One computer can run all four controllers this way;
- a row lights up while you use its gamepad, so you can tell which is which.

Keep the page's window in front: browsers only read gamepads for the active window.

**"This browser only allows gamepads on secure pages."** Some browsers only offer gamepads on `https`
pages, and the console's page is plain `http`. Save the page to the device (**Save page as** on a computer),
open the saved file, and enter the console's address in its connection box. Touch and the keyboard work either
way.

## Settings

**☰ → All settings** has this device's settings. They stay in this browser.

| Setting | What it does |
|---|---|
| **Keep the screen on** | The phone's screen doesn't turn off while the page drives a controller. |
| **Vibration** | The phone buzzes when the game rumbles, and briefly when you press a button (Android). |
| **Floating sticks** | A stick starts wherever your thumb lands in its area. |
| **Tap the touchpad to click** | A quick tap presses the touchpad button. |
| **Double-tap a stick for L3 / R3** | Off: L3 and R3 get their own buttons. |
| **Show keyboard keys** | Labels the buttons with their keyboard keys. |

The bottom of the home screen shows the version, and the round trip to the console in milliseconds: a quick
way to see whether your Wi-Fi is the slow part.

## On your phone's home screen

Add the page to your home screen and it opens like an app:

- **iPhone and iPad (Safari)**: **Share → Add to Home Screen**. It opens without Safari's bars.
- **Android (Chrome)**: **⋮ → Add to Home screen**. Because the page is plain `http`, it opens in a Chrome
  tab.

The icon only works while Control4Free is running, and it remembers the PS4's address: reserve that address
for the PS4 in your router so it doesn't change.

**Full screen**: the **⛶** button in the corner. Safari on iPhone doesn't let pages go full screen, so there
it explains how to use the home screen instead.

## Rumble and light bar

When a game rumbles the controller, the device driving it feels it:

- **an Android phone** buzzes, with **Vibration** turned on in the settings. Safari on iPhone doesn't let pages
  vibrate the phone;
- **a gamepad** playing as that controller rumbles, if the browser can rumble it. Chrome and Edge rumble
  Xbox pads; many browsers can't rumble a DualSense, and Firefox can't rumble any. A gamepad the browser
  can't rumble says *no rumble in this browser* on its row.

A gamepad's own light bar stays as it is: browsers don't let pages change it.

The page also glows in the controller's **light-bar colour**: the player colour the PS4 gives each user, or
whatever colour the game sets. Until one is set, a controller shows its own colour (1 blue, 2 red, 3 green,
4 pink).

## Leaving

- **☰ → Disconnect controller** removes it from the PS4 straight away.
- **Closing the page or locking the phone** releases its buttons at once. After 3 seconds without input the
  controller goes idle, and after 15 seconds it's removed. Come back within 15 seconds and you keep it.
- **☰ → Stop Control4Free** stops the service and disconnects everyone. It's refused while someone else is
  still playing; the app on the PS4 can always stop it.

## Rest mode

Rest mode signs every user out, so controllers have to be picked again after the PS4 wakes up, as real ones
do. Control4Free itself carries on: reopen the page and pick your controller.
