# Protocol

The service listens on TCP port 4264 and speaks plain HTTP/1.1 and WebSocket (RFC 6455). This page is for
anyone writing their own client or checking what the page does.

## HTTP

| Request | Answer |
|---|---|
| `GET /`, `GET /index.html`, `GET /?…` | The controller page, gzip-compressed, with a Content-Security-Policy. |
| `GET /manifest.webmanifest` | The web app manifest, for a home-screen shortcut. |
| `GET /icon-192.png` | The app icon. |
| `GET /ws` with `Upgrade: websocket` | The control socket, below. |
| `GET /api/status`, `POST /api/stop` | The launcher API, below. |
| Anything else | `404`. |

Every request needs a `Host` header that is a literal IPv4 address or `localhost`, with or without a port;
anything else gets `403`. This keeps out domain names that resolve to the console (DNS rebinding).

## The control socket

`GET /ws` upgrades to a WebSocket. The handshake is refused with `403` when it carries an `Origin` other than
`http://<the Host header>` or `null`; no `Origin` at all is accepted. Messages are UTF-8 JSON text frames of at
most 4096 bytes; a longer frame closes the connection. Fragmented messages and ping/pong control frames are
handled.

### Requests

```json
{"id": 7, "method": "claim", "params": [0]}
```

- `method`: a string.
- `params`: an array of up to 16 integers; required, even if empty.
- `id`: optional, an integer from 0 to 2^53−1. Requests with an `id` get a reply carrying the same `id`.
- `jsonrpc: "2.0"` is allowed and ignored. Any other key, or anything that isn't this shape, is answered with
  an `Invalid request` error.

Controllers are numbered 0 to 3 in the protocol and 1 to 4 on screen.

| Method | Params | Reply |
|---|---|---|
| `info` | `[]` | `{"version": "1.1.0", "protocol": 2, "pads": 4}` |
| `status` | `[]` | A status object, below. |
| `claim` | the controllers this connection wants, for example `[0]` or `[0, 2]` | A status object, once every controller in the claim exists. |
| `u` | input, below | None. |
| `leave` | `[pad]` | A status object. |
| `stop` | `[]` | None; the service stops and the connection closes. |
| `ping` | `[]` | `{}` |
| `invite` | `[a, b, c, d, port]`, the address the page reached, or `[]` | `{"text": "http://192.168.1.20:4264/", "size": 29, "rows": […]}` |

**`claim`** is the whole set this connection wants: controllers missing from it that it owned are removed,
and free ones in it are created. A claim that needs a new controller is answered when the controller exists,
usually within a second; the service keeps serving every other connection in the meantime. It's refused,
changing nothing, if any controller in it belongs to another connection (`409`), if another controller is
being created at that moment (`409`), or if controllers can't be created (`503`).

**`u`** sends the complete state of one controller this connection owns. Input for any other controller is
ignored: input never claims a controller.

```
[pad, buttons, lx, ly, rx, ry, l2, r2, fingers, (id, x, y) × fingers]
```

| Field | Range |
|---|---|
| `buttons` | a bit mask, below |
| `lx`, `ly`, `rx`, `ry` | 0 to 255, 128 is centred; up and left are 0 |
| `l2`, `r2` | 0 to 255 |
| `fingers` | 0 to 2 touches on the touchpad |
| touch `id` | 0 to 127, stable while the finger stays down |
| touch `x`, `y` | 0 to 1919, 0 to 941 |

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

Send the state whenever it changes, and at least once a second while the controller is in use. After 3
seconds without input the controller goes neutral and shows as `paused`; after 15 it's removed. When a
connection closes, its controllers' buttons are released at once, and they're removed 15 seconds later unless
a connection claims them again. The page doesn't do that by itself after reconnecting: the player picks the
controller again.

At most six WebSocket connections are open at once; a seventh handshake gets `503`. The service pings each
connection every 5 seconds and closes it if the matching pong hasn't come back within 10. Browsers answer
pings by themselves; another client has to answer them too. Other messages don't count as an answer.

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
  "version": "1.1.0",
  "protocol": 2,
  "pads": [
    {"pad": 0, "name": "Controller 1", "user": "Alex", "enabled": true, "open": true, "connected": true, "clients": 1,
     "mine": true, "state": "ready", "uid": "1a2b3c4d", "color": [32, 96, 255], "reports": 5120, "error": 0}
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

- `{"method": "s", "params": <status>}`: the status, whenever something changes and at least once a second.
- `{"method": "v", "params": [pad, large, small]}`: rumble, 0 to 255 for each motor, sent to the controller's
  owner when it changes and when a connection takes the controller over. `[pad, 0, 0]` stops it.
- `{"method": "error", "params": {"message": "…"}}`: a problem that isn't the reply to a request: a controller
  removed for sitting unused, a request that couldn't be parsed, or an error for a request sent without an
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
cross-origin after a CORS preflight, which the service never answers, so websites can't use these.

```
GET /api/status   →  {"application": "Control4Free", "api": 1, "version": "1.1.0", "controllers": 2, "stopping": false}
POST /api/stop    →  {"application": "Control4Free", "stopping": true}
```

`/api/stop` removes every controller, even ones in use, and the service exits a quarter of a second later.
