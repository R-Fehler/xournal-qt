# Platform research: native libraries and PDF engines

Research date: 2026-09-24. This is a written comparison only; nothing was built. It builds on
`../cross-platform-qt-research/` (especially `02-stylus-input.md` and `06-risks-and-open-questions.md`) and
answers the "Platform research" items in `TODO.md`. **[unverified]** marks claims I could not check against a
primary source.

## Summary and recommendation

1. **Split the PDF layer in two: a renderer and a writer.** Rendering (feel, speed, text search) can
   differ per platform. Writing (merging pages, `/Ink` annotations, an embedded `.xopp`) should use one portable
   library everywhere. **qpdf does the writing.** It is already a dependency, it is Apache-2.0, and
   `QPdfExport` (upstream, `src/core/pdf/base/QPdfExport.cpp`) already builds Form XObjects and copies objects between PDFs. So you can do the "pasted pages
   stay searchable" item and most of the hybrid PDF **now, without MuPDF**.
2. **Rendering: MuPDF as planned (M6), with pdfium as the measured alternative.** Both are reported to be
   clearly faster than poppler. MuPDF can use several threads (one `fz_context` per thread). pdfium is
   single-threaded, so one lock covers the whole library. pdfium is BSD/Apache and has prebuilt binaries for
   every target. Choose with the `bench-render` experiment below, not from benchmarks on the web. Keep
   poppler as the Linux fallback.
3. **Don't adopt PDFKit, Android PdfRenderer or Windows.Data.Pdf as the main engine.** They would give three
   code paths with three different sets of bugs and render differences. The render path is already cached, so
   what they could add is small. The one exception is PDFKit, which could be a later render-only backend on
   Apple for fidelity, and only if measurements call for it.
4. **Pen input: Qt already delivers most of it on Windows and iOS.** On Windows, Qt replays the coalesced
   `WM_POINTER` history (the app already sets `AA_CompressHighFrequencyEvents=false`) and reports pressure,
   tilt, rotation, the eraser and the barrel button. What native ink stacks (Windows Ink `InkPresenter`,
   PencilKit, androidx.ink) add is mainly **lower latency for the stroke still being drawn**. They cost a
   native rendering layer that must match our own final rendering pixel for pixel. **Measure latency first.**
   The cheaper fix is **predicted touches** (iOS) or motion prediction (Android), which feed into
   `CanvasInput`.
5. **Handwriting recognition:** put it behind one `InkRecognizer` interface (strokes in, ranked text out).
   Backends: the WinRT `InkRecognizerContainer` on Windows and ML Kit Digital Ink on Android and iOS. Watch
   the licensing: ML Kit is a proprietary bundled library, not a system library, so distributing a GPL build
   that links it is a GPL problem. OS recognizers (Windows, Apple) don't have this problem.

## Where platform code attaches

| Seam | File(s) | Today | What plugs in |
|---|---|---|---|
| PDF render/read backend | `src/core/pdf/base/XojPdfDocumentInterface.h`, `XojPdfPage.h`; hard-wired in `XojPdfDocument.cpp:11,13` (`new PopplerGlibDocument()`) | poppler-glib | MuPDF / pdfium / PDFKit subclasses plus a factory (ROADMAP §5). Note: the interface is typed on cairo (`render(cairo_t*)`, `cairo_region_t*`). A non-cairo engine renders into the target image surface's memory. `renderForPrinting` (print, cairo export, image export) would then produce raster output. |
| PDF writing | `src/core/pdf/base/HybridPdfExport.*`, `QPdfExport.*`, `XojPdfExportFactory.cpp` | qpdf overlay export (the default whenever a background PDF exists) | a new `PdfWriter` seam: merge pages, `/Ink`+`/AP`, embedded file, incremental save |
| Pen/touch input | `qt/src/canvas/CanvasInput.{h,cpp}` (`tabletEvent`, private `Event` struct), fed from `qt/src/quick/DocumentCanvasItem.cpp:372` | `QTabletEvent` | a platform raw-input source (Android JNI, iOS predicted touches, Pencil squeeze) that produces `CanvasInput::Event`. `Event` needs tilt, rotation and a "predicted" flag. |
| Live (wet) stroke rendering | `qt/src/render/` (`RenderService`, `PageRaster`) + upstream `StrokeToolView` via the canvas | CPU composite, dirty-tile upload | an optional native wet-ink overlay (InkPresenter, PencilKit, androidx.ink) that hands over to our renderer on pen-up |
| Recognition | upstream `control/shaperecognizer/` used by `src/core/control/tools/StrokeHandler.cpp` | shapes only | a new `InkRecognizer` interface (no seam exists yet) |

## Q1 — Platform-native libraries

| Option | What it gives | Cost / risk | Where it attaches | License |
|---|---|---|---|---|
| **Qt stock input, Windows** (status quo) | `WM_POINTER` pen with coalesced history replayed ([qwindowspointerhandler.cpp](https://github.com/qt/qtbase/blob/dev/src/plugins/platforms/windows/qwindowspointerhandler.cpp), lines ~118–135), plus pressure, tilt, rotation, the eraser (`PEN_FLAG_ERASER/INVERTED`) and the barrel button ([`GetPointerPenInfoHistory`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerpeninfohistory)) | None. Verify on the Qt version you ship; I read `dev`. | already works | LGPL/GPL |
| **Windows Ink `InkPresenter` in Win32** (`InkDesktopHost` → `IInkPresenterDesktop` in a DirectComposition tree) | system low-latency "wet" ink on a background thread, then a "dry" handover ([InkDesktopHost](https://learn.microsoft.com/en-us/windows/win32/input_ink/inkdesktophost), [ActivateCustomDrying](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.inking.inkpresenter.activatecustomdrying)) | High. You need a DirectComposition visual over Qt Quick's swap chain, and the pen input has to be split between Windows Ink and Qt. Its wet-ink look must match our pressure curve and highlighter blend, or the stroke visibly jumps on pen-up. Whether it composes with the Qt RHI (D3D11/12) window is **[unverified]**. | wet-stroke layer; on pen-up, strokes are converted to our `Stroke` | OS component (GPL system-library exception applies) |
| **PencilKit** (`PKCanvasView`, `PKDrawing`/`PKStroke`/`PKStrokePoint` with force, azimuth and altitude since iOS 14 — [PKStroke](https://developer.apple.com/documentation/pencilkit/pkstroke-swift.struct), [API diff](http://codeworkshop.net/objc-diff/sdkdiffs/ios/14.0/PencilKit.html)) | Apple's own ink latency and feel, plus the system tool picker | High. `PKCanvasView` owns both input and rendering, so it is a UIKit view on top of the Qt window. Its brushes are not our brushes, so our stroke model and the final rendering diverge. Using it only for the wet stroke means removing its strokes on pen-up. | wet-stroke layer, iOS only | OS framework |
| **UIKit extras without PencilKit** (predicted touches; `UIPencilInteraction` squeeze and double-tap; hover pose with roll, iPadOS 17.5+ with Pencil Pro — [WWDC24](https://developer.apple.com/videos/play/wwdc2024/10214/), [Squeeze](https://developer.apple.com/documentation/uikit/uipencilinteraction/squeeze)) | about one frame of latency hidden by prediction, tool switching by squeeze, flip-to-erase and roll. Qt exposes none of this (see `02-stylus-input.md`). | Low to medium: a small Objective-C++ bridge on Qt's `QUIView` plus an extended `Event` | `CanvasInput` (a predicted tail drawn as a temporary overlay); `ToolHandler` for squeeze | OS framework |
| **Android: androidx.ink / low-latency graphics + motion prediction** | front-buffered wet ink ([Ink API](https://developer.android.com/develop/ui/compose/touch-input/stylus-input/about-ink-api), [release page](https://developer.android.com/jetpack/androidx/releases/ink)) | Ink API still alpha **[per a secondary source; check the release page]**. Same "match our rendering" problem. It fits in the JNI input bridge already planned in `02-stylus-input.md`. | Android input bridge → `CanvasInput`; optional wet layer | Apache-2.0 |
| **Windows HWR: `InkRecognizerContainer` / `InkAnalyzer`** (WinRT, callable from C++/WinRT) | on-device text recognition. `InkAnalyzer` also sorts strokes into writing vs drawing and recognizes shapes and layout ([InkRecognizerContainer](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.inking.inkrecognizercontainer), [InkAnalyzer](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.inking.analysis.inkanalyzer), [guide](https://learn.microsoft.com/en-us/windows/uwp/ui-input/convert-ink-to-text)) | Low to medium. Strokes are built with `InkStrokeBuilder`. The container is null when no recognizer is installed, and recognizers depend on the installed language packs. | `InkRecognizer` interface | OS component |
| **ML Kit Digital Ink** (Android **and** iOS) | offline recognition, 300+ languages, shape and emoji models, about 20 MB per language downloaded on demand ([overview](https://developers.google.com/ml-kit/vision/digital-ink-recognition), [iOS](https://developers.google.com/ml-kit/vision/digital-ink-recognition/ios)) | Medium. Its input model (points with timestamps) matches ours. **License risk:** it is a closed binary bundled with the app, so the GPL system-library exception does not apply. Fine for private builds; distributing it needs legal thought (running it in a separate process, or an exception from all copyright holders, which is impractical for upstream code). **[my reading, not legal advice]** | `InkRecognizer` interface | proprietary (Google ML Kit terms) |
| **Apple Vision OCR on rendered ink** (`VNRecognizeTextRequest`) | handwriting OCR in a subset of languages ([docs](https://developer.apple.com/documentation/vision/vnrecognizetextrequestrevision1)). Apple has no public stroke-based recognizer; Scribble works for text fields only **[unverified whether Scribble works in Qt text fields]**. | Low. It runs on an image, so it loses stroke timing, which makes it worse than stroke-based recognition, but it is good enough for search indexing. | `InkRecognizer` (renders the page region first) | OS framework |
| MyScript iink (all platforms) | best-in-class recognition, including math | commercial SDK, same GPL-distribution problem as ML Kit | `InkRecognizer` | proprietary |

**What to take from Q1:** native *input* adds little on Windows and iOS, because Qt already has it. Native
*wet ink* is the only thing that clearly improves feel, and it is expensive and fragile. Native *recognition* is
cheap behind one interface, and its best use is **search** (recognized text in the library index). That needs
nothing on screen.

## Q2 — PDF engines

The author already likes today's speed thanks to caching, so speed matters mostly for **first paint at a new
zoom level** and on **heavy PDFs**. After that, all engines draw from the same cache.

| Engine | Feel / speed | Writes annotations (`/Ink`) and PDFs | Threads | Platforms | License | Attaches |
|---|---|---|---|---|---|---|
| **poppler-glib + cairo** (today, 24.02 installed) | the slowest of the three open engines in third-party benchmarks ([hzqtc](https://hzqtc.github.io/2012/04/poppler-vs-mupdf.html), [dc-pdf-raster-test](https://github.com/nathanstitt/dc-pdf-raster-test)); the ROADMAP also notes renders are serialized per document | **Since 25.06**, `poppler_annot_ink_new` / `_set_ink_list` / `_set_draw_below`, and a multiply blend mode since 25.10 (`glib/poppler-annot.h` and `NEWS` on [gitlab](https://gitlab.freedesktop.org/poppler/poppler/-/blob/master/glib/poppler-annot.h)). `poppler_document_save` writes annotation changes. It cannot merge pages or embed files. | one mutex per document | Linux fine; mobile via vcpkg `poppler[glib]` is unproven (R1) | GPL-2/3 | current `PopplerGlib*` |
| **MuPDF** (M6 plan) | fastest in most benchmarks; display lists + tiles fit `RenderService` | full writer: `pdf_add_annot_ink_list`/`pdf_set_annot_ink_list` + `pdf_update_annot` (generates `/AP`), `pdf_graft_page` (copies pages between PDFs), `pdf_add_embedded_file`, incremental save (all in the local 1.19 headers, `/usr/include/mupdf/pdf/{annot,document}.h`) | `fz_clone_context` per thread | all (it has its own Android/iOS viewers) | **AGPL-3** or commercial ([license](https://mupdf.readthedocs.io/en/1.26.11/license.html), [Artifex](https://artifex.com/licensing)) | new `src/core/pdf/mupdf/` |
| **pdfium** | close to MuPDF (same benchmarks); it is Chrome's engine, so real-world PDFs render with very high fidelity | yes: `FPDFAnnot_AddInkStroke`, `FPDFAnnot_SetAP`, `FPDF_ImportPages`, attachments, `FPDF_SaveAsCopy` with incremental save ([public headers](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/)) (names checked in `fpdf_annot.h`, `fpdf_ppo.h`, `fpdf_attachment.h`, `fpdf_save.h`) | **single-threaded, one global lock** ([pdfium list](https://groups.google.com/g/pdfium/c/HeZSsM_KEUk), [pdfium-render](https://github.com/ajrcarey/pdfium-render)) — this costs parallel tile rendering | all; prebuilt for iOS, Android, Windows arm64/x64 and macOS ([pdfium-binaries](https://github.com/bblanchon/pdfium-binaries)); its own build (gn) is heavy | **BSD-3 + Apache-2.0** ([LICENSE](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/LICENSE)); fine in a GPL-3+ app | new `src/core/pdf/pdfium/` |
| **Qt PDF** (`QPdfDocument`) | pdfium inside Qt; Qt WebEngine platforms plus iOS ([docs](https://doc.qt.io/qt-6/qtpdf-index.html)) | **read-only** API: render, search, links, selection | inherits pdfium's lock | where Qt WebEngine runs, plus iOS | LGPL/GPL | a quick prototype only; no write path |
| **PDFKit** (iOS/macOS) | Apple's renderer; you can render into a `CGBitmapContext` that wraps our tile memory | `PDFAnnotation` ink with `UIBezierPath`/`NSBezierPath`, saved with `PDFDocument.write` ([tutorial](https://medium.com/better-programming/ios-pdfkit-ink-annotations-tutorial-4ba19b474dce), [PSPDFKit notes](https://pspdfkit.com/blog/2019/ink-annotation-in-pdfkit/)). Developers report regressions and memory problems ([forum 722764](https://developer.apple.com/forums/thread/722764), [99789](https://developer.apple.com/forums/thread/99789)). | thread safety not documented **[unverified]** | Apple only | OS framework | `src/core/pdf/pdfkit/` (Objective-C++) |
| **Android PdfRenderer** (pdfium inside) | system pdfium | `write()` since API 35 ([diff](https://developer.android.com/sdk/api_diff/35/changes/android.graphics.pdf.PdfRenderer)); `add/update/removePageAnnotation` and page objects only since **API 37** ([diff](https://developer.android.com/sdk/api_diff/37/changes/android.graphics.pdf.PdfRenderer.Page)); `PdfRendererPreV` backports to API 30–34 ([ref](https://developer.android.com/reference/android/graphics/pdf/PdfRendererPreV)) | Java/JNI hop | Android | OS | JNI backend; not worth it next to pdfium itself |
| **Windows.Data.Pdf** | render-only WinRT API ([RenderToStreamAsync](https://learn.microsoft.com/en-us/uwp/api/windows.data.pdf.pdfpage.rendertostreamasync)) | no | async WinRT | Windows | OS | not recommended |
| **qpdf** (already linked) | no rendering | writes anything structural: copies pages between PDFs ([design notes](https://qpdf.readthedocs.io/en/12.2/design.html)), Form XObjects (`getFormXObjectForPage`, already used in `QPdfExport.cpp:102–124`), embedded files (`QPDFEmbeddedFileDocumentHelper`, present in the installed 10.6), annotations through the object API | n/a | all | **Apache-2.0** ([license](https://qpdf.readthedocs.io/en/stable/license.html)) | `PdfWriter` seam |

**A point about `/Ink` that matters for "Drawboard/Xodo-style" annotations:** an `/Ink` annotation stores
polylines (`/InkList`) with **one** border width and colour. It has no per-point width, so pressure-sensitive
strokes cannot be represented in the annotation data itself (PDF 1.7 §12.5.6.13). What other apps display is
the appearance stream `/AP`. So write the exact look as `/AP` (the ROADMAP's plan: the cairo-drawn Form XObject
that `QPdfExport` already makes) and treat `/InkList` as an approximation, so other apps can select and erase
the stroke. If another app regenerates the appearance (moves or recolours the stroke), the pressure look is
lost. Accept that, and keep the real data in the embedded `.xopp`. Highlighters need a `/BM /Multiply`
inside `/AP` **[check that Acrobat, Preview and pdfium honour it]**.

## Q3 — Hybrid PDF and "searchable text in pasted PDF pages"

| Building block | Needs | Cheapest engine | Notes |
|---|---|---|---|
| Pasted pages stay searchable (ROADMAP line 117, option 1: write a merged `name.pages.pdf`, renumber pages) | copy pages from another PDF | **qpdf, today** (`QPDF::addPage` with foreign pages; `QPdfExport` already does this for the document's own background) | still one background PDF per `.xopp`, so upstream stays compatible. Poppler renders the merged file unchanged. No reason to wait for MuPDF. |
| Other apps show strokes as they are | strokes as page content | done: `QPdfExport` overlay | already works (flattened) |
| Other apps can edit or erase strokes | `/Ink` annots with `/AP` from the overlay XObject | qpdf (object API) or MuPDF `pdf_*` | the ROADMAP M7 plan. qpdf keeps it GPL-only; MuPDF generates `/AP` itself, but not with our pressure look. |
| Reopens with every feature | the `.xopp` embedded as an attachment (as LibreOffice's [hybrid PDF](https://blog.documentfoundation.org/blog/2024/04/16/quick-tip-creating-hybrid-pdf-files-in-libreoffice/) embeds ODF) | qpdf `QPDFEmbeddedFileDocumentHelper`, or `pdf_add_embedded_file` | on open, look for the attachment. Keep a hash of the PDF bytes the `.xopp` was written against. If another app has appended an incremental update, import its new annotations as foreign ones rather than dropping them. |
| Import existing annotations | read `/Annots` | any engine | the renderer draws them today; importing them as editable strokes is separate work |
| Incremental saves (keep signatures, small diffs for sync) | append-only writing | qpdf does **not** do incremental updates **[verify]**; MuPDF and pdfium do | this is the one thing that may need MuPDF or pdfium as the writer |

So the hybrid PDF needs no platform-native engine. The same qpdf writer runs on every platform, and the
renderer only has to draw what qpdf wrote. MuPDF becomes necessary as a writer only if incremental saves
(or writing `/AP` for us) turn out to matter.

## Cheap experiments to decide

1. **Render bench (1–2 days).** Add a pdfium backend (prebuilt `pdfium-binaries`) and the MuPDF 1.19 system
   library behind `XojPdfDocumentInterface`, render-only. Run `xqt-cli bench-render` on 3 heavy PDFs (scanned,
   vector plans, text-heavy) at 1×, 2× and 4× zoom. **Measure:** tile p50/p95, time to first visible page,
   peak RSS, pixel diff against poppler. **Pick MuPDF** if it is ≥ 2× faster than poppler (the ROADMAP's target)
   and within about 20% of pdfium. **Consider pdfium** only if it beats MuPDF even with the global lock at 4
   threads, or if AGPL becomes a problem.
2. **qpdf page merge (½ day).** `xqt-cli paste-pages a.xopp b.pdf 3-5` writes `a.pages.pdf` and renumbers the
   pages. **Pass:** the text of the pasted pages is found by the app's search; the file opens unchanged in
   upstream Xournal++; 100 pages merge in under 1 s.
3. **`/Ink` + `/AP` round-trip (1 day).** Write one page of pressure strokes and highlighter as `/Ink` with the
   cairo XObject as `/AP`. Open it in Acrobat, Xodo, Drawboard, Preview/PDFKit, Chrome, Okular and Evince.
   **Pass:** a pixel diff against our render stays under a small threshold; the other app's eraser removes whole
   strokes; the file still reopens in our app afterwards.
4. **Embedded `.xopp` survival (½ day).** Take the hybrid file from experiment 3, add an ink stroke in each
   app and save. **Measure:** does the attachment survive, does the app write an incremental update or rewrite
   the whole file, and how much larger is the file.
5. **Pen latency on Surface and iPad (½ day each, needs the device).** Use a 240 fps phone camera to measure
   pen tip to ink for our app against OneNote or Windows Ink (Surface) and Notes (iPad). Also log the sample
   rate with the `xqt.input` logger or `qt/spikes/inkpad`. **Decision:** if we are within about 1 frame (≤ 17 ms)
   of the native app, skip native wet ink and do only predicted touches. If we are more than 2 frames behind,
   spike `InkPresenter` or PencilKit as a wet-only layer.
6. **Recognizer spike (1 day).** Export 50 handwritten words from real `.xopp` files as timed strokes. Feed
   them to the Windows `InkRecognizerContainer` (C++/WinRT console tool) and to ML Kit (Android emulator).
   **Measure:** word accuracy in the author's languages, latency per page, and the model download size.
   Settle the ML Kit licensing question before writing product code.
