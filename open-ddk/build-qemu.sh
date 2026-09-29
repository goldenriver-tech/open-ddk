#!/bin/bash
# Copyright 2026 GoldenRiver Technology. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Build what `fx run-venus` boots in QEMU: the hypervisor image (venus-hee) on
# the prebuilt zircon, linux 6.6 and its root file system, TF-A, and QEMU
# itself; the TEE image (venus-tee) comes prebuilt with the assets. (At this
# tag `--tee none` does not boot.) No network: bazel and what its workspaces
# download come from the release assets (out/open-ddk-bazel/).
#
#   open-ddk/build-qemu.sh
#
# Then, in a shell that sourced scripts/env.sh:  fx run-venus -z -9
set -eo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
source open-ddk/prepare.sh

# The QEMU board's package sets, as the default product resolves them, less
# what only GRT's test keys can build (the secmgr provisioning data, the TSM
# server, the signed license: TEE-era, unused): venus-hee's packages, and for
# both images their boot images (bootdata-combo-qemu-virt), nothing else of
# //build/bootfs.
P="$ROOT/out/open-ddk/pkgroot-qemu"; Q="$ROOT/out/open-ddk/packages-qemu"
rm -rf "$P" "$Q"; mkdir -p "$P/garnet" "$Q"
cp -r garnet/packages "$P/garnet/"
cp -rf open-ddk/product/default/buildroot_packages/* "$P"/
python3 open-ddk/trim-packages.py "$P" "$ROOT/out/open-ddk/trimmed"
python2 open-ddk/product-packages.py --root "$P" --package garnet/packages/venus-hee --out "$Q/venus-hee"
echo '{ "labels": [ "//build/bootfs:bootdata-combo-qemu-virt" ] }' > "$Q/bootfs"
# The TEE image is prebuilt (out/venus-tee/arm64/bootdata-combo-qemu-virt.bin,
# from the assets): only the hypervisor is built.
[ -f out/venus-tee/arm64/bootdata-combo-qemu-virt.bin ] ||
  { echo "error: no TEE image (out/venus-tee/arm64/); run open-ddk/fetch-assets.sh" >&2; exit 1; }
venus_set venus-hee "out/open-ddk/packages-qemu/venus-hee" "out/open-ddk/packages-qemu/bootfs"
fx build

source scripts/devshell/lib/vars.sh
source buildtools/vars.sh
fx-config-read
FUCHSIA_DIR="$ROOT" source build/bazel_symlink.sh
python3 open-ddk/qemu/adapt-bazel.py "$ROOT"

# bazel itself and every file its workspaces download, from the assets
B="$ROOT/out/open-ddk-bazel"
export BAZELISK_HOME="$B/bazelisk"
CACHE=(--repository_cache="$B/repos")
# no bazel server left behind
trap '(cd bazel/ree && bazelisk shutdown) 2>/dev/null; (cd bazel/host && bazelisk shutdown) 2>/dev/null' EXIT

# the version string the REE carries, written where `fx bazel-ree` writes it
(cd third_party/ree-trusty && cat > version.bzl <<EOF2
"""Nebula Version Bazel Helper"""

NEBULA_VERSION_STR = "\"$(git describe --always --tags)\""

# The following provides "NEBULA_VERSION_STR" to genrule
def _nebula_version_str_impl(ctx):
  return [
    platform_common.TemplateVariableInfo({
      "NEBULA_VERSION_STR": NEBULA_VERSION_STR
    })
  ]

nebula_version_str = rule(
  implementation = _nebula_version_str_impl,
)
EOF2
)

# REE: linux 6.6 and the root file system (what `fx bazel-ree` builds for
# QEMU, with its toolchain settings)
CLANG_TOOLCHAIN_PREFIX="${BUILDTOOLS_CLANG_DIR}/bin"
GCC_TOOLCHAIN_PREFIX="${LINARO_AARCH64_TOOLCHAIN_HOME}/bin"
GCC_TOOLCHAIN_SYSROOT="${LINARO_AARCH64_TOOLCHAIN_HOME}/aarch64-linux-gnu/libc"
(cd bazel/ree && bazelisk build "${CACHE[@]}" --config=linux_6.6 \
   //:linux-image-6.6 //:linux-rootfs \
   --action_env=CLANG_TOOLCHAIN_PREFIX="$CLANG_TOOLCHAIN_PREFIX" \
   --action_env=CLANG_TOOLCHAIN_SYSROOT="$GCC_TOOLCHAIN_SYSROOT" \
   --action_env=GCC_TOOLCHAIN_PREFIX="$GCC_TOOLCHAIN_PREFIX" \
   --action_env=GCC_TOOLCHAIN_SYSROOT="$GCC_TOOLCHAIN_SYSROOT" \
   --action_env=ANDROID_NDK_HOME="$ANDROID_NDK_HOME" \
   --action_env=ANDROID_SDK_HOME="$ANDROID_SDK_HOME" \
   --action_env=CCACHE_DIR="$CCACHE_DIR" \
   --action_env=TEE_SPD="$TEE_SPD")
mkdir -p out/arm64/linux
ln -sf "$BAZEL_REE_OUT/linux/arch/arm64/boot/Image" out/arm64/linux/Image
ln -sfn "$BAZEL_REE_OUT/linux" out/build-linux
ln -sfn "$BAZEL_REE_OUT/rootfs" out/build-linux-rootfs

# QEMU with GRT's machine options, and its serial terminal
(cd bazel/host && bazelisk build "${CACHE[@]}" //:qemu //:soc_term)

fx build-atf
echo "built; to run:  source scripts/env.sh && fx run-venus -z -9"
