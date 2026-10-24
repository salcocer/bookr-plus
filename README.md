# Bookr Plus

A PDF and e-book reader for the Sony PSP, based on **Bookr V8.1**. Bookr Plus
makes the 2009 source build with today's PSP toolchain and adds a handful of
reading improvements: smooth panning and zoom, margin trimming for scanned
comics, UTF-8 text files, and use of the PSP-2000/3000's extra memory.

It has been tested on a **PSP-3000 running ARK-4 custom firmware**.

Bookr reads plain text, PDF, CHM, HTML, DjVu and PalmDoc/Mobipocket (without
DRM). The original V8.1 readme is kept in
[README.original.md](README.original.md).

## What's new compared to Bookr V8.1

### Builds with the current PSP toolchain

The V8.1 source last compiled with a 2009 Windows/Cygwin setup. It now builds
on macOS (and should on Linux) with the [pspdev](https://github.com/pspdev/pspdev)
toolchain (tested with release v20261001, GCC 15.2). The changes needed were:

| Area | Change |
| --- | --- |
| Makefiles | Hardcoded `/usr/local/pspdev` and `c:/pspsdk` FreeType paths replaced with `psp-config --psp-prefix`; Windows `\` paths in object lists fixed; links `-lbz2`, which current FreeType needs |
| MuPDF (`pdf_font.c`) | Used FreeType's private `ftobjs.h` header to read the font driver name; now uses the public `FT_Get_Font_Format()` |
| MuPDF (`xref.h`) | Missing declaration for `pdf_uncacheobject()`, an error since GCC 14 |
| DjVuLibre | Built as C++98 with `-fpermissive` and `stddef.h` included, as the 2007 code expects |
| App | Built as C++98 (old narrowing in Vietnamese tables); `stricmp` mapped to `strcasecmp`; `pspTime` renamed to `ScePspDateTime` in the SDK; `printf` redirect declared after `stdio.h` |
| `djvu/Makefile` | Creates its `libs` folder, which git does not track |

A [`build.sh`](build.sh) script builds everything in the right order.

### Uses the PSP-2000/3000 extra memory

The app is now built with `PSP_LARGE_MEMORY = 1` (the `MEMSIZE` flag), and
Bookr already asks for "all memory minus 7 MB". On a PSP-2000/3000 that gives
it about 45 MB instead of about 17 MB, which helps with large and image-heavy
PDFs. It is named **Bookr Plus** on the XMB so it can sit next to an original
Bookr.

### UTF-8 text files with accents

Bookr's text font holds 256 glyphs addressed by byte value, so `.txt` files
had to be Latin-1, and characters like `—` `“` `”` `…` could not be shown at
all.

- Glyph slots now follow **Windows-1252**: Latin-1 plus the dashes, curly
  quotes, ellipsis, `€` and a few others in slots 0x80–0x9F.
- Text files that are valid UTF-8 (or start with a UTF-8 BOM) are detected
  and converted to those slots when opened. Latin-1 files are never valid
  UTF-8, so they keep working unchanged.
- Common characters without a slot get a stand-in (`―` → `—`, `‐` → `-`,
  typographic spaces → space); zero-width characters and BOMs are dropped;
  anything else becomes `?`.

This covers Spanish, French, Portuguese, German, Italian and other
Western European languages.

### Precise movement and zoom in PDFs

| Control | Bookr V8.1 | Bookr Plus |
| --- | --- | --- |
| D-pad | Jumps 250 px (up/down) or 460 px (left/right) | Moves **5 px** per press; hold to scroll smoothly |
| Analog stick | Up to 32 px per frame | From **1 px** per frame (light push) to 10 px (full tilt) |
| L / R zoom | Fixed steps (1.0, 1.1, … 2.0, 2.25, 2.5 …) | Page width grows or shrinks by **5 px** per press; hold to keep zooming |
| While zooming | Page re-rendered after every step | Only the "Zoom 1.27x" label updates; the page renders **once, on release** |
| After zooming | View jumped sideways (pan was multiplied by the absolute zoom) | View **stays centred** on the same spot |
| Bookmarks / last view | Saved the nearest zoom step | Save the **exact zoom** |

The step sizes are constants at the top of `Bookr/bkpdf.cpp` (`panStep`,
`analogMaxStep`) and in `BKDocument::processEventsForView` (zoom `step`).

### Trim margins

Scanned books and comics often have white side margins that change from page
to page, typically wider on the left on odd pages and on the right on even
pages. A zoom and position that frame one page well are off on the next.

**SELECT → zoom menu → Trim margins: Off/On** hides the white space at the
left and right of every page:

- When a page opens, it is rendered small (400 px wide) and each column is
  checked for ink (pixels darker than 215). Columns need at least 3 ink
  pixels, so dust specks are ignored. A 1.5% safety margin is added on each
  side, so content is never cut.
- The page's media box is narrowed to that range in memory, like a crop box.
  The PDF file is not modified.
- The result is cached per page, so each page is analysed only once while the
  book is open.
- The zoom is kept **relative to the trimmed width**, so every page's content
  fills the screen the same way. Turning trim on starts at "fit width".
- While trim is on, turning the page **keeps the view position**, so a
  framing you set with the arrows and zoom carries over to the next page.
- The trim setting and zoom are saved with the book's last view.

### Bug fix

`recalcScreenMediaBox()` used the page's `x0` where it meant `y0`. It made
no difference while every page started at (0, 0), but it would have
misplaced trimmed pages and PDFs whose crop box does not start at the origin.

## Controls

Default in-book controls (changeable in Bookr's menu):

| Button | Action |
| --- | --- |
| D-pad | Move 5 px (hold to scroll) |
| Analog stick | Move, speed depends on how far it is pushed |
| R / L | Zoom in / out |
| △ / □ | Next / previous page |
| ✕ / ○ | Forward / back 10 pages |
| SELECT | Toolbar: bookmarks, pages, zoom (fit height, fit width, zoom out, zoom in, **trim margins**), rotation |
| START | Main menu and options |

## Installing on a PSP

1. Build `EBOOT.PBP` (see below), or take it from a release.
2. Copy it to `PSP/GAME/BookrPlus/EBOOT.PBP` on the Memory Stick. Also copy
   `Bookr/WebVIQR.txt` there if you read Vietnamese HTML/CHM files.
3. Put your books anywhere on the Memory Stick; Bookr has its own file browser.

On macOS, Finder adds hidden `._` files when copying to the Memory Stick. Bookr
lists them as books, and in the `ISO` folder they can confuse the PSP's game
list. Remove them before ejecting:

```sh
dot_clean "/Volumes/NO NAME"
```

## Building from source

1. Install the [pspdev toolchain](https://github.com/pspdev/pspdev/releases)
   and put its `bin` folder on your `PATH`. On macOS, use the
   `pspdev-macos-latest-arm64` (Apple silicon) or `x86_64` (Intel) package.
2. Run:

   ```sh
   ./build.sh
   ```

   This builds the bundled MuPDF and DjVuLibre libraries, then the app, and
   writes `Bookr/EBOOT.PBP`.

After a library has been built once, `cd Bookr && make` rebuilds only the app.
The Makefiles do not track header dependencies, so after editing a header,
delete the app's `.o` files first.

## Tools for the Mac

Both tools live in [`tools/`](tools).

### `pdf2psp.swift`: make image PDFs readable in Bookr

Bookr's PDF engine dates from 2005 and cannot decode **JBIG2** or
**JPEG 2000** images. That is the "unknown filter: JBIG2Decode" error.
Internet Archive scans use both: each page is a JPEG 2000 background, a
JPEG 2000 ink colour layer, and a 1-bit JBIG2 mask holding the line art.

macOS can read those formats, so this tool redraws every page and stores it
as a plain JPEG, which Bookr handles. Black-and-white pages are stored as
grayscale, which makes them smaller and faster to decode on the PSP.

```sh
swiftc -O -o pdf2psp tools/pdf2psp.swift
./pdf2psp "input.pdf" "output.pdf" 800 0.6   # page width in px, JPEG quality
```

800 px wide at quality 0.6 kept screentones and lettering clean in testing
(about 225 KB per manga page).

### `sfo_memsize.py`: give an existing EBOOT the extra memory

Adds `MEMSIZE=1` (and optionally a new title) to an `EBOOT.PBP` without
recompiling it. The program data is copied unchanged.

```sh
python3 tools/sfo_memsize.py EBOOT.PBP EBOOT_new.PBP "My App 64MB"
```

This only helps if the program sizes its heap from available memory, as
Bookr does with `PSP_HEAP_SIZE_KB(-7*1024)`.

## Known limitations and ideas

- **JBIG2 / JPEG 2000 PDFs** still don't open on the PSP; convert them with
  `pdf2psp.swift`. MuPDF has dormant glue for both (`filt_jbig2d.c`,
  `filt_jpxd.c`). Enabling JBIG2 with jbig2dec is possible, but full-size
  JPEG 2000 layers would be slow and memory-hungry on the PSP.
- **Trim margins** keeps page numbers and other marks in the margin, so
  content can sit slightly off-centre. Ignoring small isolated marks near the
  edges would fix it.
- **Text files** support Western European characters only (Windows-1252).
  Other scripts would need a larger glyph atlas.
- **Fast images** mode is still limited to 2x zoom. The extra memory could
  raise that.
- A **manga mode** (CBZ or image folders, right-to-left page order) and
  **EPUB** support are possible next steps.

## Credits

- **Bookr**: Carlos Carrasco Martinez and Edward Choh (original); Christian
  Payeur (controls and GUI); Yang.Hu (DjVu); Nguyen Chi Tam (CHM/HTML,
  V8.0/V8.1). V8.1 source: <https://github.com/nguyenchitam/Bookr>
- **MuPDF**: Artifex Software, under the Aladdin Free Public License (see
  `Bookr/mupdf/PUBLIC`)
- **DjVuLibre** 3.5: Léon Bottou and Yann Le Cun
- **chmlib**: Jed Wing. **VnConv**: Pham Kim Long
- **pspdev**: the PSP homebrew toolchain and SDK

## License

Bookr and Bookr Plus are licensed under the **GNU General Public License
version 2 or later** (see [LICENSE](LICENSE)). The bundled libraries keep
their own licences: MuPDF under the Aladdin Free Public License, DjVuLibre
under the GPL, and chmlib under the LGPL.
