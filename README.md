# Bookr+

A PDF and e-book reader for the Sony PSP, based on **Bookr V8.1**. Bookr+
makes the 2009 source build with today's PSP toolchain and adds a handful of
reading improvements: smooth panning and zoom, margin trimming for scanned
comics, a right-to-left comic mode, EPUB books, UTF-8 text files, zoom for
text files, screenshots saved to the Memory Stick, a neobrutalist interface,
and use of the PSP-2000/3000's extra memory.

It has been tested on a **PSP-3000 running ARK-4 custom firmware**.

Bookr+ reads plain text, PDF, EPUB, CHM, HTML, DjVu and
PalmDoc/Mobipocket (EPUB and Mobipocket without DRM). The original V8.1
readme is kept in [README.original.md](README.original.md).



## Installing on a PSP

1. Unzip the bookr+.zip found in the release tab.

2. Place the bookr folder in /PSP/GAME/

3. Put your books anywhere on the Memory Stick & Profit!



## Screenshots
<table>
  <tr>
    <td width="50%"><img width="100%" alt="img_0" src="https://github.com/user-attachments/assets/f847788e-a212-4333-8071-fae3c121cbd3" /></td>
    <td width="50%"><img width="100%" alt="img_1" src="https://github.com/user-attachments/assets/ae99ade8-2e86-433f-896f-8dd66215c6cc" /></td>
  </tr>
  <tr>
    <td width="50%"><img width="100%" alt="img_2" src="https://github.com/user-attachments/assets/76026db3-d541-47ba-8741-447cd19ee2d1" /></td>
    <td width="50%"><img width="100%" alt="img_3" src="https://github.com/user-attachments/assets/7eb9be7e-9182-45b4-8b14-da74c0a96d17" /></td>
  </tr>
  <tr>
    <td width="50%"><img width="100%" alt="img_4" src="https://github.com/user-attachments/assets/f2b8f6e2-dcfd-4b2e-8715-b85006fb2137" /></td>
    <td width="50%"><img width="100%" alt="img_5" src="https://github.com/user-attachments/assets/3dd3e2a9-2706-436e-b0a6-2c1cd17908a7" /></td>
  </tr>
</table>




### Uses the PSP-2000/3000 extra memory

The app is now built with `PSP_LARGE_MEMORY = 1` (the `MEMSIZE` flag), and
Bookr already asks for "all memory minus 7 MB". On a PSP-2000/3000 that gives
it about 45 MB instead of about 17 MB, which helps with large and image-heavy
PDFs. It is named **Bookr+** on the XMB so it can sit next to an original
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

### Zoom for text files

In V8.1, L / R did nothing in text files. The text size could only be changed
in the options menu, and changing it reloaded the whole file.

- **R / L** (the zoom in / zoom out buttons) make the text 1 px bigger or
  smaller per press, from 6 to 20 px. A "Text size 16 px" banner shows the new
  size.
- The text re-wraps in place, and the paragraph at the top of the screen stays
  there. The page count updates to match.
- The size is the same setting as **Options → Plain text - Font size**, so it
  is saved and the next text file opens at that size.
- The toolbar's zoom menu now has **Zoom out** and **Zoom in** for text files
  too; it used to say "No zoom support".
- EPUB and PalmDoc/Mobipocket books use the same text engine, so they zoom
  too.

### EPUB books

`.epub` files open like any other book. The chapters are read in the book's
reading order and joined into one document, with a blank line between
chapters, so text zoom, bookmarks, the last view, rotation and screenshots all
work as they do for text files. Accents and typographic quotes show correctly.

- **Text only:** images, bold and italics, and the book's own styles are
  not shown.
- **DRM:** books whose chapters are encrypted (most bought in online stores)
  are refused with "invalid or corrupted file" instead of showing garbage.
  Font obfuscation, which many store books use, is not DRM and opens fine.
  EPUBs from Project Gutenberg, Standard Ebooks or your own unprotected files
  work.
- **Size:** a book's chapters can add up to 8 MB of text, enough for very
  long novels. A 2 MB book takes a few seconds on the "Loading..." screen.

How it works (`Bookr/bkepub.cpp`): the EPUB is a ZIP archive, so Bookr reads
its central directory and inflates the files it needs with zlib.
`META-INF/container.xml` names the package (OPF) file, whose manifest maps
ids to files and whose spine gives the chapter order (items marked
`linear="no"`, such as covers and notes, are skipped). The chapters go through
the same HTML parser as Mobipocket books. Before that, `<div>` blocks and
table rows become line breaks, and HTML5's `<header>` is renamed, because the
parser would take it for `<head>` and skip its text. The bundled TinyXML
rejects common EPUB 3 metadata, so container.xml and the OPF are read with a
small tag scanner instead.




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

## Known limitations and ideas

- **JBIG2 / JPEG 2000 PDFs** still don't open on the PSP; convert them with
  `pdf2psp.swift`. MuPDF has dormant glue for both (`filt_jbig2d.c`,
  `filt_jpxd.c`). Enabling JBIG2 with jbig2dec is possible, but full-size
  JPEG 2000 layers would be slow and memory-hungry on the PSP.
- **Trim margins** keeps page numbers and other marks in the margin, so
  content can sit slightly off-centre. Ignoring small isolated marks near the
  edges would fix it.
- **Text files and EPUBs** support Western European characters only
  (Windows-1252). Other scripts would need a larger glyph atlas.
- **Text zoom** re-wraps the whole file at every step, so a very large `.txt`
  or EPUB file can take a moment per press on the PSP.
- **EPUBs** show text only, in one continuous flow. Images, bold and italics,
  and a chapter list from the book's table of contents would be the next
  steps.
- **Screenshots** are at screen resolution (480 × 272). Saving a whole PDF
  page at full resolution would need a separate export.
- **Fast images** mode is still limited to 2x zoom. The extra memory could
  raise that.
- **CBZ** comics (or image folders) are a possible next step. Comic mode
  already handles right-to-left reading for PDFs, and the EPUB code already
  reads ZIP archives.

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

Bookr and Bookr+ are licensed under the **GNU General Public License
version 2 or later** (see [LICENSE](LICENSE)). The bundled libraries keep
their own licences: MuPDF under the Aladdin Free Public License, DjVuLibre
under the GPL, and chmlib under the LGPL.
