#!/usr/bin/env bash
# Installs the host prerequisites required to build this project on
# Debian/Ubuntu. Everything else (CommonAPI runtimes + generators) is fetched
# in-tree by cmake/CommonAPI.cmake — no system CommonAPI install needed.
#
# Usage:
#   scripts/bootstrap.sh          # install with apt (requires sudo)
#   scripts/bootstrap.sh --check  # only report what is missing
set -euo pipefail

PKGS=(
    build-essential      # g++, make
    cmake
    pkg-config
    libdbus-1-dev        # capicxx-dbus-runtime dependency (headers only; the
                         # runtime links against a patched libdbus built in-tree)
    libexpat1-dev        # libdbus configure needs libexpat
    libsystemd-dev       # lib_srs links libsystemd
    default-jre-headless # required to launch the CommonAPI generators
    unzip                # unpack the downloaded generator zips
    git                  # FetchContent clones the runtimes
    patch                # apply COVESA libdbus patches
    ca-certificates
)

check_only=0
if [[ "${1:-}" == "--check" ]]; then
    check_only=1
fi

missing=()
for p in "${PKGS[@]}"; do
    if ! dpkg -s "$p" >/dev/null 2>&1; then
        missing+=("$p")
    fi
done

if (( ${#missing[@]} == 0 )); then
    echo "All host prerequisites already installed."
    exit 0
fi

echo "Missing packages: ${missing[*]}"
if (( check_only )); then
    exit 1
fi

SUDO=""
if [[ $EUID -ne 0 ]]; then
    if ! command -v sudo >/dev/null; then
        echo "sudo not available and not running as root; cannot install." >&2
        exit 1
    fi
    SUDO="sudo"
fi

# Refresh the package index. Mirrors occasionally 404 on updated .debs whose
# index entry is still cached locally; a plain `apt-get update` usually clears
# that up, and `--fix-missing` on install tolerates transient fetch failures.
$SUDO apt-get update

APT_INSTALL=($SUDO apt-get install -y --no-install-recommends --fix-missing)

if ! "${APT_INSTALL[@]}" "${missing[@]}"; then
    echo "First install attempt failed. Refreshing index and retrying..." >&2
    $SUDO rm -rf /var/lib/apt/lists/*
    $SUDO apt-get update
    "${APT_INSTALL[@]}" "${missing[@]}"
fi

echo "Done. You can now run: cmake -S . -B build && cmake --build build -j"
