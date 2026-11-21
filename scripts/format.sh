#!/usr/bin/env bash
# Formats (or checks) every C++ source with the project's .clang-format.
#
#   scripts/format.sh           format in place
#   scripts/format.sh --check   report violations and change nothing (this is what the CI runs)
#
# The CI uses clang-format 18, the version shipped with Ubuntu 24.04 (`sudo apt install clang-format`).
# Other versions format a few constructs differently, so use the same one.
# Override the binary with CLANG_FORMAT=clang-format-18 if several versions are installed.
set -euo pipefail

cd "$(dirname "$0")/.."
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
echo "using: $("$CLANG_FORMAT" --version)"

dirs=()
for dir in src tests tools benchmarks; do
  if [ -d "$dir" ]; then
    dirs+=("$dir")
  fi
done

mode=(-i)
if [ "${1:-}" = "--check" ]; then
  mode=(--dry-run --Werror)
fi

find "${dirs[@]}" \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" \) -print0 \
  | xargs -0 -r "$CLANG_FORMAT" "${mode[@]}"
echo "done"
