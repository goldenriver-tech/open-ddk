// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <string.h> 

#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/debug.h>

#include <zircon/types.h>
#include <zircon/assert.h>

#include <soc/mt8668/mt8668-hw.h>

#include "mt8668-board.h"
// NOTE:
// mphy is already initialized in preloader stage and no need
// to change any setting of mphy, neither the PWR/CRG.
static const pbus_mmio_t i2c_mmios[13][2] = {
    // I2C0
    {
        {.base = I2C0_MMIO_BASE, .length = I2C0_MMIO_LEN,},
        {.base = I2C0_MMIO_DMA_BASE, .length = I2C0_MMIO_DMA_LEN,},
    },
    //I2C1
    {
        {.base = I2C1_MMIO_BASE, .length = I2C1_MMIO_LEN,},
        {.base = I2C1_MMIO_DMA_BASE, .length = I2C1_MMIO_DMA_LEN,},
    },
    // I2C2
    {
        {.base = I2C2_MMIO_BASE, .length = I2C2_MMIO_LEN,},
        {.base = I2C2_MMIO_DMA_BASE, .length = I2C2_MMIO_DMA_LEN,},
    },
    //I2C3
    {
        {.base = I2C3_MMIO_BASE, .length = I2C3_MMIO_LEN,},
        {.base = I2C3_MMIO_DMA_BASE, .length = I2C3_MMIO_DMA_LEN,},
    },
    // I2C4
    {
        {.base = I2C4_MMIO_BASE, .length = I2C4_MMIO_LEN,},
        {.base = I2C4_MMIO_DMA_BASE, .length = I2C4_MMIO_DMA_LEN,},
    },
    //I2C5
    {
        {.base = I2C5_MMIO_BASE, .length = I2C5_MMIO_LEN,},
        {.base = I2C5_MMIO_DMA_BASE, .length = I2C5_MMIO_DMA_LEN,},
    },
    // I2C6
    {
        {.base = I2C6_MMIO_BASE, .length = I2C6_MMIO_LEN,},
        {.base = I2C6_MMIO_DMA_BASE, .length = I2C6_MMIO_DMA_LEN,},
    },
    //I2C7
    {
        {.base = I2C7_MMIO_BASE, .length = I2C7_MMIO_LEN,},
        {.base = I2C7_MMIO_DMA_BASE, .length = I2C7_MMIO_DMA_LEN,},
    },
    // I2C8
    {
        {.base = I2C8_MMIO_BASE, .length = I2C8_MMIO_LEN,},
        {.base = I2C8_MMIO_DMA_BASE, .length = I2C8_MMIO_DMA_LEN,},
    },
    //I2C9
    {
        {.base = I2C9_MMIO_BASE, .length = I2C9_MMIO_LEN,},
        {.base = I2C9_MMIO_DMA_BASE, .length = I2C9_MMIO_DMA_LEN,},
    },
    // I2C10
    {
        {.base = I2C10_MMIO_BASE, .length = I2C10_MMIO_LEN,},
        {.base = I2C10_MMIO_DMA_BASE, .length = I2C10_MMIO_DMA_LEN,},
    },
    //I2C11
    {
        {.base = I2C11_MMIO_BASE, .length = I2C11_MMIO_LEN,},
        {.base = I2C11_MMIO_DMA_BASE, .length = I2C11_MMIO_DMA_LEN,},
    },
    // I2C12
    {
        {.base = I2C12_MMIO_BASE, .length = I2C12_MMIO_LEN,},
        {.base = I2C12_MMIO_DMA_BASE, .length = I2C12_MMIO_DMA_LEN,},
    },
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = I2C0_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C1_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C2_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C3_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C4_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C5_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C6_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C7_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C8_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C9_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C10_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C11_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = I2C12_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const pbus_bti_t i2c_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
    {
        .iommu_index = 0,
        .bti_id = 1,
    },
    {
        .iommu_index = 0,
        .bti_id = 2,
    },
    {
        .iommu_index = 0,
        .bti_id = 3,
    },
    {
        .iommu_index = 0,
        .bti_id = 4,
    },
    {
        .iommu_index = 0,
        .bti_id = 5,
    },
    {
        .iommu_index = 0,
        .bti_id = 6,
    },
    {
        .iommu_index = 0,
        .bti_id = 7,
    },
    {
        .iommu_index = 0,
        .bti_id = 8,
    },
    {
        .iommu_index = 0,
        .bti_id = 9,
    },
    {
        .iommu_index = 0,
        .bti_id = 10,
    },
    {
        .iommu_index = 0,
        .bti_id = 11,
    },
    {
        .iommu_index = 0,
        .bti_id = 12,
    },
};

zx_status_t mt8668_board_i2c_init(mt8668_board_t* board) {
    zx_status_t status = ZX_OK;
    int i2c_num = countof(i2c_mmios);
    zxlogf(ERROR, "DEBUG_I2C: i2c_num %d\n", i2c_num);
    char i2c_names[i2c_num][16];
    // Initialize I2C devices for multi bus
    for (int i = 0; i < i2c_num; i++) {
        pbus_dev_t i2c_dev_inst = {};
        memset(&i2c_dev_inst, 0, sizeof(i2c_dev_inst));
        i2c_dev_inst.mmios = i2c_mmios[i];
        i2c_dev_inst.mmio_count = 2;
        i2c_dev_inst.irqs = &i2c_irqs[i];
        i2c_dev_inst.irq_count = 1;
        i2c_dev_inst.btis = &i2c_btis[i];
        i2c_dev_inst.bti_count = 1;
        snprintf(i2c_names[i], sizeof(i2c_names[i]), "i2c%d", i);
        i2c_dev_inst.name = i2c_names[i];
        i2c_dev_inst.vid = PDEV_VID_MTK;
        i2c_dev_inst.pid = PDEV_PID_MT8668;
        i2c_dev_inst.did = PDEV_DID_GRT_I2C;

        status = pbus_device_add(&board->pbus_proto, &i2c_dev_inst, PDEV_ADD_PBUS_DEVHOST);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: pbus_device_add failed for bus %d: %d\n", __func__, i, status);
            return status;
        }
    }
    return ZX_OK;
}

