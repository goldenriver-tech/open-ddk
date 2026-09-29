# Copyright 2026 GoldenRiver Technology. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# What every open-ddk build does first, in the tree's root ($ROOT, the
# current directory): sourced by build.sh and build-qemu.sh.

[ -d garnet ] || { echo "error: no garnet/ here; check the garnet repository out at $ROOT/garnet" >&2; exit 1; }

# Third-party code and the forks are projects of their own (jiri's
# manifest/open-ddk, open-ddk/THIRD-PARTY.tsv): upstream at its commit, or --
# where GRT changed it -- GRT's commits on a fork of upstream; so is garnet,
# a fork of upstream's. jiri checks them out (README.md); open-ddk/projects.py
# checks out what is missing, with git alone. (A release snapshot has their
# files.) Before what follows makes a snapshot a local repository.
# (a checkout of open-ddk: this directory a repository's top with a commit;
# not a snapshot's local repository, which has none, nor one around it)
if [ "$(git rev-parse --show-toplevel 2>/dev/null)" = "$(pwd -P)" ] &&
   git rev-parse -q --verify HEAD >/dev/null 2>&1 && [ -f manifest/open-ddk ]; then
  python3 open-ddk/projects.py >/dev/null
fi

# zircon: open-ddk's prebuilt one (open-ddk/fetch-assets.sh puts it in where
# there is no zircon/: what the builds read of its source, and its kernels
# built, zircon/OPEN-DDK-PREBUILT), or zircon's whole source (GRT's, checked
# out at zircon/), which open-ddk/build-zircon.sh builds first.
[ -f zircon/makefile ] || { echo "error: no zircon/ here: run open-ddk/fetch-assets.sh (open-ddk's prebuilt zircon)," \
                        "or check zircon's source out at $ROOT/zircon" >&2; exit 1; }
ZIRCON_SOURCE=; [ -f zircon/OPEN-DDK-PREBUILT ] || ZIRCON_SOURCE=1
if [ -n "$ZIRCON_SOURCE" ] && [ -f out/build-zircon/OPEN-DDK-PREBUILT ]; then
  echo "zircon/ is zircon's source: building it, not open-ddk's prebuilt kernels"
  rm -rf out/build-zircon out/build-zircon-release
fi
# The GN build lists zircon's sources with `git ls-files`: zircon/ that did
# not come from git (open-ddk's prebuilt one) gets a local repository.
if [ -z "$ZIRCON_SOURCE" ] && [ ! -e zircon/.git ]; then
  git -C zircon init -q && git -C zircon add -A -f -- . ':(exclude)prebuilt/downloads'
fi
# and a tree that did not come from git at all (a release's zip) one at its
# root, as build.sh asks git what the build changed (zircon/ has its own)
if [ "$(git rev-parse --show-toplevel 2>/dev/null)" != "$(pwd -P)" ]; then
  git init -q
  x=(); [ ! -e garnet/.git ] || x=(':(exclude)garnet')
  git add -A -f -- . "${x[@]}" ':(exclude)zircon' ':(exclude)out' ':(exclude).assets'
fi

# What garnet's build reads that the garnet repository cannot carry: the
# third-party Go packages jiri nests in it (open-ddk/overlay/garnet/). A file
# the checkout already has is left alone.
if [ -d open-ddk/overlay ]; then
  (cd open-ddk/overlay && find . -type f) | while read -r f; do
    [ -e "$f" ] || { mkdir -p "$(dirname "$f")"; cp -a "open-ddk/overlay/$f" "$f"; }
  done
fi

# What open-ddk does not carry and a GN file of what it does refers to
# (open-ddk/gn-stubs/<dir>/BUILD.gn, the nebula SDK): a stub where GN looks
# when <dir> has no BUILD.gn of its own, build/secondary/<dir>/.
if [ -d open-ddk/gn-stubs ]; then
  (cd open-ddk/gn-stubs && find . -name BUILD.gn | sed 's|^\./||') | while read -r f; do
    [ -e "$f" ] || { mkdir -p "build/secondary/$(dirname "$f")"; cp "open-ddk/gn-stubs/$f" "build/secondary/$f"; }
  done
  # (an imported file has no secondary place: its stub goes where it is
  # imported from, which its project's git then ignores)
  (cd open-ddk/gn-stubs && find . -name '*.gni' | sed 's|^\./||') | while read -r f; do
    [ ! -e "$f" ] || continue
    cp "open-ddk/gn-stubs/$f" "$f"
    top=$(git -C "$(dirname "$f")" rev-parse --show-toplevel)
    ex=$(git -C "$top" rev-parse --absolute-git-dir)/info/exclude; mkdir -p "$(dirname "$ex")"
    rel=/$(realpath --relative-to="$top" "$f"); grep -qxF "$rel" "$ex" 2>/dev/null || echo "$rel" >> "$ex"
  done
fi

# Fixes for a build host whose make is 4.3 (open-ddk/host-compat/<project>/),
# over the tag's sources where they are not in yet.
for d in open-ddk/host-compat/*/; do
  [ -d "$d" ] || continue
  p=$(basename "$d")
  for f in "$d"*.patch; do
    patch -d "$p" -p1 -R -s -f --dry-run < "$f" >/dev/null 2>&1 ||
      patch -d "$p" -p1 -s -f < "$f"
  done
done

# zircon's prebuilt bootfs manifests name files by absolute path: write them
# for wherever this tree is (their templates have @ROOT@)
find out -name bootfs.manifest.in | while read -r m; do
  sed "s#@ROOT@/#$ROOT/#g" "$m" > "${m%.in}"
done

source scripts/env.sh
[ -z "$ZIRCON_SOURCE" ] || bash open-ddk/build-zircon.sh
# The device-tree compiler: garnet builds a dtb into the image with whatever
# `dtc` is on PATH, so take the one build-env ships, not the host's.
export PATH="$ROOT/open-ddk/host-bin:$PATH"

# `fx set-venus PROJECT`, with the given package files in place of the
# project's own: set-venus's `gn gen` loads every package of
# garnet/packages/PROJECT and build/packages/bootfs, and with them sources
# open-ddk does not publish. The same args.gn and .config otherwise
# (VENUS_TEE: set-venus's --tee, nebula by default).
venus_set() {  # project package-file...
  local p=$1 d="out/$1/arm64"; shift
  mkdir -p "$d"; rm -rf out/arm64; ln -s "$ROOT/$d" out/arm64
  {
    echo 'target_cpu = "arm64"'
    echo "zircon_project = \"$p\""
    echo 'bootfs_packages = true'
    echo 'with_syzkaller = 0'
    if [ -n "${CCACHE_DIR:-}" ] && [ -d "$CCACHE_DIR" ]; then echo 'use_ccache = true'; fi
    printf 'fuchsia_packages = [ %s ]\n' "$(printf '"%s", ' "$@" | sed 's/, $//')"
  } > "$d/args.gn"
  buildtools/gn gen "$d" --check
  (source scripts/devshell/lib/vars.sh && zircon_project=$p variant= &&
   fx-config-write out/arm64 "" linux_6.6 2.10 "${VENUS_TEE:-nebula}" 0)
}
