#!/usr/bin/env bash
# Subset Inter to Latin + Cyrillic + punctuation for the PSP build.
set -euo pipefail
cd "$(dirname "$0")/.."
UNICODES="U+0020-007E,U+00A0-00FF,U+0400-045F,U+2010-2027,U+2030-203A,U+2116,U+2122,U+2190-2193,U+2022,U+25A1,U+2605-2606,U+21BB"
for name in Inter-Regular Inter-SemiBold; do
    .venv/bin/pyftsubset "assets/fonts/src/$name.ttf" \
        --unicodes="$UNICODES" --layout-features='kern,liga' \
        --no-hinting --desubroutinize \
        --output-file="assets/fonts/$name.ttf"
done
cp assets/fonts/src/OFL.txt assets/fonts/OFL.txt
ls -l assets/fonts/*.ttf
