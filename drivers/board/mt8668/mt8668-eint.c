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

static const pbus_mmio_t eint_mmios[] = {
    {
        .base = 0x11ce0000,
        .length = 0x1000,
    },
    {
        .base = 0x11de0000,
        .length = 0x1000,
    },
    {
        .base = 0x11e60000,
        .length = 0x1000,
    },
    {
        .base = 0x1c01e000,
        .length = 0x1000,
    },
};

static const pbus_irq_t eint_irqs[] = {
    {
        .irq = 399,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const pbus_bti_t eint_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

static pbus_dev_t mtk_eint_dev = {
    .name = "mtk_eint",
    .vid = PDEV_VID_MTK,
    .pid = PDEV_PID_MT8668,
    .did = PDEV_DID_GRT_EINT,
    .mmios = eint_mmios,
    .mmio_count = countof(eint_mmios),
    .irqs = eint_irqs,
    .irq_count = countof(eint_irqs),
    .btis = eint_btis,
    .bti_count = countof(eint_btis),
};

zx_status_t mt8668_board_eint_init(mt8668_board_t *board) {
  zx_status_t status;

  zxlogf(INFO, "Initializing MTK EINT\n");

  if ((status = pbus_device_add(&board->pbus_proto, &mtk_eint_dev,
                                PDEV_ADD_PBUS_DEVHOST)) != ZX_OK) {
    zxlogf(ERROR, "%s: pbus_device_add failed: %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}
