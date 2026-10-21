#!/usr/bin/env bash
# Compile the writer's .ink sources to the JSON the engine loads.
# Needs inklecate 1.1.1 (https://github.com/inkle/ink/releases/tag/v1.1.1)
# on PATH or in $INKLECATE. Inky's "Export to JSON" produces the same file.
set -euo pipefail
cd "$(dirname "$0")/.."
INKLECATE=${INKLECATE:-inklecate}
for src in assets/dialogue/main.ink tests/data/features.ink; do
    "$INKLECATE" -o "$src.json" "$src"
    # inklecate writes a UTF-8 BOM; strip it so the file hashes the same everywhere
    sed -i '1s/^\xEF\xBB\xBF//' "$src.json"
done
