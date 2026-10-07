# Resolve append-style conflicts by keeping ours, then theirs. Usage: python3 keep-both.py FILE...
# For Markdown (docs, TODO.md); not for code or CMake, whose conflicts need a human.
import re
import sys

for p in sys.argv[1:]:
    s = open(p).read()
    cmake = p.endswith(('.cmake', 'CMakeLists.txt'))

    def both(m):
        a, b = m.group(1), m.group(2)
        if cmake:
            a = '\n'.join(l for l in a.split('\n') if l.strip()) + '\n' if a.strip() else ''
            b = '\n'.join(l for l in b.split('\n') if l.strip()) + '\n' if b.strip() else ''
            if a.rstrip().endswith(')') and b.rstrip().endswith(')'):
                return a.rstrip()[:-1] + '\n' + b
            return a + b
        return a + ('' if a.endswith('\n\n') or not a else '\n') + b

    s, n = re.subn(r'<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n', both, s, flags=re.S)
    assert '<<<<<<<' not in s and '>>>>>>>' not in s, p
    open(p, 'w').write(s)
    print(p, n, 'hunks')
