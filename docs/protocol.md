# Protocol

The service uses TCP port 4264 for plain HTTP/1.1 and UDP port 4264 for WebRTC data channels. This page is
for anyone writing their own client or checking what the controller page does. Replies include
`protocol: 3`, an internal message-compatibility integer checked by the controller page, separate from
the application version and the WebRTC transport. The launcher HTTP API uses `api: 1`.
For the native input fields and feedback values Control4Free exposes, see
[Virtual DS4 controls and values](how-it-works.md#virtual-ds4-controls-and-values).

## HTTP

| Request | Answer |
|---|---|
| `GET /`, `GET /index.html`, `GET /?…` | The controller page, gzip-compressed, with a Content-Security-Policy. |
| `GET /manifest.webmanifest` | The web app manifest, for a home-screen shortcut. |
| `GET /icon-192.png` | The app icon. |
| `POST /rtc` | A WebRTC SDP answer to the offer in the request body, as `text/plain`. |
| `OPTIONS /rtc` | A saved-page preflight, below. |
| `GET /api/status`, `POST /api/stop` | The launcher API, below. |
| Anything else | `404`. |

Every request needs a `Host` header that is a literal IPv4 address or `localhost`, with or without a port;
anything else gets `403`. This keeps out domain names that resolve to the console (DNS rebinding).

## Establishing a WebRTC connection

1. Create an `RTCPeerConnection` with no STUN or TURN servers. Create the two channels below with
   `negotiated: true` before making the offer.
2. Create an SDP offer and set it as the local description. The ICE-lite service supplies its host
   candidate in the answer, so the page does not need to wait for local candidate gathering to finish.
3. POST the local description's SDP to `http://<PS4-address>:4264/rtc` with `Content-Type: text/plain`.
   This is raw SDP, not a JSON object. A `Content-Length` is required; `Transfer-Encoding` is refused.
   Headers and body together must fit the 8 KiB HTTP receive buffer.
4. Set the returned text as the remote description with type `answer`, then wait for both channels to open.

| Channel | ID | Options | Messages |
|---|---|---|---|
| `control` | 0 | `negotiated: true`, `ordered: true`; no partial-reliability limit | Requests and replies for selection, status, errors and other control methods. |
| `input` | 1 | `negotiated: true`, `ordered: false`, `maxRetransmits: 0` | Controller state `i`, motion `m`, link probes `e`, optional `latency` telemetry, and rumble `v`. |

No DCEP channel-opening exchange is used. The service is ICE-lite and returns one IPv4 host candidate on
UDP port 4264. The browser performs ICE checks, DTLS and SCTP establishment. DTLS certificate fingerprints
use SHA-256; the service checks the browser's certificate against its offer. No media, STUN discovery
service or TURN relay is involved. See [SECURITY.md](../SECURITY.md) for what the unencrypted HTTP exchange
means for authentication.

`POST /rtc` is refused with `403` when its `Origin` is other than `http://<the Host header>` or `null`;
an absent `Origin` is accepted. For `Origin: null`, the answer includes `Access-Control-Allow-Origin: null`.
`OPTIONS /rtc` accepts only `Origin: null` and returns `204`, allowing `POST`, `Content-Type` and private-network
access. Other endpoints do not inherit this CORS permission. Browser local-network permission rules can
still apply to a saved page.

Malformed offers get `400`; an oversized request gets `413`; a full or unavailable service gets `503`.
At most eight WebRTC peers can exist at once. An unfinished setup expires after 20 seconds, and an established peer is
closed after 10 seconds without incoming traffic. The page creates a new HTTP offer when reconnecting.

Channel messages are UTF-8 JSON strings, with an application limit of 4096 bytes. Input is unordered and
not retransmitted by the data-channel sender; it is not guaranteed to arrive. Control is reliable and
ordered while the connection remains usable. They share one SCTP association and its congestion limits;
separate channel ordering does not remove network delay or packet loss.

### Requests

```json
{"id": 7, "method": "claim", "params": [0]}
```

- `method`: a string.
- `params`: an array of up to 32 integers; required, even if empty.
- `id`: optional, an integer from 0 to 2^53−1. Requests with an `id` get a reply carrying the same `id`.
- `jsonrpc: "2.0"` is allowed and ignored. Any other key, or anything that isn't this shape, is answered with
  an `Invalid request` error.

Controllers are numbered 0 to 3 in the protocol and 1 to 4 on screen.
`<version>` in the examples is a placeholder for the service's version, read from `VERSION`.
Send methods on `control` unless their channel is identified below.

| Method | Params | Reply |
|---|---|---|
| `info` | `[]` | `{"version": "<version>", "protocol": 3, "pads": 4, "motion": true, "input": true, "latency": true}` |
| `status` | `[]` | A status object, below. |
| `claim` | the controllers this connection wants, for example `[0]` or `[0, 2]` | A status object, once every controller in the claim exists. |
| `i` | sequenced input, below, on `input` | None. |
| `m` | sequenced motion, below, on `input` | None. |
| `e` | `[sequence, clientTime]`, on `input` | The same parameters as an `e` message on `input`. |
| `latency` | `[probeSequence, rttMs]`, on `input`, when `info.latency` is true | None. Informational RTT for the PS4 tester. |
| `leave` | `[pad]` | A status object. |
| `stop` | `[]` | None; the service stops and the connection closes. |
| `ping` | `[]` | `{}` |
| `invite` | `[a, b, c, d, port]`, the address the page reached, or `[]` | `{"text": "http://192.168.1.20:4264/", "size": 29, "rows": […]}` |

**`claim`** is the whole set this connection wants: controllers missing from it that it owned are removed,
and free ones in it are created. A claim that needs a new controller is answered when the controller exists,
usually within a second; the service keeps serving every other connection in the meantime. It's refused,
changing nothing, if any controller in it belongs to another connection (`409`), if another controller is
being created at that moment (`409`), or if controllers can't be created (`503`).

**`i`** sends the complete state of one controller this connection owns. Input for any other controller is
ignored: input never claims a controller.

```
[pad, seq, buttons, lx, ly, rx, ry, l2, r2, fingers, (id, x, y) × fingers,
 historyCount, (eventSeq, eventButtons) × historyCount]
```

| Field | Range |
|---|---|
| `seq` | a nonnegative sequence number that increases for each input message for this controller |
| `buttons` | a bit mask, below |
| `lx`, `ly`, `rx`, `ry` | 0 to 255, 128 is centred; up and left are 0 |
| `l2`, `r2` | 0 to 255 |
| `fingers` | 0 to 2 touches on the touchpad |
| touch `id` | 0 to 127, stable while the finger stays down |
| touch `x`, `y` | 0 to 1919, 0 to 941 |
| `historyCount` | 0 to 4 recent button changes |
| `eventSeq`, `eventButtons` | the input sequence and complete button mask at that change |

Use integers exactly representable in JavaScript, at most 2^53−1. Keep button history in increasing sequence
order; each entry must precede the current `seq`. Once an input sequence has been applied, messages at or
below it are ignored. On a later accepted message, history entries newer than the previous applied
sequence are queued before its current state. History is not replayed before the first accepted state.

History carries button masks, not the original timestamps or analog positions. Recovery uses the current
message's sticks, triggers and touches. It can repair some isolated losses but cannot guarantee every press,
preserve its original timing, or reconstruct an analog sequence through a long loss burst. Retain a
periodic complete-state message so a final release can be repaired even when nothing changes afterwards.

| Button | Bit | Button | Bit |
|---|---|---|---|
| Share | `0x0001` | L2 | `0x0100` |
| L3 | `0x0002` | R2 | `0x0200` |
| R3 | `0x0004` | L1 | `0x0400` |
| Options | `0x0008` | R1 | `0x0800` |
| D-pad up | `0x0010` | Triangle | `0x1000` |
| D-pad right | `0x0020` | Circle | `0x2000` |
| D-pad down | `0x0040` | Cross | `0x4000` |
| D-pad left | `0x0080` | Square | `0x8000` |
| PS | `0x10000` | Touchpad click | `0x100000` |

**`m`** sends the motion sensors of one controller this connection owns. Like `i`, it is ignored for any
other controller.

```
[pad, seq, qx, qy, qz, qw, ax, ay, az, gx, gy, gz]
```

| Field | Range |
|---|---|
| `seq` | an increasing nonnegative motion sequence, separate from `i` |
| `qx`, `qy`, `qz`, `qw` | orientation as a quaternion, times 10000: −10000 to 10000 |
| `ax`, `ay`, `az` | acceleration in thousandths of a G: −16000 to 16000 |
| `gx`, `gy`, `gz` | angular velocity in thousandths of a radian per second: −35000 to 35000 |

The axes are the ones the PS4 reads a DualShock 4 in: x to the right, y up out of its face, z towards the
player. A controller lying flat and still reads `+1` G on y. The values go into every sample from then on,
together with the buttons, sticks and touches of the last `i`. Before the first `m`, and whenever the
controller goes neutral, it reports no motion: orientation
`0, 0, 0, 1` and everything else 0. Late or duplicate motion sequences are ignored. Motion messages do not
renew the controller-state input lease: continue sending `i` while the controller is in use. A service
that understands `m` says
`"motion": true` in its `info` reply.

Send the state whenever it changes, and at least once a second while the controller is in use; the page sends
complete-state keepalives after one second without a change. After 3 seconds without controller-state input the controller goes
neutral and shows as `paused`; after 15 it's removed. When a
connection closes, its controllers' buttons are released at once, and they're removed 15 seconds later unless
a connection claims them again. The page reconnects and tries to reclaim its retained controllers. PS4
logout or controller removal clears the selection instead. Input and motion sequence state is maintained
for the current owner; a different owner starts a new sequence context.

`e` probes are echoed over the input channel for round-trip measurements. The client time
is returned unchanged; it is not compared with a PS4 clock and does not impose an input freshness deadline.
The page sends a probe every two seconds while connected and calculates RTT using its own monotonic clock.

When `info.latency` is true, a client may send `latency: [probeSequence, rttMs]` after receiving an echo.
Use a nonnegative, increasing probe sequence no greater than 2^53−1 and an RTT from 0 to 60000 milliseconds.
The service accepts one report matching this connection's most recently echoed probe, within six seconds
of that echo; unmatched, repeated, expired or invalid reports are ignored. This optional diagnostic method
does not acknowledge controller input or renew its lease. The number is supplied by the client, not
independently measured or authenticated by the PS4.

While a claim is being answered, every controller in it is reserved: another `claim` from the same
connection, or one that wants any of those controllers, gets `409`, and so does `leave` from that connection.
Send one claim at a time. Creating all four controllers can take up to about 18 seconds.

**`stop`** is refused with `409` while another connection owns a controller.

**`invite`** returns a QR code of the page's address: `[]` uses the console's own address. `rows` holds one
hex string per row of modules, most significant bit first, `size` modules wide; add a light border of 4
modules around it when you draw it.

### The status object

```json
{
  "version": "<version>",
  "protocol": 3,
  "pads": [
    {"pad": 0, "name": "Controller 1", "user": "Alex", "enabled": true, "open": true, "connected": true, "clients": 1,
     "mine": true, "state": "ready", "uid": "1A2B3C4D", "color": [32, 96, 255], "reports": 5120, "error": 0}
  ]
}
```

| Field | Meaning |
|---|---|
| `user` | The name of the PS4 user signed in on it, or `""` (not signed in, or not known yet). |
| `open` | The virtual controller exists. |
| `connected` | A connection owns it and is sending input. |
| `clients` | 1 if a connection owns it, else 0. |
| `mine` | This connection owns it. |
| `state` | `free`, `connecting`, `select` (waiting for a user to be chosen on the PS4), `ready` (signed in), `paused` (no input lately, or nobody connected), or `error` (the console refused input). |
| `uid` | The signed-in user's ID in hex, or `unassigned-…` before sign-in. |
| `color` | The light bar's colour, brightened so its strongest channel is 255; the controller's own colour until one is set. |
| `reports` | Samples given to the console so far. |
| `error` | The console's error code for the last refused sample, or 0. |

### Messages from the service

PS4 user logout removes that user's virtual controllers. Their status becomes `free`, with `open`,
`connected` and `mine` false, `clients: 0`, and an empty `user`. Owners receive `[pad, 0, 0]` to stop rumble
and a disconnection message. Pages clear the affected selections instead of automatically claiming them.

- `{"method": "s", "params": <status>}` on `control`: the status, whenever something changes and at least once a second.
- `{"method": "v", "params": [pad, large, small]}` on `input`: rumble, 0 to 255 for each motor, sent to the controller's
  owner when it changes and when a connection takes the controller over. Nonzero rumble is repeated every
  200 ms. `[pad, 0, 0]` stops it; messages can be lost on this channel. The page stops rumble if no renewal
  arrives for 650 ms, including when a final stop message is lost.
- `{"method": "e", "params": [sequence, clientTime]}` on `input`: the reply to a link probe.
- `{"method": "error", "params": {"message": "…"}}`: a problem that isn't the reply to a request: a controller
  removed for sitting unused or PS4 user logout, a request that couldn't be parsed, or an error for a request sent without an
  `id`.

Errors in reply to a request:

```json
{"id": 7, "error": {"code": 409, "message": "Controller is in use on another device"}}
```

| Code | Meaning |
|---|---|
| `400` | The request or its values are invalid. |
| `404` | Unknown method: the page is newer than the service. |
| `409` | Someone else owns it, another controller is being created, or it isn't yours to `leave`. |
| `503` | Controllers can't be created now, the kernel log is busy, or the service is stopping. |

## The launcher API

For the app on the PS4. Both requests need the header `X-Control4Free-Launcher: 1`, and are refused with `403`
if they carry an `Origin`, an `Upgrade`, a body or `Transfer-Encoding`. A browser only sends a custom header
cross-origin after a CORS preflight, which these API endpoints never answer, so websites can't use them.

`GET /api/status` returns:

```json
{
  "application": "Control4Free", "api": 1, "version": "<version>",
  "controllers": 1, "stopping": false, "latency": true,
  "pad0User": 439041101, "pad0Rtt": 8, "pad0Age": 120,
  "pad1User": -1, "pad1Rtt": -1, "pad1Age": -1,
  "pad2User": -1, "pad2Rtt": -1, "pad2Age": -1,
  "pad3User": -1, "pad3Rtt": -1, "pad3Age": -1
}
```

The flat `padN` fields use zero-based slots. `padNUser` is the numeric PS4 user ID for an active,
signed-in controller with an owner and fresh input; otherwise it is `-1`. `padNRtt` is that owner's
latest accepted RTT, or `-1` when none is fresh. `padNAge` is the number of milliseconds since the service
echoed the measured probe, or `-1` with an unknown RTT. A page owning multiple controllers supplies one
connection RTT shared by those slots.

The service expires RTT measurements six seconds after their probe and requires a new probe after controller
neutralization or ownership changes. The launcher also accounts for the report's age and time since
its HTTP check. It shows every matching slot for the selected PS4 user: a fresh number, `measuring...`
without a fresh report, or no controller page when no slot matches. An unavailable service or one
without these optional fields produces `--`; it does not assign a network RTT to a local controller.

`POST /api/stop` returns `{"application": "Control4Free", "stopping": true}`.

`/api/stop` removes every controller, even ones in use, and the service exits a quarter of a second later.
