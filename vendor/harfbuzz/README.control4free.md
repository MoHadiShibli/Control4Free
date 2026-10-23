# HarfBuzz

Unmodified `src/` and `COPYING` from HarfBuzz **11.2.1**:

https://github.com/harfbuzz/harfbuzz/tree/11.2.1

Source archive: https://codeload.github.com/harfbuzz/harfbuzz/tar.gz/refs/tags/11.2.1

Archive SHA-256: `057d5754c3ac0c499bbf4d729d52acf134c7bb4ba8868ba22e84ae96bc272816`.

`tools/build_text.py` builds the upstream unity source with `HB_TINY`, exceptions,
RTTI and thread-safe static guards disabled. This keeps OpenType shaping and its
Unicode tables, including Arabic joining and positioning, without FreeType, ICU,
fontconfig or a C++ runtime. Static TrueType and OpenType/CFF fonts use their
OpenType layout tables; stb_truetype rasterizes their outlines separately.
This profile does not support variable or color fonts. HarfBuzz objects belong to the drawing
thread because `HB_TINY` disables internal locking.

The OpenOrbis SDK's bundled libc++ headers are used only at compile time. Its
`cmath` header expects `cstdlib` to have declared `abs`, so the build explicitly
includes `cstdlib` first. No vendor source or SDK patch is required.

License: see `COPYING` and the notices retained in the sources (including
`src/ms-use/COPYING`).
