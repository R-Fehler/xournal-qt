# gemoji (vendored data)

The emoji shortcodes (`:smile:` → 😄) and the words the emoji picker searches, from GitHub's
[gemoji](https://github.com/github/gemoji) `db/emoji.json` at commit `ee6e06c648e85d601756b573d356327d4e51031d`
(2023-03-29, Unicode 15; MIT license, `LICENSE`). 1870 emoji, 1913 shortcodes.

Only the data is used, reduced to a C++ table (`gemoji.inc`, 110 KB instead of the JSON's 410 KB) by
`make-inc.py`; see there for how to update it. Used by `qt/src/markdown/EmojiData.cpp`.
