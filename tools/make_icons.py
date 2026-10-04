"""Render the home-screen icons the payload serves, with the launcher's own
drawing code, so the page's icon is the app's icon.

    python3 tools/make_icons.py

Writes client/icon-192.png, which the manifest and iOS both use. One size only:
the dithered gradient does not compress, and a 512 copy would add 150 KB to the
payload for a splash screen a plain-HTTP shortcut never shows.
Needs the launcher toolchain image (clang for the host); tests/test_launcher.py
checks the committed files still match a fresh render.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import package_launcher as package

ICONS = {'client/icon-192.png': 192}


def main():
    package.build_art_tool()
    for name, size in ICONS.items():
        package.render(['icon', str(size)], ROOT / name, size, size)
        print(f'{name}  {size}x{size}  {(ROOT / name).stat().st_size:,} bytes')


if __name__ == '__main__':
    main()
