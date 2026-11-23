#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

available_projects() {
  local dir name
  for dir in "$root_dir"/projects/*/; do
    [[ -d "$dir" ]] || continue
    name="$(basename "$dir")"
    if [[ -f "${dir}${name}.json" ]]; then
      printf '%s\n' "$name"
    fi
  done
}

usage() {
  cat <<EOF
Usage: ./build.sh [linux|windows|android|web] [project]

A project is a directory under projects/ with an entry file named <project>.json.
Available projects: $(available_projects | paste -sd ' ' -)

If either argument is omitted, the script prompts for it.
Each build is written to build/<project>/<platform>.
The web option then serves that directory.
EOF
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
   [[ ! -f "$root_dir/projects/$project/$project.json" ]]; then
  echo "Unknown Allwhither project '$project'." >&2
  echo "Available projects: $(available_projects | paste -sd ' ' -)" >&2
  exit 2
fi

java_feature_version() {
  "$1" -version 2>&1 | sed -n 's/.* version "\([0-9][0-9]*\).*/\1/p' | head -1
}

android_jdk_is_compatible() {
  local version="$1"
  [[ "$version" =~ ^[0-9]+$ ]] && (( version >= 17 && version <= 21 ))
}

android_jdk_home_from_java() {
  local java_bin resolved
  java_bin="$1"
  resolved="$(readlink -f "$java_bin")"
  dirname "$(dirname "$resolved")"
}

find_android_jdk() {
  local candidate version home wanted pattern
  if [[ -x /usr/libexec/java_home ]]; then
    for wanted in 21 17; do
      home="$(/usr/libexec/java_home -v "$wanted" 2>/dev/null || true)"
      if [[ -n "$home" && -x "$home/bin/javac" ]]; then
        version="$(java_feature_version "$home/bin/java")"
        if android_jdk_is_compatible "$version"; then
          printf '%s\n' "$home"
          return 0
        fi
      fi
    done
  fi
  for pattern in \
    "/usr/lib/jvm/java-21-openjdk-*" \
    "/usr/lib/jvm/java-17-openjdk-*" \
    "/usr/lib/jvm/jdk-21*" \
    "/usr/lib/jvm/jdk-17*"
  do
    for candidate in $pattern; do
      [[ -x "$candidate/bin/javac" ]] || continue
      version="$(java_feature_version "$candidate/bin/java")"
      if android_jdk_is_compatible "$version"; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done
  done
  return 1
}

prepare_android_toolchain() {
  local java_bin="" current_home="" current_version="" home=""
  if [[ -n "${JAVA_HOME:-}" && -x "$JAVA_HOME/bin/java" ]]; then
    current_home="$JAVA_HOME"
    java_bin="$JAVA_HOME/bin/java"
  else
    java_bin="$(command -v java || true)"
    if [[ -n "$java_bin" && -x "$java_bin" ]]; then
      current_home="$(android_jdk_home_from_java "$java_bin")"
    fi
  fi
  if [[ -n "$java_bin" && -x "$java_bin" ]]; then
    current_version="$(java_feature_version "$java_bin")"
  fi
  if android_jdk_is_compatible "$current_version" && [[ -n "$current_home" && -x "$current_home/bin/javac" ]]; then
    export JAVA_HOME="$current_home"
  else
    home="$(find_android_jdk)" || {
      echo "Android builds need JDK 17 or 21. The current Java is ${current_version:-unavailable}." >&2
      exit 1
    }
    if [[ -n "$current_version" ]]; then
      echo "Java $current_version cannot build this Android project. Using $home." >&2
    fi
    export JAVA_HOME="$home"
  fi

  if [[ -z "${ANDROID_HOME:-}" && -n "${ANDROID_SDK_ROOT:-}" ]]; then
    export ANDROID_HOME="$ANDROID_SDK_ROOT"
  fi
  if [[ -z "${ANDROID_HOME:-}" ]]; then
    local candidate
    for candidate in \
      "$HOME/Android/Sdk" \
      "$HOME/Library/Android/sdk" \
      "/opt/android-sdk" \
      "/usr/lib/android-sdk"
    do
      if [[ -d "$candidate/platforms" ]]; then
        export ANDROID_HOME="$candidate"
        break
      fi
    done
  fi
  if [[ -z "${ANDROID_HOME:-}" || ! -d "$ANDROID_HOME/platforms" ]]; then
    echo "Android SDK not found. Set ANDROID_HOME to the SDK directory." >&2
    exit 1
  fi
  export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$ANDROID_HOME}"
}

cd "$root_dir"

case "$platform" in
  linux)
    cmake -S platform/linux -B "build/${project}/linux" "-DALLWHITHER_PROJECT=$project"
    cmake --build "build/${project}/linux"
    echo "Built build/${project}/linux/${project}"
    ;;
  windows)
    dotnet build platform/windows/Allwhither.Windows.csproj \
      --artifacts-path "build/${project}/windows" \
      "-p:EnableWindowsTargeting=true" "-p:AllwhitherProject=$project"
    echo "Built build/${project}/windows"
    ;;
  android)
    prepare_android_toolchain
    (
      cd platform/android
      ./gradlew assembleDebug "-PallwhitherProject=$project"
    )
    echo "Built build/${project}/android"
    ;;
  web)
    web_dir="build/${project}/web"
    rm -rf "$web_dir"
    mkdir -p "$web_dir"
    for file in platform/web/*; do
      name="$(basename "$file")"
      [[ -f "$file" && "$name" != "server.js" ]] || continue
      cp "$file" "$web_dir/"
    done
    python3 tools/embed_resources.py \
      --platform json \
      --project-dir "projects/${project}" \
      --output "${web_dir}/payload.json"
    echo "Built ${web_dir}"
    echo "Serving http://127.0.0.1:${PORT:-8080}/"
    exec node platform/web/server.js "$web_dir"
    ;;
esac
