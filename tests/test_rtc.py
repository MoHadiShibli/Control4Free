"""RTC codecs and the production HTTP surface, built and run inside Docker."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

import mbedtls_host

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/web-rtc-host-test'
FLAGS = ['clang-18', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
         '-Iinclude', '-Ivendor/jsmn', '-Ivendor/qrcodegen', *mbedtls_host.FLAGS]
SOURCES = ['src/net.c', 'src/web.c', 'src/rtc.c', 'src/stun.c', 'src/sctp.c',
           'src/klog_line.c', 'vendor/qrcodegen/qrcodegen.c', 'tests/web_stub.c',
           'build/client.c', 'build/assets.c']


def build():
    subprocess.run(['python3', 'tools/embed_client.py', 'client/index.html', 'build/client.c'], check=True)
    subprocess.run(['python3', 'tools/embed_file.py', 'build/assets.c',
                    'c4fManifest=client/manifest.webmanifest', 'c4fIcon=client/icon-192.png'], check=True)
    library = mbedtls_host.library()
    subprocess.run([*FLAGS, 'src/stun.c', 'src/sctp.c', 'tests/rtc_test.c', library,
                    '-o', 'build/rtc-test'], check=True)
    subprocess.run(['build/rtc-test'], check=True, timeout=30)
    subprocess.run([*FLAGS, 'src/stun.c', 'src/sctp.c', 'tests/rtc_io_test.c', library,
                    '-o', 'build/rtc-io-test'], check=True)
    subprocess.run(['build/rtc-io-test'], check=True, timeout=30)
    # No C4F_TEST_WEBSOCKET: browser checks exercise the production protocol.
    subprocess.run([*FLAGS, *SOURCES, library, '-pthread', '-o', str(BINARY)], check=True)


def http(request):
    with socket.create_connection(('127.0.0.1', 4264), timeout=3) as connection:
        connection.sendall(request)
        result = bytearray()
        while chunk := connection.recv(65536):
            result.extend(chunk)
        return bytes(result)


def main():
    build()
    with tempfile.TemporaryFile() as trace:
        service = subprocess.Popen([str(BINARY)], stdin=subprocess.PIPE, stdout=trace, stderr=trace,
                                   env={**os.environ, 'C4F_TEST_KLOG_FD': '0'})
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    page = http(b'GET / HTTP/1.1\r\nHost: 127.0.0.1:4264\r\n\r\n')
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        trace.seek(0)
                        raise AssertionError(trace.read().decode(errors='replace'))
                    time.sleep(.03)
            assert page.startswith(b'HTTP/1.1 200')
            headers = page.split(b'\r\n\r\n', 1)[0]
            assert b"connect-src 'self';" in headers and b'ws:' not in headers
            upgrade = (b'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:4264\r\nUpgrade: websocket\r\n'
                       b'Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n'
                       b'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n')
            assert http(upgrade).startswith(b'HTTP/1.1 404'), 'production accepted a WebSocket'
            request = b'POST /rtc HTTP/1.1\r\nHost: 127.0.0.1:4264\r\n'
            assert b'403' in http(request + b'Origin: https://evil.example\r\nContent-Length: 3\r\n\r\nv=0')
            assert b'400' in http(request + b'Content-Length: 3\r\n\r\nv=0')
            saved_error = http(request + b'Origin: null\r\nContent-Length: 3\r\n\r\nv=0')
            assert b'400' in saved_error and b'Access-Control-Allow-Origin: null' in saved_error
            assert b'400' in http(request + b'Content-Length: 3\r\nContent-Length: 3\r\n\r\nv=0')
            assert b'400' in http(request + b'Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n')
            assert b'413' in http(request + b'Content-Length: 65535\r\n\r\n')
            assert b'400' in http(request + b'Origin: null\r\nOrigin: null\r\nContent-Length: 3\r\n\r\nv=0')
            preflight = http(b'OPTIONS /rtc HTTP/1.1\r\nHost: 127.0.0.1:4264\r\nOrigin: null\r\n\r\n')
            assert b'204 No Content' in preflight and b'Access-Control-Allow-Origin: null' in preflight
            assert b'Access-Control-Allow-Private-Network: true' in preflight
            assert b'403' in http(b'OPTIONS /rtc HTTP/1.1\r\nHost: 127.0.0.1:4264\r\nOrigin: https://evil.example\r\n\r\n')
            status = http(b'GET /api/status HTTP/1.1\r\nHost: 127.0.0.1:4264\r\nX-Control4Free-Launcher: 1\r\n\r\n')
            assert b'200 OK' in status and b'"application":"Control4Free"' in status
            print('PASS production transport: HTTP page/API, WebSocket refused, RTC origin/body bounds and saved-page preflight', flush=True)
        finally:
            service.terminate()
            service.wait(timeout=5)
            service.stdin.close()


if __name__ == '__main__':
    main()
