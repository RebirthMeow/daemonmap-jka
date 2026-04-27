#!/usr/bin/env bash
# build-msys2.sh - Build DaemonMap from inside an MSYS2 MinGW64 terminal
#
# Run this if you prefer working directly in the MSYS2 shell rather than
# using the PowerShell launcher (build.ps1).
#
# Requirements:
#   Open "MSYS2 MinGW 64-bit" from the Start Menu (NOT MSYS2 MSYS or UCRT64).
#   cd into this project directory, then:  ./build-msys2.sh
#
# What it does:
#   1. Verifies you are in the correct MSYS2 environment (MINGW64)
#   2. Installs all required MinGW-w64 packages via pacman
#   3. Calls easy-builder to configure, build, and install
#   4. Bundles required runtime DLLs into the install/ folder

set -e
set -o pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# -----------------------------------------------------------------------
# Verify correct MSYS2 environment
# -----------------------------------------------------------------------

if [[ "${MSYSTEM}" != "MINGW64" ]]; then
    echo ""
    echo "ERROR: Wrong MSYS2 environment."
    echo "  Current: ${MSYSTEM:-<none>}"
    echo "  Required: MINGW64"
    echo ""
    echo "Please open 'MSYS2 MinGW 64-bit' from the Start Menu and try again."
    echo "(Do NOT use 'MSYS2 MSYS', 'MSYS2 UCRT64', or 'MSYS2 CLANG64')"
    echo ""
    exit 1
fi

echo ""
echo "MSYS2 environment: OK ($MSYSTEM)"

# -----------------------------------------------------------------------
# Install build dependencies
# -----------------------------------------------------------------------

echo ""
echo "Checking / installing build dependencies..."
echo ""

PACKAGES=(
    mingw-w64-x86_64-gcc
    mingw-w64-x86_64-cmake
    mingw-w64-x86_64-make
    mingw-w64-x86_64-pkgconf
    mingw-w64-x86_64-glib2
    mingw-w64-x86_64-libxml2
    mingw-w64-x86_64-minizip
    git
)

pacman -S --needed --noconfirm "${PACKAGES[@]}"

# -----------------------------------------------------------------------
# Build
# -----------------------------------------------------------------------

echo ""
echo "Running easy-builder..."
echo ""

cd "${PROJECT_DIR}"
./easy-builder

# -----------------------------------------------------------------------
# Bundle MinGW runtime DLLs so the exe runs on machines without MSYS2
# -----------------------------------------------------------------------

EXE="${PROJECT_DIR}/install/daemonmap.exe"
DEST="${PROJECT_DIR}/install"
MINGW_BIN="/mingw64/bin"

if [[ ! -f "${EXE}" ]]; then
    echo "WARNING: ${EXE} not found — skipping DLL bundling."
    echo "Check the build output above."
    exit 1
fi

echo ""
echo "Bundling runtime DLLs into install/ ..."

# Recursively copy MinGW DLLs needed by the exe and its dependencies
bundle_dlls() {
    local binary="$1"
    # ldd lists deps; filter to those living in /mingw64/
    ldd "${binary}" 2>/dev/null \
        | awk '/\/mingw64\// { print $3 }' \
        | while IFS= read -r dll; do
            local base
            base="$(basename "${dll}")"
            local dest_file="${DEST}/${base}"
            if [[ ! -f "${dest_file}" ]]; then
                cp "${dll}" "${dest_file}"
                # Recurse to catch transitive deps
                bundle_dlls "${dest_file}"
            fi
        done
}

bundle_dlls "${EXE}"

echo ""
echo "┌──────────────────────────────────────────────────────┐"
echo "│  Build complete!                                     │"
echo "└──────────────────────────────────────────────────────┘"
echo ""
echo "  Output:  ${DEST}/"
echo "  Binary:  ${EXE}"
echo ""
echo "The install/ folder is self-contained — copy it anywhere."
echo ""
