#!/bin/bash
# Configure and build on macOS using the system Bash (3.2 or newer).
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]

  --configuration Release|Debug  Build configuration (default: Release)
  --test                         Build all targets and run non-GPU tests
  --run                          Start the application after a successful build/test
  --jobs N                       Parallel build jobs, 1-1024 (default: at most 8)
  -h, --help                     Show this help

Requires macOS, Xcode Command Line Tools, CMake 3.24+, Ninja and the Vulkan SDK.
If the SDK is not installed in system search paths, source its setup-env.sh first.
The first build downloads dependencies. Output:
  build/macos-release/Release/emergences
  build/macos-debug/Debug/emergences
EOF
}

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

run_command() {
    printf '\n>'
    printf ' %q' "$@"
    printf '\n'
    if "$@"; then
        return 0
    else
        local status=$?
        printf 'Command failed (exit code %s). See output above.\n' "$status" >&2
        exit "$status"
    fi
}

configuration=Release
run_tests=false
run_app=false
jobs=''

while [ "$#" -gt 0 ]; do
    case "$1" in
        --configuration)
            [ "$#" -ge 2 ] || fail '--configuration requires Release or Debug.'
            case "$2" in
                Release|release) configuration=Release ;;
                Debug|debug) configuration=Debug ;;
                *) fail '--configuration must be Release or Debug.' ;;
            esac
            shift 2
            ;;
        --test) run_tests=true; shift ;;
        --run) run_app=true; shift ;;
        --jobs)
            [ "$#" -ge 2 ] || fail '--jobs requires a number from 1 to 1024.'
            [[ "$2" =~ ^[1-9][0-9]*$ ]] && [ "${#2}" -le 4 ] && [ "$2" -le 1024 ] \
                || fail '--jobs must be a number from 1 to 1024.'
            jobs=$2
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) fail "Unknown option: $1 (use --help for usage)." ;;
    esac
done

[ "$(uname -s)" = Darwin ] || fail 'This script builds the macOS application.'
for tool in cmake ninja xcrun; do
    command -v "$tool" >/dev/null 2>&1 || fail "Required tool not found in PATH: $tool. See --help."
done
xcrun --find clang++ >/dev/null 2>&1 \
    || fail 'Apple Clang was not found. Install or select Xcode Command Line Tools.'
if [ "$run_tests" = true ]; then
    command -v ctest >/dev/null 2>&1 || fail 'CTest was not found in PATH. Repair the CMake installation.'
fi

if [ -z "$jobs" ]; then
    jobs=$(sysctl -n hw.logicalcpu 2>/dev/null || printf '1')
    [[ "$jobs" =~ ^[1-9][0-9]*$ ]] || jobs=1
    if [ "$jobs" -gt 8 ]; then
        jobs=8
    fi
fi

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
case "$configuration" in
    Release) build_dir="$project_dir/build/macos-release" ;;
    Debug) build_dir="$project_dir/build/macos-debug" ;;
esac
cd "$project_dir"

printf 'Building emergences (%s, macOS, %s parallel jobs)\n' "$configuration" "$jobs"
if [ -n "${VULKAN_SDK:-}" ]; then
    printf 'Vulkan SDK: %s\n' "$VULKAN_SDK"
fi

# Let CMake find Vulkan in VULKAN_SDK or system search paths and report missing
# headers, libraries or glslc before downloading dependencies.
run_command cmake -S "$project_dir" -B "$build_dir" -G Ninja \
    "-DCMAKE_BUILD_TYPE=$configuration" -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DEMERGENCES_BUILD_APP=ON -DBUILD_TESTING=ON

build_args=(--build "$build_dir" --parallel "$jobs")
if [ "$run_tests" = false ]; then
    build_args+=(--target emergences)
fi
run_command cmake "${build_args[@]}"

if [ "$run_tests" = true ]; then
    # Exclude GPU tests even when they are enabled in an existing CMake cache.
    run_command ctest --test-dir "$build_dir" -C "$configuration" \
        --output-on-failure --no-tests=error -LE gpu
fi

output_dir="$build_dir/$configuration"
executable="$output_dir/emergences"
[ -x "$executable" ] || fail "Build finished but the executable is missing: $executable"
[ -d "$output_dir/shaders" ] || fail "Build finished but shaders are missing: $output_dir/shaders"
[ -f "$output_dir/emergences.conf.example" ] \
    || fail "Build finished but the configuration sample is missing: $output_dir/emergences.conf.example"
printf '\nBuild succeeded: %s\n' "$executable"
printf 'Keep the adjacent shaders/ directory and emergences.conf.example with the executable.\n'

if [ "$run_app" = true ]; then
    cd "$output_dir"
    run_command "$executable"
fi
