// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/debug.h>

#include <zircon/types.h>
#include <zircon/assert.h>

#include <soc/mt8668/mt8668-hw.h>

#include "mt8668-board.h"

static const pbus_mmio_t rtc_spmi_mmios[] = {
    {
        .base = 0x1cc14000,
        .length = 0x0030c0,
    },
    {
        .base = 0x1cc10000,
        .length = 0x0030c0,
    },
};

static const pbus_dev_t rtc_dev = {
    .name = "rtc",
    .vid = PDEV_VID_MTK,
    .pid = PDEV_PID_MT8668,
    .did = PDEV_DID_GRT_RTC,
    .mmios = rtc_spmi_mmios,
    .mmio_count = countof(rtc_spmi_mmios),
};

zx_status_t mt8668_board_rtc_init(mt8668_board_t* board) {
    zx_status_t status = pbus_device_add(&board->pbus_proto, &rtc_dev,
            PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_device_add failed: %d\n",
                __func__, status);
        return status;
    }
    return ZX_OK;
}
