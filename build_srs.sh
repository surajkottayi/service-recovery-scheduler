#!/usr/bin/env bash
# build_srs.sh -- interactive helper for the service-recovery-scheduler workspace.
#
#   ./build_srs.sh           -> shows the menu
#   ./build_srs.sh 1         -> build (release-ish, no coverage)
#   ./build_srs.sh 2         -> build with coverage + run UT + generate HTML report
#   ./build_srs.sh -h        -> help
#
# All build artefacts land in ./build/  (repo-relative).

set -euo pipefail

# Always run from the repo root, regardless of caller's cwd.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build"
JOBS="$(nproc 2>/dev/null || echo 4)"

c_reset=$'\033[0m'; c_bold=$'\033[1m'; c_green=$'\033[32m'; c_cyan=$'\033[36m'; c_red=$'\033[31m'

log()  { printf '%s[build_srs.sh]%s %s\n' "$c_cyan" "$c_reset" "$*"; }
die()  { printf '%s[build_srs.sh] error:%s %s\n' "$c_red" "$c_reset" "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Usage: ./build_srs.sh [option]

  1   Build the project (no coverage instrumentation)
  2   Build with coverage, run unit tests, generate HTML report
  h   Show this help

If no option is given, an interactive menu is shown.
EOF
}

configure() {
    local extra_flags=("$@")
    log "Configuring (${extra_flags[*]:-defaults})"
    # -Wno-dev silences developer warnings from third-party FetchContent'd
    # projects (CMP0024, CMAKE_SKIP_INSTALL_RULES) that we can't fix upstream.
    cmake -S . -B "$BUILD_DIR" -Wno-dev "${extra_flags[@]}"
}

# Nuke $BUILD_DIR when the requested coverage state differs from the cached
# one. Reconfiguring FetchContent'd subprojects in-place after a CXXFLAGS
# change (e.g. adding --coverage) is racy under `make -j`: some third-party
# targets fail with `opening dependency file ... .o.d: No such file or
# directory` because per-source subdirectories get invalidated but the mkdir
# rule doesn't re-run before parallel compiles. A clean rebuild is fast enough
# and completely deterministic.
require_clean_if_flag_flipped() {
    local want_cov="$1"    # ON or OFF
    local cache="$BUILD_DIR/CMakeCache.txt"
    [[ -f "$cache" ]] || return 0
    local have_cov
    have_cov="$(grep -oE '^ENABLE_COVERAGE:BOOL=(ON|OFF)' "$cache" | cut -d= -f2 || true)"
    if [[ -n "$have_cov" && "$have_cov" != "$want_cov" ]]; then
        log "Coverage flag changed ($have_cov -> $want_cov); wiping $BUILD_DIR/ for a clean rebuild"
        rm -rf "$BUILD_DIR"
    fi
}

build_all() {
    log "Building targets (jobs=$JOBS)"
    cmake --build "$BUILD_DIR" -j "$JOBS"
}

action_build() {
    require_clean_if_flag_flipped OFF
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        configure -DENABLE_COVERAGE=OFF -DBUILD_TESTING=ON
    fi
    build_all
    printf '\n%sBuild complete.%s Artefacts under %s/\n' "$c_green$c_bold" "$c_reset" "$BUILD_DIR"
}

action_ut() {
    require_clean_if_flag_flipped ON
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        configure -DENABLE_COVERAGE=ON -DBUILD_TESTING=ON
    fi
    build_all

    log "Running unit tests via ctest"
    ctest --test-dir "$BUILD_DIR" --output-on-failure

    log "Generating HTML coverage + test-result dashboard"
    cmake --build "$BUILD_DIR" --target coverage

    local report="$BUILD_DIR/coverage/index.html"
    printf '\n%sUT + coverage complete.%s Report: %s\n' "$c_green$c_bold" "$c_reset" "$report"
    printf 'Open with:  xdg-open %s   (or drag the file into a browser)\n' "$report"
}

pick_menu() {
    # Route menu text to stderr so `$(pick_menu)` in main() only captures the choice.
    {
        printf '\n%sservice-recovery-scheduler build menu%s\n' "$c_bold" "$c_reset"
        printf '  1) Build                            (no coverage instrumentation)\n'
        printf '  2) UT   (build + run tests + generate HTML coverage report)\n'
        printf '  h) Help\n'
        printf '  q) Quit\n\n'
    } >&2
    local choice
    read -r -p "Select [1/2/h/q]: " choice
    echo "$choice"
}

main() {
    local opt="${1-}"
    if [[ -z "$opt" ]]; then
        opt="$(pick_menu)"
    fi
    case "$opt" in
        1|build)         action_build ;;
        2|ut|UT|test)    action_ut ;;
        h|-h|--help)     usage ;;
        q|Q|quit|exit)   log "aborted"; exit 0 ;;
        *)               usage; die "unknown option: $opt" ;;
    esac
}

main "$@"
