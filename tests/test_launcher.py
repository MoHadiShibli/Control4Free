"""Loopback-only tests of the real launcher client and payload HTTP management."""
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import test_web as web

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / 'build/launcher-host-test'
BUNDLED = ROOT / 'build/control4free.elf'
STATUS = b'{"application":"Control4Free","api":1,"version":"0.2.0","controllers":1,"stopping":false}'


def command(*args, **env):
    return subprocess.check_output([str(CLI), *args], text=True, timeout=30, env=dict(os.environ, **env)).strip()


def request(method='GET', path='/api/status', extra=b'', marker=True):
    return web.http(f'{method} {path} HTTP/1.1\r\nHost: 127.0.0.1:4264\r\n'.encode() +
                    (b'X-Control4Free-Launcher: 1\r\n' if marker else b'') + extra + b'\r\n')


def one_reply(address, reply, check=None):
    """A listener on address:4264 answering one request with `reply`."""
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((address, 4264)); listener.listen()

    def serve():
        conn, _ = listener.accept()
        with conn:
            data = conn.recv(4096)
            if check:
                check(data)
            conn.sendall(reply)
        listener.close()
    thread = threading.Thread(target=serve); thread.start()
    return thread


def main():
    web.build()
    # The launcher's own input reader and action navigation, with only the
    # native PS4 pad calls replaced. This also exercises a remote-only launch.
    for diagnostic in (False, True):
        binary = 'build/launcher-input-diag-host-test' if diagnostic else 'build/launcher-input-host-test'
        subprocess.run(['clang-18', '-std=gnu11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                        *(['-DC4F_DIAG'] if diagnostic else []), '-Ilauncher', 'launcher/input.c',
                        'launcher/navigation.c', 'tests/launcher_input_test.c', '-o', binary], check=True)
        subprocess.run([binary], check=True)
    subprocess.run(['clang-18', '-std=gnu11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                    '-ffunction-sections', '-fdata-sections', '-Ilauncher', '-Itests/include',
                    'tests/diag_test.c', '-Wl,--gc-sections,--wrap=write,--wrap=fsync', '-pthread',
                    '-o', 'build/diag-host-test'], check=True)
    subprocess.run(['build/diag-host-test'], check=True)
    subprocess.run(['clang-18', '-std=gnu11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                    '-Ilauncher', '-Ivendor/jsmn', 'launcher/service.c', 'launcher/autorun.c', 'launcher/sandbox.c',
                    'tests/launcher_cli.c', '-o', str(CLI)], check=True)
    assert command('probe').startswith('0 ')
    assert 'PayLoader did not answer' in command('start', str(BUNDLED))
    assert 'payload is missing' in command('start', 'build/no-such-payload.elf')
    print('PASS stopped service and missing PayLoader/file reporting', flush=True)
    server = web.Server()
    try:
        assert command('probe').startswith('1 0 0 ')
        assert 'Already running' in command('start', str(BUNDLED))
        assert not any(r.startswith('ADD') for r in server.rows())
        for extra, marker in [(b'', False), (b'Origin: http://127.0.0.1:4264\r\n', True),
                              (b'Origin: null\r\n', True), (b'Content-Length: 2\r\n', True),
                              (b'Transfer-Encoding: chunked\r\n', True)]:
            assert b'403' in request('POST', '/api/stop', extra, marker)
        assert b'404' in request('GET', '/api/stop')
        assert server.p.poll() is None
        assert command('probe').startswith('1 0 0 ')
        server.c.request('claim', [0, 1])
        server.c.input(0, 0x4000)
        time.sleep(.04)
        assert command('probe').startswith('1 2 0 ')
        assert command('stop').startswith('0 Stopped')
        assert server.p.wait(timeout=3) == 0
        rows = server.rows()
        assert len([r for r in rows if r.startswith('REMOVE')]) == 2
        for i, row in enumerate(rows):
            if row.startswith('REMOVE'):
                assert rows[i-1].startswith('FRAME') and rows[i-1].split()[3] == '0'
        print('PASS idempotent start, protected management, active-controller cleanup and stop acknowledgment', flush=True)
    finally:
        server.close()

    # An older service (or an unrelated port owner) must block another send,
    # and the screen gets the reason.
    thread = one_reply('127.0.0.1', b'HTTP/1.1 404 Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
    out = command('start', str(BUNDLED))
    thread.join(timeout=3)
    assert 'Control4Free is not responding (127.0.0.1 HTTP 404)' in out, out
    print('PASS older/unrecognized service prevents duplicate loading and is explained', flush=True)

    # The app's sandbox may not reach 127.0.0.1: the console's own address is
    # the second way in. A refusal on one address alone is not "stopped".
    def host_header(data):
        assert b'Host: 127.0.0.2:4264' in data and b'X-Control4Free-Launcher: 1' in data, data
    thread = one_reply('127.0.0.2', b'HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s'
                       % (len(STATUS), STATUS), host_header)
    out = command('probe', C4F_TEST_HOST='127.0.0.2')
    thread.join(timeout=3)
    assert out.startswith('1 1 0 0.2.0|'), out
    thread = one_reply('127.0.0.2', b'HTTP/1.1 403 Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
    out = command('probe', C4F_TEST_HOST='127.0.0.2')
    thread.join(timeout=3)
    assert out.startswith('-1 ') and '127.0.0.1 refused; 127.0.0.2 HTTP 403' in out, out
    assert command('probe', C4F_TEST_HOST='127.0.0.2').startswith('0 ')
    print('PASS console-address fallback, with the reason when neither path answers', flush=True)

    received = bytearray()
    child = []
    with socket.socket() as loader:
        loader.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        loader.bind(('127.0.0.1', 9090)); loader.listen()

        def load():
            conn, _ = loader.accept()
            with conn:
                while data := conn.recv(4096):
                    received.extend(data)
                    time.sleep(.001)
            child.append(web.Server())
        thread = threading.Thread(target=load); thread.start()
        try:
            assert command('start', str(BUNDLED)).startswith('0 Ready')
            thread.join(timeout=3)
            assert bytes(received) == BUNDLED.read_bytes()
            assert not any(r.startswith('ADD') for r in child[0].rows())
            assert command('stop').startswith('0 Stopped')
            print('PASS complete payload upload, ready confirmation, no automatic controller creation', flush=True)
        finally:
            for instance in child:
                instance.close()

    # Auto-start: the files GoldHEN's Payloader LaunchPad uses, in a stand-in /user/data.
    root = Path(tempfile.mkdtemp())
    (root / 'GoldHEN').mkdir()
    ini = root / 'GoldHEN/payloads.ini'
    ours = '/user/data/payloads/control4free.elf = 1'
    autorun = lambda *args, **env: command(args[0], str(root), *args[1:], **env)
    assert autorun('autorun-check', str(BUNDLED)).startswith('0|')
    ini.write_bytes(b'[AutoRun]\r\n/user/data/payloads/other.bin = 1\r\n')
    assert autorun('autorun-on', str(BUNDLED)).startswith('0 Auto-start is on')
    text = ini.read_text()
    assert text.count('[AutoRun]') == 1 and '/user/data/payloads/other.bin = 1' in text and ours in text, text
    assert (root / 'payloads/control4free.elf').read_bytes() == BUNDLED.read_bytes()
    assert autorun('autorun-check', str(BUNDLED)).startswith('1|')
    (root / 'payloads/control4free.elf').unlink()
    assert autorun('autorun-check', str(BUNDLED)).startswith('2|Enabled, but the payload is missing')
    autorun('autorun-on', str(BUNDLED))
    original_ini = ini.read_bytes()
    ini.unlink(); ini.mkdir()
    assert autorun('autorun-check', str(BUNDLED)).startswith('-1|Could not inspect AutoRun')
    ini.rmdir(); ini.write_bytes(original_ini)
    autorun('autorun-on', str(BUNDLED))
    assert ini.read_text().count('control4free.elf') == 1
    (root / 'payloads/control4free.elf').write_bytes(BUNDLED.read_bytes()[:-1] + b'\x01')
    assert autorun('autorun-check', str(BUNDLED)).startswith('2|')
    assert autorun('autorun-off').startswith('0 Auto-start is off')
    text = ini.read_text()
    assert 'control4free.elf' not in text and '/user/data/payloads/other.bin = 1' in text, text
    assert autorun('autorun-check', str(BUNDLED)).startswith('0|')
    ini.write_text('[Other]\n/user/data/payloads/control4free.elf = 1\n')
    assert autorun('autorun-check', str(BUNDLED)).startswith('0|')
    ini.unlink()
    assert autorun('autorun-on', str(BUNDLED)).startswith('0 ')
    assert ini.read_text() == '[AutoRun]\n' + ours + '\n'
    assert 'GoldHEN did not let the app' in autorun('autorun-check', str(BUNDLED), C4F_TEST_NO_GOLDHEN='1')
    assert autorun('autorun-on', str(BUNDLED), C4F_TEST_NO_GOLDHEN='1').startswith('-1 GoldHEN did not let')
    print('PASS auto-start setup: copy, AutoRun entry, other entries kept, older copy detected, turned off', flush=True)

    # The shipped drawing code, built for this machine: every screen state and
    # the package icon. The PNGs in build/ are for looking at.
    sys.path.insert(0, str(ROOT / 'tools'))
    import package_launcher as package
    package.build_art_tool()
    for state in ('setup', 'running', 'stopped', 'outdated', 'mismatch', 'confirm', 'busy', 'locked', 'unknown',
                  'remote-actions-stopped', 'remote-actions-running', 'remote-confirm-cancel',
                  'remote-confirm-stop', 'remote-actions-busy', 'remote-actions-unavailable'):
        png = ROOT / f'build/launcher-preview-{state}.png'
        package.render(['screen', state], png, 1920, 1080)
        assert png.stat().st_size > 50000, png
    package.build_art_tool(diagnostic=True)
    for state in ('remote-actions-diag', 'remote-confirm-diag', 'remote-input-diag'):
        png = ROOT / f'build/launcher-preview-{state}.png'
        package.render(['screen', state], png, 1920, 1080, diagnostic=True)
        assert png.stat().st_size > 50000, png
    package.render(['icon', '512'], ROOT / 'build/launcher-icon.png', 512, 512)
    assert subprocess.run([str(package.ART), 'screen', 'nonsense', '/dev/null']).returncode != 0
    # The icon the payload serves is committed, so the payload build needs no
    # renderer. Catch it drifting away from the app's own icon.
    fresh = ROOT / 'build/icon-192.png'
    package.render(['icon', '192'], fresh, 192, 192)
    shipped = ROOT / 'client/icon-192.png'
    assert fresh.read_bytes() == shipped.read_bytes(),         f'{shipped} is out of date: run tools/make_icons.py'
    print('PASS native screen states and icon rendered from the shipped drawing code', flush=True)

    # OpenOrbis's headers give a few constants their Linux values, and the PS4's
    # FreeBSD kernel reads those as something else: MSG_NOSIGNAL (0x4000, not
    # 0x20000), SIGSYS (31, not 12) and CLOCK_MONOTONIC (1, which is CLOCK_VIRTUAL,
    # not 4). Each has broken the app on the console once. Only a C4F_ definition
    # that translates one, a comment, or a Linux-only guard may name them.
    trap = re.compile(r'\b(CLOCK_MONOTONIC|MSG_NOSIGNAL|SIGSYS)\b')
    allowed = re.compile(r'^\s*(#define C4F_|/\*|\*|//)|__linux__')
    found = []
    for source in sorted(list((ROOT / 'launcher').glob('*.c')) + list((ROOT / 'launcher').glob('*.cpp'))):
        lines = source.read_text().splitlines()
        for number, line in enumerate(lines, 1):
            guarded = number > 1 and '__linux__' in lines[number - 2]
            if trap.search(line) and not allowed.search(line) and not guarded:
                found.append(f'{source.relative_to(ROOT)}:{number}: {line.strip()}')
    assert not found, 'OpenOrbis Linux-valued constant used directly:\n' + '\n'.join(found)
    print('PASS no OpenOrbis constant with a Linux value is used directly', flush=True)

    # The package's param.sfo, as the PS4 reads it (built by `make -C launcher`).
    sfo = ROOT / 'build/launcher/pkg/sce_sys/param.sfo'
    if not sfo.exists():
        print('SKIP param.sfo checks: build the package first (make -C launcher)', flush=True)
        return
    data = sfo.read_bytes()
    magic, _, keys, values, count = struct.unpack_from('<4sIIII', data)
    assert magic == b'\0PSF', magic
    fields = {}
    for i in range(count):
        key_at, kind, length, _, value_at = struct.unpack_from('<HHIII', data, 20 + 16 * i)
        name = data[keys + key_at:data.index(b'\0', keys + key_at)].decode()
        raw = data[values + value_at:values + value_at + length]
        fields[name] = struct.unpack('<I', raw)[0] if kind == 0x0404 else raw.rstrip(b'\0').decode()
    version = (ROOT / 'VERSION').read_text().strip()
    major, minor = version.split('.')[:2]
    # Listed under Applications rather than Games, as Apollo Save Tool is.
    assert (fields['CATEGORY'], fields['APP_TYPE'], fields['ATTRIBUTE']) == ('gde', 1, 32), fields
    assert fields['TITLE_ID'] == 'CFRE00001' and fields['TITLE'] == 'Control4Free', fields
    assert fields['APP_VER'] == fields['VERSION'] == f'{int(major):02d}.{int(minor):02d}', fields
    print('PASS param.sfo: an application, its title, and the version from VERSION', flush=True)


if __name__ == '__main__':
    main()
