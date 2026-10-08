# Licence of the handwriting model `crnn-de-en-large`

This folder holds the model xournal-qt reads handwriting with (German and English): `model_int8.onnx` (the weights),
`alphabet.txt` and `model.json` (its manifest, with the sha256 of each file and what it was trained on).

**For non-commercial use.** The weights were trained by the xournal-qt project from scratch
(`qt/research/hwr/train`), on these datasets:

| Dataset | Licence |
| --- | --- |
| fhswf German handwriting (`fhswf/german_handwriting`) | AFL-3.0 |
| Synthetic lines: text from Tatoeba and Wikipedia, handwriting fonts from Google Fonts | CC BY 2.0 FR (Tatoeba), CC BY-SA 4.0 / GFDL (Wikipedia), OFL / Apache-2.0 (fonts) |
| IAM Handwriting Database (lines, via `Teklia/IAM-line`) | IAM terms: non-commercial research use |
| CVL Database (Zenodo 1492267) | CC BY-NC 4.0 |

Because IAM and CVL may be used for non-commercial purposes only, these weights are offered for non-commercial use
only. This applies to the model files in this folder, not to xournal-qt's code, which stays under the GNU General
Public License, version 2 or later (`LICENSE` in the repository). The app runs without the model: switching the
handwriting search off, or removing this folder from an installation, leaves everything else working.
