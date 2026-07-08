#!/usr/bin/env bash
# build_srs.sh -- interactive helper for the service-recovery-scheduler workspace.
#
#   ./build_srs.sh           -> shows the menu
#   ./build_srs.sh 1         -> build (release-ish, no coverage)
#   ./build_srs.sh 2         -> build with coverage + run UT + generate HTML report
#   ./build_srs.sh 3         -> build with AddressSanitizer + UBSan + run UT
#   ./build_srs.sh 4         -> build a release TGZ/DEB package via cpack
#   ./build_srs.sh 5         -> build (and optionally run) a Docker image
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
  3   Build with AddressSanitizer + UBSan and run unit tests
  4   Build a release package (cpack: TGZ + DEB when dpkg available)
  5   Build a Docker image (multi-stage) and optionally run it
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

# Nuke $BUILD_DIR when a compile-flag option (coverage / sanitizers) differs
# from the cached one. Reconfiguring FetchContent'd subprojects in-place after
# a CXXFLAGS change is racy under `make -j`: some third-party targets fail
# with `opening dependency file ... .o.d: No such file or directory` because
# per-source subdirectories get invalidated but the mkdir rule doesn't re-run
# before parallel compiles. A clean rebuild is fast enough and deterministic.
require_clean_if_flag_flipped() {
    local want_cov="$1"    # ON or OFF
    local want_san="$2"    # ON or OFF
    local cache="$BUILD_DIR/CMakeCache.txt"
    [[ -f "$cache" ]] || return 0
    local have_cov have_san
    have_cov="$(grep -oE '^ENABLE_COVERAGE:BOOL=(ON|OFF)'   "$cache" | cut -d= -f2 || true)"
    have_san="$(grep -oE '^ENABLE_SANITIZERS:BOOL=(ON|OFF)' "$cache" | cut -d= -f2 || true)"
    if { [[ -n "$have_cov" && "$have_cov" != "$want_cov" ]]; } \
       || { [[ -n "$have_san" && "$have_san" != "$want_san" ]]; }; then
        log "Build flavor changed (COVERAGE:$have_cov->$want_cov SAN:$have_san->$want_san); wiping $BUILD_DIR/"
        rm -rf "$BUILD_DIR"
    fi
}

build_all() {
    log "Building targets (jobs=$JOBS)"
    cmake --build "$BUILD_DIR" -j "$JOBS"
}

action_build() {
    require_clean_if_flag_flipped OFF OFF
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        configure -DENABLE_COVERAGE=OFF -DENABLE_SANITIZERS=OFF -DBUILD_TESTING=ON
    fi
    build_all
    printf '\n%sBuild complete.%s Artefacts under %s/\n' "$c_green$c_bold" "$c_reset" "$BUILD_DIR"
}

action_ut() {
    require_clean_if_flag_flipped ON OFF
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        configure -DENABLE_COVERAGE=ON -DENABLE_SANITIZERS=OFF -DBUILD_TESTING=ON
    fi
    build_all

    log "Running unit tests via ctest"
    # `ctest --test-dir` was added in CTest 3.20; the project supports CMake
    # 3.16 so run ctest with the build dir as cwd instead.
    ( cd "$BUILD_DIR" && ctest --output-on-failure )

    log "Generating HTML coverage + test-result dashboard"
    cmake --build "$BUILD_DIR" --target coverage

    local report="$BUILD_DIR/coverage/index.html"
    printf '\n%sUT + coverage complete.%s Report: %s\n' "$c_green$c_bold" "$c_reset" "$report"
    printf 'Open with:  xdg-open %s   (or drag the file into a browser)\n' "$report"
}

action_sanitize() {
    require_clean_if_flag_flipped OFF ON
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        configure -DENABLE_COVERAGE=OFF -DENABLE_SANITIZERS=ON -DBUILD_TESTING=ON
    fi
    build_all
    log "Running unit tests under ASan+UBSan"
    ASAN_OPTIONS="halt_on_error=1:strict_string_checks=1:detect_leaks=1" \
    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
        ctest --test-dir "$BUILD_DIR" --output-on-failure
    printf '\n%sSanitizer run complete.%s\n' "$c_green$c_bold" "$c_reset"
}

action_package() {
    require_clean_if_flag_flipped OFF OFF
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        configure -DENABLE_COVERAGE=OFF -DENABLE_SANITIZERS=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
    fi
    build_all
    log "Running cpack (TGZ + DEB if dpkg available)"
    local gens="TGZ"
    command -v dpkg >/dev/null 2>&1 && gens="$gens;DEB"
    ( cd "$BUILD_DIR" && cpack -G "$gens" )
    printf '\n%sPackage(s) written to %s/:%s\n' "$c_green$c_bold" "$BUILD_DIR" "$c_reset"
    ls -1 "$BUILD_DIR"/*.tar.gz "$BUILD_DIR"/*.deb 2>/dev/null || true
}

# Docker image build/deploy. Uses the repo Dockerfile (multi-stage: builder
# compiles from source, runtime is a slim debian with just the daemon and
# its shared libs). No dependency on the host $BUILD_DIR tree; the build
# happens entirely inside the builder stage.
action_docker() {
    command -v docker >/dev/null 2>&1 \
        || die "docker not found on PATH. Install Docker Engine first."
    [[ -f Dockerfile ]] || die "Dockerfile missing at repo root."

    local image_tag="${SRS_IMAGE:-service-recovery-scheduler:local}"
    log "Building Docker image '$image_tag' (this pulls all build deps into the builder stage)"
    docker build -t "$image_tag" .

    printf '\n%sImage built:%s %s\n' "$c_green$c_bold" "$c_reset" "$image_tag"
    docker image inspect --format \
        'size={{.Size}}B  created={{.Created}}' "$image_tag" || true

    local run_now="${SRS_DOCKER_RUN:-}"
    if [[ -z "$run_now" ]]; then
        read -r -p "Run the image now? [y/N]: " run_now || run_now="n"
    fi
    case "$run_now" in
        y|Y|yes|1|true)
            log "Starting container (Ctrl-C to stop)"
            docker run --rm -it --name srs-local "$image_tag"
            ;;
        *)
            printf '\nHint: docker run --rm -it %s\n' "$image_tag"
            printf '      docker run --rm -it -e SRS_CONSOLE=1 %s   # interactive query loop\n' "$image_tag"
            ;;
    esac
}

pick_menu() {
    # Route menu text to stderr so `$(pick_menu)` in main() only captures the choice.
    {
        printf '\n%sservice-recovery-scheduler build menu%s\n' "$c_bold" "$c_reset"
        printf '  1) Build                            (no coverage instrumentation)\n'
        printf '  2) UT       (build + run tests + generate HTML coverage report)\n'
        printf '  3) Sanitize (build with ASan+UBSan and run tests)\n'
        printf '  4) Package  (release build + cpack TGZ/DEB)\n'
        printf '  5) Docker   (build multi-stage image, optional run)\n'
        printf '  h) Help\n'
        printf '  q) Quit\n\n'
    } >&2
    local choice
    read -r -p "Select [1/2/3/4/5/h/q]: " choice
    echo "$choice"
}

main() {
    local opt="${1-}"
    if [[ -z "$opt" ]]; then
        opt="$(pick_menu)"
    fi
    case "$opt" in
        1|build)           action_build ;;
        2|ut|UT|test)      action_ut ;;
        3|san|sanitize)    action_sanitize ;;
        4|pkg|package)     action_package ;;
        5|docker)          action_docker ;;
        h|-h|--help)       usage ;;
        q|Q|quit|exit)     log "aborted"; exit 0 ;;
        *)                 usage; die "unknown option: $opt" ;;
    esac
}

main "$@"
