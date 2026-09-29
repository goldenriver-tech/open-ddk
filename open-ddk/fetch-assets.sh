#!/bin/bash
# Copyright 2026 GoldenRiver Technology. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Fetch and unpack the binary parts that are not kept in git (toolchains,
# prebuilt zircon with the part of its source the builds read, bazel and its
# downloads), checked against open-ddk/ASSETS.sha256.
#
#   open-ddk/fetch-assets.sh [--from DIR_OR_URL]
#
# By default they come from the GitHub release of the repository this is
# checked out from: <origin>/releases/download/<release>/<file>, the release
# named in ASSETS.sha256. --from takes a local directory or a base URL.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIST="$ROOT/open-ddk/ASSETS.sha256"
RELEASE="$(sed -n 's/^# ASSETS_RELEASE=//p' "$LIST")"
FROM=""
[ "${1:-}" = --from ] && FROM="$2"
if [ -z "$FROM" ]; then
  # (this repository's: garnet is a project of its own, its origin the garnet fork)
  origin=$(git -C "$ROOT" remote get-url origin 2>/dev/null || true)
  case "$origin" in
    git@github.com:*) origin="https://github.com/${origin#git@github.com:}" ;;
    ssh://git@github.com/*) origin="https://github.com/${origin#ssh://git@github.com/}" ;;
  esac
  case "$origin" in
    https://github.com/*) FROM="${origin%.git}/releases/download/$RELEASE" ;;
    *) echo "error: cannot tell where the release assets are (origin:" \
            "${origin:-none}); use --from <dir or URL>" >&2; exit 1 ;;
  esac
fi
echo "assets from $FROM"
# zircon: where zircon/ is zircon's whole source already (its makefile, and
# not open-ddk's prebuilt one), the builds build it (open-ddk/build-zircon.sh):
# open-ddk's prebuilt zircon, its source subset and kernels, is not put in
SOURCE=; [ -f "$ROOT/zircon/makefile" ] && [ ! -f "$ROOT/zircon/OPEN-DDK-PREBUILT" ] && SOURCE=1
[ -z "$SOURCE" ] || echo "zircon/ is zircon's source: not putting open-ddk's prebuilt zircon in"
DL="$ROOT/.assets"; mkdir -p "$DL"
grep -v '^#' "$LIST" | while read -r sum name; do
  # (zircon's source needs its whole toolchains, the prebuilt one a part)
  case $name in
    open-ddk-zircon-*) [ -z "$SOURCE" ] || continue ;;
    open-ddk-toolchain-clang-gcc-full-*) [ -n "$SOURCE" ] || continue ;;
    open-ddk-toolchain-clang-gcc-*) [ -z "$SOURCE" ] || continue ;;
  esac
  f="$DL/$name"
  if [ ! -f "$f" ] || ! echo "$sum  $f" | sha256sum -c --status; then
    if [ -d "$FROM" ]; then cp "$FROM/$name" "$f"; else curl -fL -o "$f" "$FROM/$name"; fi
    echo "$sum  $f" | sha256sum -c --quiet
  fi
  echo "unpacking $name"
  tar -xJf "$f" -C "$ROOT"
done
echo "assets in place"
