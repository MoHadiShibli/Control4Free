"""Build and test the shipped Arabic renderer, entirely inside Docker."""
import subprocess
import sys
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--font-dir', type=Path, help='Optional privately supplied PS4 fonts; never bundled')
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT / 'tools'))
    import package_launcher as package
    package.embed_fonts()
    includes, libraries = package.text_host_flags()
    output = ROOT / 'build/launcher/text-render-host-test'
    flags = ['clang-18', '-std=gnu11', '-O2', '-D_DEFAULT_SOURCE', '-Ilauncher', '-Ivendor/stb', *includes]
    stb = output.with_suffix('.stb.o')
    subprocess.run([*flags, '-w', '-c', 'launcher/stb_truetype.c', '-o', str(stb)], cwd=ROOT, check=True)
    subprocess.run([*flags, '-Wall', '-Wextra', '-Werror', 'tests/launcher_text_test.c',
                    'launcher/draw.c', str(stb), str(package.FONTS), *libraries, '-lm', '-o', str(output)], cwd=ROOT, check=True)
    subprocess.run([str(output)], cwd=ROOT, check=True, timeout=30)
    if args.font_dir:
        subprocess.run([str(output), str(args.font_dir / 'SSTArabic-Light.otf'),
                        str(args.font_dir / 'SSTArabic-Roman.otf')], cwd=ROOT, check=True, timeout=30)


if __name__ == '__main__':
    main()
