"""Suspend and network failure checks against the actual service loop."""
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time
import test_web as web


def connect_again():
    end = time.monotonic() + 4
    while time.monotonic() < end:
        try:
            return web.Client()
        except (OSError, AssertionError):
            time.sleep(.05)
    raise AssertionError('listener did not recover')


def main():
    subprocess.run(['clang-18', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-Iinclude', '-Ivendor/jsmn', 'src/net.c', 'src/web.c', 'src/klog_line.c',
                    'tests/web_stub.c', 'tests/recovery_faults.c', 'build/client.c', 'build/assets.c', '-pthread',
                    '-Wl,--wrap=select,--wrap=accept,--wrap=getsockopt', '-o', str(web.BINARY)], check=True)
    with tempfile.TemporaryDirectory() as directory:
        fault = Path(directory) / 'fault'
        for name in ('select', 'accept', 'socket'):
            server = web.Server(C4F_TEST_FAULT=str(fault))
            try:
                assert server.c.request('claim', [0])['result']['pads'][0]['mine']
                server.c.input(0, 0x4000)
                time.sleep(.04)
                fault.write_text(name)
                if name == 'accept':
                    try:
                        socket.create_connection(('127.0.0.1', 4264), timeout=1).close()
                    except OSError:
                        pass
                end = time.monotonic() + 3
                while not any('network failed' in row for row in server.rows()):
                    assert time.monotonic() < end, server.rows()
                    time.sleep(.02)
                server.c.close()
                server.c = connect_again()
                assert server.c.request('info')['result']['pads'] == 4
                assert server.c.request('claim', [0])['result']['pads'][0]['mine']
                rows = server.rows()
                split = next(i for i, row in enumerate(rows) if 'network failed' in row)
                assert all(row.split()[3] == '0' for row in rows[split:] if row.startswith('FRAME'))
                assert len([r for r in rows if r.startswith('ADD')]) == 1
                assert any('listener recovered' in row for row in rows)
                print(f'PASS {name} failure: listener returns, old buttons cleared, no duplicate pad', flush=True)
            finally:
                server.close()

    server = web.Server()
    try:
        server.c.request('claim', [0])
        server.c.input(0, 0x4000)
        time.sleep(.04)
        os.kill(server.p.pid, signal.SIGSTOP)
        time.sleep(6)
        before = len(server.rows())
        os.kill(server.p.pid, signal.SIGCONT)
        time.sleep(.1)
        server.c.close(); server.c = connect_again()
        assert server.c.request('claim', [0])['result']['pads'][0]['mine']
        after = server.rows()[before:]
        assert any('service resumed after a gap' in row for row in after)
        assert all(row.split()[3] == '0' for row in after if row.startswith('FRAME'))
        assert len([r for r in server.rows() if r.startswith('ADD')]) == 1
        print('PASS process suspension: fresh connection, neutral input and retained controller', flush=True)
    finally:
        os.kill(server.p.pid, signal.SIGCONT)
        server.close()

    server = web.Server(C4F_TEST_KLOG_BUSY='1')
    try:
        assert server.c.request('claim', [0])['error']['code'] == 503
        assert not any(row.startswith('ADD') for row in server.rows())
        assert server.c.request('info')['result']['pads'] == 4
        print('PASS busy log reader: creation refused before any device is added; service stays usable', flush=True)
    finally:
        server.close()

    server = web.Server()
    try:
        assert not any(row.startswith('KLOG ') for row in server.rows())
        server.c.request('claim', [0])
        os.write(server.log, b'DEVICE_OWNER_CHANGED [DeviceId:0x11030d][UserId:0x1a2b3c4d]\n')
        time.sleep(.04)
        assert server.c.request('status')['result']['pads'][0]['state'] == 'ready'
        assert 'released klog reader' in server.rows()
        server.c.input(0, 0x4000)
        time.sleep(.04)
        assert any(r.startswith('FRAME') and r.split()[3] == '16384' for r in server.rows())
        server.c.request('claim', [0, 1])
        assert len([r for r in server.rows() if r.startswith('KLOG ')]) == 2
        print('PASS klog acquired on demand, released after sign-in, gameplay continues, reopened for next pad', flush=True)
    finally:
        server.close()


if __name__ == '__main__':
    main()
