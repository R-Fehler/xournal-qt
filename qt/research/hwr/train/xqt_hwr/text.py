"""Text as the app compares it (qt/src/session/TextMatch.*): NFC, per-character simple case folding, words as runs
of letters and digits, ligatures written out."""
from __future__ import annotations

import re
import unicodedata

_LIGATURES = {"ﬀ": "ff", "ﬁ": "fi", "ﬂ": "fl", "ﬃ": "ffi", "ﬄ": "ffl", "ﬅ": "st",
              "ﬆ": "st"}
_WORD = re.compile(r"[^\W_]+")


def nfc(text: str) -> str:
    return unicodedata.normalize("NFC", text)


def fold_char(c: str) -> str:
    """Simple case folding of one character (QChar::toCaseFolded): one character stays one ("ß" stays "ß")."""
    if c < "\x80":
        return c.lower()
    f = c.casefold()
    if len(f) == 1:
        return f
    low = c.lower()
    return low if len(low) == 1 else c


def fold(text: str) -> str:
    text = nfc(text)
    for k, v in _LIGATURES.items():
        text = text.replace(k, v)
    return "".join(fold_char(c) for c in text)


def words(text: str) -> list[str]:
    """The words of a text as the search sees them: case folded runs of letters and digits."""
    return _WORD.findall(fold(text))


def clean_line(text: str) -> str:
    """A transcription as a training target: NFC, invisible format characters left out (zero-width spaces and
    joiners, soft hyphens, byte order marks: Wikipedia's text has them, and ink never shows them), whitespace runs to
    one space, trimmed."""
    text = "".join(c for c in nfc(text) if unicodedata.category(c) != "Cf")
    return " ".join(text.split())
