# open-ddk

The open sources of the MT8668 hypervisor, with what building them needs:
build the hypervisor image `gz.img`, or build and run the whole system in
QEMU.

## Host

Ubuntu 22.04 x86-64:

```sh
sudo apt-get install -y git curl xz-utils make python2 python3 python3-yaml \
    build-essential perl ant default-jdk-headless bc bison flex libelf-dev \
    libssl-dev pkg-config libglib2.0-dev libpixman-1-dev python3-venv \
    ninja-build expect netcat-openbsd cpio rsync unzip
sudo ln -sf python2 /usr/bin/python        # the build runs `python` and needs 2.7
```

About 30 GB of disk for the tree and the output of both builds.

## Get it

Anywhere you like, either the release's source snapshot:

```sh
unzip release-<date>.zip          # from the repository's GitHub release
cd release-<date>/nebula
open-ddk/fetch-assets.sh --from https://github.com/goldenriver-tech/open-ddk/releases/download/<version>
```

or the repository and its projects, with [jiri](https://fuchsia.googlesource.com/jiri),
Fuchsia's checkout tool:

```sh
curl -s "https://fuchsia.googlesource.com/jiri/+/HEAD/scripts/bootstrap_jiri?format=TEXT" |
    base64 --decode | bash -s nebula
cd nebula
export PATH=$PWD/.jiri_root/bin:$PATH
cat > .jiri_manifest <<'EOF'
<manifest>
  <imports>
    <import name="open-ddk" manifest="manifest/open-ddk" root=""
            remote="https://github.com/goldenriver-tech/open-ddk" remotebranch="main"/>
  </imports>
</manifest>
EOF
jiri update
open-ddk/fetch-assets.sh   # toolchains, the reference zircon, bazel's downloads (from this release)
```

`manifest/open-ddk` follows `main` and, for each fork of upstream, its
branch: `jiri update` brings the latest release. Upstream's own code in it
is pinned to the commit open-ddk takes. A release is this repository's tag
and, in each fork, the tag `open-ddk-<version>` (the release's zip has them
all as files). Without jiri, git alone: `git clone https://github.com/goldenriver-tech/open-ddk
nebula`, then `open-ddk/projects.py` in it checks out every project
(`--tag open-ddk-<version>`: the forks at a release's tag). (`open-ddk/build.sh`
checks out what is missing too.)

Code from upstream is a project of its own (`manifest/`,
`open-ddk/THIRD-PARTY.tsv`): upstream's repository at the commit taken, or,
where it was changed, a fork of upstream beside this repository whose branch
is upstream as taken and the changes as commits on top -- garnet (Fuchsia's
garnet), linux-6.6 (`linux`, AOSP's android15-6.6 kernel), trusted-firmware-a
(`tf-a`), third_party/qemu, third_party/littlefs and the fuse library, and
most of the rest of `third_party/` and `buildtools/` (go, protobuf, openssl,
grpc, ...: where upstream is one of Fuchsia's mirrors, at the mirror's commit).
garnet's fork (and build's, scripts' and gen_rootfs': Fuchsia's and OP-TEE's)
is upstream as taken, one commit with GRT's changes up to
open-ddk 1.0, then a commit for each change of this repository's history to
it, with the same message. Of GRT's changes it carries what the builds read;
upstream's other files stay as upstream has them. Each but these four has its
own SPDX document in `open-ddk/sbom/third-party/`; open-ddk's own describes
them, its projects. linux-6.6's nebula and trusty drivers, which GRT's tree
links from its 4.9 kernel, are files in the linux fork: the Trusty driver is
AOSP's, the nebula driver GRT's own, derived from it.

## The hypervisor image

```sh
open-ddk/build.sh
```

```
out/open-ddk/gz.img       # unsigned
out/open-ddk/gz-rel.img   # release build
```

and the images it packs, in `out/open-ddk-images/prebuilt-images-mt8668/`
(`boot-shim.bin`, `zircon.bin`, `bootdata-fs.bin` and their `-rel` variants).
`zircon.bin` is the reference zircon kernel (see below).

## Zircon

The zircon kernel open-ddk builds with is a reference. The prebuilt kernels in
the release assets (`out/build-zircon*/`) are what `gz.img` and the QEMU images
are built and checked with; they are not for production. Devices run the SoC
vendor's own zircon, an implementation of the same interfaces, which open-ddk
does not carry. `zircon/` is not in this repository: `open-ddk/fetch-assets.sh`
puts open-ddk's prebuilt zircon in, the kernels and, as source, what the
builds read of zircon (the user-space libraries and public headers the other
sources compile against, and its make files), marked `zircon/OPEN-DDK-PREBUILT`.
Where `zircon/` is zircon's whole source instead (checked out there before
`fetch-assets.sh`, which then puts zircon's whole toolchains in instead of
the part the prebuilt needs), `open-ddk/build.sh` and
`open-ddk/build-qemu.sh` build it first (`open-ddk/build-zircon.sh`); the
commands are the same.

## QEMU

```sh
open-ddk/build-qemu.sh
source scripts/env.sh
fx run-venus -z -9
```

`build-qemu.sh` builds the hypervisor and TEE images for the QEMU board,
linux 6.6 and its root file system, TF-A and QEMU. It downloads nothing: bazel
and what its workspaces fetch come from the release assets. `fx run-venus`
opens a terminal for each of the hypervisor, TEE and linux consoles (xterm,
gnome-terminal, or tmux with `-x`); type `c` and Enter in the QEMU monitor to
start, and the hypervisor console reaches its shell (`$ `).

## What is in here

| path | |
| --- | --- |
| `manifest/` | jiri's manifest, `open-ddk`: the forks follow their branches, upstream's code is pinned |
| `third_party/{hee-trusty,ree-trusty}/`, `open-ddk/qemu/rules_nbl/` | projects: GRT's own, repositories beside this one (what open-ddk publishes of them, with its history) |
| `garnet/`, `build/`, `scripts/`, `gen_rootfs/`, `linux-6.6/`, `trusted-firmware-a/`, `third_party/qemu/` | projects: forks of upstream with the changes as commits |
| `buildtools/`, most of `third_party/` (littlefs, the fuse library, busybox, go, protobuf, openssl, grpc, boringssl's `src/`, ...), garnet's Go packages | projects: upstream, or a fork of it (`open-ddk/THIRD-PARTY.tsv`) |
| the rest of `third_party/` | what the builds read of the other sources: libc-tests (its upstream is gone), Fuchsia's wrapper of boringssl, and build files for code the builds do not compile |
| `open-ddk/build.sh`, `open-ddk/build-qemu.sh` | the builds |
| `open-ddk/fetch-assets.sh`, `open-ddk/ASSETS.sha256` | the binary parts and their checksums |
| `open-ddk/product/` | the mt8668 product: package lists, bootfs manifests, kernel command line |
| `open-ddk/gz/` | the image packer (`build-gz.sh`), device trees, guest configuration, host tools |
| `open-ddk/qemu/` | the REE bazel workspace without network or old kernels, and the bazel rules it uses |
| assets: `zircon/`, `out/`, `buildtools/linux-x64/`, `prebuilts/`, `out/open-ddk-bazel/` | the reference zircon (prebuilt, not for production, and what the builds read of its source), toolchains, bazel and its downloads |
