"""Throwaway Python port of the app's fuzzy word rules (qt/src/session/WordMatch.h) for the trials."""
import re, unicodedata

def words(text):
    t = unicodedata.normalize("NFC", text).casefold()
    return [w for w in re.findall(r"[^\W_]+", t)]

def gaps_of(term, word):
    if not word or word[0] != term[0]:
        return -1
    i, gaps, last = 0, 0, -1
    for j, c in enumerate(word):
        if i < len(term) and c == term[i]:
            if last >= 0:
                gaps += j - last - 1
            last, i = j, i + 1
    return gaps if i == len(term) else -1

def edit_distance(a, b, mx):
    if abs(len(a) - len(b)) > mx:
        return mx + 1
    d = [[0] * (len(b) + 1) for _ in range(len(a) + 1)]
    for i in range(len(a) + 1): d[i][0] = i
    for j in range(len(b) + 1): d[0][j] = j
    for i in range(1, len(a) + 1):
        for j in range(1, len(b) + 1):
            c = 0 if a[i-1] == b[j-1] else 1
            d[i][j] = min(d[i-1][j] + 1, d[i][j-1] + 1, d[i-1][j-1] + c)
            if i > 1 and j > 1 and a[i-1] == b[j-2] and a[i-2] == b[j-1]:
                d[i][j] = min(d[i][j], d[i-2][j-2] + 1)
    return min(d[-1][-1], mx + 1)

def typos_allowed(n, typos=1):
    if typos <= 0 or n < 5: return 0
    if typos >= 2 and n >= 8: return 2
    return 1

def match(term, word, typos=1):
    """2 exact, 1 fuzzy, 0 none."""
    if term in word: return 2
    g = gaps_of(term, word)
    if g >= 0 and g <= len(term) // 2: return 1
    t = typos_allowed(len(term), typos)
    if t and edit_distance(term, word, t) <= t: return 1
    return 0
