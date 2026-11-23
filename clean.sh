#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

for path in \
  "$root_dir/build" \
  "$root_dir/platform/linux/build" \
  "$root_dir/platform/windows/bin" \
  "$root_dir/platform/windows/obj" \
  "$root_dir/platform/android/build" \
  "$root_dir/platform/android/app/build"
do
  if [[ -d "$path" ]]; then
    rm -rf -- "$path"
    printf 'Removed %s\n' "${path#"$root_dir"/}"
  fi
done

printf 'Build directories cleaned.\n'
