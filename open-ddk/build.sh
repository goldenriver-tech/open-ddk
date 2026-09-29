#!/bin/bash
# Copyright 2026 GoldenRiver Technology. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Build the mt8668 hypervisor image (gz.img) from build-env + garnet:
# garnet for venus-tee and venus-hee against the prebuilt zircon, the
# buildroot, the mtk8668 product overlay, then the image packer.
#
#   open-ddk/build.sh [-o OUT]     (default OUT: out/open-ddk)
#
# OUT/gz.img and OUT/gz-rel.img are unsigned.
set -eo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/out/open-ddk"
while getopts 'o:' opt; do
  case $opt in o) OUT="$(realpath -m "$OPTARG")" ;; *) exit 2 ;; esac
done
cd "$ROOT"
source open-ddk/prepare.sh
# Build only what the mt8668 image is made of. GRT's release flow builds every
# package of venus-tee and venus-hee and then keeps the product's; here the
# product's package set is resolved first (garnet/packages with the product
# overlay over it, as that flow sees it) and only that is built. venus-tee
# contributes nothing to the mt8668 image; export-buildroot still wants its
# build directory, so it is built with one trivial package and exports none.
P="$ROOT/out/open-ddk/pkgroot"
rm -rf "$P"; mkdir -p "$P/garnet"
cp -r garnet/packages "$P/garnet/"
cp -rf open-ddk/product/default/buildroot_packages/* "$P"/
cp -rf open-ddk/product/mtk8668/buildroot_packages/* "$P"/
# venus-tee: one trivial package (GN and buildroot_initialize both need at
# least one), the same for the build and the export
echo '{ "packages": { "simplest-garnet-app": "//garnet/examples/simplest-garnet-app" } }' \
    > "$P/garnet/packages/venus-tee"
python3 open-ddk/trim-packages.py "$P" "$ROOT/out/open-ddk/trimmed"
mkdir -p out/open-ddk/packages
python2 open-ddk/product-packages.py --root "$P" --package garnet/packages/venus-hee \
    --out out/open-ddk/packages/venus-hee
cp "$P/garnet/packages/venus-tee" out/open-ddk/packages/venus-tee

for p in venus-tee venus-hee; do
  venus_set "$p" "out/open-ddk/packages/$p"
  fx build
  # export-buildroot copies the kernel command lines; the full package set
  # built them by way of build/packages/bootfs
  fx build build/gn:cmdline build/gn:cmdline.rel
done

BR="$ROOT/out/buildroot"; PKG="$BR/build/nebula/buildroot_packages"
fx export-buildroot -o "$BR" -p "$P"
# The product's packages as buildroot_gen_product resolves them: a copy, not
# GRT's symlink into garnet/packages, so the overlay below cannot write into
# the garnet checkout.
rm -rf "$PKG/garnet"; mkdir -p "$PKG"; cp -r "$P/garnet" "$PKG/"
cp -rf open-ddk/product/default/buildroot_packages/* "$PKG"/
cp -rf open-ddk/product/mtk8668/buildroot_packages/* "$PKG"/
cp -f open-ddk/product/mtk8668/product.json "$PKG"/
python3 open-ddk/trim-packages.py "$PKG" "$BR/out/open-ddk/trimmed" "$BR"
cp -f "$PKG/bootfs.manifest.debug.venus-hee" "$BR/out/build-zircon/build-venus-hee/bootfs.manifest"
cp -f "$PKG/bootfs.manifest.release.venus-hee" "$BR/out/build-zircon-release/build-venus-hee/bootfs.manifest"
(cd "$BR" && ./build/gn/buildroot_gen_product.py --packages-home "$PKG")

IMG="$ROOT/out/open-ddk-images/prebuilt-images-mt8668"; mkdir -p "$IMG"
Z=$BR/out/build-zircon/build-venus-hee; ZR=$BR/out/build-zircon-release/build-venus-hee; A=$BR/out/venus-hee/arm64
cp "$Z/mtkv8-boot-shim.bin" "$IMG/boot-shim.bin";      cp "$Z/zircon.bin" "$IMG/zircon.bin"
cp "$ZR/mtkv8-boot-shim.bin" "$IMG/boot-shim-rel.bin"; cp "$ZR/zircon.bin" "$IMG/zircon-rel.bin"
cp "$A/bootdata-fs-mtkv8-mtk8668.bin" "$IMG/bootdata-fs.bin"
cp "$A/bootdata-fs-mtkv8-mtk8668-rel.bin" "$IMG/bootdata-fs-rel.bin"

rm -rf "$ROOT/out/open-ddk-gz"; cp -a open-ddk/gz "$ROOT/out/open-ddk-gz"
(cd "$ROOT/out/open-ddk-gz" && bash build-gz.sh --project auto8668p1_64_sos --no-ddk \
    --image-path "$ROOT/out/open-ddk-images/prebuilt-images")
if [ -e garnet/.git ] && [ -n "$(git -C garnet status --porcelain)" ]; then   # a checkout of its own (a jiri project)
  echo "error: the build changed the garnet checkout:" >&2
  git -C garnet status --short >&2; exit 1
elif [ ! -e garnet/.git ]; then
  # what the working tree has that the index does not (prepare.sh may have
  # just made the index, for a tree that came without git)
  changed=$(git status --porcelain -- garnet | awk 'substr($0, 2, 1) != " "')
  [ -z "$changed" ] || { echo "error: the build changed garnet/:" >&2; echo "$changed" >&2; exit 1; }
fi
mkdir -p "$OUT"
cp "$ROOT/out/open-ddk-gz/out/gz.img" "$ROOT/out/open-ddk-gz/out/gz-rel.img" "$OUT/"
(cd "$OUT" && sha256sum gz.img gz-rel.img)
