"""Verify real Arabic shaping and a PS4 link with no additional runtime."""
import importlib.util
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    spec = importlib.util.spec_from_file_location("build_text", ROOT / "tools/build_text.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    host = module.build(ROOT / "build/text-host", host=True)
    includes = ["-Ivendor/harfbuzz/src", "-Ivendor/sheenbidi/Headers"]
    archives = [str(host / "libharfbuzz.a"), str(host / "libsheenbidi.a")]
    executable = host / "text-dependencies-test"
    subprocess.run(["clang-18", "-std=c11", "-Wall", "-Wextra", "-Werror", *includes,
                    "tests/text_dependencies_test.c", *archives, "-lm", "-o", str(executable)],
                   cwd=ROOT, check=True)
    fonts = ROOT / "vendor/noto-sans-arabic"
    subprocess.run([str(executable), str(fonts / "NotoSansArabic-Light.ttf"),
                    str(fonts / "NotoSansArabic-Regular.ttf")], check=True, timeout=15)
    sdk = Path(os.environ["OO_PS4_TOOLCHAIN"])
    native = module.build(ROOT / "build/text")
    obj = native / "gate.o"
    elf = native / "gate.elf"
    subprocess.run(["clang-18", "--target=x86_64-pc-freebsd12-elf", "-fPIC", "-isysroot",
                    str(sdk), "-isystem", str(sdk / "include"), *includes, "-c",
                    "tests/text_dependencies_test.c", "-o", str(obj)], cwd=ROOT, check=True)
    subprocess.run(["ld.lld-18", "-m", "elf_x86_64", "-pie", "--script", str(sdk / "link.x"),
                    "--eh-frame-hdr", "-o", str(elf), str(obj), str(native / "libharfbuzz.a"),
                    str(native / "libsheenbidi.a"), "-L" + str(sdk / "lib"), "-lc", "-lkernel",
                    str(sdk / "lib/crt1.o")], check=True)
    dynamic = subprocess.check_output(["llvm-readelf-18", "-d", str(elf)], text=True)
    needed = [line for line in dynamic.splitlines() if "(NEEDED)" in line]
    assert needed and all("[libkernel.so]" in line for line in needed), needed
    print("Arabic text dependencies: OpenOrbis static link, no C++ runtime PASS", flush=True)


if __name__ == "__main__":
    main()
