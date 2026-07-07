// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <sync/completion.h>
#include <ddk/protocol/block.h>
#include <stdint.h>
#include <stdbool.h>

struct guest_ctx;
struct vblock_drv;

typedef struct guest_ctx guest_ctx_t;
typedef struct vblock_drv vblock_drv_t;

typedef struct {
  completion_t completion;
  zx_status_t status;
} sync_read_ctx_t;

zx_status_t vpart_isolate_init(guest_ctx_t *guest, vblock_drv_t *dev, int vmid);
void vpart_isolate_release(guest_ctx_t *guest);
void vpart_isolate_get_vbinfo(guest_ctx_t *guest, uint64_t *vblock_count);
zx_status_t vpart_isolate_process_bop(guest_ctx_t *guest, block_op_t *bop,
                                      bool *rgpt_flag);
