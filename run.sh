#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

available_projects() {
  local dir name
  for dir in "$root_dir"/apps/*/; do
    [[ -d "$dir" ]] || continue
    name="$(basename "$dir")"
    if [[ -f "${dir}${name}.json" ]]; then
      printf '%s\n' "$name"
    fi
  done
}

usage() {
  cat <<EOF
Usage: ./run.sh [linux|windows|android|web] [project]

A project is a directory under apps/ with an entry file named <project>.json.
Available projects: $(available_projects | paste -sd ' ' -)

If either argument is omitted, the script prompts for it.
The script launches the existing build in build/<project>/<platform>.
Run ./build.sh first if that build does not exist.
The web option serves that directory at http://127.0.0.1:8080/.
Set PORT to use another port.
The android option installs the debug APK and starts it.
On Linux, the windows option launches a cross-compiled DLL with Wine and the
.NET 8 Windows Desktop runtime. Set ALLWHITHER_WINEPREFIX to that prefix
(default: ~/.cache/allwhither-wine).
EOF
}

missing_build() {
  echo "No $1 build for '$project' at $2." >&2
  echo "Run ./build.sh $1 $project first." >&2
  exit 1
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if (( $# > 2 )); then
  usage >&2
  exit 2
fi

platform="${1:-}"
project="${2:-}"

if [[ -z "$platform" ]]; then
  read -r -p "Platform (linux/windows/android/web): " platform || {
    echo "A platform is required." >&2
    exit 2
  }
fi

if [[ -z "$project" ]]; then
  read -r -p "Allwhither project ($(available_projects | paste -sd '/' -)): " project || {
    echo "An Allwhither project is required." >&2
    exit 2
  }
fi

case "$platform" in
  linux|windows|android|web) ;;
  *)
    echo "Unknown platform '$platform'. Choose linux, windows, android, or web." >&2
    exit 2
    ;;
esac

if [[ ! "$project" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] ||
   [[ ! -f "$root_dir/apps/$project/$project.json" ]]; then
  echo "Unknown Allwhither project '$project'." >&2
  echo "Available projects: $(available_projects | paste -sd ' ' -)" >&2
  exit 2
fi

cd "$root_dir"

find_adb() {
  if command -v adb >/dev/null 2>&1; then
    command -v adb
    return
  fi
  local root candidate
  for root in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Android/Sdk"; do
    [[ -n "$root" ]] || continue
    candidate="$root/platform-tools/adb"
    if [[ -x "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return
    fi
  done
  return 1
}

is_elf() {
  local header
  [[ -f "$1" ]] || return 1
  header="$(head -c 4 -- "$1" 2>/dev/null || true)"
  [[ "$header" == $'\x7fELF' ]]
}

launch_windows() {
  local config dir wine_prefix wine_dotnet
  wine_prefix="${ALLWHITHER_WINEPREFIX:-$HOME/.cache/allwhither-wine}"
  wine_dotnet="${wine_prefix}/drive_c/Program Files/dotnet/dotnet.exe"
  for config in debug release; do
    dir="build/${project}/windows/bin/Allwhither.Windows/${config}"
    if [[ -f "${dir}/${project}.exe" ]]; then
      exec "${dir}/${project}.exe"
    fi
    if [[ ! -f "${dir}/${project}.dll" ]]; then
      continue
    fi
    # A Linux cross-compile writes an ELF apphost next to the DLL. That
    # apphost cannot load Microsoft.WindowsDesktop.App.
    if [[ -x "${dir}/${project}" ]] && ! is_elf "${dir}/${project}"; then
      exec "${dir}/${project}"
    fi
    if command -v wine >/dev/null 2>&1 && [[ -f "$wine_dotnet" ]]; then
      cd "$dir"
      export WINEPREFIX="$wine_prefix"
      exec wine "$wine_dotnet" "${project}.dll"
    fi
    if ! is_elf "${dir}/${project}" && command -v dotnet >/dev/null 2>&1; then
      exec dotnet "${dir}/${project}.dll"
    fi
    echo "No Windows runtime can launch ${dir}/${project}.dll." >&2
    echo "On Linux, install Wine and the .NET 8 Windows Desktop runtime in ${wine_prefix}." >&2
    exit 1
  done
  missing_build windows "build/${project}/windows"
}

launch_android() {
  local apk adb devices
  apk="build/${project}/android/app/outputs/apk/debug/${project}-debug.apk"
  if [[ ! -f "$apk" ]]; then
    missing_build android "$apk"
  fi
  if ! adb="$(find_adb)"; then
    echo "adb was not found. Install Android platform-tools or set ANDROID_HOME." >&2
    exit 1
  fi
  mapfile -t devices < <("$adb" devices | awk 'NR > 1 && $2 == "device" { print $1 }')
  if ((${#devices[@]} == 0)); then
    echo "No Android device or emulator is connected." >&2
    exit 1
  fi
  if ((${#devices[@]} > 1)) && [[ -z "${ANDROID_SERIAL:-}" ]]; then
    echo "More than one Android device is connected. Set ANDROID_SERIAL." >&2
    printf '  %s\n' "${devices[@]}" >&2
    exit 1
  fi
  "$adb" install -r "$apk"
  "$adb" shell am force-stop org.allwhither.starter
  "$adb" shell am start -n org.allwhither.starter/.MainActivity
}

case "$platform" in
  linux)
    app="build/${project}/linux/${project}"
    if [[ ! -x "$app" ]]; then
      missing_build linux "$app"
    fi
    exec "$app"
    ;;
  windows)
    launch_windows
    ;;
  android)
    launch_android
    ;;
  web)
    web_dir="build/${project}/web"
    if [[ ! -f "${web_dir}/payload.json" ]]; then
      missing_build web "$web_dir"
    fi
    echo "Serving http://127.0.0.1:${PORT:-8080}/"
    exec node platform/web/server.js "$web_dir"
    ;;
esac
