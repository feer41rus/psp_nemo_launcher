# Fails if any import library's NID or stub range overlaps the next one (psp-fixup-imports miscount).
import struct, sys
d = open(sys.argv[1], 'rb').read()
shoff = struct.unpack('<I', d[0x20:0x24])[0]
shentsize, shnum, shstrndx = struct.unpack('<HHH', d[0x2E:0x34])
secs = [struct.unpack('<IIIIIIIIII', d[shoff + i * shentsize:shoff + (i + 1) * shentsize]) for i in range(shnum)]
def name(o):
    s = d[secs[shstrndx][4] + o:]
    return s[:s.index(b'\0')].decode()
def v2o(a):
    for s in secs:
        if s[3] and s[3] <= a < s[3] + s[5] and s[1] != 8:
            return s[4] + a - s[3]
ents = []
for s in secs:
    if name(s[0]) == '.lib.stub':
        for i in range(s[5] // 20):
            nm, _, _, _, _, cnt, nids, stubs = struct.unpack('<IHHBBHII', d[s[4] + i * 20:s[4] + i * 20 + 20])
            o = v2o(nm)
            ents.append((d[o:d.index(b'\0', o)].decode(), cnt, nids, stubs))
bad = 0
for key, size in ((2, 4), (3, 8)):
    srt = sorted(ents, key=lambda e: e[key])
    for a, b in zip(srt, srt[1:]):
        if a[key] + a[1] * size > b[key]:
            print('OVERLAP: %s (%d) runs into %s' % (a[0], a[1], b[0])); bad = 1
print('imports: %d libraries, %s' % (len(ents), 'BAD' if bad else 'ok'))
sys.exit(bad)
