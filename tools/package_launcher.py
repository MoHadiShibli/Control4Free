"""Build the native launcher package using OpenOrbis/LibOrbisPkg only."""
import argparse
import binascii
import shutil
import struct
import subprocess
import zlib
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/launcher'
STAGE = OUT / 'pkg'
ART = OUT / 'launcher-art'
FONTS = OUT / 'fonts.c'
TITLE_ID = 'CFRE00001'  # 4 letters + 5 digits, the usual title ID format
CONTENT_ID = f'IV0000-{TITLE_ID}_00-CONTROL4FREE0000'
assert len(CONTENT_ID) == 36
VERSION = (ROOT / 'VERSION').read_text().strip()
# param.sfo wants MM.mm; the patch level is not part of an app version.
major, minor = VERSION.split('.')[:2]
SFO_VERSION = f'{int(major):02d}.{int(minor):02d}'


def embed_fonts():
    sources = [ROOT / 'vendor/roboto/Roboto-Light.ttf', ROOT / 'vendor/roboto/Roboto-Regular.ttf']
    if FONTS.exists() and all(FONTS.stat().st_mtime >= s.stat().st_mtime for s in sources):
        return
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(['python3', str(ROOT / 'tools/embed_file.py'), str(FONTS),
                    f'c4fFontLight={sources[0]}', f'c4fFontRegular={sources[1]}'], check=True)


def build_art_tool(diagnostic=False):
    """Host build of the launcher's drawing code, for the icon and previews."""
    embed_fonts()
    art_version = VERSION + ('-diag' if diagnostic else '')
    flags = ['clang-18', '-std=gnu11', '-O2', '-D_DEFAULT_SOURCE', f'-I{ROOT}/launcher',
             f'-I{ROOT}/vendor/stb', f'-I{ROOT}/vendor/qrcodegen',
             f'-DC4F_LAUNCHER_VERSION="{art_version}"']
    extra_sources = []
    extra_flags = []
    art = ART
    if diagnostic:
        # Preview only: discard native startup/storage paths and retain the real
        # diagnostic drawing function. Existing host declarations provide types;
        # no console APIs or diagnostic file operations are called by this tool.
        flags += ['-DC4F_DIAG', '-ffunction-sections', '-fdata-sections', f'-I{ROOT}/tests/include']
        extra_sources = [str(ROOT / 'launcher/diag.c')]
        extra_flags = ['-Wl,--gc-sections', '-pthread']
        art = ART.with_name(ART.name + '-diag')
    stb = OUT / 'stb_truetype-host.o'
    subprocess.run([*flags, '-w', '-c', str(ROOT / 'launcher/stb_truetype.c'), '-o', str(stb)], check=True)
    subprocess.run([*flags, '-Wall', '-Wextra', '-Werror', str(ROOT / 'tools/launcher_art.c'),
                    str(ROOT / 'launcher/screen.c'), str(ROOT / 'launcher/navigation.c'),
                    str(ROOT / 'launcher/draw.c'), str(ROOT / 'launcher/logo.c'), *extra_sources,
                    str(ROOT / 'vendor/qrcodegen/qrcodegen.c'), str(FONTS), str(stb), '-lm', *extra_flags, '-o', str(art)],
                   check=True)


def write_png(path, width, height, rgba, alpha=False):
    """Writes a PNG, picking the filter that compresses each row best. The gradient
    backgrounds these icons use come out several times larger without it."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data) & 0xffffffff)

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else b if pb <= pc else c

    step = 4 if alpha else 3
    pixels = rgba if alpha else bytes(b for i, b in enumerate(rgba) if i % 4 != 3)
    stride = width * step
    rows = []
    previous = bytes(stride)
    for y in range(height):
        row = pixels[y * stride:(y + 1) * stride]
        best = None
        for kind in range(5):
            out = bytearray(stride)
            for i in range(stride):
                left = row[i - step] if i >= step else 0
                up = previous[i]
                upleft = previous[i - step] if i >= step else 0
                if kind == 0:   out[i] = row[i]
                elif kind == 1: out[i] = (row[i] - left) & 0xff
                elif kind == 2: out[i] = (row[i] - up) & 0xff
                elif kind == 3: out[i] = (row[i] - ((left + up) >> 1)) & 0xff
                else:           out[i] = (row[i] - paeth(left, up, upleft)) & 0xff
            # The usual heuristic: keep the row whose bytes are closest to zero.
            score = sum(b if b < 128 else 256 - b for b in out)
            if best is None or score < best[0]:
                best = (score, kind, bytes(out))
        rows.append(bytes([best[1]]) + best[2])
        previous = row
    header = struct.pack('>IIBBBBB', width, height, 8, 6 if alpha else 2, 0, 0, 0)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) +
                           chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b''))


def render(args, path, width, height, diagnostic=False):
    """Draw with the art tool (build_art_tool first) and save a PNG."""
    raw = OUT / ('art-diag.rgba' if diagnostic else 'art.rgba')
    art = ART.with_name(ART.name + '-diag') if diagnostic else ART
    subprocess.run([str(art), *args, str(raw)], check=True)
    write_png(path, width, height, raw.read_bytes())
    raw.unlink()


def main():
    global OUT, STAGE, ART, FONTS
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', required=True, type=Path)
    # The diagnostic build (make -C launcher C4F_DIAG=1): its own folder, payload,
    # title and file name, so it can never be mistaken for a release.
    parser.add_argument('--diag', action='store_true')
    args = parser.parse_args()
    sdk = args.sdk
    suffix = '-diag' if args.diag else ''
    OUT = ROOT / f'build/launcher{suffix}'
    STAGE, ART, FONTS = OUT / 'pkg', OUT / 'launcher-art', OUT / 'fonts.c'
    binary = sdk / 'bin/linux'
    # Start clean, so nothing left over from an older build gets packaged.
    shutil.rmtree(STAGE, ignore_errors=True)
    for directory in ('sce_sys/about', 'sce_module', 'assets', 'assets/licenses'):
        (STAGE / directory).mkdir(parents=True, exist_ok=True)
    run = lambda *args: subprocess.run([str(arg) for arg in args], cwd=STAGE, check=True)
    run(binary / 'create-fself', '-in=' + str(OUT / 'launcher.elf'), '-out=' + str(OUT / 'launcher.oelf'),
        '--eboot', 'eboot.bin', '--paid', '0x3800000000000011')
    shutil.copyfile(ROOT / f'build/control4free{suffix}.elf', STAGE / 'assets/control4free.elf')
    shutil.copyfile(sdk / 'samples/graphics/sce_sys/about/right.sprx', STAGE / 'sce_sys/about/right.sprx')
    # OpenOrbis's stub modules. Its CHANGELOG (v0.5) says homebrew packages
    # should carry them in sce_module/, and every sample does.
    for name in ('libc.prx', 'libSceFios2.prx'):
        shutil.copyfile(sdk / 'samples/graphics/sce_module' / name, STAGE / 'sce_module' / name)
    licenses = STAGE / 'assets/licenses'
    shutil.copyfile(ROOT / 'LICENSE', licenses / 'Control4Free-GPL-3.0.txt')
    shutil.copyfile(ROOT / 'THIRD_PARTY.md', licenses / 'THIRD_PARTY.txt')
    shutil.copyfile(ROOT / 'vendor/jsmn/LICENSE', licenses / 'jsmn-MIT.txt')
    shutil.copyfile(ROOT / 'vendor/roboto/LICENSE', licenses / 'Roboto-Apache-2.0.txt')
    qrcodegen = (ROOT / 'vendor/qrcodegen/qrcodegen.h').read_text()
    (licenses / 'qrcodegen-MIT.txt').write_text(qrcodegen.split('*/', 1)[0] + '*/\n')
    stb = (ROOT / 'vendor/stb/stb_truetype.h').read_text()
    (licenses / 'stb_truetype.txt').write_text(stb[stb.index('This software is available under 2 licenses'):])
    # The home-screen icon comes from the launcher's own drawing code.
    build_art_tool()
    render(['icon', '512'], STAGE / 'sce_sys/icon0.png', 512, 512)
    pkgtool = binary / 'PkgTool.Core'
    sfo = 'sce_sys/param.sfo'
    run(pkgtool, 'sfo_new', sfo)
    # Listed under Applications, not Games: the values Apollo Save Tool ships with
    # (bucanero/apollo-ps4, built with the same OpenOrbis tools), which lists there
    # and starts fine. CATEGORY gde is "Non-Game Mini App"; gd, which we had, is
    # "Game Digital Application". APP_TYPE must stay 1: with 4 ("Freemium") the PS4
    # refused to start the app (CE-39929-2, firmware 10.01, 2026-10-05).
    entries = {
        'APP_TYPE': ('Integer', 4, '1'), 'APP_VER': ('Utf8', 8, SFO_VERSION),
        'ATTRIBUTE': ('Integer', 4, '32'), 'CATEGORY': ('Utf8', 4, 'gde'),
        'CONTENT_ID': ('Utf8', 48, CONTENT_ID), 'DOWNLOAD_DATA_SIZE': ('Integer', 4, '0'),
        'SYSTEM_VER': ('Integer', 4, '0'),
        'TITLE': ('Utf8', 128, 'Control4Free (diagnostic)' if args.diag else 'Control4Free'),
        'TITLE_ID': ('Utf8', 12, TITLE_ID), 'VERSION': ('Utf8', 8, SFO_VERSION),
    }
    for key, (kind, size, value) in entries.items():
        run(pkgtool, 'sfo_setentry', sfo, key, '--type', kind, '--maxsize', size, '--value', value)
    files = ['eboot.bin', 'sce_sys/param.sfo', 'sce_sys/icon0.png', 'sce_sys/about/right.sprx',
             'sce_module/libc.prx', 'sce_module/libSceFios2.prx', 'assets/control4free.elf']
    files += sorted(str(p.relative_to(STAGE)) for p in (STAGE / 'assets/licenses').iterdir())
    project = STAGE / 'launcher.gp4'
    run(binary / 'create-gp4', '-out', project, '--content-id=' + CONTENT_ID, '--files', ' '.join(files))
    # create-gp4 ships a sample directory tree. Describe the actual package,
    # including nested license files, and keep source paths relative to it.
    tree = ET.parse(project)
    rootdir = tree.getroot().find('rootdir')
    rootdir.clear()
    for filename in files:
        parent = rootdir
        for part in Path(filename).parts[:-1]:
            child = next((p for p in parent if p.get('targ_name') == part), None)
            if child is None:
                child = ET.SubElement(parent, 'dir', targ_name=part)
            parent = child
    ET.indent(tree)
    tree.write(project, encoding='utf-8', xml_declaration=True)
    shutil.copyfile(project, OUT / 'launcher.gp4')
    run(pkgtool, 'pkg_build', project, OUT)
    generated = OUT / (CONTENT_ID + '.pkg')
    target = ROOT / f'build/Control4Free-{VERSION}{suffix}.pkg'
    shutil.copyfile(generated, target)
    print(f'Built {target} ({target.stat().st_size:,} bytes)', flush=True)


if __name__ == '__main__':
    main()
