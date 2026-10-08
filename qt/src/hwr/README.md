# hwr: handwriting search

Target `xqt-hwr` (`qt/cmake/XqtHwr.cmake`). Reads handwriting in the background so the search finds it. The ink is
never converted: the readings go into the search index (and an invisible text layer in PDFs the app writes).

| Class | Job |
| --- | --- |
| `HandwritingSearch` | the app's handwriting search: the setting, the recognisers, the worker, an indexer per open document |
| `InkLayout` | a page's ink as lines and words, from the strokes' geometry alone |
| `Recognizer` | what a recogniser is to the search (one line in, per word several readings out) |
| `TrocrRecognizer`, `CtcRecognizer` | the recognisers: TrOCR-small and small CTC line models in ONNX Runtime |
| `MultiRecognizer`, `LanguagePlan` | several models (English, German) on the same lines; which models a document needs |
| `OrtRuntime` | ONNX Runtime loaded at run time (only its C API's headers are vendored) |
| `BeamSearch`, `CtcDecode`, `WordAlignment`, `LineImage` | decoding and the pieces between the layout and a model: a reading's words go on the word boxes where a CTC model read them (its frames), else by their letters' shares |
| `InkRecognitionService` | the one worker thread at idle priority shared by all documents and the library |
| `InkTextIndexer` | keeps one open document's handwriting searchable |
| `InkCopy` | "Copy handwriting as text" |
| `LineDataset`, `ModelInfo` | a document's ink as a training dataset; what a model folder holds |
| `FakeRecognizer` | scripted readings for the tests |

**May depend on**: `xqt-session` and below (upstream's model: strokes and pages; `control/settings`), Qt Gui (PNGs of
the dataset). Not on canvas, quick, shell or app (the library's job is `shell/LibraryInkJob`).

**Threads**: one worker at idle priority (`InkRecognitionService`); the library is read only on mains power.

**Tests**: `qt/tests/hwr` (label `hwr`; tiny stand-in models in `qt/tests/hwr/data`). **Docs**:
[handwriting search](../../docs/features/handwriting-search.md), the research and the training in
[`qt/research/hwr`](../../research/hwr/README.md).
