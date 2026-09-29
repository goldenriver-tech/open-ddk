#!/bin/bash
# Copyright 2026 GoldenRiver Technology. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Build zircon from source, as GRT's release flow does, where zircon/ is
# zircon's whole source (GRT's), not open-ddk's prebuilt one (prepare.sh
# runs this then, and build.sh and build-qemu.sh go on with what it makes).
#
#   open-ddk/build-zircon.sh
#
# The kernels carry a build id, a version string and a build time; to make
# GRT's own byte for byte, give GRT's (environment): ZIRCON_BUILDID,
# ZIRCON_VERSION_STR, ZIRCON_TIMESTAMP_DEBUG, ZIRCON_TIMESTAMP_RELEASE; and
# build at GRT's path (the debug kernel names its own files) in GRT's locale,
# LANG=en_US.UTF-8 (its symbol table is sorted in the locale's order).
set -eo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
source scripts/env.sh
source scripts/devshell/lib/vars.sh
q() { printf '"%s"' "$1"; }
[ -z "${ZIRCON_BUILDID:-}" ] || export BUILDID="$(q "$ZIRCON_BUILDID")"
[ -z "${ZIRCON_VERSION_STR:-}" ] || export NEBULA_VERSION_STR="$(q "$ZIRCON_VERSION_STR")"
at() {  # time command...: with BUILDTIMESTAMP where a time is given
  local t=$1; shift
  if [ -n "$t" ]; then BUILDTIMESTAMP="$(q "$t")" "$@"; else "$@"; fi
}
# GRT's flow is fx set-venus, then fx build-zircon (scripts/build-zircon.sh
# for the architecture, the project, and its release build); set-venus's GN
# run reads GRT's whole package lists, which open-ddk does not carry, and
# build.sh sets its own: build-zircon.sh directly, in GRT's order and times
Z=scripts/build-zircon.sh; A=(USE_CLANG=true WITH_SYZKALLER=0)
at "${ZIRCON_TIMESTAMP_DEBUG:-}" $Z -t arm64 "${A[@]}"
at "${ZIRCON_TIMESTAMP_DEBUG:-}" $Z -t venus-tee "${A[@]}"
at "${ZIRCON_TIMESTAMP_DEBUG:-}" $Z -r -t venus-tee "${A[@]}"
at "${ZIRCON_TIMESTAMP_DEBUG:-}" $Z -t venus-hee "${A[@]}"
at "${ZIRCON_TIMESTAMP_RELEASE:-}" $Z -r -t venus-hee "${A[@]}"
