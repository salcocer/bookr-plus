"""Add MEMSIZE=1 to an EBOOT.PBP's PARAM.SFO so the PSP-2000/3000 give the
app their extended memory. Usage: sfo_memsize.py <in.PBP> <out.PBP>"""
import struct
import sys

FMT_UTF8, FMT_INT = 0x0204, 0x0404


def read_sfo(sfo):
    magic, version, kt, dt, n = struct.unpack("<4sIIII", sfo[:20])
    assert magic == b"\0PSF", "not a PARAM.SFO"
    entries = []
    for i in range(n):
        ko, fmt, ln, mx, do = struct.unpack("<HHIII", sfo[20 + i * 16:36 + i * 16])
        key = sfo[kt + ko:sfo.index(b"\0", kt + ko)].decode()
        entries.append((key, fmt, ln, mx, sfo[dt + do:dt + do + mx]))
    return version, entries


def write_sfo(version, entries):
    entries = sorted(entries, key=lambda e: e[0])
    keys, data, index = b"", b"", b""
    for key, fmt, ln, mx, value in entries:
        index += struct.pack("<HHIII", len(keys), fmt, ln, mx, len(data))
        keys += key.encode() + b"\0"
        data += value.ljust(mx, b"\0")
    kt = 20 + len(index)
    keys = keys.ljust((len(keys) + 3) & ~3, b"\0")
    dt = kt + len(keys)
    return struct.pack("<4sIIII", b"\0PSF", version, kt, dt, len(entries)) + index + keys + data


pbp = open(sys.argv[1], "rb").read()
assert pbp[:4] == b"\0PBP", "not an EBOOT.PBP"
offsets = list(struct.unpack("<8I", pbp[8:40])) + [len(pbp)]
parts = [pbp[offsets[i]:offsets[i + 1]] for i in range(8)]

version, entries = read_sfo(parts[0])
entries = [e for e in entries if e[0] != "MEMSIZE"]
entries.append(("MEMSIZE", FMT_INT, 4, 4, struct.pack("<I", 1)))
if len(sys.argv) > 3:  # optional new TITLE, so both copies can sit side by side
    title = sys.argv[3].encode() + b"\0"
    entries = [e for e in entries if e[0] != "TITLE"]
    entries.append(("TITLE", FMT_UTF8, len(title), 128, title))
parts[0] = write_sfo(version, entries)

header, pos = pbp[:8], 40
new_offsets = []
for p in parts:
    new_offsets.append(pos)
    pos += len(p)
open(sys.argv[2], "wb").write(header + struct.pack("<8I", *new_offsets) + b"".join(parts))

# verify by reading back
check = open(sys.argv[2], "rb").read()
o = struct.unpack("<8I", check[8:40])
_, back = read_sfo(check[o[0]:o[1]])
print({k: (struct.unpack("<I", v[:4])[0] if f == FMT_INT else v.rstrip(b"\0").decode()) for k, f, _, _, v in back})
assert check[o[6]:o[7]] == parts[6], "program data changed"
print("program data unchanged:", len(parts[6]), "bytes")
