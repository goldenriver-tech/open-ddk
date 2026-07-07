// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_cluster.h"

#include <string.h>

#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <thread>
#include "lib/fxl/logging.h"

namespace machina {

void VirtioCluster::NotifyGuestCluster(const char *buf, uint32_t len) {
  uint16_t head;
  if (len > CLUSTER_MAX_BUF_SIZE) {
    FXL_LOG(ERROR) << "[virtio cluster]Buffer length exceeds maximum size: \n" << len;
    return;
  }
  queue(1)->Wait(&head);
  virtio_desc_t desc;
  zx_status_t status = queue(1)->ReadDesc(head, &desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "[virtio cluster]Failed to read descriptor: " << status;
  }
  auto event_out = static_cast<virtio_cluster_rx*>(desc.addr);
  event_out->size = len;
  memcpy(event_out->buf, buf, len);
  queue(1)->Return(head, sizeof(struct virtio_cluster_rx));
}

zx_status_t VirtioCluster::cluster_write(struct virtio_cluster_tx* tx,
  struct virtio_cluster_rsp* rsp, uint32_t *used) {
  zx_status_t status = spi_client_->Send(tx->data, tx->size);
  rsp->ret = status;
  *used += sizeof(struct virtio_cluster_rsp);
  return status;
}

VirtioCluster::VirtioCluster(const PhysMem& phys_mem) : VirtioDeviceBase(phys_mem) {}
VirtioCluster::~VirtioCluster() = default;

zx_status_t VirtioCluster::Init(async_t* async, SpiTransportClient* spi_client) {
  spi_client_ = spi_client;
  zx_status_t status = queue(0)->PollAsync(
    async, &command_queue_wait_, &VirtioCluster::CommandQueueHandler, this);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to PollAsync: " << status;
    return status;
  }
  spi_client_->ReigsterRxCallback(
    [this](const char* data, size_t size) {
      NotifyGuestCluster(data, size);
  });

  return status;
}

zx_status_t VirtioCluster::CommandQueueHandler(VirtioQueue* queue,
                                             uint16_t head,
                                             uint32_t* used,
                                             void* ctx) {
  auto thiz = reinterpret_cast<VirtioCluster*>(ctx);
  return thiz->HandleCommand(queue, head, used);
}

zx_status_t VirtioCluster::HandleCommand(VirtioQueue* queue,
                                       uint16_t head,
                                       uint32_t* used) {
  virtio_desc_t desc;
  zx_status_t err = ZX_OK;
  auto status = queue->ReadDesc(head, &desc);
  FXL_DCHECK(status == ZX_OK);

  auto tx = (struct virtio_cluster_tx*)desc.addr;
  *used = 0;

  if (desc.has_next) {
    head = desc.next;
    status = queue->ReadDesc(head, &desc);
    FXL_DCHECK(status == ZX_OK);

    if (desc.len != sizeof(struct virtio_cluster_rsp)) {
      FXL_LOG(ERROR) << "unexpected vq response buffer size: " << desc.len;
      return ZX_ERR_INTERNAL;
    }

    auto rsp = (struct virtio_cluster_rsp*)desc.addr;
    err = cluster_write(tx, rsp, used);
  }

  return err;
}

}  // namespace machina
