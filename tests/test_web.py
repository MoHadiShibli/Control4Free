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
    subprocess.run(['clang-18', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-Iinclude', '-Ivendor/jsmn', '-DC4F_STALE_MS=400', '-DC4F_RELEASE_MS=1800',
                    'src/net.c', 'src/web.c', 'src/klog_line.c', 'tests/web_stub.c', 'build/client.c',
                    '-pthread', '-o', str(BINARY)], check=True)


class Client:
    def __init__(self, port=4264):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=3)
        self.buf = b''
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
        first, second = self.exact(2)
        size = second & 127
        if size == 126:
            size = struct.unpack('!H', self.exact(2))[0]
        assert not second & 128
        return first & 15, self.exact(size)

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


if __name__ == '__main__':
    main()
