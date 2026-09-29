#!/bin/bash
# Copyright 2023 GoldenRiver Technology Co., Ltd.

set -eo pipefail; [[ "${TRACE}" ]] && set -x

readonly CUR_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly OUT_DIR="${CUR_DIR}/out"
readonly DTS_DIR="${CUR_DIR}/nebula/dts"

readonly JQ="${CUR_DIR}/nebula/host-tools/jq"
readonly MKBOOTFS="${CUR_DIR}/nebula/host-tools/mkbootfs"
readonly MKIMAGE="${CUR_DIR}/nebula/host-tools/mkimage"
readonly DTC="${CUR_DIR}/nebula/host-tools/dtc"
declare PREBUILT_IMAGE_DIR="${CUR_DIR}/nebula/prebuilt-images"

function usage() {
    >&2 echo "usage: $0 --project <project> [--image-path <prebuilt image dir>] [--help|-h]"
    >&2 echo "options:"
    >&2 echo "  --project        project name"
    >&2 echo "  --image-path     t-hyp prebuilt images directory path"
    >&2 echo "                   (default: ${PREBUILT_IMAGE_DIR})"
    >&2 echo "  --help | -h      show help"
}

while [[ $# -ne 0 ]]; do
  case "$1" in
    --project)
      PROJECT="${2%/}"
      shift
      ;;
    --image-path)
      if [[ $# -lt 2 ]]; then
        usage
        exit 1
      fi
      PREBUILT_IMAGE_DIR="$(realpath ${2})"
      shift
      ;;
    --no-ddk)
      WITH_NO_DDK=1
      ;;
    --help|-h)
      usage
      exit 1
      ;;
    *)
      usage
      exit 1
      ;;
  esac
  shift
done

if [ -z $PROJECT ]; then
  usage
  exit 1
fi

readonly GUEST_CONFIG_DIR="${CUR_DIR}/$PROJECT"
readonly IC_NAME="mt${PROJECT:4:4}"
readonly PRODUCT_JSON="${CUR_DIR}/$PROJECT/product.json"
PREBUILT_IMAGE_DIR="${PREBUILT_IMAGE_DIR}-${IC_NAME}"

if [ ! -d "$GUEST_CONFIG_DIR" ]; then
  echo "project not exist"
  exit 1
fi

pushd ${CUR_DIR} > /dev/null
mkdir -p ${OUT_DIR}
echo "generating system image patch:  ${OUT_DIR}/patch.system.bin"
cat << EOF > patch.system.manifest
data/sos.lua=$PROJECT/sos_${IC_NAME}.lua
data/sos1.lua=$PROJECT/sos_${IC_NAME}.lua
data/sos2.lua=$PROJECT/sos_${IC_NAME}.lua
data/sos3.lua=$PROJECT/sos_${IC_NAME}.lua
data/product.lua=$PROJECT/product_${IC_NAME}.lua
data/rproc.pb.txt=$PROJECT/rproc.pb.txt
data/guest_manager.pb.txt=$PROJECT/guest_manager.pb.txt
data/sos_bootloader.pb.txt=$PROJECT/sos_bootloader.pb.txt
EOF

if [ "$PROJECT" = "auto8668p1_64_sos_sta" ]; then
cat << EOF >> patch.system.manifest
data/tbox_bootloader.pb.txt=$PROJECT/tbox_bootloader.pb.txt
data/uos_tbox_pv8668.lua=$PROJECT/uos_tbox_pv8668.lua
EOF
fi

if [ -z $WITH_NO_DDK ]; then
  DDK_OUT=ddk_out
  sed "s|=|=$DDK_OUT/|" "$DDK_OUT/system.manifest" >> patch.system.manifest
fi

${MKBOOTFS} -o ${OUT_DIR}/patch.system.bin patch.system.manifest

PRODUCT=$(${JQ} -r .product ${PRODUCT_JSON})
VID=$(${JQ} -r .vid ${PRODUCT_JSON})
PID=$(${JQ} -r .pid ${PRODUCT_JSON})
SBT=$(${JQ} -r .sbt ${PRODUCT_JSON})
echo "generating plid image:  ${OUT_DIR}/bootdata-plid.bin"
echo "product=${PRODUCT} vid=${VID} pid=${PID} sbt=${SBT}"
${MKBOOTFS} -o ${OUT_DIR}/bootdata-plid.bin --vid ${VID} --pid ${PID} --sbt ${SBT} --board ${PRODUCT}
echo "generating plid-rel image:  ${OUT_DIR}/bootdata-plid-rel.bin"
echo "product=${PRODUCT} vid=${VID} pid=${PID} sbt=${SBT}"
${MKBOOTFS} -o ${OUT_DIR}/bootdata-plid-rel.bin --vid ${VID} --pid ${PID} --sbt ${SBT} --board ${PRODUCT}

${DTC} -I dts -O dtb -Wno-simple_bus_reg -o ${OUT_DIR}/nebula.dtb ${DTS_DIR}/${PROJECT}.dts

echo "packing t-hyp debug image:      ${OUT_DIR}/nebula.bin"
${MKBOOTFS} -o ${OUT_DIR}/nebula.bin --header ${PREBUILT_IMAGE_DIR}/boot-shim.bin --header-align 65536 \
    ${PREBUILT_IMAGE_DIR}/zircon.bin \
    --devicetree ${OUT_DIR}/nebula.dtb \
    ${OUT_DIR}/patch.system.bin \
    ${PREBUILT_IMAGE_DIR}/bootdata-fs.bin \
    ${OUT_DIR}/bootdata-plid.bin

echo "packing t-hyp release image:    ${OUT_DIR}/nebula-rel.bin"
${MKBOOTFS} -o ${OUT_DIR}/nebula-rel.bin --header ${PREBUILT_IMAGE_DIR}/boot-shim-rel.bin --header-align 65536 \
    ${PREBUILT_IMAGE_DIR}/zircon-rel.bin \
    --devicetree ${OUT_DIR}/nebula.dtb \
    ${OUT_DIR}/patch.system.bin \
    ${PREBUILT_IMAGE_DIR}/bootdata-fs-rel.bin \
    ${OUT_DIR}/bootdata-plid-rel.bin

echo "building gz debug image:        ${OUT_DIR}/gz.img"
${MKIMAGE} ${OUT_DIR}/nebula.bin ${GUEST_CONFIG_DIR}/img_hdr_lk.cfg > ${OUT_DIR}/gz.img

echo "building gz release image:      ${OUT_DIR}/gz-rel.img"
${MKIMAGE} ${OUT_DIR}/nebula-rel.bin ${GUEST_CONFIG_DIR}/img_hdr_lk.cfg > ${OUT_DIR}/gz-rel.img

popd > /dev/null

echo "done."
