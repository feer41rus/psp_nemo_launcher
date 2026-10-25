#!/usr/bin/env python3
"""docs/CREDITS(.en).md -> plain text for "About › Thanks" ("# " lines are headings in Nemo)."""
import re
import sys

out = []
for line in open(sys.argv[1], encoding="utf-8").read().splitlines():
    if line.startswith("# ") or ("CREDITS" in line and line.startswith(("[", "**"))):
        continue                                   # title and language switch: the page has its own
    line = re.sub(r"^#{2,}\s*", "# ", line)
    line = re.sub(r"^\s*-\s+", "- ", line)
    line = line.replace("**", "").replace("`", "")
    out.append(line)
open(sys.argv[2], "w", encoding="utf-8").write("\n".join(out).strip() + "\n")
