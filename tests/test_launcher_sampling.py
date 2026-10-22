"""Exercise the shipped sampler independently of the native display APIs."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / 'launcher/main.cpp').read_text()
    commands_at = source.index('enum { C4F_DO_NOTHING')
    commands = source[commands_at:source.index('};', commands_at) + 2]
    start = source.index('static void *c4fInputWorker(')
    stop = source.index('\nint main(void)', start)
    out = ROOT / 'build/launcher-sampling-source.inc'
    out.parent.mkdir(exist_ok=True)
    out.write_text(commands + '\n' + source[start:stop])
    objects = []
    for name in ('input', 'tester', 'navigation'):
        obj = f'build/sampling-{name}.o'
        subprocess.run(['clang-18', '-std=gnu11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                        '-Ilauncher', '-c', f'launcher/{name}.c', '-o', obj], cwd=ROOT, check=True)
        objects.append(obj)
    subprocess.run(['clang++-18', '-std=c++17', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                    '-Ilauncher', '-Ibuild', 'tests/launcher_sampling_test.cpp', *objects,
                    '-pthread', '-lm', '-o', 'build/launcher-sampling-test'], cwd=ROOT, check=True)
    subprocess.run(['build/launcher-sampling-test'], cwd=ROOT, check=True, timeout=10)


if __name__ == '__main__':
    main()
