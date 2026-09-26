#!/usr/bin/env python3
"""Makes gemoji.inc (the table compiled into xournal-qt, qt/src/markdown/EmojiData.cpp) from gemoji's db/emoji.json.

    curl -LO https://raw.githubusercontent.com/github/gemoji/<commit>/db/emoji.json
    python3 make-inc.py emoji.json > gemoji.inc
"""
import json
import sys

CATEGORIES = ["Smileys & Emotion", "People & Body", "Animals & Nature", "Food & Drink", "Travel & Places",
              "Activities", "Objects", "Symbols", "Flags"]


def c_string(s):
    out = []
    for ch in s:
        if ch in '"\\':
            out.append("\\" + ch)
        elif ord(ch) < 0x20:
            out.append("\\x%02x" % ord(ch))
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def main():
    data = json.load(open(sys.argv[1], encoding="utf-8"))
    print("// Made by make-inc.py from gemoji's db/emoji.json (MIT, see LICENSE). Do not edit.")
    print("// emoji, aliases, tags, description, category, skin tones")
    for e in data:
        print("{%s, %s, %s, %s, %d, %s}," % (
            c_string(e["emoji"]), c_string(" ".join(e["aliases"])), c_string(" ".join(e["tags"])),
            c_string(e["description"]), CATEGORIES.index(e["category"]),
            "true" if e.get("skin_tones") else "false"))


if __name__ == "__main__":
    main()
