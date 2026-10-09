# Third-party material

- The virtual-device implementation derives from seregonwar/SplashDown's
  `psbutton.c` (https://github.com/seregonwar/SplashDown), GPL-3.0.
- `vendor/jsmn/jsmn.h` is from https://github.com/zserge/jsmn, copyright Serge
  Zaitsev, under the MIT license included in `vendor/jsmn/LICENSE`.
  Retrieved 2026-10-04; SHA-256:
  `c04533e9181e1e33baceb0f55ac449b05145bb936e8c68cc77dfe0d8277514fb`.
- The toolchain uses https://github.com/ps4-payload-dev/sdk; its version is pinned
  in `docker/Dockerfile` and it retains its own licensing.
- The native launcher uses the OpenOrbis PS4 Toolchain and its software-display
  initialization pattern (https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain,
  GPL-3.0). The toolchain supplies `sce_sys/about/right.sprx` and its stub
  `sce_module/libc.prx` and `sce_module/libSceFios2.prx`; LibOrbisPkg produces the
  package. `docker/Dockerfile.launcher` pins the OpenOrbis image by digest.
- `launcher/sandbox.c` calls GoldHEN's SDK command (syscall 500, jailbreak and
  unjailbreak) with the `jailbreak_backup` layout from the GoldHEN Plugins SDK
  (https://github.com/GoldHEN/GoldHEN_Plugins_SDK), MIT. No SDK code is included.
- `vendor/qrcodegen/qrcodegen.c` and `.h` are Project Nayuki's QR generator v1.8.0
  (https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/c), MIT. The full license
  is retained at the top of each file. SHA-256 for `.c`:
  `300eff07ee25baaa7578f20284411638154716379437391e7e689c0e6ce81403`;
  for `.h`: `e82df4bff37d18b5863b9e7486fe6bda1b6cda8c3b9ecebfec473907265cb589`.
- `vendor/stb/stb_truetype.h` is Sean Barrett's stb_truetype v1.26
  (https://github.com/nothings/stb), public domain or MIT, as chosen at the end of
  the file. Retrieved 2026-10-04; SHA-256:
  `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab`.
- `vendor/roboto/` holds Roboto Light and Regular v2.138
  (https://github.com/googlefonts/roboto), Apache License 2.0
  (`vendor/roboto/LICENSE`). The launcher draws its text with them. SHA-256:
  `Roboto-Light.ttf` `a08729d794801eaa158c53b1558f4cc351c3b1791eb3d22faf7058b2b7df9c8c`,
  `Roboto-Regular.ttf` `f3edb8058e523f5612bfd99d0745e661568ad85e1b6217bc62f786fabae624c6`.

- The on-screen DualShock 4 buttons in `client/index.html` are drawn after the
  [DualShock 4 layout diagram](https://commons.wikimedia.org/wiki/File:Dualshock_4_Layout.svg)
  by Tokyoship, licensed under CC BY 3.0
  (https://creativecommons.org/licenses/by/3.0/). The PS button's mark uses the
  diagram's paths, resized and recolored; the surrounding buttons are adapted
  to the interactive page.

These license notices are also included inside the package.

The HTTP/WebSocket transport in `src/net.c` is a new implementation.
