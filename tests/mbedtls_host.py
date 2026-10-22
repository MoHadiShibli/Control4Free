"""The vendored Mbed TLS, built once for the host tests: build/mbedtls-host/libmbedtls.a.
It is rebuilt only when a source or the configuration is newer than the library."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = ROOT / 'build/mbedtls-host/libmbedtls.a'
FLAGS = ['-Iinclude', '-Ivendor/mbedtls/include', '-DMBEDTLS_CONFIG_FILE=<c4f_mbedtls_config.h>']


def library():
    sources = sorted((ROOT / 'vendor/mbedtls/library').glob('*.c'))
    inputs = sources + list((ROOT / 'vendor/mbedtls').rglob('*.h')) + [ROOT / 'include/c4f_mbedtls_config.h']
    newest = max(p.stat().st_mtime for p in inputs)
    if LIBRARY.exists() and LIBRARY.stat().st_mtime >= newest:
        return str(LIBRARY)
    LIBRARY.parent.mkdir(parents=True, exist_ok=True)

    def compile_one(source):
        target = LIBRARY.parent / (source.stem + '.o')
        subprocess.run(['clang-18', '-std=gnu11', '-O2', '-g', *FLAGS, '-c', str(source), '-o', str(target)],
                       check=True, cwd=ROOT)
        return str(target)

    with ThreadPoolExecutor() as pool:
        objects = list(pool.map(compile_one, sources))
    LIBRARY.unlink(missing_ok=True)
    subprocess.run(['ar', 'rcs', str(LIBRARY), *objects], check=True)
    return str(LIBRARY)
