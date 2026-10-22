# Security

## Reporting a problem

Report anything you find through GitHub's private vulnerability reporting on this
repository (**Security → Report a vulnerability**), not in a public issue.

## What Control4Free assumes

Control4Free turns a phone or PC into a controller for your PS4 over your own
network. There is no pairing or password: a party guest can open a link and play.
The WebRTC channels are encrypted with DTLS. The controller page and connection
handshake are served over plain HTTP, so this does not authenticate the console
against an attacker who can modify traffic on your network.

**So treat the console's port 4264 the way you treat your TV remote.** Anyone who
can reach your PS4 over the network can:

- take a free controller and play, on the home screen, at sign-in and in games;
- press PS and Share, which reach the system menus;
- stop Control4Free, which disconnects every controller;
- see the names and IDs of the users signed in on the controllers, and the reported connection RTT.

Controller input can operate the PS4's user-selection screen, so someone on your
network can select an available user. The PS4's own login restrictions still apply.
Control4Free does not stream the screen.

Run Control4Free on a network you trust. Do not forward TCP or UDP port 4264 through your
router, and do not run it on open or guest Wi-Fi. When you are finished, stop it
from the launcher app or the controller page, or turn the console off.

## What is defended

Being open to your network is not the same as being open to the internet. These
are the limits the code does enforce.

**Websites are kept out, with one deliberate gap.** A page on the internet that you
visit can make your browser send requests to your PS4, so the service refuses the
ones that matter:

- A WebRTC offer posted to `/rtc` carrying another site's `Origin` is refused with
  403, so an ordinary page's script cannot establish the controller connection.
- The page is only served for a literal IP address or `localhost` in the `Host`
  header, so a domain name that resolves to your console (DNS rebinding) gets 403.
- The launcher's own `GET /api/status` and `POST /api/stop` require the header
  `X-Control4Free-Launcher: 1` and refuse any request that carries an `Origin` at
  all. A browser only sends a custom header cross-origin after a CORS preflight,
  which the launcher API never answers.
- The page is served with a Content-Security-Policy that keeps it to its own
  resources.

The gap: an offer with `Origin: null` is accepted, with CORS responses and a
preflight for `/rtc`. That is what a browser sends for a controller page opened
from a saved file, and the saved file is how gamepads
work in browsers that only allow the Gamepad API on secure pages. A website can
produce the same `Origin: null` from a sandboxed frame, so a page you have open
while on your home network could take a free controller and press buttons, as
anyone on your network can. It cannot take a controller someone is using or stream
your screen, but its input can operate the user-selection screen.

**One controller has one owner.** A slot is claimed by one WebRTC connection.
Another device asking for the same slot is refused (409), and input never claims a
slot implicitly. A client that leaves has its buttons released at once.

**Input is validated.** Button masks, axes, trigger values and touch coordinates
are range-checked before they reach the virtual device, and a request that is not
well-formed JSON of the expected shape is rejected rather than guessed at.

**Connections and buffers are bounded.** At most 4 controllers and 8 WebRTC peers
exist at a time. HTTP connections and transport queues also have limits, and an
HTTP request larger than the receive buffer is refused.

**WebRTC validates its peer.** ICE checks use the handshake credentials, and the
DTLS certificate must match the SHA-256 fingerprint in the offer. The browser
checks the answer's fingerprint too. This ties the encrypted connection to the
HTTP exchange; it does not add a player password or protect a modified HTTP page.

**Connection RTT is diagnostic data.** Each page reports the duration of its own
echoed probe. The service matches a recent probe from that connection, associates
the result only with controllers it owns, and expires old reports. A modified
client can still misreport its own RTT. The value does not grant ownership,
change input handling or act as an authentication signal.

**Stopping from the page is deliberate.** The page's Stop is refused while another
device owns a controller, so one player cannot cut another off by accident. The
launcher's `POST /api/stop` has no such guard, because the app on the console is
how you stop a service nobody is using any more. Its source address is not
checked: the app's own sandbox does not reliably appear as `127.0.0.1`, and anyone
on the network can already stop Control4Free from the page.

**The host process is put back.** GoldHEN runs the payload inside an existing
system process. Control4Free raises that process's credentials for the virtual
device calls and restores exactly what it saved before it exits, and it refuses to
touch them at all if it could not save them first.

## Versions

Fixes go into the next release; there is no separate maintenance branch. Use the
most recent release.
