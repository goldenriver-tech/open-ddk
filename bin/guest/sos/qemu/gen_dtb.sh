#!/bin/bash

set -eo pipefail; [[ "${TRACE}" ]] && set -x

base_path="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dts_path="${base_path}/tbox_dts"

if [[ -z ${1} ]]; then
    echo "Usage: ${0} <output_dtb_path>"
    exit 1
fi

dts_path=$(realpath ${dts_path})
dtb_path="${1}"
dtc_bin="${DTC:-dtc}"

if ! command -v "${dtc_bin}" >/dev/null 2>&1; then
    repo_root="$(cd "${base_path}/../../../../.." && pwd)"
    dtc_bin="${repo_root}/vendor/nxp/imx-bsp/bin/dtc"
fi

mkdir -p "$(dirname ${dtb_path})"

"${dtc_bin}" -o ${dtb_path} -S 4096 ${dts_path}
