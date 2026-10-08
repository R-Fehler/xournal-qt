# Handwriting models not shipped

Exported model folders (FORMATS.md §2: `model.json`, the int8 ONNX file, the alphabet, `LICENCE.md`) that are ready to
replace the bundled one without training again. The bundled model is the folder under `qt/resources/hwr/`; every folder
there goes into every package, and the app uses whichever reads a language, so keep exactly one there.

| Folder | What | fhswf test (German) | IAM test (English) | Time per line* | Size |
| --- | --- | ---: | ---: | ---: | ---: |
| `qt/resources/hwr/crnn-de-en` (**shipped**) | shared CTC, 5.7 M parameters, best at step 62,000 | 97.5 % | 90.3 % | 0.070 s | 9.2 MB |
| `crnn-de-en-large` | shared CTC, 12.7 M parameters (`configs/en-de-combined-ctc-large.yaml`), best at step 70,000 | 98.0 % | 91.0 % | 0.130 s | 20.4 MB |

Words found on the held-out test lines, read as the app reads them (int8, ONNX Runtime on 2 threads, the app's
decoding). *On a Zen+ desktop CPU (2018); a phone is in the same range.

To ship another model: `git mv qt/resources/hwr/<shipped> qt/research/hwr/models/` and
`git mv qt/research/hwr/models/<other> qt/resources/hwr/`, then build. The packages, the smoke tests
(`xournal-qt --hwr-info`, `qt/scripts/hwr-package-check.sh`) and the app find the model by its `model.json`; nothing
names it. Results the search stored are named by the model, so the library is read again once.
