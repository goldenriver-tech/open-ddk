#!/bin/bash

set -e
set -x

PROJECT="0"
CLEAN=0
VENDOR_DRIVER_DIR=""
VENDOR_DRIVER_FILES=(
    mt8668.so
    mt8668_i2c.so
    mt8668_rtc.so
    mt8668_spi.so
    mt8668_ufs.so
    mt8676_gpio.so
    mtk_eint.so
    watchdog.so
)

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--clean)
            CLEAN=1
            rm -rf tmp/
            echo "====clean done====="
            shift
            ;;
        -p|--project)
            shift
            if [[ -n "$1" && ! "$1" =~ ^- ]]; then
                PROJECT="$1"
                shift
            else
                echo "Error: -p|--project requires a project name." >&2
                exit 1
            fi
            ;;
        --vendor-driver-dir)
            shift
            if [[ -n "$1" && ! "$1" =~ ^- ]]; then
                VENDOR_DRIVER_DIR="$(realpath "$1")"
                shift
            else
                echo "Error: --vendor-driver-dir requires a directory." >&2
                exit 1
            fi
            ;;
        *)
            shift
            ;;
    esac
done

./set_env.sh || exit 1

if [[ "$PROJECT" != "mt8676" && "$PROJECT" != "mt8678" && "$PROJECT" != "mt8668" ]]; then
    echo "Unknown PROJECT: $PROJECT" >&2
    exit 1
fi

if [ ! -d tmp ]; then
    mkdir -p tmp/
    ln -s "../" "tmp/garnet"
    echo "====tar -xzf build_env.tar.gz======"
    cat build_env.tar.gz.part-* > build_env.tar.gz
    tar -xzf build_env.tar.gz -C tmp/
    echo "====tar -xJf headers.tar.xz======"
    tar -xJf headers.tar.xz -C tmp/
fi

echo "====start build.sh======"
output_path=$(realpath ./out/prebuilt/hypervisor/grt_"$PROJECT"/ddk_out/)
cd tmp/
if [[ -n "$VENDOR_DRIVER_DIR" ]]; then
    source scripts/env.sh
    mkdir -p out/venus-hee/arm64
    rm -rf out/arm64
    ln -s "$(pwd)/out/venus-hee/arm64" out/arm64
    buildtools/gn gen out/arm64 --check --args="target_cpu=\"arm64\" zircon_project=\"venus-hee\" bootfs_packages=true with_syzkaller=0 fuchsia_packages=[\"garnet/packages/venus-hee-vendor-prebuilt\",\"build/packages/bootfs\",] vendor_driver_dir=\"$VENDOR_DRIVER_DIR\""
    scripts/devshell/use out/arm64
    fx build
    fx export-yocto-prebuilt -o "$output_path"
    mkdir -p "$output_path/arm64-shared"
    system_manifest="$output_path/system.manifest"
    for driver_file in "${VENDOR_DRIVER_FILES[@]}"; do
        if [[ -f "$output_path/$driver_file" ]]; then
            mv -f "$output_path/$driver_file" "$output_path/arm64-shared/$driver_file"
            if [[ -f "$system_manifest" ]]; then
                sed -i "s|=$driver_file$|=arm64-shared/$driver_file|" "$system_manifest"
            fi
        fi
    done
else
    ./build.sh -o "$output_path"
fi
echo "====end build.sh======"
