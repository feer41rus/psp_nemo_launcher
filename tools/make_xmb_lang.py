#!/usr/bin/env python3
"""Build lang_ru.json / lang_en.json that rename ARK's "Custom Launcher" XMB item to "Nemo Launcher".

ARK (XMBControl, LoadTextLanguage) reads ARK_01234/lang_<lang>.json before LANG.ARK, and the ARK
updater does not touch these files. lang_ru.json is ARK's own Russian file with one value changed;
lang_en.json holds only that key (English has no file in LANG.ARK, the rest stays ARK's default).

Usage: make_xmb_lang.py LANG.ARK OUTDIR
"""
import re
import struct
import sys
from pathlib import Path

KEY = "xmbmsgtop_custom_launcher"
NAME = "Nemo Launcher"
APP_KEY = "xmbmsgtop_custom_app"          # Nemo's backup copy in PSP/APP/CUSTOM (1.1.11)
APP_NAME = {"ru": "Nemo Launcher (резерв)", "en": "Nemo Launcher (backup)"}


def read_pkg(data: bytes) -> dict:
    """LANG.ARK layout (findPkgOffset in ARK-5/XMBControl/src/xmbpatch.c):
    entries of u32 offset, u32 name length, name + NUL; 0xFFFFFFFF ends the table."""
    entries, pos = [], 0
    while True:
        (offset,) = struct.unpack_from("<I", data, pos)
        if offset == 0xFFFFFFFF:
            break
        (namelen,) = struct.unpack_from("<I", data, pos + 4)
        name = data[pos + 8:pos + 8 + namelen].decode("ascii")
        entries.append((name, offset))
        pos += 8 + namelen + 1
    ends = [off for _, off in entries[1:]] + [len(data)]
    return {name: data[off:end] for (name, off), end in zip(entries, ends)}


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    files = read_pkg(Path(sys.argv[1]).read_bytes())
    if "lang_ru.json" not in files:
        sys.exit("lang_ru.json not found in " + sys.argv[1])
    ru = files["lang_ru.json"].decode("utf-8")
    pattern = re.compile(r'("%s"\s*:\s*")[^"]*(")' % KEY)
    ru_new, n = pattern.subn(r"\g<1>%s\g<2>" % NAME, ru)
    if n != 1:
        sys.exit('expected exactly one "%s" in lang_ru.json, found %d' % (KEY, n))
    app = re.compile(r'("%s"\s*:\s*")[^"]*(")' % APP_KEY)
    ru_new, n = app.subn(r"\g<1>%s\g<2>" % APP_NAME["ru"], ru_new)
    if n != 1:
        sys.exit('expected exactly one "%s" in lang_ru.json, found %d' % (APP_KEY, n))
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    (out / "lang_ru.json").write_bytes(ru_new.encode("utf-8"))
    (out / "lang_en.json").write_text('{\n    "%s": "%s",\n    "%s": "%s"\n}\n' % (KEY, NAME, APP_KEY, APP_NAME["en"]),
                                      encoding="utf-8")
    print("xmb name: %s/lang_ru.json, %s/lang_en.json" % (out, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
