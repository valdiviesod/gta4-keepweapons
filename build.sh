#!/usr/bin/env bash
# Builds KeepWeapons.asi (32-bit Windows DLL) with a user-local Zig cross compiler.
#
#   ./build.sh               build using ./tools/zig-linux-x86_64-0.13.0/zig (or $ZIG, or zig on PATH)
#   ./build.sh --fetch-zig   download zig 0.13.0 from ziglang.org into ./downloads/zig, verify its
#                            sha256, extract it into ./tools, then build
#   ./build.sh --test        also build and run the host unit test of the pattern scanner
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
ZIG_VER="0.13.0"
ZIG_TARBALL="zig-linux-x86_64-${ZIG_VER}.tar.xz"
ZIG_URL="https://ziglang.org/download/${ZIG_VER}/${ZIG_TARBALL}"
ZIG_SHA256="d45312e61ebcc48032b77bc4cf7fd6915c11fa16e4aad116b66c9468211230ea"
ZIG_HOME="${DIR}/tools/zig-linux-x86_64-${ZIG_VER}"

DO_FETCH=0
DO_TEST=0
for arg in "$@"; do
    case "$arg" in
        --fetch-zig) DO_FETCH=1 ;;
        --test) DO_TEST=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

if [ "$DO_FETCH" = 1 ] && [ ! -x "${ZIG_HOME}/zig" ]; then
    mkdir -p "${DIR}/downloads/zig"
    if [ ! -f "${DIR}/downloads/zig/${ZIG_TARBALL}" ]; then
        curl -fSL -o "${DIR}/downloads/zig/${ZIG_TARBALL}" "${ZIG_URL}"
    fi
    echo "${ZIG_SHA256}  ${DIR}/downloads/zig/${ZIG_TARBALL}" | sha256sum -c -
    mkdir -p "${DIR}/tools"
    tar -xJf "${DIR}/downloads/zig/${ZIG_TARBALL}" -C "${DIR}/tools"
fi

if [ -n "${ZIG:-}" ]; then
    ZIG_BIN="$ZIG"
elif [ -x "${ZIG_HOME}/zig" ]; then
    ZIG_BIN="${ZIG_HOME}/zig"
elif command -v zig >/dev/null 2>&1 && [ "$(zig version)" = "${ZIG_VER}" ]; then
    ZIG_BIN="$(command -v zig)"
else
    echo "zig ${ZIG_VER} not found. Run: ./build.sh --fetch-zig" >&2
    exit 1
fi
echo "using zig: ${ZIG_BIN} ($("${ZIG_BIN}" version))"

CFLAGS=(-target x86-windows-gnu -std=gnu11 -O2 -Wall -Wextra -fno-strict-aliasing)

if [ "$DO_TEST" = 1 ]; then
    cc -std=gnu11 -O1 -Wall -Wextra -I "${DIR}/src" -o "${DIR}/tests/test_patscan" "${DIR}/tests/test_patscan.c"
    "${DIR}/tests/test_patscan"
fi

"${ZIG_BIN}" cc "${CFLAGS[@]}" -shared \
    -I "${DIR}/src" \
    -o "${DIR}/KeepWeapons.asi" \
    "${DIR}/src/KeepWeapons.c" \
    -lkernel32 -luser32

# zig also writes an import library and a PDB next to the output; the plugin does not need them.
rm -f "${DIR}/KeepWeapons.lib" "${DIR}/KeepWeapons.pdb"

file "${DIR}/KeepWeapons.asi"
ls -l "${DIR}/KeepWeapons.asi"
