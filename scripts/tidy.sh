#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

build_dir=${1:-build/linux-debug}

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "error: clang-tidy not found on PATH" >&2
  echo "install (ubuntu/debian): sudo apt-get install clang-tidy" >&2
  exit 1
fi

if [[ ! -f "${build_dir}/compile_commands.json" ]]; then
  echo "error: ${build_dir}/compile_commands.json not found" >&2
  echo "hint: run: cmake --preset linux-debug" >&2
  exit 1
fi

files=$(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx')

if [[ -z "${files}" ]]; then
  echo "no C/C++ source files found to lint"
  exit 0
fi

echo "running clang-tidy using build dir: ${build_dir}"

echo "${files}" | xargs clang-tidy -p "${build_dir}"

echo "done"
