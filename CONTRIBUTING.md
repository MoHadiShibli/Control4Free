# Contributing to Control4Free

Thanks for helping! Bug reports, compatibility reports, fixes and reviews are all welcome.

## Reporting a bug

Use the [bug report form](https://github.com/MoHadiShibli/Control4Free/issues/new/choose). The most useful
details:
- the Control4Free version, shown at the bottom of the page and of the app;
- the firmware and GoldHEN versions;
- the phone or computer and browser you used;
- what you did and what happened;
- the service's log, if you can get it: `/data/control4free/control4free.log` on the PS4, over GoldHEN's FTP
  server (port 2121). The previous run is kept in `control4free.log.previous`.

The log contains your console's network address. Remove it before posting if you prefer.

Security problems: please follow [SECURITY.md](SECURITY.md) instead of opening a public issue.

## Building

Everything builds in Docker, so Docker is all you need:

```shell
git clone https://github.com/MoHadiShibli/Control4Free
cd Control4Free
docker build -t control4free-build docker/
docker build -t control4free-launcher-build -f docker/Dockerfile.launcher docker/
docker run --rm -v "$PWD:/src" -w /src control4free-launcher-build bash -lc 'make && make -C launcher'
```

- `build/control4free.elf` is the service, with the page and its icon built in.
- `build/Control4Free-<version>.pkg` is the app, with its own copy of the service.

The first image pins the [ps4-payload-sdk](https://github.com/ps4-payload-dev/sdk) release that builds the
service; the second adds the [OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain),
pinned by digest, for the app and the package. On Windows, `tools/build-launcher.ps1` runs all three steps.

The version comes from the `VERSION` file and nowhere else: the Makefiles and the packager read it, and
the page and the app show the version the service reports.

## Testing

```shell
docker run --rm --network none -v "$PWD:/src" -w /src control4free-launcher-build python3 -B tests/run.py
```

The suites build the real sources for the host with only the PS4 calls stubbed, so they need no console:
- `test_web` and `test_recovery` run the actual service against a fake kernel log
  (`tests/web_stub.c`) that writes the same lines firmware 10.01 does, over real HTTP and WebSocket
  connections;
- `test_frontend` runs the page's own functions under Node;
- `test_launcher` drives the app's logic, renders every state of its screen to `build/launcher-preview-*.png`,
  and checks the package's `param.sfo`;
- `test_logging` covers the log, the kernel-log opener and DeviceId matching.

Add a test with your fix: one that fails without it. Then test on a real console if you can, and say in your
pull request which firmware, GoldHEN version and games you tried.

To try changes to the page without rebuilding, open `client/index.html` from disk and enter the console's
address, or add `?host=<ps4-ip>:4264` to the page's URL.

## Code style

Match the code around your change:
- C with GNU extensions (gnu11), 4-space indent, `camelCase` functions with a `c4f` prefix, `C4f` types and
  `C4F_` macros;
- log with `c4fLog`. `c4fNotify` shows a notification on the PS4's screen: keep those rare;
- comments explain *why*, not *what*;
- only button bits confirmed on hardware go in `include/c4f_sce.h`;
- the page is one file with no build step and no libraries: keep it that way;
- OpenOrbis's headers give `MSG_NOSIGNAL`, `SIGSYS` and `CLOCK_MONOTONIC` their Linux values, which the PS4
  reads as something else. Don't use them directly in `launcher/`; a test checks.

Build without new warnings; the app is built with `-Werror`.

## Releasing

1. Update `VERSION`, and add a section to `CHANGELOG.md`.
2. Commit, tag `vX.Y.Z`, and push the tag.
3. CI builds both images, runs the tests, and publishes the release with the ELF, the package and
   `SHA256SUMS`. It refuses a tag that doesn't match `VERSION`.

## Reviewing AI-written code

Much of Control4Free was written with the help of AI models (Claude Opus 5.5 by Anthropic and Astra GPT-6 by
OpenAI). It has been tested, on the console as well, but AI-written code can look right and still be wrong. Please read it
critically: if you find dead code, wrong assumptions or needless complexity,
[open an issue](https://github.com/MoHadiShibli/Control4Free/issues) or send a pull request.

## License

Contributions are accepted under the [GNU General Public License v3](LICENSE), the project's license.
