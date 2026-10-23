# Languages

The web interface and PS4 app include English, Brazilian Portuguese and Arabic.
Translations are included with Control4Free. They work without an internet connection
and do not use an online translation service.

## Choose a language

On the web, use the language picker at the bottom of the home screen or in
**Settings → This device**. Automatic follows the browser's primary language.
The choice is saved on that phone or PC and applies to saved copies of the page too.

In the PS4 app, press **L1** on the launcher home screen to cycle through automatic,
English, Português (Brasil) and العربية. Automatic follows the PS4 system language.
The app saves its choice separately from the browser.

Arabic menus and text read from right to left. Numbers use **0–9**. The controller
diagram, physical button positions, stick directions, RGB slider direction, addresses
and technical readings keep their usual orientation. Menu navigation follows the
visible positions, including wrapping at the edges of the tester's tool grid.

Names are displayed as entered. Changing languages does not rename users, connected
gamepads or saved profiles. Errors that Control4Free does not recognize stay readable
in English.

## Arabic fonts

The PS4 app reads the console's installed **SST Arabic Light and Roman** fonts.
The Arabic web page loads those same fonts from the local PS4 service. This uses the
home network, without contacting a font website. A saved HTML page requests them
from the console address you enter.

Both interfaces also include **Noto Sans Arabic Light and Regular**. These display
Arabic immediately while the PS4 font loads and remain available if it cannot be
read or the console is unreachable. Sony's font files are not included in downloads.

## Implementation

Interface text uses explicit message keys. Dynamic messages translate their template
before inserting names and other values. Values are plain text and use directional
isolation in mixed Arabic/Latin messages. Translations and names are never interpreted
as HTML.

The native app uses statically linked HarfBuzz for Arabic joining and mark positioning,
SheenBidi for mixed-direction text, and the existing stb rasterizer for glyph drawing.
Text measurement, wrapping and drawing use the same layout. Bounded layout and glyph
caches belong to the drawing thread; controller input is sampled independently.

Only the two fixed font endpoints are available: `/fonts/arabic-light.otf` and
`/fonts/arabic-regular.otf`. They read a bounded pair of system font files at service
startup. Requests cannot select filesystem paths. See [Third-party material](../THIRD_PARTY.md)
for the bundled fonts and text library licenses.
