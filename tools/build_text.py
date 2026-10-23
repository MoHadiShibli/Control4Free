#!/usr/bin/env python3
"""Build the pinned, offline text libraries for the launcher or host tests."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def build(out, host=False):
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    common = ["-O2", "-fPIC", "-ffunction-sections", "-fdata-sections", "-DNDEBUG"]
    if not host:
        sdk = Path(os.environ["OO_PS4_TOOLCHAIN"])
        common += ["--target=x86_64-pc-freebsd12-elf", "-isysroot", str(sdk),
                   "-isystem", str(sdk / "include")]
    specs = [
        ("harfbuzz", "clang++-18", ["-std=c++17", "-fno-exceptions", "-fno-rtti",
         "-fno-threadsafe-statics", "-DHB_TINY"],
         ROOT / "vendor/harfbuzz/src/harfbuzz.cc", ROOT / "vendor/harfbuzz/src"),
        ("sheenbidi", "clang-18", ["-std=c11", "-DSB_CONFIG_UNITY"],
         ROOT / "vendor/sheenbidi/Source/SheenBidi.c", ROOT / "vendor/sheenbidi/Headers"),
    ]
    for name, compiler, flags, source, include in specs:
        if not host and compiler == "clang++-18":
            # OpenOrbis' cmath expects the integer abs declaration from cstdlib.
            flags = [*flags, "-isystem", str(sdk / "include/c++/v1"),
                     "-include", "cstdlib"]
        obj = out / (name + ".o")
        archive = out / ("lib" + name + ".a")
        command = [compiler, *common, *flags, "-I", str(include), "-c",
                   str(source), "-o", str(obj)]
        stamp = out / (name + "-command.json")
        source_dir = ROOT / "vendor" / name
        newest = max([Path(__file__).stat().st_mtime_ns,
                      *(p.stat().st_mtime_ns for p in source_dir.rglob("*") if p.is_file())])
        recorded = json.dumps(command)
        if (archive.is_file() and archive.stat().st_mtime_ns >= newest and
                stamp.is_file() and stamp.read_text() == recorded):
            continue
        subprocess.run(command, check=True)
        subprocess.run(["llvm-ar-18", "rcs", str(archive), str(obj)], check=True)
        stamp.write_text(recorded)
    return out


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", action="store_true", help="Build for Docker host tests")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    build(args.out, host=args.host)
