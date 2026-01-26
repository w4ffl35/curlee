#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v clang-format >/dev/null 2>&1; then
  echo "error: clang-format not found on PATH" >&2
  echo "install (ubuntu/debian): sudo apt-get install clang-format" >&2
  exit 1
fi

# Format C/C++ sources tracked by git.
files=$(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx')

if [[ -z "${files}" ]]; then
  echo "no C/C++ files found to format"
  exit 0
fi

echo "running clang-format on:" 
echo "${files}" | sed 's/^/  - /'

echo "${files}" | xargs clang-format -i

echo "done"
