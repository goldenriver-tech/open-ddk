// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ios>
#include <vector>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/task.h>
#include <pal/platform_defs.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/threads.h>

#include "garnet/bin/guest/guest_config.h"
#include "garnet/bin/guest/linux.h"
#include "garnet/bin/guest/message_handler.h"
#include "garnet/bin/guest/zircon.h"
#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/inspect_service_impl.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/ipc_client.h"
#include "garnet/lib/machina/pci.h"
#include "garnet/lib/machina/uart.h"
#include "garnet/lib/machina/utrace.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/vhm_device.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "garnet/lib/machina/virtio_block.h"
#include "lib/fxl/files/file.h"

#include "garnet/lib/machina/mmio_bus.h"
#include "garnet/bin/guest/proto/vm_config.pb.h"
#include "garnet/lib/machina/arch/arm64/pl031.h"
#include "garnet/lib/machina/arch/arm64/smmu_v3.h"
#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/ipc_message.h"
#include "garnet/lib/machina/monitor_virtio.h"
#include "garnet/lib/machina/remoteproc.h"
#include "garnet/lib/machina/remoteproc_vm_monitor.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "tipc_vqueue_notifier.h"
#include "vmlog_srv.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include <nbl_fwk/nmt_tee.h>
#include <trusty_std.h>

#define LK2IMG_SIZE_OFFSET 20

#include "garnet/lib/machina/arch/arm64/gic_its.h"
#include "garnet/lib/machina/virtio_spi.h"
#include "garnet/lib/machina/virtio_i2c.h"
#include "garnet/lib/machina/virtio_eint.h"
#include "garnet/lib/machina/virtio_rtc.h"
#include "garnet/lib/machina/virtio_rpmb.h"

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
static constexpr int32_t kMmioIrqVector = 180;
static constexpr char kGuestMemoryAllocator[] =
    "/dev/sys/platform/00:00:f/guest_memory_allocator";

namespace {
static std::vector<machina::VmemSpec> vmems_build_from(nbl_vmm::VmConfig& cfg) {
  std::vector<machina::VmemSpec> vmems;
  for (int i = 0; i < cfg.mem_size(); i++) {
    auto& v = cfg.mem(i);
    machina::VmemSpec vmem = {
        .name = v.name(),
        .gpa_base = v.gpa_base(),
        .hpa_base = v.hpa_base(),
        .size = v.size(),
        .policy = static_cast<machina::MemoryPolicy>(v.policy()),
        .is_physmem = v.is_physmem(),
        .is_reservedmem = v.is_reservedmem(),
    };
    vmems.push_back(vmem);
  }

  return vmems;
}

static uint64_t get_phys_base(nbl_vmm::VmConfig& cfg) {
  for (int i = 0; i < cfg.mem_size(); i++) {
    auto& v = cfg.mem(i);
    if (v.is_physmem()) {
      return v.gpa_base();
    }
  }
  FXL_CHECK(false);
  __builtin_unreachable();
}

static machina::VgicSpec vgic_build_from(nbl_vmm::VmConfig& cfg) {
  auto& v = cfg.vgic();

  machina::VgicSpec vgic = {
      .gicd_paddr = v.gicd_paddr(),
      .gicr_paddr = v.gicr_paddr(),
      .has_its = v.has_its(),
      .its_paddr = v.its_paddr(),
  };
  for (int i = 0; i < v.interrupts_size(); i++) {
    vgic.irqs.push_back(v.interrupts(i));
  }
  for (int i = 0; i < v.percpu_interrupts_size(); i++) {
    vgic.percpu_irqs.push_back(v.percpu_interrupts(i));
  }

  return vgic;
}

static std::vector<machina::VsmmuSpec> vsmmu_build_from(
    nbl_vmm::VmConfig& cfg) {
  std::vector<machina::VsmmuSpec> vsmmus;

  for (int i = 0; i < cfg.vsmmu_size(); i++) {
    auto& v = cfg.vsmmu(i);
    machina::VsmmuSpec vsmmu;
    vsmmu.paddr = v.paddr();
    vsmmu.irq = v.irq();

    for (int i = 0; i < v.sids_size(); i++) {
      vsmmu.sids.push_back(v.sids(i));
    }
    vsmmus.push_back(vsmmu);
  }

  return vsmmus;
}

}  // namespace

zx_status_t update_cmdline(nbl_vmm::VmConfig& cfg, void* dtb, size_t dtb_size) {
  std::string cmdline = cfg.cmdline();
  if (cmdline.empty())
    return ZX_OK;

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offset = fdt_path_offset(dtb, "/chosen");
  if (offset < 0) {
    FXL_LOG(WARNING) << "//chosen path not found, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }

  int len = 0;
  const void* bootargs = fdt_getprop(dtb, offset, "bootargs", &len);
  if (!bootargs || (len < 0)) {
    FXL_LOG(WARNING) << "//chosen path not found, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }

  cmdline.append(static_cast<const char*>(bootargs));
  ret = fdt_setprop(dtb, offset, "bootargs", cmdline.data(),
                    cmdline.length() + 1);
  if (ret < 0) {
    FXL_LOG(INFO) << "cannot modify bootargs";
    fdt_pack(dtb);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  fdt_pack(dtb);
  return ZX_OK;
}

static bool is_qemu_platform() {
  static uint32_t product_id;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID, &product_id);
  return product_id == BOARD_VID_QEMU;
}

static void update_virtio_gpu_irq(nbl_vmm::VmConfig& cfg,
                                  void* dtb,
                                  size_t dtb_size) {
  if (cfg.gpu_irqs_size() <= 0) {
    return;
  }

  std::vector<uint32_t> props;
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
  props.push_back(cpu_to_fdt32(cfg.gpu_irqs(1) - 32));
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
  if (!is_qemu_platform())
    props.push_back(0);
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
  props.push_back(cpu_to_fdt32(cfg.gpu_irqs(0) - 32));
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
  if (!is_qemu_platform())
    props.push_back(0);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "virtio_gpu_irq");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add virtio gpu irq node.";
    goto out;
  }

  ret = fdt_setprop_string(dtb, irq_offset, "compatible", "nbl,virtio_gpu_irq");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static void update_virtio_vsock_irq(nbl_vmm::VmConfig& cfg,
                                    void* dtb,
                                    size_t dtb_size) {
  uint32_t cnt = cfg.vsock_irqs_size();
  if (cnt == 0) {
    return;
  }

  std::vector<uint32_t> props;
  uint32_t idx = 0;
  while (idx < cnt) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(cfg.vsock_irqs(idx) - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
    if (!is_qemu_platform())
      props.push_back(0);
    idx++;
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "virtio_vsock_irq");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add virtio vsock irq node.";
    goto out;
  }

  ret =
      fdt_setprop_string(dtb, irq_offset, "compatible", "nbl,virtio_vsock_irq");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_vsock compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static void update_virtio_apu_irq(nbl_vmm::VmConfig& cfg,
                                  void* dtb,
                                  size_t dtb_size) {
  uint32_t cnt = cfg.apu_irqs_size();
  if (cnt == 0) {
    return;
  }

  std::vector<uint32_t> props;
  uint32_t idx = 0;
  while (idx < cnt) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(cfg.apu_irqs(idx) - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
    if (!is_qemu_platform())
      props.push_back(0);
    idx++;
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "virtio_apu_irq");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add virtio apu irq node.";
    goto out;
  }

  ret = fdt_setprop_string(dtb, irq_offset, "compatible", "nbl,virtio_apu_irq");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_apu_irq compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static void update_virtio_cmdq_irq(nbl_vmm::VmConfig& cfg,
                                   void* dtb,
                                   size_t dtb_size) {
  uint32_t cnt = cfg.cmdq_irqs_size();
  if (cnt == 0) {
    return;
  }

  std::vector<uint32_t> props;
  uint32_t idx = 0;
  while (idx < cnt) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(cfg.cmdq_irqs(idx) - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
    if (!is_qemu_platform())
      props.push_back(0);
    idx++;
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "virtio_cmdq_irq");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add virtio apu irq node.";
    goto out;
  }

  ret =
      fdt_setprop_string(dtb, irq_offset, "compatible", "nbl,virtio_cmdq_irq");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_cmdq_irq compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static void update_smc_irq(nbl_vmm::VmConfig& cfg,
                           void* dtb, size_t dtb_size) {
  std::vector<uint32_t> props;
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
  props.push_back(cpu_to_fdt32(cfg.smc_irq() - 32));
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
  if (!is_qemu_platform())
    props.push_back(0);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "nebula_smc");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add nebula_smc node.";
    goto out;
  }

  ret =
      fdt_setprop_string(dtb, irq_offset, "compatible", "grt,nebula_smc");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set nebula_smc compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set nebula_smc interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static int find_subnode_offset(const void* dtb, int start_offset,
                        const char* search_string) {
  int offset = start_offset;
  int depth = 0;
  const char* node_name = NULL;

  if (!dtb) {
    FXL_LOG(WARNING) << "Invalid args!";
    return -FDT_ERR_INTERNAL;
  }

  // Start from the root node
  for (depth = 0; (offset >= 0) && (depth >= 0);
       offset = fdt_next_node(dtb, offset, &depth)) {
    if (depth > 2) {
      continue;
    }

    node_name = fdt_get_name(dtb, offset, NULL);
    if (node_name == NULL) {
      continue;
    }

    // Check if node name contains the search string
    if (strstr(node_name, search_string)) {
      return offset;
    }
  }

  return -FDT_ERR_NOTFOUND;
}

static zx_status_t create_virtio_pci_nodes(nbl_vmm::VmConfig& cfg,
                           void* dtb, size_t dtb_size) {
  if (is_qemu_platform())
    return ZX_OK;
  int ret = 0;
  int32_t* pci_global_irqs = machina::PciBus::get_pci_global_irqs();
  if (pci_global_irqs == nullptr) {
    FXL_LOG(ERROR) << "Invalid pci_global_irqs pointer";
    return ZX_ERR_INTERNAL;
  }

  ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int root_offset = fdt_path_offset(dtb, "/");
  check_status(root_offset, "root");

  int pci_offset = fdt_add_subnode(dtb, root_offset, "pci@fde000000");
  bool node_exists = (pci_offset == -FDT_ERR_EXISTS);

  if (node_exists) {
    FXL_LOG(INFO) << "pci@fde000000 node already exists, get node offset";
    pci_offset = fdt_path_offset(dtb, "/pci@fde000000");
    if (pci_offset < 0) {
      FXL_LOG(ERROR) << "Failed to get pci@fde000000 node offset";
      return ZX_ERR_NOT_FOUND;
    }
  } else {
    check_status(pci_offset, "pci@fde000000");

    ret = fdt_setprop_string(dtb, pci_offset, "device_type", "pci");
    check_status(ret, "device_type");

    ret = fdt_setprop_string(dtb, pci_offset, "compatible",
                             "pci-host-ecam-generic");
    check_status(ret, "compatible");

    uint64_t base = 0xfde000000;
    uint64_t size = 0x1000000;
    ret = fdt_setprop_cells_u64(dtb, pci_offset, "reg", 2, base, size);
    check_status(ret, "reg");

    ret = fdt_setprop_cells_u32(dtb, pci_offset, "bus-range", 2, 1u, 1u);
    check_status(ret, "bus-range");

    ret = fdt_setprop_cells_u32(dtb, pci_offset, "ranges", 7, 0x2000000u, 0x0u,
                                0xb0001000u, 0xfu, 0xb0001000u, 0x0u, 0xfff000u);
    check_status(ret, "ranges");

    ret = fdt_setprop_cell(dtb, pci_offset, "#address-cells", 0x03);
    check_status(ret, "#address-cells");

    fdt_setprop_cell(dtb, pci_offset, "#size-cells", 0x02);
    check_status(ret, "#size-cells");

    fdt_setprop_cell(dtb, pci_offset, "#interrupt-cells", 0x01);
    check_status(ret, "#interrupt-cells");
  }

  int its_offset = find_subnode_offset(dtb, root_offset, "its");
  check_status(its_offset, "its");

  uint32_t its_phandle = 0xa0001;
  ret = fdt_getprop_cells_u32(dtb, its_offset, "phandle", 1, &its_phandle);
  if (ret < 0) {
    FXL_LOG(INFO) << "its phandle not found, add its phandle node";
    ret = fdt_setprop_cells_u32(dtb, its_offset, "phandle", 1, its_phandle);
    check_status(ret, "its/phandle");
  }

  if (!node_exists) {
    fdt_setprop_cell(dtb, pci_offset, "msi-parent", its_phandle);
    check_status(ret, "msi-parent");
  }

  uint32_t gic_phandle = 0xa0000;

  int gic_offset = fdt_node_offset_by_prop_value(dtb, root_offset, "compatible",
                                                 "arm,gic-v3", 11);
  check_status(gic_offset, "gic");

  ret = fdt_getprop_cells_u32(dtb, gic_offset, "phandle", 1, &gic_phandle);
  if (ret < 0) {
    FXL_LOG(INFO) << "gic phandle not found, add gic phandle node";
    ret = fdt_setprop_cells_u32(dtb, gic_offset, "phandle", 1, gic_phandle);
    check_status(ret, "gic/phandle");
  }

  ret = fdt_setprop_cells_u32(dtb, pci_offset, "interrupt-map-mask", 4, 0xf800u,
                              0x0u, 0x0u, 0x1u);
  check_status(ret, "interrupt-map-mask");

  uint32_t full_irq_map[11 * PCI_MAX_DEVICES] = {0};
  uint32_t* irq_map = full_irq_map;
  uint32_t map_index = 0;

  while (pci_global_irqs[map_index] != -1 && map_index < PCI_MAX_DEVICES) {
    // interrupt-map entry format:
    // <addr_hi addr_mid addr_low addr_space> <&gic 0 0 0 irq_type irq_flags>
    irq_map[map_index * 11 + 0] = cpu_to_fdt32(map_index << 11);
    irq_map[map_index * 11 + 1] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 2] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 3] = cpu_to_fdt32(0x1u);
    irq_map[map_index * 11 + 4] = cpu_to_fdt32(gic_phandle);
    irq_map[map_index * 11 + 5] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 6] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 7] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 8] = cpu_to_fdt32(pci_global_irqs[map_index] - 32);
    irq_map[map_index * 11 + 9] = cpu_to_fdt32(0x4u);
    irq_map[map_index * 11 + 10] = cpu_to_fdt32(0x0u);
    map_index++;
  }

  ret = fdt_setprop(dtb, pci_offset, "interrupt-map", full_irq_map,
                    map_index * 11 * sizeof(uint32_t));
  check_status(ret, "interrupt-map");

  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t get_root_resource(zx::resource* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

static int update_nodes(void* src_dtb,
                        void* dest_dtb,
                        std::vector<std::string>& nodes) {
  int src_root = fdt_path_offset(src_dtb, "/");
  if (src_root < 0) {
    FXL_LOG(INFO) << "Failed to get src_dtb root";
  }

  int dest_root = fdt_path_offset(dest_dtb, "/");
  if (dest_root < 0) {
    FXL_LOG(INFO) << "Failed to get dest_dtb root";
  }

  for (std::string& node : nodes) {
    int src_node = fdt_node_offset_by_compatible(src_dtb, 0, node.c_str());
    if (src_node < 0) {
      FXL_LOG(INFO) << "Failed to get src_dtb root";
    }

    int dest_node = fdt_node_offset_by_compatible(dest_dtb, 0, node.c_str());
    if (dest_node == -FDT_ERR_NOTFOUND) {
      int new_node = fdt_add_subnode(dest_dtb, dest_root,
                                     fdt_get_name(src_dtb, src_node, NULL));
      if (new_node < 0) {
        FXL_LOG(INFO) << "Failed to add node in dest DTB";
        return new_node;
      }

      int src_prop = fdt_first_property_offset(src_dtb, src_node);
      while (src_prop > 0) {
        const char* prop_name;
        int len = 0;

        const void* prop_value =
            fdt_getprop_by_offset(src_dtb, src_prop, &prop_name, &len);
        if (prop_name == NULL || len == 0) {
          FXL_LOG(INFO) << "Not found any prop in node";
        }

        if (strcmp(prop_name, "phandle") != 0) {
          int result =
              fdt_setprop(dest_dtb, new_node, prop_name, prop_value, len);
          if (result < 0) {
            FXL_LOG(INFO) << "Failed to set node properties";
            return result;
          }
        }

        src_prop = fdt_next_property_offset(src_dtb, src_prop);
      }
    } else if (dest_node >= 0) {
      int src_prop = fdt_first_property_offset(src_dtb, src_node);
      while (src_prop > 0) {
        const char* prop_name;
        int len = 0;

        const void* prop_value =
            fdt_getprop_by_offset(src_dtb, src_prop, &prop_name, &len);
        if (prop_name == NULL || len == 0) {
          FXL_LOG(INFO) << "Not found any prop in node";
        }

        if (strcmp(prop_name, "compatible") == 0) {
          break;
        }

        int result =
            fdt_setprop(dest_dtb, dest_node, prop_name, prop_value, len);
        if (result < 0) {
          FXL_LOG(INFO) << "Failed to set node properties";
          return result;
        }

        src_prop = fdt_next_property_offset(src_dtb, src_prop);
      }
    }
  }

  return 0;
}

static void copy_sos_dtb_node(machina::DeviceTreeSpec dtb_spec,
                              zx_handle_t handle,
                              void* dtb,
                              size_t dtb_size) {
  zx::vmo vmo;
  zx::resource root_resource;
  std::vector<std::string> append_nodes = {"mediatek,mddriver",
                                           "mediatek,md_attr_node"};

  zx_status_t status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return;
  }

  FXL_LOG(ERROR) << "get sos dtb base=" << dtb_spec.base
                 << " size=" << dtb_spec.size;
  status = zx_vmo_create_physical(root_resource.get(), dtb_spec.base,
                                  dtb_spec.size, vmo.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
    return;
  }

  status = vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
    return;
  }

  size_t size;
  zx_vmo_get_size(vmo.get(), &size);
  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_MAP_CONTIGUOUS;
  zx_paddr_t mapped_addr;
  status = zx_vmar_map(handle, map_flags, 0, vmo.get(), 0, size, &mapped_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map trace physical memory " << status;
    return;
  }
  FXL_CHECK(vmo);

  void* sos_dtb = reinterpret_cast<void*>(mapped_addr);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    goto out;
  }

  ret = update_nodes(sos_dtb, dtb, append_nodes);
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to append device node";
    goto out;
  }

out:
  zx_vmar_unmap(handle, mapped_addr, dtb_spec.size);
  fdt_pack(dtb);
}

static void add_vtee_irq_to_vgic(nbl_vmm::VmConfig& cfg,
                                 machina::VgicSpec& vgic) {
  if (cfg.vtee_notifier_irq() == 0) {
    return;
  }

  auto& irq = vgic.irqs;
  for (auto i = 0; i < irq.size(); i++) {
    if (irq[i] == cfg.vtee_notifier_irq()) {
      return;
    }
  }

  irq.push_back(cfg.vtee_notifier_irq());
}

static zx_status_t create_notify_irq_node(nbl_vmm::VmConfig& cfg,
                                          void* dtb,
                                          size_t dtb_size,
                                          const std::string& node,
                                          uint32_t irq) {
  if (irq == 0) {
    return ZX_OK;
  }

  if (irq < 32) {
    FXL_LOG(ERROR) << "Set irq value too small, set irq > 32";
    return ZX_ERR_OUT_OF_RANGE;
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int vtee_virtio_offset = fdt_path_offset(dtb, node.c_str());
  if (vtee_virtio_offset < 0) {
    FXL_LOG(WARNING) << "vtee dts not found, skipped: " << node;
    fdt_pack(dtb);
    return ZX_OK;
  }

  uint32_t props[] = {cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
                      cpu_to_fdt32(cfg.vtee_notifier_irq() - 32),
                      cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI),
                      cpu_to_fdt32(0)};
  ret = fdt_setprop(dtb, vtee_virtio_offset, "interrupts", props,
                    sizeof(props));
  check_status(ret, "interrupts");

  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t probe_vtee(nbl_vmm::VmConfig& cfg,
                              void* dtb,
                              size_t dtb_size) {
  int status = ZX_OK;
  uint32_t tee_vendor = (zx_system_get_tee_vendor_id() & ZX_TEE_VENDOR_MASK);
  std::string tee_node;
  switch (tee_vendor) {
    case ZX_TEE_VENDOR_NEBULA:
      tee_node = "/nebula-vtee/virtio";
      status = create_notify_irq_node(cfg, dtb, dtb_size, tee_node,
                                      cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK)
            << "Failed to create nebula tee vqueue notify irq node " << status;
      } else {
        FXL_LOG(INFO) << "Nebula vTEE probe ok";
      }
      break;
    case ZX_TEE_VENDOR_TRUSTONIC:
      tee_node = "/mobicore";
      status = create_notify_irq_node(cfg, dtb, dtb_size, tee_node,
                                      cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK)
            << "Failed to create trustonic irq node " << status;
      } else {
        FXL_LOG(INFO) << "Trustonic vTEE probe ok";
      }
      break;
    default:
      break;
  }
  return status;
}

static zx_status_t sec_boot_uos_lk2(uintptr_t guest_ip, machina::Guest& guest) {
  uint32_t product_id;
  uint32_t sec_boot;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID, &product_id);
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_SEC_BOOT, &sec_boot);
  FXL_LOG(INFO) << "product_id:" << product_id << "   vmid:" << guest.vmid()
                << "   sec_boot:" << sec_boot;

  int rc = nbl_act_status(NMT_ACT_PRODUCT_BASIC);
  if (rc != 0) {
    FXL_LOG(WARNING) << "SECURE BOOT DISABLED due to License not activated";
    return ZX_OK;
  }

  std::string str_pubk;
  // according produce id to slect str_pubk
  switch (product_id) {
    case BOARD_VID_QEMU:
      return ZX_OK;
    case 20:  // product_id = 20 , 8678 for BYD
      // get pubk
      str_pubk =
          "-----BEGIN PUBLIC KEY-----\n"
          "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA2zVjz+NL4XCxpTAmOsep\n"
          "Uuieh1lRID0+w5VG3vRELegjipre752rUiWCp0u0tf/TTcmzSakxVIFCvkdEFpYQ\n"
          "ze8+05ZBT/1uws9k02C5vorB1phXdDsTk8JPew5QNo4b8/nxsASufwifdmodUQuC\n"
          "fsvmSngazA0qYtRgMVShe097C7cPeSS40JvhsagY49cELMjNVKWmgMd7a8e+RUfa\n"
          "2ZwgKkwY9JgEqIeqI+8UUENJqnCsK2bG9IjFscZrM11IZrQt3Ise/CydG5TuxC2Q\n"
          "Te7UV3Bl0lF0r44RNCKO9o3sMTFNb206EIwxs4qVDDDVtpNG0Dalt7su86hMs2Gu\n"
          "XwIDAQAB\n"
          "-----END PUBLIC KEY-----\n";
      break;
    default:
      break;
  }
  if (sec_boot) {
    if (sec_boot == 11) {
      str_pubk =
          "-----BEGIN PUBLIC KEY-----\n"
          "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvXjNmeR7iflrbcp/A+3i\n"
          "3B07blfQUZOZ1+TbDpbJe1YikaV5KBdIeolaOPkPlirxjYH6pJTt4ACBMlbzkTgX\n"
          "C2q1+w1QVsseOsP5xnTTYK4V0asc6q66I9Cg5se16nHoCHVzvfqpidP5NFr7APA2\n"
          "I4J1ANGH8Hb7x+H018JBWLdaF3LQHNd/pnT5CNANY1f6hkQtptifLydkkz34FL+6\n"
          "TDUgix5Es9OljY1/WejRBr97czzNekWhRwTlb5o9D20bigJUxPcU0oMGM3b9cG6s\n"
          "Pj7lVBCgdqh6bw4wCVVRMuuqA2hkwQr9WWKXMyEqEWmMg5pd/cF/axcz2p0MisA9\n"
          "TQIDAQAB\n"
          "-----END PUBLIC KEY-----\n";
    } else if (sec_boot == 12) {
      str_pubk =
          "-----BEGIN PUBLIC KEY-----\n"
          "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvn55XgD1e5pR4+3kFDHn\n"
          "LQlVIJC1CtpS+1VaI68SuPLZIFf78QzGr3ypScZ9BMdVwwdHMrg6CNLRjkoqFPHr\n"
          "82PPtb+0DVSCfTq2wA8MyiHc3Fp3Mv4mTwMYazrFTAhU62fug0WTVUF2ZqhrBuXX\n"
          "arhSEO0ZCtOYq/nEQ1AWaqIgjgR8rE98ECSrRz0vAjdT5BChTsx/4cJ1z4jv6icF\n"
          "tAhHqMGWhzQjJP5LHWeass7DivNXC9JrY+Ca0okAmyXAKSwFhYJTPXNqi1qrIsCV\n"
          "9COLpnnId9qCuRca9cZhkf888Ygh/PJ1EbMD37sqH5HQ5jX0WuTYmusNU6iL4Hc7\n"
          "lwIDAQAB\n"
          "-----END PUBLIC KEY-----\n";
    }

    uintptr_t image_addr_start_pre = (uintptr_t)(guest_ip - 0x2000);

    int status = guest.Init_Sec_Boot(image_addr_start_pre);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map sec_boot mem";
      return status;
    }

    zx_vaddr_t image_addr_start = guest.va_sec_boot + 0xd00;
    zx_vaddr_t addr_lk2img_size = image_addr_start + LK2IMG_SIZE_OFFSET;
    char* str_size = reinterpret_cast<char*>(addr_lk2img_size);

    /* deal string */
    size_t size_Value = std::stoi(str_size);
    const char* str_pubk_ptr = str_pubk.c_str();

    // get signature
    zx_vaddr_t addr_signature = image_addr_start + (zx_vaddr_t)size_Value;
    unsigned char* str_signature =
        reinterpret_cast<unsigned char*>(addr_signature);
    OpenSSL_add_all_algorithms();
    ERR_load_BIO_strings();
    ERR_load_crypto_strings();
    BIO* bio = BIO_new(BIO_s_mem());
    BIO_puts(bio, str_pubk_ptr);
    EVP_PKEY* pubKey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pubKey);
    void* file_addr_start = reinterpret_cast<void*>(image_addr_start);
    EVP_DigestVerifyUpdate(ctx, file_addr_start, size_Value);
    size_t signature_len = 256;  // signature len
    int result = EVP_DigestVerifyFinal(ctx, str_signature, signature_len);
    if (result != 1) {
      FXL_LOG(ERROR) << "loading bl-an.img sec boot fail:";
      return -1;
    }
    FXL_LOG(INFO) << "loading bl-an.img sec boot success:";
    EVP_MD_CTX_free(ctx);
    BIO_free_all(bio);
    EVP_PKEY_free(pubKey);
    if (sec_boot > 0) {
      status = guest.Release_Init_Sec_Boot();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to unmap sec_boot mem";
      }
    }
  }
  return 0;
}

static zx_status_t platform_init_rpmb(fbl::unique_ptr<machina::VirtioRpmb>& rpmb,
                                      machina::Guest& guest,
                                      machina::PciBus& bus) {
  if (is_qemu_platform())
    return ZX_OK;

  int const guest_vmid = guest.vmid();
  int const rpmb_vmid = guest_vmid + 1;
  if (rpmb_vmid < 0 || rpmb_vmid >= UFS_VIRTIO_RPMB_MAX_GUESTS) {
    FXL_LOG(ERROR) << "invalid rpmb backend vmid: guest_vmid=" << guest_vmid
                   << " rpmb_vmid=" << rpmb_vmid;
    return ZX_ERR_INVALID_ARGS;
  }

  FXL_LOG(INFO) << "InitializeVirtioRpmb guest_vmid=" << guest_vmid
                << " backend_vmid=" << rpmb_vmid
                << " phys_base=0x" << std::hex << guest.phys_mem().phys_base()
                << " size=0x" << guest.phys_mem().size() << std::dec;

  auto virtio_rpmb = fbl::make_unique<machina::VirtioRpmb>(
      guest.phys_mem(), static_cast<uint16_t>(rpmb_vmid),
      machina::VirtioDevice::Transport::PCI, guest.device_async());
  if (!virtio_rpmb) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = bus.Connect(virtio_rpmb->pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "pci rpmb device connect bus failed " << status;
    return status;
  }

  rpmb = fbl::move(virtio_rpmb);
  return ZX_OK;
}

static zx_status_t init_virtio_block(
    std::vector<fbl::unique_ptr<machina::VirtioBlock>> &virtio_blocks,
    machina::Guest &guest, machina::MmioBus &mmio_bus, machina::PciBus &bus) {
  if (is_qemu_platform())
    return ZX_OK;
  char const *blk_backend_name = "/dev/class/block/002";
  zx_status_t status;

  fbl::unique_ptr<machina::BlockDispatcher> dispatcher;
  machina::BlockDispatcher::DispatcherOptions fdio_opts = {
      .mode = machina::BlockDispatcher::Mode::RW,
      .data_plane = machina::BlockDispatcher::DataPlane::DIRECTIO,
  };
  status = machina::BlockDispatcher::CreateFromPath(
      blk_backend_name, fdio_opts, guest.phys_mem(), &dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create direct-IO block_dispatcher" << status;
    return status;
  }

  auto fdio_block = fbl::make_unique<machina::VirtioBlock>(guest.phys_mem());
  status = fdio_block->SetDispatcher(fbl::move(dispatcher));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create virtio_block" << status;
  }

  status = mmio_bus.Connect(fdio_block->mmio_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "mmio device connect bus failed " << status;
  }
  virtio_blocks.push_back(fbl::move(fdio_block));

  fbl::unique_ptr<machina::BlockDispatcher> fifo_dispatcher;
  machina::BlockDispatcher::DispatcherOptions fifo_opts = {
      .mode = machina::BlockDispatcher::Mode::RW,
      .data_plane = machina::BlockDispatcher::DataPlane::FIFO,
      .vmid = guest.vmid(),
  };
  status = machina::BlockDispatcher::CreateFromPath(
      blk_backend_name, fifo_opts, guest.phys_mem(), &fifo_dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create fifo block_dispatcher " << status;
    return status;
  }

  auto block = fbl::make_unique<machina::VirtioBlock>(
          guest.phys_mem(), machina::VirtioBlock::Transport::PCI);
  if (!block) {
    return ZX_ERR_NO_MEMORY;
  }
  block->SetDispatcher(fbl::move(fifo_dispatcher));

  status = block->Start();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VirtioBlock start failed " << status;
    return status;
  }

  status = bus.Connect(block->pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "devices connect bus failed " << status;
    return status;
  }
  virtio_blocks.push_back(fbl::move(block));

  return ZX_OK;
}

int main(int argc, char** argv) {
  FXL_CHECK(argc == 2) << "Missing vmid argument";
  uint16_t vmid = std::stoul(argv[1]);

  fsl::MessageLoop loop;
  std::unique_ptr<component::ApplicationContext> application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  machina::VhmDevice vhm_device(vmid);
  component::ConnectToEnvironmentService(vhm_device.NewRequest());
  vhm_device.FetchVmConfig();

  nbl_vmm::VmConfig cfg;
  FXL_CHECK(cfg.ParseFromString(vhm_device.cfg()));

  machina::Guest guest;
  guest.SetVmid(vmid);
  auto vmems = vmems_build_from(cfg);
  zx_status_t status = guest.Init(vmems);
  FXL_CHECK(status == ZX_OK) << "failed to init guest:" << status;
  if (status != ZX_OK) {
    return status;
  }

#if 0
  // Setup multi SMMUv3
  std::vector<std::unique_ptr<machina::Smmuv3>> vsmmus;
  auto smmu_specs = vsmmu_build_from(cfg);
  if (smmu_specs.empty()) {
    FXL_LOG(INFO) << "Failed to initialize UOS vSmmu devices: no valid "
                     "configurations provided.";
  }
  for (const auto& spec : smmu_specs) {
    auto smmu_type = machina::Smmuv3::GetSmmuType(spec.paddr);
    auto vsmmu = std::make_unique<machina::Smmuv3>(
        &guest, static_cast<uint8_t>(smmu_type));

    status = vsmmu->Init(spec);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create UOS vSmmu device";
      return status;
    }
    vsmmus.push_back(std::move(vsmmu));
  }

  if (vsmmus.empty())
    FXL_LOG(ERROR) << "All UOS vSmmu device initializations failed.";
  else
    FXL_LOG(INFO) << "Successfully created multi UOS vSmmu devices.";
#endif

  guest.SetSchedIRQ(cfg.sched_irq());
  guest.SetDumpIRQ(cfg.dump_irq());

  std::vector<uint32_t> pci_irqs;
  int pci_irqs_size = cfg.pci_irqs_size();
  for (int i = 0; i < pci_irqs_size; i++) {
    auto irq = cfg.pci_irqs(i);
    pci_irqs.push_back(irq);
    // FXL_LOG(INFO) << "pci irq: " << irq;
  }

  status = machina::PciBus::config_pci_global_irqs(pci_irqs);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to config pci global irqs: " << status;
    return status;
  }

  uintptr_t guest_ip = cfg.entry();

  status = sec_boot_uos_lk2(guest_ip, guest);
  FXL_CHECK(status == ZX_OK) << "failed to init guest:" << status;
  if (status != ZX_OK) {
    return status;
  }

  std::vector<uint32_t> bind_pcpus;
  int bind_cpu_size = cfg.bind_pcpus_size();
  if (bind_cpu_size != 0) {
    FXL_CHECK(bind_cpu_size == cfg.num_cpus())
        << "cpu num mismtach:" << bind_cpu_size << "," << cfg.num_cpus();

    for (int i = 0; i < bind_cpu_size; i++) {
      zx_poweron_phy_cpu(cfg.bind_pcpus(i));
      bind_pcpus.push_back(cfg.bind_pcpus(i));
    }
    status = guest.SetCpuAffinity(bind_pcpus.data(), bind_cpu_size);
    FXL_CHECK(status == ZX_OK) << "failed to set cpu affinity:" << status;
  }

  guest.SetCpuNums(cfg.num_cpus());
  guest.SetUserVmid(guest.vmid() & 0xff);
  guest.SetSchedId((uint8_t)cfg.sched_id());
  std::vector<uint32_t> wakeup_irqs;
  auto wakeup_irq_num = cfg.wakeup_irqs_size();
  for (int i = 0; i < wakeup_irq_num; i++) {
    wakeup_irqs.push_back(cfg.wakeup_irqs(i));
  }

  guest.SetWakeupIrqs(wakeup_irqs.data(), wakeup_irq_num);
  guest.SetSmcIRQ(cfg.smc_irq());

  // Instantiate the inspect service.
  machina::InspectServiceImpl inspect_svc(application_context.get(),
                                          guest.phys_mem());

  FXL_LOG(INFO) << "create VM logstore";
  std::unique_ptr<VmlogStore> log_store_ = std::make_unique<VmlogStore>();
  if (log_store_ == nullptr) {
    FXL_LOG(ERROR) << "Failed to create vm log store";
    return ZX_ERR_NO_MEMORY;
  }

  status = log_store_->Initialize(guest.vmid());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initial vmlog store: " << status;
    return status;
  }

  // Setup virtual UARTs.
  FXL_LOG(INFO) << "create UOS uart device";
  machina::Uart uart;
  status = uart.Init(&guest, machina::kPl011PhysBase, log_store_.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create UART at " << std::hex
                   << machina::kPl011PhysBase;
    return status;
  }

  // Setup interrupt controller.
  machina::InterruptController interrupt_controller(&guest);
  machina::GicIts gic_its(&guest, &interrupt_controller);
  auto vgic = vgic_build_from(cfg);
  if (cfg.has_vgic()) {
    interrupt_controller.SetSPIVcpuMask(0xFF);
    add_vtee_irq_to_vgic(cfg, vgic);
    status = interrupt_controller.Init(cfg.num_cpus(), machina::Gic::V3, vgic);
    if (status == ZX_OK && vgic.has_its) {
      status = gic_its.Init(vgic.its_paddr);
      if (status == ZX_OK) {
        interrupt_controller.EnableIts(&gic_its);
      }
    }
  } else {
    std::vector<uint16_t> interrupts, percpu_interrupts;
    status = interrupt_controller.Init(cfg.num_cpus(), machina::Gic::V3,
                                       interrupts, percpu_interrupts);
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create interrupt controller";
    return status;
  }

  /* the MMIO interrupt number should be same with configuration in .lua */
  machina::MmioBus mmio_bus(&guest, &interrupt_controller, kMmioIrqVector);

  // Setup PCI.
  machina::PciBus bus(&guest, &interrupt_controller, &gic_its);
  status = bus.Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PCI bus";
    return status;
  }

  fbl::unique_ptr<machina::VirtioRpmb> virtio_rpmb;

  std::vector<fbl::unique_ptr<machina::VirtioBlock>> virtio_blocks;
  status = init_virtio_block(virtio_blocks, guest, mmio_bus, bus);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize block device";
    return status;
  }

  status = platform_init_rpmb(virtio_rpmb, guest, bus);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize virtio rpmb device";
    return status;
  }

  status = vhm_device.Init(&guest, &interrupt_controller, &gic_its);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize vhm device";
    return status;
  }

  //Setup spi BE
  FXL_LOG(INFO) << "spi be create.";
  machina::VirtioSPI spi(guest.phys_mem());
  if (status != ZX_OK) {
    return status;
  }
  status = bus.Connect(spi.pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "spi devices connect bus failed " << status;
    return status;
  }

  //Setup i2c BE
  FXL_LOG(INFO) << "i2c be create.";
  machina::VirtioI2C i2c(guest.phys_mem());
  if (status != ZX_OK) {
    return status;
  }
  status = bus.Connect(i2c.pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "i2c devices connect bus failed " << status;
    return status;
  }

  //Setup eint BE
  FXL_LOG(INFO) << "eint be create.";
  machina::VirtioEINT eint(guest.phys_mem());
  eint.SetVmid(vmid);
  status = bus.Connect(eint.pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "eint devices connect bus failed " << status;
    return status;
  }

  //Setup rtc BE
  FXL_LOG(INFO) << "rtc be create.";
  machina::VirtioRTC rtc(guest.phys_mem());
  if (!is_qemu_platform()) {
    status = rtc.Init(guest.vmid());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to init RTC with vmid: " << status;
      return status;
    }
    status = bus.Connect(rtc.pci_device());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "rtc device connect bus failed " << status;
      return status;
    }
    FXL_LOG(INFO) << "rtc device connect pci bus successed. device id: " << rtc.device_id();
  }

  machina::IpcClient ipc_mbox_client(&guest);
  component::ConnectToEnvironmentService(ipc_mbox_client.NewRequest());
  ipc_mbox_client.Init(vmid);
  guest.RegisterIpcRequestHandler(&ipc_mbox_client);

  guest.InitDump();

  machina::IpcMessageServiceSyncPtr IpcMessClient;
  component::ConnectToEnvironmentService(IpcMessClient.NewRequest());
  zx_vaddr_t tmp_mem_pa_;
  size_t tmp_mem_sz_;
  IpcMessClient->GetTraceBuf(&tmp_mem_pa_, &tmp_mem_sz_);
  guest.SetTraceMem(tmp_mem_pa_, tmp_mem_sz_);

#ifdef VHE_MMIO_TRAP_DEBUG
  IpcMessClient->GetMonitorVirtioMem(&tmp_mem_pa_, &tmp_mem_sz_);
  guest.SetMonitorVirtioMem(tmp_mem_pa_, tmp_mem_sz_);
#endif

  machina::RprocServiceSyncPtr rproc_svc;
  component::ConnectToEnvironmentService(rproc_svc.NewRequest());
  RprocClient rproc_client(vmid, std::move(rproc_svc), guest,
                           interrupt_controller);

  auto vsock_vq_cnt = cfg.vsock_irqs_size() / 2;
  if (vsock_vq_cnt) {
    std::vector<uint16_t> irqs;
    for (int qidx = 0; qidx < vsock_vq_cnt; qidx++) {
      auto irq = cfg.vsock_irqs(2 * qidx + 1);
      irqs.push_back(irq);
    }
    status = interrupt_controller.PassThroughInterrupts(irqs);
    FXL_CHECK(status == ZX_OK);
  }

  auto apu_vq_cnt = cfg.apu_irqs_size() / 2;
  if (apu_vq_cnt) {
    std::vector<uint16_t> irqs;
    for (int qidx = 0; qidx < apu_vq_cnt; qidx++) {
      auto irq = cfg.apu_irqs(2 * qidx + 1);
      irqs.push_back(irq);
    }
    status = interrupt_controller.PassThroughInterrupts(irqs);

    FXL_CHECK(status == ZX_OK);
  }

  auto cmdq_vq_cnt = cfg.cmdq_irqs_size() / 2;
  if (cmdq_vq_cnt) {
    std::vector<uint16_t> irqs;
    for (int qidx = 0; qidx < cmdq_vq_cnt; qidx++) {
      auto irq = cfg.cmdq_irqs(2 * qidx + 1);
      irqs.push_back(irq);
    }
    status = interrupt_controller.PassThroughInterrupts(irqs);

    FXL_CHECK(status == ZX_OK);
  }

  // Setup vmlog sinking
  FXL_LOG(INFO) << "create VmlogSrv BE";
  std::unique_ptr<VmlogSrv> vmlog_sink_;
  std::unique_ptr<TipcVqueueNotifier> vqueue_notifier_;
  fxl::UniqueFD alloc_fd(open(kGuestMemoryAllocator, O_RDWR));
  if (!alloc_fd.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open guest memory allocator";
    return ZX_ERR_INTERNAL;
  }
  vmlog_sink_ = std::make_unique<VmlogSrv>(alloc_fd, guest.vmid(), log_store_.get());
  if (!vmlog_sink_) {
    FXL_LOG(ERROR) << "Failed to create vmlog_sink_";
    return ZX_ERR_NO_MEMORY;
  }
  vmlog_sink_->Initialize();
  if (guest.vmid() == 0) {
    // for alps vmlog sink driver
    vqueue_notifier_ = std::make_unique<TipcVqueueNotifier>(UINT32_MAX, guest.vmid());
    if (vqueue_notifier_ == nullptr) {
      FXL_LOG(ERROR) << "Failed to create vqueue notifier,vmid: "<< guest.vmid();
      return ZX_ERR_NO_MEMORY;
    }
    vmlog_sink_->Start(vqueue_notifier_.get());
  }

  // In the case fdt blob is directly loaded from SOS filesystem, we can
  // patch fdt directly. (extra_params[0] is assumed to be GPA of fdt blob)
  if (!cfg.extra_params().empty()) {
    auto dtb_base = cfg.extra_params()[0];
    auto phys_base = get_phys_base(cfg);
    uint64_t dtb_size = 2 * PAGE_SIZE;
    machina::DeviceTreeSpec dtb = {dtb_base, dtb_size};
    rproc_client.PatchDeviceTree(dtb, phys_base);

    uintptr_t dtb_offset = dtb.base - phys_base;
    void* dtb_ptr = guest.phys_mem().as<void>(dtb_offset, dtb_size);
    update_virtio_gpu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_vsock_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_apu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_cmdq_irq(cfg, dtb_ptr, dtb_size);
    update_smc_irq(cfg, dtb_ptr, dtb_size);
    create_virtio_pci_nodes(cfg, dtb_ptr, dtb_size);
    probe_vtee(cfg, dtb_ptr, dtb_size);
    vmlog_sink_->CreateAlpsVmlogSinkNodes(dtb_ptr, dtb_size);
  }

  guest.RegisterDeviceTreePatcher([&cfg, &ipc_mbox_client, &guest,
                                   &rproc_client, &IpcMessClient, &vmlog_sink_](
                                      uint64_t dtb_base, uint64_t dtb_size) {
    machina::DeviceTreeSpec dtb{dtb_base, dtb_size};
    auto phys_base = get_phys_base(cfg);
    auto status = ipc_mbox_client.CreateMboxDeviceTreeNodes(dtb, phys_base, 0);
    FXL_CHECK(status == ZX_OK);

    guest.CreateDtbSpec(dtb);
#ifdef VHE_MMIO_TRAP_DEBUG
    auto monitor_virtio = machina::Monitor_virtio::GetInstance();
    FXL_CHECK(monitor_virtio != nullptr);

    monitor_virtio->SetMonitorVirtioMem(guest.monitor_virtio_pa(),
                                        guest.monitor_virtio_size());
    status = monitor_virtio->MapMonitorVirtioMem(guest, false);
    FXL_CHECK(status == ZX_OK);
    if (status != ZX_OK)
      FXL_LOG(ERROR) << "Failed to map Monitor Memory!";
    monitor_virtio->PatchMonitorVirtioDts(guest, phys_base);
#endif
    auto trace = machina::Utrace::GetInstance();
    FXL_CHECK(trace != nullptr);
    trace->SetTraceMem(guest.trace_pa(), guest.trace_size());
    status = trace->MapTraceBuf(guest, false);
    // FXL_CHECK(status == ZX_OK);
    if (ZX_OK == status) {
      trace->PatchTraceDts(guest, phys_base);
    } else {
      FXL_LOG(INFO) << "May not support nebula_trace!";
    }

    guest.InitSched(phys_base, false);
    guest.RegSchedIRQ();

    status = guest.AddVmemDeviceTreeNodes(dtb, phys_base);
    FXL_CHECK(status == ZX_OK);

    uintptr_t dtb_offset = dtb.base - phys_base;
    void* dtb_ptr = guest.phys_mem().as<void>(dtb_offset, dtb_size);
    status = update_cmdline(cfg, dtb_ptr, dtb_size);
    FXL_CHECK(status == ZX_OK);

    update_virtio_gpu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_vsock_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_apu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_cmdq_irq(cfg, dtb_ptr, dtb_size);
    update_smc_irq(cfg, dtb_ptr, dtb_size);
    status = create_virtio_pci_nodes(cfg, dtb_ptr, dtb_size);
    FXL_CHECK(status == ZX_OK);

    rproc_client.PatchDeviceTree(dtb, phys_base);
    if (guest.vmid() == 1) {
      machina::DeviceTreeSpec sos_dtb;
      IpcMessClient->GetDeviceTree(&sos_dtb.base, &sos_dtb.size);

      copy_sos_dtb_node(sos_dtb, zx_vmar_root_self(), dtb_ptr, dtb_size);
    }
    probe_vtee(cfg, dtb_ptr, dtb_size);
    vmlog_sink_->CreateAlpsVmlogSinkNodes(dtb_ptr, dtb_size);
  });

  auto initialize_vcpu = [&cfg, &interrupt_controller, &bind_pcpus](
                             machina::Guest* guest, uintptr_t guest_ip,
                             uint64_t id, machina::Vcpu* vcpu) {
    zx_status_t status =
        vcpu->Create(guest, guest_ip, id, kUosVcpuPriority, kUosVcpuTimeSlice);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create VCPU";
      return status;
    }

    // percpu interrupts should be bound in vcpu thread
    vcpu->RegisterStartCallback([&cfg, vcpu, id, &interrupt_controller] {
      auto& redistributors = interrupt_controller.redistributors();
      auto vgic = vgic_build_from(cfg);
      zx_status_t status = redistributors[id]->Init(vgic.percpu_irqs);
      FXL_CHECK(status == ZX_OK);
      redistributors[id]->BindVcpu(vcpu);
    });

    // Register VCPU with ID 0.
    status = interrupt_controller.RegisterVcpu(id, vcpu);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to register VCPU with interrupt controller";
      return status;
    }
    // Setup initial VCPU state.
    zx_vcpu_state_t vcpu_state = {};

    int reg = 0;
    auto extra_params = cfg.extra_params();
    for (auto& param : extra_params) {
      FXL_LOG(INFO) << "x[" << reg << "]=0x" << std::hex << param;
      vcpu_state.x[reg++] = param;
    }

    // Config VCPU group.
    if (id < bind_pcpus.size()) {
      vcpu->SetGroup(bind_pcpus[id]);
    } else {
      vcpu->SetGroup(id);
    }

    // Config VCPU budget
    auto budgets = cfg.budgets();
    if (id < budgets.size()) {
      vcpu->SetBudget(budgets[id]);
    }

    // Begin VCPU execution.
    return vcpu->Start(&vcpu_state);
  };

  guest.RegisterVcpuFactory(initialize_vcpu);

  status = guest.StartVcpu(guest_ip, 0 /* id */);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start VCPU-0 " << status;
    loop.PostQuitTask();
  }

  // Vcpu-0 started and ready to receive VMCall from NBL_VMM
  VmMessageHandler handler(&guest, application_context.get());
  vhm_device.RegisterMessageListener(handler.NewBinding());

  loop.Run();

  int ret = guest.Join();
  vmlog_sink_->Shutdown();
  log_store_->Shutdown();

  return ret;
}
