# Resolve a TODO.md conflict: keep HEAD's hunk, but replace the merged block's item ("- [ ] `qt/<block>`" ...) with
# the branch's version of that item. Usage: python3 todo-merge.py <block>
import re, sys
block = sys.argv[1]; p = 'TODO.md'; s = open(p).read()
def item(lines, i):
    j = i + 1
    while j < len(lines) and lines[j].startswith('  '):
        j += 1
    return i, j
def fix(m):
    a = m.group(1).splitlines(True); b = m.group(2).splitlines(True)
    key = '`qt/%s`' % block
    ia = next((i for i, l in enumerate(a) if l.startswith('- [') and key in l), None)
    ib = next((i for i, l in enumerate(b) if l.startswith('- [') and key in l), None)
    if ib is None:
        raise SystemExit('item not in the branch hunk')
    bi, bj = item(b, ib)
    if ia is None:
        raise SystemExit('item not in HEAD hunk')
    ai, aj = item(a, ia)
    return ''.join(a[:ai] + b[bi:bj] + a[aj:])
s, n = re.subn(r'<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n', fix, s, flags=re.S)
assert '<<<<<<<' not in s; open(p, 'w').write(s); print(n, 'hunks')
