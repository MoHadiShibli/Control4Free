"""Cache regression and optional host timings for the shipped tester renderer.

Run in the build Docker image. Timings measure CPU drawing on this host, exclude
display/copy/input scheduling, and must not be reported as PS4 frame timings.
"""
import argparse
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def build(renderer=None, output=None):
    sys.path.insert(0, str(ROOT / 'tools'))
    import package_launcher as package
    package.embed_fonts()
    output = Path(output or ROOT / 'build/launcher/tester-render-host-test')
    output.parent.mkdir(parents=True, exist_ok=True)
    version = (ROOT / 'VERSION').read_text().strip()
    flags = ['clang-18', '-std=gnu11', '-O2', '-D_DEFAULT_SOURCE', '-Ilauncher',
             '-Ivendor/stb', '-Ivendor/qrcodegen', f'-DC4F_LAUNCHER_VERSION="{version}"']
    stb = output.with_suffix('.stb.o')
    subprocess.run([*flags, '-w', '-c', 'launcher/stb_truetype.c', '-o', str(stb)], cwd=ROOT, check=True)
    subprocess.run([*flags, '-Wall', '-Wextra', '-Werror', 'tests/tester_render_test.c',
                    'launcher/screen.c', 'launcher/navigation.c', 'launcher/tester.c',
                    str(renderer or 'launcher/tester_draw.c'), 'launcher/draw.c', 'launcher/logo.c',
                    'vendor/qrcodegen/qrcodegen.c', str(package.FONTS), str(stb), '-lm', '-o', str(output)],
                   cwd=ROOT, check=True)
    return output


def capture(binary, *args):
    return subprocess.check_output([str(binary), *map(str, args)], cwd=ROOT, text=True, timeout=120).splitlines()


def check(binary):
    names = capture(binary, '--list')
    expected = dict(capture(binary, '--hash', i)[0].split() for i in range(len(names)))
    sequence = capture(binary)
    assert len(sequence) == 3 * len(names)
    for row in sequence:
        name, actual = row.split()
        assert actual == expected[name], f'render cache retained a previous state for {name}: {actual} != {expected[name]}'
    for name in names[1:]:
        assert expected[name] != expected['idle'], f'{name} did not change the idle display'
    focus = [expected[name] for name in ('menu', 'menu-user', 'menu-orientation', 'menu-color',
                                       'menu-large', 'menu-small', 'menu-reset', 'menu-exit')]
    assert len(set(focus)) == len(focus), 'menu focus is not visibly distinct'
    picker = [expected[name] for name in names if name.startswith('color-')]
    assert len(set(picker)) == len(picker), 'RGB selection, precision, color, error or user is not visibly distinct'
    for a, b in (('menu-unavailable', 'menu-large'), ('menu-error', 'menu-unavailable'),
                 ('menu-output', 'menu-large'), ('menu-no-users', 'menu-unavailable'),
                 ('menu-other-user', 'menu-large'), ('menu-renamed-user', 'menu-large')):
        assert expected[a] != expected[b], f'menu state is not visibly distinct: {a} and {b}'
    for prefix, base in (('', 'idle'), ('menu-', 'menu-large')):
        states = [base, *(prefix + 'rtt-' + suffix for suffix in (
            'known', 'updated', 'measuring', 'no-page', 'unavailable', 'four', 'four-measuring', 'other-slot'))]
        assert len({expected[name] for name in states}) == len(states), f'{prefix}RTT states are not visibly distinct'
        assert expected[prefix + 'rtt-unavailable-stale'] == expected[prefix + 'rtt-unavailable'], \
            'unavailable connection displayed stale RTT values'
        no_user = 'menu-no-users' if prefix else 'no-users'
        assert expected[prefix + 'rtt-no-user'] == expected[no_user], 'RTT displayed without a selected user'
    print(f'PASS tester renderer: {len(names)} fresh references and {len(sequence)} warm transitions match', flush=True)


def benchmark(binary, samples=120):
    names = capture(binary, '--list')
    for label, name, mode in (('cold-idle', 'idle', '--cold'), ('cold-menu', 'menu', '--cold'),
                              ('first-menu-after-idle', 'menu', '--entry')):
        times = sorted(float(capture(binary, mode, names.index(name))[0].split('ms=')[1]) for _ in range(12))
        print(f'bench {label} median_ms={statistics.median(times):.3f} '
              f'p95_ms={times[int((len(times) - 1) * .95)]:.3f} max_ms={times[-1]:.3f} samples={len(times)}', flush=True)
    for row in capture(binary, '--benchmark', samples):
        print(row, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--renderer', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    binary = build(args.renderer, args.output)
    check(binary)
    if args.benchmark:
        benchmark(binary)


if __name__ == '__main__':
    main()
