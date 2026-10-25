#!/usr/bin/env python3
"""Copy the thanks lists into the READMEs (between <!-- credits:start --> and <!-- credits:end -->):
docs/CREDITS.en.md -> README.md, docs/CREDITS.md -> README.ru.md. Run after editing the lists."""
import re
from pathlib import Path

PAIRS = [("docs/CREDITS.en.md", "README.md"), ("docs/CREDITS.md", "README.ru.md")]

for src, readme in PAIRS:
    body = []
    for line in Path(src).read_text(encoding="utf-8").splitlines():
        if line.startswith("# ") or "CREDITS" in line and line.startswith(("[", "**")):
            continue                                   # title and the language switch of the credits file
        body.append(re.sub(r"^## ", "### ", line))
    text = Path(readme).read_text(encoding="utf-8")
    block = "<!-- credits:start -->\n" + "\n".join(body).strip() + "\n<!-- credits:end -->"
    new, n = re.subn(r"<!-- credits:start -->.*?<!-- credits:end -->", block, text, flags=re.S)
    if n != 1:
        raise SystemExit("%s: credits markers not found" % readme)
    Path(readme).write_text(new, encoding="utf-8")
    print("synced", src, "->", readme)
