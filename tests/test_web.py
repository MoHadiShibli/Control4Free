"""Real transport + application tests; VDA calls are replaced by web_stub.c."""
import base64
import gzip
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/web-host-test'


def build():
    subprocess.run(['python3', 'tools/embed_client.py', 'client/index.html', 'build/client.c'], check=True)
    subprocess.run(['python3', 'tools/embed_file.py', 'build/assets.c',
                    'c4fManifest=client/manifest.webmanifest', 'c4fIcon=client/icon-192.png'], check=True)
    subprocess.run(['clang-18', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-Iinclude', '-Ivendor/jsmn', '-Ivendor/qrcodegen', '-DC4F_STALE_MS=400', '-DC4F_RELEASE_MS=1800',
                    'src/net.c', 'src/web.c', 'src/klog_line.c', 'vendor/qrcodegen/qrcodegen.c', 'tests/web_stub.c', 'build/client.c',
                    'build/assets.c', '-pthread', '-o', str(BINARY)], check=True)


class Client:
    def __init__(self, port=4264):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=3)
        self.buf = b''
        self.pushed = []   # messages the service sent on its own, oldest first
        key = 'dGhlIHNhbXBsZSBub25jZQ=='
        self.sock.sendall((f'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nOrigin: http://127.0.0.1:{port}\r\n'
                           f'Connection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        while b'\r\n\r\n' not in self.buf:
            self.buf += self.sock.recv(8192)
        head, self.buf = self.buf.split(b'\r\n\r\n', 1)
        assert b'101 Switching' in head, head
        assert b's3pPLMBiTxaQ9kYGzzhZRbK+xOo=' in head, head
        self.next_id = 1

    def exact(self, size):
        while len(self.buf) < size:
            chunk = self.sock.recv(8192)
            assert chunk, 'unexpected close'
            self.buf += chunk
        result, self.buf = self.buf[:size], self.buf[size:]
        return result

    def frame(self, payload, op=1, fin=True, masked=True):
        if isinstance(payload, str):
            payload = payload.encode()
        mask = b'abcd'
        head = bytes([(128 if fin else 0) | op, (128 if masked else 0) | (len(payload) if len(payload) < 126 else 126)])
        if len(payload) >= 126:
            head += struct.pack('!H', len(payload))
        self.sock.sendall(head + (mask if masked else b'') + bytes(b ^ (mask[i % 4] if masked else 0) for i, b in enumerate(payload)))

    def receive(self):
        while True:
            first, second = self.exact(2)
            size = second & 127
            if size == 126:
                size = struct.unpack('!H', self.exact(2))[0]
            assert not second & 128
            data = self.exact(size)
            if first & 15 == 9:
                self.frame(data, op=10)
                continue
            return first & 15, data

    def request(self, method, params=()):
        ident = self.next_id
        self.next_id += 1
        self.frame(json.dumps(dict(id=ident, method=method, params=list(params))))
        while True:
            op, text = self.receive()
            if op != 1:
                continue
            msg = json.loads(text)
            if msg.get('id') == ident:
                return msg
            if 'id' not in msg:
                self.pushed.append(msg)

    def wait_for(self, match, timeout=2):
        """The first pushed message `match` accepts, seen already or still to come."""
        end = time.monotonic() + timeout
        while True:
            for i, msg in enumerate(self.pushed):
                if match(msg):
                    del self.pushed[:i + 1]
                    return msg
            self.pushed.clear()
            assert time.monotonic() < end, 'the expected message never came'
            op, text = self.receive()
            if op == 1:
                msg = json.loads(text)
                if 'id' not in msg:
                    self.pushed.append(msg)

    def input(self, pad, buttons=0, lx=128, ly=128, rx=128, ry=128, l2=0, r2=0, touch=()):
        params = [pad, buttons, lx, ly, rx, ry, l2, r2, len(touch)]
        for t in touch:
            params.extend(t)
        self.frame(json.dumps(dict(method='u', params=params)))

    def close(self):
        self.sock.close()


class Server:
    def __init__(self, **options):
        readfd, self.log = os.pipe()
        os.set_blocking(readfd, False)
        self.trace = tempfile.TemporaryFile(mode='w+')
        env = dict(os.environ, **{'C4F_TEST_KLOG_FD': str(readfd), **options})
        self.p = subprocess.Popen([str(BINARY)], env=env, pass_fds=(readfd,), stdout=self.trace)
        os.close(readfd)
        for _ in range(100):
            try:
                self.c = Client()
                break
            except ConnectionRefusedError:
                time.sleep(.02)
        else:
            raise AssertionError('server not listening')

    def rows(self):
        self.trace.seek(0)
        return self.trace.read().splitlines()

    def close(self):
        self.c.close()
        if self.p.poll() is None:
            self.p.terminate()
        self.p.wait(timeout=3)
        if self.log is not None:
            os.close(self.log)
        self.trace.close()


def http(request):
    with socket.create_connection(('127.0.0.1', 4264), timeout=3) as s:
        s.sendall(request)
        parts = []
        while chunk := s.recv(65536):
            parts.append(chunk)
        return b''.join(parts)


def main():
    build()
    server = Server()
    try:
        response = http(b'GET / HTTP/1.1\r\nHost: 127.0.0.1:4264\r\n\r\n')
        headers, body = response.split(b'\r\n\r\n', 1)
        assert b'200 OK' in headers and b'Content-Security-Policy:' in headers
        assert gzip.decompress(body) == Path('client/index.html').read_bytes()
        assert not any(r.startswith('ADD') for r in server.rows()), 'page load created a controller'
        assert b'403' in http(b'GET / HTTP/1.1\r\nHost: evil.example:4264\r\n\r\n')
        assert b'403' in http(b'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:4264\r\nOrigin: https://evil.example\r\nUpgrade: websocket\r\n\r\n')
        assert b'404' in http(b'GET /../README.md HTTP/1.1\r\nHost: 127.0.0.1:4264\r\n\r\n')
        print('PASS embedded page, RFC handshake, origin/host/path checks, no automatic creation', flush=True)

        # Keeping the page on a phone's home screen needs these two files.
        headers, body = http(b'GET /manifest.webmanifest HTTP/1.1\r\nHost: 127.0.0.1:4264\r\n\r\n').split(b'\r\n\r\n', 1)
        assert b'200 OK' in headers and b'Content-Type: application/manifest+json' in headers
        manifest = json.loads(body)
        assert manifest['start_url'] == '/' and manifest['icons'][0]['src'] == '/icon-192.png'
        assert body == Path('client/manifest.webmanifest').read_bytes()
        headers, body = http(b'GET /icon-192.png HTTP/1.1\r\nHost: 127.0.0.1:4264\r\n\r\n').split(b'\r\n\r\n', 1)
        assert b'200 OK' in headers and b'Content-Type: image/png' in headers
        assert body == Path('client/icon-192.png').read_bytes() and body[:8] == b'\x89PNG\r\n\x1a\n'
        page = Path('client/index.html').read_text()
        assert 'href="/manifest.webmanifest"' in page and 'href="/icon-192.png"' in page
        assert b'403' in http(b'GET /icon-192.png HTTP/1.1\r\nHost: evil.example:4264\r\n\r\n')
        print('PASS manifest and icon served for a home-screen shortcut', flush=True)

        c = server.c
        assert c.request('info')['result']['pads'] == 4
        c.frame(b'ping', op=9)
        while True:
            op, data = c.receive()
            if op == 10:
                assert data == b'ping'
                break
        c.frame('{"id":99,"method":', fin=False)
        c.frame('"status","params":[]}', op=0)
        while True:
            _, data = c.receive()
            msg = json.loads(data)
            if msg.get('id') == 99:
                assert msg['result']['pads'][0]['state'] == 'free'
                break
        assert 'error' in c.request('claim', [4])
        assert c.request('claim', [0])['result']['pads'][0]['state'] == 'select'
        assert all(row.split()[3] == '0' for row in server.rows() if row.startswith('FRAME')), 'creation injected a button'
        other = Client()
        assert other.request('claim', [0])['error']['code'] == 409
        assert c.request('claim', [0, 1, 2, 3])['result']['pads'][3]['mine']
        print('PASS ping, fragmented messages, explicit creation, four slots, exclusive ownership', flush=True)

        # Exact device event determines assignment; no generic physical-pad read handle.
        os.write(server.log, b'<118>DEVICE_OWNER_CHANGED [DeviceId:0x11030d][UserId:0x1a2b3c4d]\n')
        time.sleep(.04)
        state = c.request('status')['result']['pads']
        assert state[0]['state'] == 'ready' and state[1]['state'] == 'select'
        c.input(0, 0x4000, lx=1, ry=254, l2=12, r2=230, touch=[(3, 1000, 500)])
        c.input(0)
        c.input(1, 0x10000)
        time.sleep(.10)
        frames = [r.split() for r in server.rows() if r.startswith('FRAME')]
        assert any(r[2:11] == ['11030d', '16384', '1', '128', '128', '254', '12', '230', '1'] for r in frames)
        assert any(r[2] == '12030d' and r[3] == '65536' for r in frames)
        assert not any(r[2] in ['13030d', '14030d'] and r[3] != '0' for r in frames)
        assert any(r[2] == '11030d' and r[3] == '0' for r in frames[-30:])
        print('PASS device-specific assignment, PS/Cross, axes/triggers/touch, button edges, pad isolation', flush=True)

        time.sleep(.5)
        assert c.request('status')['result']['pads'][1]['state'] == 'paused'
        frames = [r.split() for r in server.rows() if r.startswith('FRAME')]
        assert all(r[3] == '0' for r in frames[-12:]), 'stale input stayed pressed'
        c.input(1, 0x20)
        time.sleep(.04)
        assert c.request('status')['result']['pads'][1]['state'] == 'select'
        assert other.request('stop')['error']['code'] == 409
        other.close()
        assert not c.request('claim', [])['result']['pads'][0]['open']
        assert len([r for r in server.rows() if r.startswith('REMOVE')]) == 4
        print('PASS idle neutralization, recovery, release and stopping protection', flush=True)

        assert c.request('claim', [0])['result']['pads'][0]['open']
        c.input(0, 0x4000)
        time.sleep(.03)
        c.close()
        time.sleep(.08)
        c = server.c = Client()
        assert c.request('status')['result']['pads'][0]['state'] == 'paused'
        assert c.request('claim', [0])['result']['pads'][0]['mine']
        time.sleep(1.9)
        assert c.request('status')['result']['pads'][0]['state'] == 'free'
        c.frame(json.dumps(dict(method='stop', params=[])))
        assert server.p.wait(timeout=3) == 0
        print('PASS disconnect releases buttons, reconnect grace and inactive-device cleanup', flush=True)
    finally:
        server.close()

    server = Server(C4F_FAIL_ADD='1')
    try:
        assert server.c.request('claim', [0])['error']['code'] == 503
        assert server.c.request('claim', [0])['error']['code'] == 503
        assert server.c.request('status')['result']['pads'][0]['state'] == 'free'
        server.c.frame(json.dumps(dict(method='stop', params=[])))
        assert server.p.wait(timeout=3) == 0
        print('PASS failed device creation is visible and retries do not accumulate orphan devices', flush=True)
    finally:
        server.close()

    # AutoRun can start the payload before GoldHEN's klog server listens.
    server = Server(C4F_TEST_KLOG_BUSY='1')
    try:
        assert server.c.request('claim', [0])['error']['code'] == 503
        assert 'KLOG none' in server.rows() and not any(r.startswith('ADD') for r in server.rows())
    finally:
        server.close()
    # A reader that opens but delivers nothing is dropped before AddDevice runs.
    server = Server(C4F_TEST_KLOG_DEAD='1')
    try:
        assert server.c.request('claim', [0])['error']['code'] == 503
        assert not any(r.startswith('ADD') for r in server.rows())
        assert 'released klog reader' in server.rows()
        assert server.c.request('info')['result']['pads'] == 4
    finally:
        server.close()
    # A klog source that dies is reopened for the next new controller.
    server = Server()
    try:
        assert server.c.request('claim', [0])['result']['pads'][0]['mine']
        os.write(server.log, b'C4F-TEST-CLOSE-KLOG\n')
        end = time.monotonic() + 3
        while not any('klog source closed' in r for r in server.rows()):
            assert time.monotonic() < end, [r for r in server.rows() if not r.startswith('FRAME')]
            server.c.input(0)   # keep the controller, so only the dead log can free the reader
            time.sleep(.02)
        assert server.c.request('claim', [0, 1])['result']['pads'][1]['mine']
        assert len([r for r in server.rows() if r.startswith('KLOG ') and r != 'KLOG none']) == 2
        print('PASS klog unavailable, silent or closed: refused safely, then reopened for the next controller', flush=True)
    finally:
        server.close()

    # A creation in progress must not stop anyone else being served.
    server = Server(C4F_TEST_ADD_DELAY='900')
    try:
        first = server.c
        assert first.request('claim', [0])['result']['pads'][0]['mine']
        second = Client()
        result = []
        thread = threading.Thread(target=lambda: result.append(second.request('claim', [1])))
        thread.start()
        time.sleep(.3)
        started = time.monotonic()
        state = first.request('status')['result']['pads']
        elapsed = time.monotonic() - started
        assert elapsed < .1, f'status waited {elapsed:.2f}s for the other player'
        assert state[1]['state'] == 'connecting'
        first.input(0, 0x4000)
        time.sleep(.05)
        frames = [r.split() for r in server.rows() if r.startswith('FRAME')]
        assert any(r[2] == '11030d' and r[3] == '16384' for r in frames), 'input stopped during creation'
        thread.join(timeout=5)
        assert result and result[0]['result']['pads'][1]['mine']
        second.close()
        print('PASS a controller being created does not block the other players', flush=True)
    finally:
        server.close()

    # Input goes out when it arrives, and an idle controller costs almost nothing.
    server = Server()
    try:
        c = server.c
        assert c.request('claim', [0])['result']['pads'][0]['mine']
        assert 'result' in c.request('ping')
        time.sleep(.6)                       # settle into the keepalive rate
        before = len([r for r in server.rows() if r.startswith('FRAME')])
        c.input(0, 0x4000)
        time.sleep(.02)
        frames = [r.split() for r in server.rows() if r.startswith('FRAME')]
        pressed = next((i for i, r in enumerate(frames) if i >= before and r[3] == '16384'), None)
        assert pressed is not None, 'the press never reported'
        assert pressed - before <= 1, f'the press waited for {pressed - before} keepalive reports'
        # While input is moving: about 250 Hz. Idle: the keepalive rate.
        start = len([r for r in server.rows() if r.startswith('FRAME')])
        for _ in range(10):
            c.input(0, 0x4000)
            time.sleep(.02)
        moving = len([r for r in server.rows() if r.startswith('FRAME')]) - start
        time.sleep(.8)
        start = len([r for r in server.rows() if r.startswith('FRAME')])
        time.sleep(.4)
        idle = len([r for r in server.rows() if r.startswith('FRAME')]) - start
        assert moving >= 30, f'only {moving} reports in 200ms of input'
        assert idle <= 40, f'{idle} reports in 400ms of idling'
        print('PASS input reports on arrival, fast while it moves and slow while it does not', flush=True)
    finally:
        server.close()

    # A controller must survive the moment it is created. A claim finishes inside a
    # main-loop iteration and stamps its clock after the iteration read its own; on
    # the console the gap is InsertData and klog work, here C4F_TEST_SLOW_MS. Those
    # unsigned stamps compared directly wrapped, and the idle reaper deleted every
    # controller in the claim at once, reporting it as unused (console, 1.0.0 dev).
    server = Server(C4F_TEST_SLOW_MS='3')
    try:
        c = server.c
        for round in range(4):
            assert c.request('claim', [0])['result']['pads'][0]['mine']
            time.sleep(.05)
            state = c.request('status')['result']['pads'][0]
            assert state['open'] and state['mine'], f'round {round}: the new controller went away: {state}'
            # Adding a second one refreshes both: neither may go.
            assert c.request('claim', [0, 1])['result']['pads'][1]['mine']
            time.sleep(.05)
            pads = c.request('status')['result']['pads']
            assert all(pads[i]['open'] and pads[i]['mine'] for i in (0, 1)), f'round {round}: {pads[:2]}'
            c.request('claim', [])
        assert not any('removed after' in r for r in server.rows()),             [r for r in server.rows() if 'removed after' in r]
        print('PASS a new controller is not mistaken for an idle one', flush=True)
    finally:
        server.close()

    # Rumble goes to the controller's owner; the light bar, brightened, to everyone.
    def rumble(pad, large, small):
        return lambda m: m.get('method') == 'v' and m['params'] == [pad, large, small]
    server = Server()
    try:
        c = server.c
        assert c.request('claim', [0])['result']['pads'][0]['mine']
        assert c.request('status')['result']['pads'][0]['color'] == [32, 96, 255], 'unlit: its own colour'
        os.write(server.log, b'C4F-TEST-FEEDBACK 11030d 200 40 64 0 0\n')
        c.wait_for(rumble(0, 200, 40))
        time.sleep(.05)
        assert c.request('status')['result']['pads'][0]['color'] == [255, 0, 0], 'player 2 red, at full'
        # Latest colour is logged within a second, while feedback itself stays fast.
        end = time.monotonic() + 1.2
        while 'web controller 1 light bar 40 00 00' not in server.rows():
            assert time.monotonic() < end, 'latest colour never logged'
            c.input(0)
            time.sleep(.02)
        watcher = Client()
        os.write(server.log, b'C4F-TEST-FEEDBACK 11030d 0 0 0 64 0\n')
        c.wait_for(rumble(0, 0, 0))
        time.sleep(.05)
        assert watcher.request('status')['result']['pads'][0]['color'] == [0, 255, 0], 'everyone sees it'
        os.write(server.log, b'C4F-TEST-FEEDBACK 11030d 0 0 32 0 32\n')
        time.sleep(.1)
        assert c.request('status')['result']['pads'][0]['color'] == [255, 0, 255], 'pink keeps its hue'
        assert not any(m.get('method') == 'v' for m in watcher.pushed), 'rumble went to a non-owner'
        # Rumble holds while the game holds it; one message per change, not per poll.
        os.write(server.log, b'C4F-TEST-FEEDBACK 11030d 90 0 32 0 32\n')
        c.wait_for(rumble(0, 90, 0))
        time.sleep(.3)
        c.request('status')
        assert not any(m.get('method') == 'v' for m in c.pushed), 'unchanged rumble was sent again'
        # Whoever takes the controller over is told the current state at once.
        c.close()
        server.c = c = Client()
        assert c.request('claim', [0])['result']['pads'][0]['mine']
        c.wait_for(rumble(0, 90, 0))
        # And told when it stops, so a page never keeps buzzing on old news.
        os.write(server.log, b'C4F-TEST-FEEDBACK 11030d 0 0 0 0 0\n')
        c.wait_for(rumble(0, 0, 0))
        time.sleep(.05)
        assert c.request('status')['result']['pads'][0]['color'] == [32, 96, 255], 'unlit again: its own colour'
        watcher.close()
        print('PASS rumble to the owner on change, the light bar to everyone, the state to a new owner', flush=True)
    finally:
        server.close()

    # Without the call, controllers work as before.
    server = Server(C4F_TEST_NO_FEEDBACK='1')
    try:
        assert server.c.request('claim', [0, 1])['result']['pads'][1]['mine']
        time.sleep(.1)
        assert server.c.request('status')['result']['pads'][0]['color'] == [32, 96, 255]
        assert len([r for r in server.rows() if 'not exported' in r]) == 1
        print('PASS no rumble call: logged once, controllers unaffected', flush=True)
    finally:
        server.close()

    # Who's signed in on each controller, made safe for JSON and UTF-8, kept out of the log.
    server = Server()
    try:
        c = server.c
        assert c.request('claim', [0, 1])['result']['pads'][1]['mine']
        assert c.request('status')['result']['pads'][0]['user'] == ''
        os.write(server.log, b'<118>#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_OWNER_CHANGED [DeviceId:0x11030d][UserId:0x1a2b3c4d]\n')
        os.write(server.log, b'<118>#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_OWNER_CHANGED [DeviceId:0x12030d][UserId:0x1a2b3c4e]\n')
        time.sleep(1.2)   # Sam's name only comes on the retry, half a second later
        pads = c.request('status')['result']['pads']
        assert pads[0]['user'] == 'Alex', pads[0]
        assert pads[1]['user'] == 'Sam "S" \\ ? ? \u00e9', repr(pads[1]['user'])
        assert pads[2]['user'] == '' and pads[3]['user'] == ''
        assert not any('Alex' in r or 'Sam' in r for r in server.rows()), 'a name reached the log'
        assert any('user name found (4 bytes)' in r for r in server.rows())
        print('PASS signed-in names shown, made safe for JSON and UTF-8, kept out of the log', flush=True)
    finally:
        server.close()

    # The Invite panel's QR code: a real QR code of the address the page names.
    server = Server()
    try:
        r = server.c.request('invite', [192, 168, 1, 20, 4264])['result']
        assert r['text'] == 'http://192.168.1.20:4264/', r
        size = r['size']
        assert size in (21, 25, 29, 33, 37) and len(r['rows']) == size
        bits = [[int(row[x >> 2], 16) >> (3 - (x & 3)) & 1 for x in range(size)] for row in r['rows']]
        assert all(len(row) == (size + 3) // 4 for row in r['rows'])
        # The three finder squares: a dark ring, a light ring, a dark 3x3 core.
        finder = [[1,1,1,1,1,1,1], [1,0,0,0,0,0,1], [1,0,1,1,1,0,1], [1,0,1,1,1,0,1],
                  [1,0,1,1,1,0,1], [1,0,0,0,0,0,1], [1,1,1,1,1,1,1]]
        for ox, oy in ((0, 0), (size - 7, 0), (0, size - 7)):
            assert [bits[oy + y][ox:ox + 7] for y in range(7)] == finder, (ox, oy)
        assert server.c.request('invite', [300, 1, 1, 1, 4264])['error']['code'] == 400
        assert server.c.request('invite', [192, 168, 1, 20, 0])['error']['code'] == 400
        reply = server.c.request('invite', [])   # the console's own address, when it has a route
        assert reply.get('result', {}).get('text', 'http://').startswith('http://') or reply['error']['code'] == 503
        print('PASS invite: a real QR code of the page address, bad addresses refused', flush=True)
    finally:
        server.close()

    # Two claims cannot create at once: the second is refused, not queued.
    server = Server(C4F_TEST_ADD_DELAY='600')
    try:
        first = server.c
        result = []
        thread = threading.Thread(target=lambda: result.append(first.request('claim', [0])))
        thread.start()
        time.sleep(.3)
        second = Client()
        assert second.request('claim', [1])['error']['code'] == 409
        thread.join(timeout=5)
        assert result and result[0]['result']['pads'][0]['mine']
        assert second.request('claim', [1])['result']['pads'][1]['mine']
        second.close()
        print('PASS one controller is created at a time, and the next request works', flush=True)
    finally:
        server.close()


def logout_regressions():
    def users(server, result=0, ids=(0x1a2b3c4d, 0x1a2b3c4e)):
        ids = list(ids) + [0xffffffff] * (4 - len(ids))
        os.write(server.log, ('C4F-TEST-USERS ' + str(result) + ' ' + ' '.join(f'{i:x}' for i in ids) + '\n').encode())

    def bind(server, handle, user):
        os.write(server.log, f'DEVICE_OWNER_CHANGED [DeviceId:0x{handle:x}][UserId:0x{user:x}]\n'.encode())

    def wait(c, predicate, timeout=2):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            pads = c.request('status')['result']['pads']
            for p in pads:
                if p['mine']: c.input(p['pad'])
            if predicate(pads): return pads
            time.sleep(.03)
        raise AssertionError(pads)

    server = Server()
    try:
        c = server.c
        c.request('claim', [0, 1])
        bind(server, 0x11030d, 0x1a2b3c4d); bind(server, 0x12030d, 0x1a2b3c4e)
        wait(c, lambda p: p[0]['user'] == 'Alex' and p[1]['state'] == 'ready')
        time.sleep(.3)
        assert 'released klog reader' in server.rows(), 'logout must work without a klog reader'
        users(server, -5, ())
        time.sleep(.3)
        pads = c.request('status')['result']['pads']
        assert pads[0]['mine'] and pads[0]['user'] == 'Alex', 'failed lookup is not logout'
        # List order/foreground changes do not disconnect another logged-in player.
        users(server, ids=(0x1a2b3c4e, 0x1a2b3c4d))
        c.input(0); c.input(1); time.sleep(.3)
        assert all(p['mine'] for p in c.request('status')['result']['pads'][:2])
        os.write(server.log, b'C4F-TEST-FEEDBACK 11030d 80 30 64 0 0\n')
        c.wait_for(lambda m: m.get('method') == 'v' and m['params'] == [0, 80, 30])
        c.input(0, 0x10000); c.input(1, 0x4000)
        adds = len([r for r in server.rows() if r.startswith('ADD ')])
        users(server, ids=(0x1a2b3c4e,))
        pads = wait(c, lambda p: not p[0]['open'])
        assert pads[0]['state'] == 'free' and not pads[0]['mine'] and pads[0]['clients'] == 0 and pads[0]['user'] == ''
        assert pads[0]['uid'].startswith('unassigned-') and pads[1]['mine'] and pads[1]['state'] == 'ready'
        c.wait_for(lambda m: m.get('method') == 'v' and m['params'] == [0, 0, 0])
        rows = server.rows(); removed = rows.index('REMOVE 11030d')
        assert rows[removed - 1].split()[2:10] == ['11030d', '0', '128', '128', '128', '128', '0', '0'], rows[removed - 1]
        c.input(0, 0x10000); c.input(1)
        time.sleep(.1)
        assert not c.request('status')['result']['pads'][0]['open']
        assert len([r for r in server.rows() if r.startswith('ADD ')]) == adds, 'late input recreated a logged-out controller'
        assert len([r for r in server.rows() if 'login session read failed' in r]) == 1
        print('PASS logout snapshot after klog release: neutral/delete, clear name/ownership, stop rumble, preserve other player; failed reads and foreground changes safe', flush=True)
    finally:
        server.close()

    server = Server()
    try:
        c = server.c
        c.request('claim', [0, 1, 2])
        bind(server, 0x11030d, 0x1a2b3c4d); bind(server, 0x12030d, 0x1a2b3c4d); bind(server, 0x13030d, 0x1a2b3c4e)
        wait(c, lambda p: all(s['state'] == 'ready' for s in p[:3]))
        os.write(server.log, b'C4F-TEST-USER-EVENT 1 1a2b3c4d\n')
        pads = wait(c, lambda p: not p[0]['open'] and not p[1]['open'])
        assert pads[2]['mine'] and pads[2]['state'] == 'ready'
        assert all(pads[i]['user'] == '' for i in (0, 1))
        print('PASS explicit logout removes every controller for that user while a different user stays connected', flush=True)
    finally:
        server.close()

    server = Server()
    try:
        c = server.c
        users(server, ids=())
        c.request('claim', [0, 1])
        bind(server, 0x11030d, 0x1a2b3c4d)
        wait(c, lambda p: p[0]['state'] == 'ready')
        time.sleep(.3)
        assert c.request('status')['result']['pads'][0]['mine'], 'sign-in race treated as logout'
        os.write(server.log, b'C4F-TEST-USER-EVENT 1 1a2b3c4d\n')
        pads = wait(c, lambda p: not p[0]['open'])
        assert pads[1]['mine'] and pads[1]['state'] == 'select'
        # A known assigned controller losing its device owner is also removed.
        bind(server, 0x12030d, 0x1a2b3c4e)
        wait(c, lambda p: p[1]['state'] == 'ready')
        # Keep a third unassigned pad so the assignment log reader remains open.
        c.request('claim', [1, 2])
        bind(server, 0x12030d, 0xffffffff)
        wait(c, lambda p: not p[1]['open'])
        print('PASS delayed initial login listing, fast logout event and explicit unassignment', flush=True)
    finally:
        server.close()

    server = Server(C4F_TEST_ADD_DELAY='650')
    try:
        c = server.c
        c.request('claim', [0]); bind(server, 0x11030d, 0x1a2b3c4d)
        wait(c, lambda p: p[0]['state'] == 'ready')
        replies = []
        worker = threading.Thread(target=lambda: replies.append(c.request('claim', [0, 1])))
        worker.start()
        end = time.monotonic() + 2
        while not any(r.startswith('ADD 12030d') for r in server.rows()):
            assert time.monotonic() < end
            time.sleep(.02)
        os.write(server.log, b'C4F-TEST-USER-EVENT 1 1a2b3c4d\n')
        worker.join(timeout=4)
        assert replies and replies[0]['error']['code'] == 409, replies
        pads = c.request('status')['result']['pads']
        assert not pads[0]['open'] and not pads[1]['open'] and not pads[0]['mine']
        assert 'REMOVE 12030d' in server.rows(), 'issued creation leaked after logout invalidated its retained slot'
        print('PASS logout during pending claim: revalidation rejects it and cleans up only its new devices', flush=True)
    finally:
        server.close()


def reliability():
    # Reserve retained and newly-created slots throughout a multi-device claim.
    server = Server(C4F_TEST_ADD_DELAY='1250')
    clients = []
    try:
        first = server.c
        first.request('claim', [0])
        first.close()
        owner = Client(); observer = Client(); clients += [owner, observer]
        owner.frame(json.dumps(dict(id=80, method='claim', params=[0, 1, 2])))
        time.sleep(.25)
        assert observer.request('claim', [0])['error']['code'] == 409
        owner.frame(json.dumps(dict(id=81, method='claim', params=[])))
        owner.frame(json.dumps(dict(id=82, method='claim', params=[3])))
        replies = {}
        while 80 not in replies:
            op, data = owner.receive()
            if op == 1:
                msg = json.loads(data)
                if 'id' in msg: replies[msg['id']] = msg
        assert replies[81]['error']['code'] == 409 and replies[82]['error']['code'] == 409
        assert all(replies[80]['result']['pads'][i]['mine'] for i in (0, 1, 2))
        assert len([r for r in server.rows() if r.startswith('ADD')]) == 3
        assert not any(r.startswith('REMOVE') for r in server.rows()), 'retained reservation was reaped'
        print('PASS full-claim reservations, retained-pad reaper protection and same-client competing claims', flush=True)
    finally:
        for c in clients: c.close()
        server.close()

    # A departing client must not cause the remaining AddDevice calls to run.
    server = Server(C4F_TEST_ADD_DELAY='600')
    try:
        c = server.c
        c.request('claim', [0])
        c.frame(json.dumps(dict(id=70, method='claim', params=[0, 1, 2, 3])))
        end = time.monotonic() + 2
        while len([r for r in server.rows() if r.startswith('ADD')]) < 2:
            assert time.monotonic() < end
            time.sleep(.01)
        c.close()
        time.sleep(.75)
        server.c = Client()
        assert len([r for r in server.rows() if r.startswith('ADD')]) == 2
        assert any(r == 'REMOVE 12030d' for r in server.rows())
        assert not any(r == 'REMOVE 11030d' for r in server.rows()), 'pre-existing device rolled back'
        assert server.c.request('claim', [0])['result']['pads'][0]['mine']
        print('PASS cancellation captures issued creation, skips remaining work and preserves pre-existing pads', flush=True)
    finally:
        server.close()

    server = Server(C4F_FAIL_INPUT='1')
    try:
        server.c.request('claim', [0]); server.c.input(0, 0x4000)
        time.sleep(.08)
        assert any('InsertData = ' in r for r in server.rows())
        server.c.input(0); time.sleep(.04)
        assert server.c.request('status')['result']['pads'][0]['error'] == 0
        print('PASS successful input insertion clears transient errors', flush=True)
    finally:
        server.close()

    server = Server()
    abandoned = []
    try:
        abandoned = [Client() for _ in range(5)]
        handshake = (b'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:4264\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                     b'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n')
        assert b'503' in http(handshake), 'seventh upgrade accepted'
        assert b'200 OK' in http(b'GET /api/status HTTP/1.1\r\nHost: 127.0.0.1:4264\r\nX-Control4Free-Launcher: 1\r\n\r\n')
        start = time.monotonic()
        # Unrelated text and wrong pongs cannot refresh a challenge deadline.
        noisy = abandoned[0]
        while time.monotonic() - start < 15.6:
            assert server.c.request('ping')['result'] == {}
            try: noisy.frame(b'wrong-token', op=10); noisy.frame('{"method":"ping","params":[]}')
            except OSError: pass
            time.sleep(.15)
        for c in abandoned:
            c.sock.settimeout(1)
            while c.sock.recv(65536): pass
        newcomer = Client(); newcomer.close()
        assert server.c.request('info')['result']['pads'] == 4
        print('PASS six-WebSocket cap preserves HTTP; matching pong keeps live client; abandoned/noisy sockets reclaimed', flush=True)
    finally:
        for c in abandoned: c.close()
        server.close()


if __name__ == '__main__':
    main()
    logout_regressions()
    reliability()
