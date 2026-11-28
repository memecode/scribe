# HTML printing

## Current State

The HtmlViewer harness exercises printer-target layout and drawing through
`lgi/trunk/src/common/Text/Html2.cpp`. It creates fonts and the HTML font cache
for the print surface, then paints each page to `LPrintDC`.

The HtmlViewer test bed now provides a command-line PDF export on GTK:

    HtmlTest -print path/to/page.html

This writes the rendered document to `path/to/page.html.pdf`, using the print
operation's page size and splitting content across pages. This gives us a
repeatable way to inspect HTML rendering against a print device context.

The harness lives here:

    lgi/trunk/test/HtmlViewer

The existing `-render` option continues to render the selected HTML to PNG.

## Integration into Scribe

Previously, HTML email printing rendered the whole document into an `LMemDC`
and scaled and split that bitmap into printer pages.

The print path now:

1. Use `Html2::LHtml` for print layout, with its default font and HTML font
   cache created against the active `LPrintDC`. The selected font surface is
   retained when the HTML document and its font cache are reloaded.
2. Lay out within the printer's available rectangle, inset by Scribe's
   configured margins. On GTK, render the first page
   through a separate `LPrintDC` into a page-sized bitmap, using at least
   300 DPI or the printer DPI if higher. `LZoomView` displays this bitmap;
   it is not used as print output.
3. Keep the HTML control alive after the preview dialog and paint it directly
   to `LPrintDC` for each output page. Page count and scrolling use the print
   layout and the available area after the mail headers.
4. Preserve the surface origin while Html2 applies its per-page scroll offset,
   so page margins and the header area remain in effect.

The GTK preview shows the first page, including its mail headers and margins.
Other backends still use the previous memory-surface preview and do not yet
support `LPrintDC` bitmap capture.
The Scribe HTML email print flow still needs end-to-end runtime verification.
On GTK, HTML wider than the content rectangle is uniformly scaled down to fit;
pagination uses the same scale. Other backends do not yet shrink overflowing
fixed-width HTML.

`LPrintDC::GetPrintableRect` returns coordinates relative to the print surface
before any application origin is applied. On GTK these are points, relative to
the GTK page context (normally already inset by page-setup margins). Hardware
margins, when available, further restrict that rectangle. When GTK cannot
report hardware margins, its page-context bounds are used instead. On Windows,
the surface bounds are the printer DC's imageable area in device pixels. Other
backends report their surface bounds when available; the current macOS print DC
does not yet provide usable page geometry. Scribe's own margins are additional
to the reported printable area.

On GTK, the HtmlViewer harness exercises bitmap capture at 300 and 600 DPI
and verifies that print-font measurements and rasterized text are stable
across printer resolutions before exporting the same content to PDF.
GTK print coordinates are in points: point-sized fonts use those units
directly, while CSS pixel-sized fonts use 72/96 points per pixel.

    HtmlTestSuite -test-print-bitmap path/to/output.pdf

The test also writes the 300-DPI capture to `path/to/output.pdf.preview.png`.
It checks loaded HTML images, failed image allocation, source cropping,
pixel-format conversion, transparency, and image clipping.
Run `HtmlTestSuite -test-zoom-opaque32` to check zooming GTK's opaque bitmap
format.
