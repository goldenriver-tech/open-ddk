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
#include <cstring>
#include <ios>
#include <vector>
#include "garnet/bin/guest/vmm.h"

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/cpp/machina.h>
#include <lib/async/cpp/task.h>
#include <libfdt.h>
#include <pal/platform_defs.h>
#include <threads.h>
#include <trusty_std.h>
#include <uapi/err.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/threads.h>

// _trusty_ioctl deps
#include <trusty_std.h>
#include <trusty_syscalls.h>
#include <lib/zx/vmar.h>
#include "google/protobuf/text_format.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/log_settings.h"

static uint64_t kQemuItsPhysAddr = 0x8080000;
static uint64_t kMmioIrqVector = 180;

static constexpr char kDefaultPath[] = "/system/data/";

static constexpr char kGuestMemoryAllocator[] =
    "/dev/sys/platform/00:00:f/guest_memory_allocator";

static void* get_dtb(const machina::Guest& guest, GuestConfig& cfg) {
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;

  return guest.phys_mem().as<void>(dtb_offset, dtb_size);
}

static void* open_dtb(const machina::Guest& guest, GuestConfig& cfg) {
  auto dtb_spec = cfg.dtb();
  void* dtb = get_dtb(guest, cfg);

  int ret = fdt_open_into(dtb, dtb, dtb_spec.size);
  if (ret < 0)
    return nullptr;

  return dtb;
}

static void flush_dtb(const machina::Guest& guest, GuestConfig& cfg) {
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;

  auto ptr = guest.phys_mem().as<void>(dtb_offset, dtb_size);
  zx_cache_flush(ptr, dtb_size, ZX_CACHE_FLUSH_DATA);
}

static zx_status_t read_guest_cfg(const char* cfg_path, GuestConfig* cfg) {
  GuestConfigParser parser(cfg);
  if (parser.ParseConfigFromLua(cfg_path, cfg)) {
    FXL_LOG(ERROR) << "Failed to read config: " << cfg_path;
    return ZX_ERR_NOT_FILE;
  }

  return ZX_OK;
}

static zx_status_t read_product_cfg(const char* cfg_path, GuestConfig* cfg) {
  GuestConfigParser parser(cfg);
  if (parser.ParseProductConfigFromLua(cfg_path, cfg)) {
    FXL_LOG(ERROR) << "Failed to read config: " << cfg_path;
    return ZX_ERR_NOT_FILE;
  }
  return ZX_OK;
}

static int update_nodes(void* src_dtb,
                        void* dest_dtb,
                        std::vector<std::string>& nodes) {
  if (fdt_check_header((void*)src_dtb) != 0) {
      FXL_LOG(ERROR) << "Invalid DTB magic or format";
  }
  int src_root = fdt_path_offset(src_dtb, "/");
  if (src_root < 0) {
    FXL_LOG(INFO) << "Failed to get src_dtb root";
    return src_root;
  }
  int dest_root = fdt_path_offset(dest_dtb, "/");
  if (dest_root < 0) {
    FXL_LOG(INFO) << "Failed to get dest_dtb root";
    return dest_root;
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
      std::vector<std::string> mddriver_target_nodes = {"ccci,modem_info_v2",
                                              "md1_ccb_gear_list",
                                              "md1_ccb_gear_ver",
                                              "md1_ccb_cap_gear",
                                              "md_low_power_addr",
                                              "md_dbm_addr"};
      int src_prop = fdt_first_property_offset(src_dtb, src_node);
      while (src_prop > 0) {
        const char* prop_name;
        int len = 0;
        const void* prop_value =
            fdt_getprop_by_offset(src_dtb, src_prop, &prop_name, &len);
        if (prop_name == NULL || len == 0) {
          FXL_LOG(INFO) << "Not found any prop in node";
        }
        if (strcmp(prop_name, "compatible") == 0 || strcmp(prop_name, "phandle") == 0) {
          src_prop = fdt_next_property_offset(src_dtb, src_prop);
          continue;;
        }
        for (std::string& target_node : mddriver_target_nodes) {
          if (strcmp(prop_name, target_node.c_str()) == 0) {
            int result =
                fdt_setprop(dest_dtb, dest_node, prop_name, prop_value, len);
            if (result < 0) {
              FXL_LOG(INFO) << "Failed to set node properties";
              return result;
            }
          }
        }
        src_prop = fdt_next_property_offset(src_dtb, src_prop);
      }
    }
  }
  return 0;
}

[[maybe_unused]] static int find_subnode_offset(const void* dtb,
                                                int start_offset,
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

static std::vector<int> find_resv_mem_nodes_with_string(
    const void* dtb,
    const char* search_string,
    bool mulit_nodes) {
  int offset = 0;
  int depth = 0;
  const char* node_name = NULL;
  char ch = '0';
  std::vector<int> node_offset;

  if ((!search_string) || (!dtb) || (strlen(search_string) < 1)) {
    FXL_LOG(WARNING) << "Invalid args!";
    return node_offset;
  }

  // Start from the reserved-memory node
  offset = fdt_path_offset(dtb, "/reserved-memory");
  for (depth = 0; (offset >= 0) && (depth >= 0);
       offset = fdt_next_node(dtb, offset, &depth)) {
    if (depth != 1) {
      continue;
    }

    node_name = fdt_get_name(dtb, offset, NULL);
    if (node_name == NULL) {
      continue;
    }

    // Check if node name contains the search string
    if (strstr(node_name, search_string)) {
      ch = node_name[strlen(node_name) - strlen(search_string) - 2];
      if (ch >= '0' && ch <= '9') {
        // match pattern: mblock-[0-9]+-<search_string>
        node_offset.push_back(offset);
      } else if (ch == 'k') {
        // match pattern: mblock-<search_string>
        node_offset.push_back(offset);
      } else {
        continue;
      }

      if (!mulit_nodes) {
        break;
      }
    }
  }

  return node_offset;
}

static constexpr uint64_t kMegaBytes = 1ull << 20;
static constexpr uintptr_t kReservedGuestMemorySize = 1536 * kMegaBytes;

static int get_reserve_memory(void* dtb, const char* suffix) {
  std::vector<int> node_offset;
  node_offset = find_resv_mem_nodes_with_string(dtb, suffix, false);
  if (node_offset.size() == 1) {
    return node_offset[0];
  }
  return -1;
}

static std::vector<int> get_reserve_memory_multi(void* dtb,
                                                 const char* suffix) {
  return find_resv_mem_nodes_with_string(dtb, suffix, true);
}

static zx_status_t probe_guest_reserved_memory(void* dtb,
                                               uint64_t* base,
                                               uint64_t* size) {
  uint64_t rk1_base, rk1_size;
  zx_status_t status = ZX_ERR_BAD_STATE;

  int gz_guest_off1 = get_reserve_memory(dtb, "gz-guest-rk1");
  if (gz_guest_off1 > 0) {
    fdt_getprop_cells_u64(dtb, gz_guest_off1, "reg", 2, &rk1_base, &rk1_size);
  }

  uint64_t rk0_base, rk0_size;
  int gz_guest_off0 = get_reserve_memory(dtb, "gz-guest-rk0");
  if (gz_guest_off0 > 0) {
    fdt_getprop_cells_u64(dtb, gz_guest_off0, "reg", 2, &rk0_base, &rk0_size);
  }

  if ((gz_guest_off1 > 0) && (gz_guest_off0 > 0)) {
    FXL_CHECK(rk0_base + rk0_size == rk1_base)
        << "guest reserved memory is not contiguous";
    *base = rk0_base;
    *size = rk0_size + rk1_size;
  } else if (gz_guest_off1 > 0) {
    *base = rk1_base;
    *size = rk1_size;
  } else if (gz_guest_off0 > 0) {
    *base = rk0_base;
    *size = rk0_size;
  }

  if (gz_guest_off1 > 0) {
    fdt_del_node(dtb, gz_guest_off1);
    status = ZX_OK;
  }
  if (gz_guest_off0 > 0) {
    fdt_del_node(dtb, gz_guest_off0);
    status = ZX_OK;
  }

  return status;
}

static bool is_qemu_platform() {
  static uint32_t product_id;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID, &product_id);
  return product_id == BOARD_VID_QEMU;
}

static zx_status_t unmap_mpu_mem_region(machina::Guest& guest,
                                        GuestConfig& cfg) {
  void* dtb = open_dtb(guest, cfg);
  if (dtb == nullptr) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // remove reserved memory region protected by SMPU from guest stage 2 mapping
  for (const machina::VmemSpec& spec : guest.vmems()) {
    if (spec.policy != machina::MemoryPolicy::kMpu) {
      continue;
    }

    uint64_t mem_base, mem_size;
    zx_status_t status = ZX_ERR_BAD_STATE;

    std::vector<int> node_offsets =
        get_reserve_memory_multi(dtb, spec.name.c_str());
    if (node_offsets.size() == 0) {
      FXL_LOG(INFO) << "MPU memory region not found in dtb, ignore unmap: "
                    << spec.name;
      continue;
    }

    for (int offset : node_offsets) {
      const char* node_name = fdt_get_name(dtb, offset, NULL);

      fdt_getprop_cells_u64(dtb, offset, "reg", 2, &mem_base, &mem_size);
      status = guest.UnmapPhysicalMemory(mem_base, mem_size);
      if (status != ZX_OK) {
        FXL_LOG(WARNING) << "Failed to unmap MPU memory region: " << node_name
                         << ", base: " << std::hex << mem_base
                         << ", size: " << std::hex << mem_size
                         << ", status: " << status;
      } else {
        FXL_LOG(INFO) << "Successfully unmap MPU memory region: " << node_name
                      << ", base: " << std::hex << mem_base
                      << ", size: " << std::hex << mem_size;
      }
    }
  }

  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t create_acrn_vhm_node(const machina::Guest& guest,
                                        GuestConfig& cfg) {
  uint32_t cells_size = 3;
  int ret = 0;

  void* dtb = open_dtb(guest, cfg);
  if (dtb == nullptr) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  guest.GetGICInterruptCellSize(dtb, cells_size);

  int root_offset = fdt_path_offset(dtb, "/");
  check_status(root_offset, "root");

  int hsm_offset = fdt_add_subnode(dtb, root_offset, "acrn_hsm");
  check_status(hsm_offset, "acrn_hsm");

  ret = fdt_setprop_string(dtb, hsm_offset, "compatible", "nbl,acrn-hsm");
  check_status(ret, "compatible");

  uint32_t props[] = {
      cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI), cpu_to_fdt32(cfg.vhm_irq() - 32),
      cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI), cpu_to_fdt32(0)};
  ret = fdt_setprop(dtb, hsm_offset, "interrupts", props,
                    cells_size * sizeof(uint32_t));
  check_status(ret, "interrupts");

  if (cfg.gpu_irq() > 0) {
    int irq_notify_offset = fdt_add_subnode(dtb, hsm_offset, "irq_notify_gpu");
    check_status(irq_notify_offset, "irq_notify");

    ret = fdt_setprop_string(dtb, irq_notify_offset, "compatible",
                             "acrn,irq_notify");
    check_status(ret, "compatible");

    // clang-format off
    uint32_t irq_notify[] = {cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
                            cpu_to_fdt32(cfg.gpu_irq() - 32),
                            cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI),
                            cpu_to_fdt32(0)};
    // clang-format on
    ret = fdt_setprop(dtb, irq_notify_offset, "interrupts", irq_notify,
                      cells_size * sizeof(uint32_t));
    check_status(ret, "interrupts");
  }

  auto vsock_irqs = cfg.vsock_irqs();
  if (!vsock_irqs.empty()) {
    int irq_notify_offset =
        fdt_add_subnode(dtb, hsm_offset, "irq_notify_vsock");
    check_status(irq_notify_offset, "irq_notify");

    ret = fdt_setprop_string(dtb, irq_notify_offset, "compatible",
                             "acrn,irq_notify");
    check_status(ret, "compatible");

    std::vector<uint32_t> props;
    for (auto& irq : vsock_irqs) {
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
      props.push_back(cpu_to_fdt32(irq - 32));
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
      if (!is_qemu_platform())
        props.push_back(0);
    }
    ret = fdt_setprop(dtb, irq_notify_offset, "interrupts", props.data(),
                      props.size() * sizeof(uint32_t));
    check_status(ret, "interrupts");
  }

  auto apu_irqs = cfg.apu_irqs();
  if (!apu_irqs.empty()) {
    int irq_notify_offset = fdt_add_subnode(dtb, hsm_offset, "irq_notify_apu");
    check_status(irq_notify_offset, "irq_notify");

    ret = fdt_setprop_string(dtb, irq_notify_offset, "compatible",
                             "acrn,irq_notify");
    check_status(ret, "compatible");

    std::vector<uint32_t> props;
    for (auto& irq : apu_irqs) {
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
      props.push_back(cpu_to_fdt32(irq - 32));
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
      if (!is_qemu_platform())
        props.push_back(0);
    }
    ret = fdt_setprop(dtb, irq_notify_offset, "interrupts", props.data(),
                      props.size() * sizeof(uint32_t));
    check_status(ret, "interrupts");
  }

  auto cmdq_irqs = cfg.cmdq_irqs();
  if (!cmdq_irqs.empty()) {
    int irq_notify_offset = fdt_add_subnode(dtb, hsm_offset, "irq_notify_cmdq");
    check_status(irq_notify_offset, "irq_notify");

    ret = fdt_setprop_string(dtb, irq_notify_offset, "compatible",
                             "acrn,irq_notify");
    check_status(ret, "compatible");

    std::vector<uint32_t> props;
    for (auto& irq : cmdq_irqs) {
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
      props.push_back(cpu_to_fdt32(irq - 32));
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
      if (!is_qemu_platform())
        props.push_back(0);
    }
    ret = fdt_setprop(dtb, irq_notify_offset, "interrupts", props.data(),
                      props.size() * sizeof(uint32_t));
    check_status(ret, "interrupts");
  }

  int reserved_mem_offset = fdt_path_offset(dtb, "/reserved-memory");
  check_status(ret, "reserved_mem_offset");

  int guest_mem_offset =
      fdt_add_subnode(dtb, reserved_mem_offset, "acrn_guest_memory");
  check_status(guest_mem_offset, "acrn_guest_memory");

  ret = fdt_setprop_string(dtb, guest_mem_offset, "compatible",
                           "nbl,guest-memory");
  check_status(ret, "compatible");

  ret = fdt_setprop(dtb, guest_mem_offset, "no_map", nullptr, 0);
  check_status(ret, "no-map");

  uint64_t base, size;
  if (probe_guest_reserved_memory(dtb, &base, &size) == ZX_OK) {
    ret = fdt_setprop_cells_u64(dtb, guest_mem_offset, "reg", 2, base, size);
    check_status(ret, "reg");
  } else {
    if (cfg.guest_reserved_memory() != 0) {
      ret = fdt_setprop_u64(dtb, guest_mem_offset, "size",
                            cfg.guest_reserved_memory() * kMegaBytes);
    } else {
      ret = fdt_setprop_u64(dtb, guest_mem_offset, "size",
                            kReservedGuestMemorySize);
    }

    check_status(ret, "size");
    ret = fdt_setprop_u64(dtb, guest_mem_offset, "alignment", 2 * kMegaBytes);
    check_status(ret, "alignment");

    uint64_t value[] = {cpu_to_fdt64(cfg.phys_base()),
                        cpu_to_fdt64(cfg.phys_size())};
    ret = fdt_setprop(dtb, guest_mem_offset, "alloc-ranges", value,
                      sizeof(value));
    check_status(ret, "alloc-ranges");
  }

  fdt_pack(dtb);

  return ZX_OK;
}

static zx_status_t create_smc_irq_node(const machina::Guest& guest,
                                       GuestConfig& cfg) {
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;

  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int root_offset = fdt_path_offset(dtb, "/");
  check_status(root_offset, "root");

  int irq_offset = fdt_add_subnode(dtb, root_offset, "nebula_smc");
  check_status(irq_offset, "nebula_smc");

  ret = fdt_setprop_string(dtb, irq_offset, "compatible", "grt,nebula_smc");
  check_status(ret, "compatible");

  std::vector<uint32_t> props;
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
  props.push_back(cpu_to_fdt32(cfg.smc_irq() - 32));
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
  if (!is_qemu_platform())
    props.push_back(0);

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  check_status(ret, "interrupts");

  fdt_pack(dtb);

  return ZX_OK;
}

static const std::string get_product_path(const machina::Guest& guest,
                                          virtualization::Config& vmm_cfg,
                                          GuestConfig& cfg) {
  auto default_path = std::string(kDefaultPath) + vmm_cfg.default_cfg.get();
  return default_path;
}

static zx_status_t add_pmu_irq_node(const machina::Guest& guest,
                                    GuestConfig& cfg) {
  int pmu_use = 0;
  int ret = 0;
  auto irq = cfg.vgic().percpu_irqs;
  for (auto it = irq.begin(); it != irq.end(); ++it) {
    if (*it == 23) {
      pmu_use = 1;
      break;
    }
  }

  void* dtb = open_dtb(guest, cfg);
  if (dtb == nullptr) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  int pmu_blackhawk_offset = fdt_path_offset(dtb, "/pmu-blackhawk");
  if (pmu_blackhawk_offset < 0) {
    FXL_LOG(WARNING) << "pmu is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if (pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_blackhawk_offset, "status", "disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_blackhawk_offset, "status", "okay");
  check_status(ret, "status");
  int pmu_hunter_elp_offset = fdt_path_offset(dtb, "/pmu-hunter-elp");
  if (pmu_hunter_elp_offset < 0) {
    FXL_LOG(WARNING) << "pmu_hunter_elp_offset is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if (pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_hunter_elp_offset, "status", "disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_hunter_elp_offset, "status", "okay");
  int pmu_hunter_offset = fdt_path_offset(dtb, "/pmu-hunter");
  if (pmu_hunter_offset < 0) {
    FXL_LOG(WARNING) << "pmu_hunter_offset is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if (pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_hunter_offset, "status", "disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_hunter_offset, "status", "okay");
  check_status(ret, "status");
  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t add_pmu76_irq_node(const machina::Guest& guest,
                                      GuestConfig& cfg) {
  int pmu_use = 0;
  auto irq = cfg.vgic().percpu_irqs;
  for (auto it = irq.begin(); it != irq.end(); ++it) {
    if (*it == 23) {
      pmu_use = 1;
      break;
    }
  }

  int ret = 0;
  void* dtb = open_dtb(guest, cfg);
  if (dtb == nullptr) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  int pmu_blackhawk_offset = fdt_path_offset(dtb, "/pmu-a715");
  if (pmu_blackhawk_offset < 0) {
    FXL_LOG(WARNING) << "pmu is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if (pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_blackhawk_offset, "status", "disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_blackhawk_offset, "status", "okay");
  check_status(ret, "status");
  int pmu_hunter_elp_offset = fdt_path_offset(dtb, "/pmu-a510");
  if (pmu_hunter_elp_offset < 0) {
    FXL_LOG(WARNING) << "pmu_hunter_elp_offset is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if (pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_hunter_elp_offset, "status", "disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_hunter_elp_offset, "status", "okay");
  check_status(ret, "status");
  fdt_pack(dtb);
  return ZX_OK;
}

/*
virtio-pci device node:

pci@fde000000 {
  device_type = "pci";
  compatible = "pci-host-ecam-generic";
  reg = <0xf 0xde000000 0x0 0x1000000>;
  bus-range = <0 0>;
  ranges = <0x2000000 0x0 0xb0001000 0xf 0xb0001000 0x0 0xfff000>;
  #address-cells = <3>;
  #size-cells = <2>;
  #interrupt-cells = <1>;
  msi-parent = <&its>;

  interrupt-map-mask = <0xf800 0 0 1>;
  interrupt-map =
          <0x0000 0 0 1 &gic 0 0 0 400 4 0>, // HIGH MID LOW WIRE GIC SPI 432 HIGH/CPU-0
          <0x0800 0 0 1 &gic 0 0 0 401 4 0>, // HIGH MID LOW WIRE GIC SPI 433 HIGH/CPU-0
          <0x1000 0 0 1 &gic 0 0 0 402 4 0>, // HIGH MID LOW WIRE GIC SPI 434 HIGH/CPU-0
          <0x1800 0 0 1 &gic 0 0 0 403 4 0>, // HIGH MID LOW WIRE GIC SPI 435 HIGH/CPU-0
          <0x2000 0 0 1 &gic 0 0 0 404 4 0>, // HIGH MID LOW WIRE GIC SPI 436 HIGH/CPU-0
          <0x2800 0 0 1 &gic 0 0 0 405 4 0>, // HIGH MID LOW WIRE GIC SPI 437 HIGH/CPU-0
          <0x3000 0 0 1 &gic 0 0 0 406 4 0>, // HIGH MID LOW WIRE GIC SPI 438 HIGH/CPU-0
          <0x3800 0 0 1 &gic 0 0 0 407 4 0>, // HIGH MID LOW WIRE GIC SPI 439 HIGH/CPU-0
          <0x4000 0 0 1 &gic 0 0 0 408 4 0>, // HIGH MID LOW WIRE GIC SPI 440 HIGH/CPU-0
          <0x4800 0 0 1 &gic 0 0 0 409 4 0>, // HIGH MID LOW WIRE GIC SPI 441 HIGH/CPU-0
          <0x5000 0 0 1 &gic 0 0 0 410 4 0>, // HIGH MID LOW WIRE GIC SPI 442 HIGH/CPU-0
          <0x5800 0 0 1 &gic 0 0 0 411 4 0>, // HIGH MID LOW WIRE GIC SPI 443 HIGH/CPU-0
};
*/

[[maybe_unused]] static zx_status_t create_virtio_pci_nodes(
    const machina::Guest& guest,
    GuestConfig& cfg,
    int32_t* pci_global_irqs) {
  int ret = 0;

  if (pci_global_irqs == nullptr) {
    FXL_LOG(ERROR) << "Invalid pci_global_irqs pointer";
    return ZX_ERR_INVALID_ARGS;
  }

  void* dtb = open_dtb(guest, cfg);
  if (dtb == nullptr) {
    FXL_LOG(ERROR) << "Invalid device tree";
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

    ret = fdt_setprop_cell(dtb, pci_offset, "#size-cells", 0x02);
    check_status(ret, "#size-cells");

    ret = fdt_setprop_cell(dtb, pci_offset, "#interrupt-cells", 0x01);
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
    ret = fdt_setprop_cell(dtb, pci_offset, "msi-parent", its_phandle);
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

// TODO: read vmlog mblock table from sos.json config file
// format: {mblock_name, log_prefix}
static std::map<std::string, std::string> vmlog_mblocks = {
    {"vmlog0", "alps"},
    {"vmlog1", "tbox"},
};

static zx_status_t create_vmlog_nodes(const machina::Guest& guest,
                                      GuestConfig& cfg) {
  std::string tmp_node_name;
  int ret = 0;

  void* dtb = open_dtb(guest, cfg);
  if (dtb == nullptr) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int nebula_node_offset = fdt_path_offset(dtb, "/nebula");
  check_status(nebula_node_offset, "nebula");

  for (auto m : vmlog_mblocks) {
    const char* name = m.first.c_str();
    const char* log_prefix = m.second.c_str();
    int tmp_node_offset = get_reserve_memory(dtb, name);
    if (tmp_node_offset < 0) {
      FXL_LOG(ERROR) << "failed to get reserved memory " << name;
      continue;
    }

    const char* node_name = fdt_get_name(dtb, tmp_node_offset, NULL);
    FXL_CHECK(node_name != NULL)
        << "failed to get reserved memory node name: " << name;

    // the memory content of the node_name point to might be change after
    // the device tree is modified, we should copy the node_name first.
    tmp_node_name.assign(node_name);

    int vmlog_node_offset = fdt_add_subnode(dtb, nebula_node_offset, name);
    check_status(vmlog_node_offset, name);

    ret = fdt_setprop_string(dtb, vmlog_node_offset, "compatible",
                             "grt,vm-log-srv");
    check_status(ret, "compatible");

    ret = fdt_setprop_string(dtb, vmlog_node_offset, "log_prefix", log_prefix);
    check_status(ret, "log_prefix");

    ret = fdt_setprop_string(dtb, vmlog_node_offset, "reserved_mem",
                             tmp_node_name.c_str());
    check_status(ret, "reserved_mem");

    FXL_LOG(INFO) << "created vmlog dts node: " << name;
  }

  fdt_pack(dtb);
  return ret;
}

static void add_vtee_irq_to_vgic(GuestConfig& cfg) {
  if (cfg.vtee_notifier_irq() == 0) {
    return;
  }

  auto& irq = cfg.vgic().irqs;
  for (auto i = 0; i < irq.size(); i++) {
    if (irq[i] == cfg.vtee_notifier_irq()) {
      return;
    }
  }

  irq.push_back(cfg.vtee_notifier_irq());
}

static zx_status_t create_notify_irq_node(const machina::Guest& guest,
                                          GuestConfig& cfg,
                                          const std::string& node,
                                          uint32_t irq) {
  if (irq == 0) {
    return ZX_OK;
  }

  if (irq < 32) {
    FXL_LOG(ERROR) << "Set irq value too small, set irq > 32";
    return ZX_ERR_OUT_OF_RANGE;
  }

  int ret;
  void* dtb = open_dtb(guest, cfg);
  if (dtb == nullptr) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int vtee_virtio_offset = fdt_path_offset(dtb, node.c_str());
  if (vtee_virtio_offset < 0) {
    FXL_LOG(WARNING) << "vtee dts not found, skipped: " << node;
    fdt_pack(dtb);
    return ZX_OK;
  }

  uint32_t props[] = {
      cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI), cpu_to_fdt32(irq - 32),
      cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI), cpu_to_fdt32(0)};

  ret =
      fdt_setprop(dtb, vtee_virtio_offset, "interrupts", props, sizeof(props));
  check_status(ret, "interrupts");

  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t probe_vtee(const machina::Guest& guest, GuestConfig& cfg) {
  int status = ZX_OK;
  uint32_t tee_vendor = (zx_system_get_tee_vendor_id() & ZX_TEE_VENDOR_MASK);
  std::string tee_node;
  switch (tee_vendor) {
    case ZX_TEE_VENDOR_NEBULA:
      tee_node = "/nebula-vtee/virtio";
      status =
          create_notify_irq_node(guest, cfg, tee_node, cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK)
            << "Failed to create nebula tee vqueue notify irq node " << status;
      } else {
        FXL_LOG(INFO) << "Nebula vTEE probe ok";
      }
      break;
    case ZX_TEE_VENDOR_TRUSTONIC:
      tee_node = "/mobicore";
      status =
          create_notify_irq_node(guest, cfg, tee_node, cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK)
            << "Failed to create trustonic tee vqueue notify irq node "
            << status;
      } else {
        FXL_LOG(INFO) << "Trustonic vTEE probe ok";
      }
      break;
    default:
      break;
  }

  return status;
}

static void config_irq_monitor(machina::IrqMonitorSpec& spec) {
  struct irq_monitor_cfg cfg = {
      .enable = spec.enable, .irq_cnt_threshold = spec.threshold, .irqs = {0}};

  for (auto irq : spec.irqs) {
    if (irq < 0) {
      memset(cfg.irqs, 0xff, sizeof(cfg.irqs));
      break;
    }
    cfg.irqs[irq / 64] |= 1 << (irq & 63);
  }

  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_IRQ_MONITOR_CONFIG, (void*)&cfg);
}

static int nbl_loading_show_task_start(void* ctx) {
  return nbl_loading_show_task(ctx);
}

static uint64_t parse_node_address(const std::string& node_path) {
  size_t pos = node_path.rfind('@');
  if (pos == std::string::npos || pos + 1 >= node_path.size()) {
    std::cerr << "Invalid node path: missing or empty '@' suffix: " << node_path
              << "\n";
    return 0;
  }
  const char* addr_str = node_path.c_str() + pos + 1;
  char* endptr = nullptr;
  uint64_t addr = strtoull(addr_str, &endptr, 16);
  if (!endptr || endptr == addr_str) {
    std::cerr << "Failed to convert address string: " << addr_str << "\n";
    return 0;
  }
  return addr;
}

static uint64_t get_its_paddr(machina::Guest& guest, GuestConfig& cfg) {
  if (is_qemu_platform()) {
    return kQemuItsPhysAddr;
  }

  uint64_t paddr;
  void* dtb = get_dtb(guest, cfg);

  int sym_offset = fdt_path_offset(dtb, "/__symbols__");
  if (sym_offset < 0) {
    FXL_LOG(ERROR) << "Failed to find __symbols__ node";
    return 0;
  }
  int len = 0;
  const char* its_path =
      static_cast<const char*>(fdt_getprop(dtb, sym_offset, "its", &len));
  if (!its_path || len <= 0) {
    FXL_LOG(ERROR) << "Failed to get 'its' property from __symbols__";
    return 0;
  }
  std::string path_str(its_path, len - 1);  // remove trailing null
  paddr = parse_node_address(path_str);
  if (paddr == 0) {
    FXL_LOG(ERROR) << "Failed to parse address from ITS path: " << path_str;
    return 0;
  }
  return paddr;
}

template <typename T>
static T duplicate(const T& handle, zx_rights_t rights) {
  T handle_out;
  zx_status_t status = handle.duplicate(rights, &handle_out);
  FXL_CHECK(status == ZX_OK) << "Failed to duplicate handle";
  return handle_out;
}

Vmm::Vmm(component::ApplicationContext* app_context)
    : application_context_(app_context),
      interrupt_controller_(&guest_),
      gic_its_(&guest_, &interrupt_controller_),
      bus_(&guest_, &interrupt_controller_, &gic_its_),
      mmio_bus_(&guest_, &interrupt_controller_, kMmioIrqVector),
      cluster_(guest_.phys_mem()),
      spi_(guest_.phys_mem()),
      i2c_(guest_.phys_mem()),
      eint_(guest_.phys_mem()),
      rtc_(guest_.phys_mem()) {}

Vmm::~Vmm() {
  vsock_->Stop();
  guest_.Join();
  block_.reset();
  fdio_block_.reset();
  guest_.TeeVmDestroy(guest_.vmid());
  vmlog_sink_->Shutdown();
  log_store_->Shutdown();
  FXL_LOG(INFO) << "VMM gracefully terminated";
}

zx_status_t Vmm::InitializeVirtioBlock() {
  char const *blk_backend_name = "/dev/class/block/002";

  fbl::unique_ptr<machina::BlockDispatcher> dispatcher;
  machina::BlockDispatcher::DispatcherOptions fdio_opts = {
      .mode = machina::BlockDispatcher::Mode::RW,
      .data_plane = machina::BlockDispatcher::DataPlane::DIRECTIO,
  };
  auto status = machina::BlockDispatcher::CreateFromPath(
      blk_backend_name, fdio_opts, guest_.phys_mem(), &dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create direct-IO block_dispatcher" << status;
    return status;
  }

  fdio_block_ = std::make_unique<machina::VirtioBlock>(guest_.phys_mem());
  status = fdio_block_->SetDispatcher(fbl::move(dispatcher));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create virtio_block" << status;
    return status;
  }

  status = mmio_bus_.Connect(fdio_block_->mmio_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "mmio device connect bus failed " << status;
    return status;
  }

  fbl::unique_ptr<machina::BlockDispatcher> fifo_dispatcher;
  machina::BlockDispatcher::DispatcherOptions fifo_opts = {
      .mode = machina::BlockDispatcher::Mode::RW,
      .data_plane = machina::BlockDispatcher::DataPlane::FIFO,
      .vmid = guest_.vmid(),
  };
  status = machina::BlockDispatcher::CreateFromPath(
      blk_backend_name, fifo_opts, guest_.phys_mem(), &fifo_dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create fifo block_dispatcher " << status;
    return status;
  }

  block_ = fbl::make_unique<machina::VirtioBlock>(
          guest_.phys_mem(), machina::VirtioBlock::Transport::PCI);
  if (!block_) {
    return ZX_ERR_NO_MEMORY;
  }
  block_->SetDispatcher(fbl::move(fifo_dispatcher));

  status = block_->Start();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VirtioBlock start failed " << status;
    return status;
  }

  status = bus_.Connect(block_->pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "devices connect bus failed " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t Vmm::InitializeVirtioRpmb() {
  int const guest_vmid = guest_.vmid();
  int const rpmb_vmid = guest_vmid + 1;

  if (rpmb_vmid < 0 || rpmb_vmid >= UFS_VIRTIO_RPMB_MAX_GUESTS) {
    FXL_LOG(ERROR) << "invalid rpmb backend vmid: guest_vmid=" << guest_vmid
                   << " rpmb_vmid=" << rpmb_vmid;
    return ZX_ERR_INVALID_ARGS;
  }

  FXL_LOG(INFO) << "InitializeVirtioRpmb guest_vmid=" << guest_vmid
                << " backend_vmid=" << rpmb_vmid
                << " phys_base=0x" << std::hex << guest_.phys_mem().phys_base()
                << " size=0x" << guest_.phys_mem().size() << std::dec;

  rpmb_ = std::make_unique<machina::VirtioRpmb>(guest_.phys_mem(),
                                                static_cast<uint16_t>(rpmb_vmid),
                                                machina::VirtioDevice::Transport::PCI,
                                                guest_.device_async());
  if (!rpmb_) {
    return ZX_ERR_NO_MEMORY;
  }
  FXL_LOG(INFO) << "pci rpmb device connect bus begin";
  zx_status_t status = bus_.Connect(rpmb_->pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "pci rpmb device connect bus failed " << status;
    return status;
  }
  FXL_LOG(INFO) << "pci rpmb device connect bus done";

  return ZX_OK;
}

zx_status_t Vmm::PatchMdDtb(zx_handle_t vmo_handle) {
  size_t dtb_size;
  zx_vaddr_t dtb_mem = 0;
  zx_vmo_get_size(vmo_handle, &dtb_size);
  zx_status_t status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_FLAG_PERM_READ,
                0, vmo_handle, 0, dtb_size, &dtb_mem);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to zx_vmar_map -> status : " << status;
  }
  void* src_dtb = (void*)dtb_mem;
  void* dtb = open_dtb(guest_, cfg_);
  if (dtb == nullptr) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  std::vector<std::string> append_nodes = {"mediatek,mddriver",
                                           "mediatek,md_attr_node"};
  int ret = update_nodes(src_dtb, dtb, append_nodes);
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to append node to dtb";
  }
  fdt_pack(dtb);
  flush_dtb(guest_, cfg_);
  zx_vmar_unmap(zx_vmar_root_self(), dtb_mem, MAX_DTB_SIZE);
  zx_handle_close(vmo_handle);
  return ret;
}

zx_status_t Vmm::PatchDeviceTree(uint64_t dtb_base, uint64_t size) {
  zx_status_t status;

  cfg_.set_dtb(dtb_base, size);
  guest_.CreateDtbSpec(cfg_.dtb());
  status = unmap_mpu_mem_region(guest_, cfg_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to unmap mpu memory " << status;
    return status;
  }

#ifdef VHE_MMIO_TRAP_DEBUG
  auto monitor_virtio = machina::Monitor_virtio::GetInstance();
  if (monitor_virtio == nullptr) {
    FXL_LOG(ERROR) << "Cannot get monitor_virtio instance";
    return ZX_ERR_INTERNAL;
  }

  status = monitor_virtio->MonitorVirtioMemFromDtb(guest_, cfg_.phys_base());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Monitor virtio: buffer not found! " << status;
    return status;
  }

  status = monitor_virtio->MapMonitorVirtioMem(guest_, true);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Monitor virtio: Map trace buffer failed! " << status;
    return status;
  }
  ipc_msg_->SetMonitorVirtioMem(monitor_virtio->pa(), monitor_virtio->size());
#endif

  auto trace = machina::Utrace::GetInstance();
  zx_status_t trace_status;
  if (trace) {
    trace_status = trace->TraceMemFromDtb(guest_, cfg_.phys_base(),
                                          cfg_.nbl_trace_mem_enable());
    if (ZX_OK == trace_status) {
      status = trace->MapTraceBuf(guest_, true);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Trace: Map trace buffer failed! " << status;
        return status;
      }

      status = trace->SetTraceBufToKernel();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Trace: Failed to set trace to kernel! " << status;
        return status;
      }

      status = trace->SetTraceBufToCpuFreq();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Trace: Failed to set trace to cpufreq! " << status;
        return status;
      }

      guest_.SetTraceMem(trace->pa(), trace->size());
    } else {
      FXL_LOG(INFO) << "May not support nebula_trace!" << status;
      trace_status = ZX_ERR_NO_RESOURCES;
    }
  } else {
    FXL_LOG(INFO) << "Do not support nebula_trace!" << status;
    trace_status = ZX_ERR_NO_RESOURCES;
  }

  if (ZX_OK == trace_status) {
    auto args = new top_task_args_t{(machina::Utrace*)&trace};
    thrd_t thread;
    int ret = thrd_create_with_name(&thread, nbl_loading_show_task_start, args,
                                    "nbl_loading_show");
    if (ret != thrd_success)
      FXL_LOG(ERROR) << "Failed to create nbl_loading_show thread " << ret;

    ret = thrd_detach(thread);
    if (ret != thrd_success)
      FXL_LOG(ERROR) << "Failed to detach balloon thread " << ret;
  }
  if (trace_status == ZX_OK)
    ipc_msg_->set_trace_mem(trace->pa(), trace->size());

  status = guest_.SchedMemFromDtb(cfg_.phys_base());
  if (status == ZX_OK) {
    status = guest_.InitDump();
    if (status == ZX_OK) {
      guest_.InitSched(cfg_.phys_base(), true);
      guest_.RegSchedIRQ();
    }
  }

  status = create_smc_irq_node(guest_, cfg_);
  FXL_CHECK(status == ZX_OK) << "Failed to create smc_irq node" << status;
  if (status != ZX_OK) {
    return status;
  }

  if (cfg_.vhm_irq()) {
    status = create_acrn_vhm_node(guest_, cfg_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create acrn vhm node " << status;
      return status;
    }

    status = create_vmlog_nodes(guest_, cfg_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create vmlog nodes: " << status;
      return status;
    }
  }

  if (cfg_.tipc_vq_notifier_irq() != 0) {
    std::string tipc_vqueue_node = "/nebula/virtio";
    status = create_notify_irq_node(guest_, cfg_, tipc_vqueue_node,
                                    cfg_.tipc_vq_notifier_irq());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create tipc vqueue notify irq node"
                     << status;
      return status;
    }
  }

  status = machina::PciBus::config_pci_global_irqs(cfg_.pci_global_irqs());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to config pci global irqs: " << status;
    return status;
  }

  int32_t* pci_irqs = machina::PciBus::get_pci_global_irqs();
  status = create_virtio_pci_nodes(guest_, cfg_, pci_irqs);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create virtio pci nodes: " << status;
    return status;
  }

  status = add_pmu_irq_node(guest_, cfg_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add pmu irq node: " << status;
    return status;
  }

  status = add_pmu76_irq_node(guest_, cfg_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add pmu76 irq node: " << status;
    return status;
  }

  status = probe_vtee(guest_, cfg_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to probe vtee" << status;
    return status;
  }

  uint64_t its_paddr = get_its_paddr(guest_, cfg_);
  FXL_LOG(INFO) << "its_paddr: " << std::hex << its_paddr;
  status = gic_its_.Init(its_paddr);
  if (status == ZX_OK) {
    interrupt_controller_.EnableIts(&gic_its_);
  }

  rproc_client_->PatchDeviceTree(cfg_.dtb(), cfg_.phys_base());
  auto mbox_spec = cfg_.ipc_mbox();
  if (mbox_spec.size()) {
    for (size_t i = 0; i < cfg_.ipc_mbox().size(); ++i) {
      status = ipc_mbox_svc_->getIpchandlerMap()[i]->CreateMboxDeviceTreeNodes(
          cfg_.dtb(), cfg_.phys_base(), i);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to create ipc mbox device tree node " << i
                       << ": " << status;
        return status;
      }
      ipc_mbox_svc_->getIpchandlerMap()[i]->set_sched_mem(
          guest_.sched_phys_base(), guest_.sched_size());
    }
  }
  vmlog_sink_->CreateVmlogSinkNodes(guest_, cfg_);

  flush_dtb(guest_, cfg_);
  return ZX_OK;
}

zx_status_t Vmm::Initialize(virtualization::Config vmm_cfg) {
  FXL_LOG(INFO) << "Vmm::Initialize";
  zx_status_t status = read_product_cfg("/system/data/product.lua", &cfg_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read product config " << status;
    return status;
  }

  const std::string& pfile = get_product_path(guest_, vmm_cfg, cfg_);
  status = read_guest_cfg(pfile.c_str(), &cfg_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read sos config " << status;
    return status;
  }

  fxl::UniqueFD alloc_fd(open(kGuestMemoryAllocator, O_RDWR));
  if (!alloc_fd.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open guest memory allocator";
    return ZX_ERR_INTERNAL;
  }

  mem_resp_t resp;
  auto memory = vmm_cfg.memory;
  auto memory_path = vmm_cfg.memory_path;
  int ret;
  if (!memory_path->empty()) {
    mem_req_t req = {};
    strncpy(req.path, memory_path->c_str(), memory_path->size());
    ret = ioctl_get_guest_memory_with_path(alloc_fd.get(), &req,
                                           &resp);
  } else if (!memory->empty()) {
    mem_req_t req = {};
    strncpy(req.compatible, memory->c_str(), memory->size());
    ret = ioctl_get_guest_memory(alloc_fd.get(), &req, &resp);
  } else {
    ret = ioctl_get_system_memory(alloc_fd.get(), &resp);
  }
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to get guest memory from allocator";
    return ZX_ERR_INTERNAL;
  }

  machina::VmemSpec system_mem = {
      .name = "ram",
      .gpa_base = resp.addr,
      .hpa_base = resp.addr,
      .size = resp.size,
      .policy = machina::MemoryPolicy::kCached,
      .is_physmem = 1,
      .is_reservedmem = 0,
  };
  cfg_.append_vmem(system_mem);

  for (auto& vmem : cfg_.vmem_auto()) {
    mem_req_t req = {};
    mem_resp_t resp;
    strncpy(req.search_string, vmem.name.c_str(), sizeof(req.search_string));
    ret = ioctl_get_reserved_memory(alloc_fd.get(), &req, &resp);
    if (ret < 0) {
      FXL_LOG(WARNING) << "Failed to get reserved memory " << vmem.name << ", ignored";
      continue;
    }

    auto resv = vmem;
    resv.gpa_base = resp.addr;
    resv.hpa_base = resp.addr;
    resv.size = resp.size;
    cfg_.append_vmem(resv);
  }

  guest_.SetVmid((int32_t)vmm_cfg.vmid);
  status = guest_.Init(cfg_.vmem());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to init guest memory " << status;
    return status;
  }

  FXL_LOG(INFO) << "Vmm::Initialize for vmid: " << (int32_t)vmm_cfg.vmid;

  guest_.SetUserVmid(vmm_cfg.vmid);
  status = guest_.TeeVmCreate(vmm_cfg.vmid);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Tee vm create failed " << status;
    return status;
  }

  auto bootloader_path = std::string(kDefaultPath) + vmm_cfg.bootloader.get();
  bootloader_ = Bootloader::BuildFromProto(bootloader_path, guest_);
  if (bootloader_ == nullptr) {
    FXL_LOG(ERROR) << "Failed to build bootloader from " << bootloader_path;
    return ZX_ERR_INTERNAL;
  }

  status = bootloader_->Setup(cfg_, &guest_ip_, extra_params_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to setup bootloader " << status;
    return status;
  }

  add_vtee_irq_to_vgic(cfg_);

  guest_.SetSchedIRQ(cfg_.sched_irq());
  guest_.SetDumpIRQ(cfg_.dump_irq());
  guest_.SetMonitorIRQ(cfg_.monitor_irq());
  guest_.SetSchedId(cfg_.sched_id());
  std::vector<uint32_t>& bind_pcpus = cfg_.bind_pcpus();
  int bind_cpu_size = bind_pcpus.size();
  if (bind_cpu_size != 0) {
    FXL_CHECK(bind_cpu_size == cfg_.num_cpus())
        << "cpu num mismtach:" << bind_cpu_size << "," << cfg_.num_cpus();

    status = guest_.SetCpuAffinity(bind_pcpus.data(), bind_cpu_size);
    FXL_CHECK(status == ZX_OK) << "failed to set cpu affinity:" << status;
  }

  guest_.SetCpuNums(cfg_.num_cpus());

  std::vector<uint32_t>& wakeup_irqs = cfg_.wakeup_irqs();
  int wakeup_irq_num = wakeup_irqs.size();
  if (wakeup_irq_num != 0) {
    guest_.SetWakeupIrqs(wakeup_irqs.data(), wakeup_irq_num);
  }

  guest_.SetSmcIRQ(cfg_.smc_irq());

  // Setup interrupt controller.
  interrupt_controller_.SetSPIVcpuMask(0xFF);
  status = interrupt_controller_.Init(cfg_.num_cpus(), cfg_.gic_version(),
                                      cfg_.vgic());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create interrupt controller";
    return status;
  }

  if (!is_qemu_platform()) {
    status = machina::PciBus::config_pci_global_irqs(cfg_.pci_global_irqs());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to config pci global irqs: " << status;
      return status;
    }
  }

  // Setup PCI.
  status = bus_.Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create PCI bus";
    return status;
  }

  // Setup cluster device.
  component::ConnectToEnvironmentService(spi_client_.NewRequest());
  status = spi_client_.Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to init spi client " << status;
  }
  status = cluster_.Init(guest_.device_async(), &spi_client_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create cluster device";
    return status;
  }
  status = bus_.Connect(cluster_.pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "devices connect bus failed " << status;
    return status;
  }

  //Setup eint BE
  FXL_LOG(INFO) << "eint be create.";
  eint_.SetVmid(guest_.vmid());
  status = bus_.Connect(eint_.pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "eint devices connect bus failed " << status;
    return status;
  }

  //Setup rtc BE
  FXL_LOG(INFO) << "rtc be create.";
  if (!is_qemu_platform()) {
    status = rtc_.Init(guest_.vmid());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to init RTC with vmid: " << status;
      return status;
    }
    status = bus_.Connect(rtc_.pci_device());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "rtc device connect bus failed " << status;
      return status;
    }
  }
  FXL_LOG(INFO) << "rtc device connect pci bus successed. device id: " << rtc_.device_id();

  // Setup virtio-console
  FXL_LOG(INFO) << "virtio-console BE ";

  zx::socket server;
  status = zx::socket::create(0, &server, &console_socket_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket pair for virtio-console: "
                   << status;
    return status;
  }
  console_ = std::make_unique<machina::VirtioConsole>(
      guest_.phys_mem(), guest_.device_async(), std::move(server));
  if (!console_) {
    FXL_LOG(ERROR) << "Failed to create virtio-console instance: " << status;
    return ZX_ERR_NO_MEMORY;
  }

  status = console_->Start();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start console: " << status;
    return status;
  }
  status = bus_.Connect(console_->pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to connect virtio-console devices to pci bus: "
                   << status;
    return status;
  }

  if (!is_qemu_platform()) {
    status = InitializeVirtioBlock();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to initialize virtio block device: " << status;
      return status;
    }

    status = InitializeVirtioRpmb();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to initialize virtio rpmb device: " << status;
      return status;
    }
  }

  // Setup virtio-vsock
  FXL_LOG(INFO) << "virtio-vsock BE";
  vsock_ = std::make_unique<machina::VirtioVsock>(guest_.phys_mem(),
                                                  guest_.device_async());
  if (!vsock_) {
    FXL_LOG(ERROR) << "Failed to create virtio-vsock instance: " << status;
    return ZX_ERR_NO_MEMORY;
  }

  status = bus_.Connect(vsock_->pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to connect virtio-vsock devices to pci bus: "
                   << status;
    return status;
  }

  auto new_connector = host_vsock_endpoint_.ConnectorNewBinding();
  vsock_->Start(std::move(new_connector));

  gzfs_vsock_srv_ = std::make_unique<GzFsVsockService>(guest_.vmid());
  if (!gzfs_vsock_srv_) {
    FXL_LOG(ERROR) << "Failed to create gzfs vsock service instance: "
                   << status;
    return ZX_ERR_NO_MEMORY;
  }

  auto request = gzfs_vsock_srv_->VsockEndpointNewRequest();
  host_vsock_endpoint_.AddBinding(std::move(request));
  gzfs_vsock_srv_->Listen();

  log_store_ = std::make_unique<VmlogStore>();
  if (log_store_ == nullptr) {
    FXL_LOG(ERROR) << "Failed to create vm log store";
    return ZX_ERR_NO_MEMORY;
  }

  status = log_store_->Initialize(guest_.vmid());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initial vmlog store: " << status;
    return status;
  }

  // Setup virtual UARTs.
  FXL_LOG(INFO) << "create SOS uart device";
  status = uart_.Init(&guest_, machina::kPl011PhysBase, log_store_.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create UART at " << std::hex
                   << machina::kPl011PhysBase;
    return status;
  }

  //Setup spi BE
  status = bus_.Connect(spi_.pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "devices connect bus failed " << status;
    return status;
  }

  //Setup i2c BE
  status = bus_.Connect(i2c_.pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "devices connect bus failed " << status;
    return status;
  }

  // Setup vmlog sinking
  FXL_LOG(INFO) << "create VmlogSrv BE";
  vmlog_sink_ = std::make_unique<VmlogSrv>(alloc_fd,
                                           guest_.vmid(), log_store_.get());
  if (!vmlog_sink_) {
    FXL_LOG(ERROR) << "Failed to create vmlog_sink_";
    return ZX_ERR_NO_MEMORY;
  }
  vmlog_sink_->Initialize();

  FXL_LOG(INFO) << "create multi SOS vSmmu device";
  auto smmu_specs = cfg_.vsmmus();
  if (smmu_specs.empty()) {
    FXL_LOG(INFO) << "Failed to initialize SOS vSmmu devices: no valid "
                     "configurations provided.";
  }

  for (const auto& spec : smmu_specs) {
    auto smmu_type = machina::Smmuv3::GetSmmuType(spec.paddr);
    auto vsmmu = std::make_unique<machina::Smmuv3>(
        &guest_, static_cast<uint8_t>(smmu_type));

    status = vsmmu->Init(spec);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create SOS vSmmu device";
      return status;
    }
    vsmmus_.push_back(std::move(vsmmu));
  }

  if (vsmmus_.empty())
    FXL_LOG(ERROR) << "All SOS vSmmu device initializations failed.";
  else
    FXL_LOG(INFO) << "Successfully created multi SOS vSmmu devices.";

  if (cfg_.vhm_irq()) {
    config_irq_monitor(cfg_.irq_monitor());
    CreateNestedEnvironment();

    vhm_svc_ = std::make_unique<machina::VhmServiceImpl>(
        &service_provider_bridge_, env_launcher_, &interrupt_controller_,
        cfg_.vhm_irq(), guest_.phys_mem(), cfg_.phys_base(), &guest_);
    if (vhm_svc_ == nullptr) {
      FXL_LOG(ERROR) << "Failed to create vhm service";
      return ZX_ERR_NO_MEMORY;
    }
    guest_.RegisterVhmRequestHandler(vhm_svc_.get());

    // Config each vcpu status(RT, spinlock, IRQ) priority.
    uint32_t vcpu_status = 0;
    auto sched_priority = cfg_.sched_priority();
    for (auto priority : sched_priority) {
      struct sched_param_req req;
      req.type = SCHED_PARAM_TYPE_PRIORITY;
      req.priority_info.vcpu_status = vcpu_status++;
      req.priority_info.priority = priority;
      _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM, (void*)&req);
    }

    // Config each vcpu status(RT, spinlock, IRQ) time slice.
    vcpu_status = 0;
    auto sched_timeslice = cfg_.sched_timeslice();
    for (auto timeslice : sched_timeslice) {
      struct sched_param_req req;
      req.type = SCHED_PARAM_TYPE_TIMESLICE;
      req.timeslice_info.vcpu_status = vcpu_status++;
      req.timeslice_info.timeslice = timeslice;
      _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM, (void*)&req);
    }

    // Config each vcpu group scheduler period.
    uint32_t group = 0;
    auto periods = cfg_.periods();
    for (auto period : periods) {
      struct sched_param_req req;
      req.type = SCHED_PARAM_TYPE_PERIOD;
      req.period_info.group = group++;
      req.period_info.period = period;
      _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM, (void*)&req);
    }
  }

  auto mbox_spec = cfg_.ipc_mbox();
  if (mbox_spec.size()) {
    ipc_mbox_svc_ = std::make_unique<machina::IpcServiceImpl>(
        &service_provider_bridge_, &guest_, cfg_.ipc_mbox());
    guest_.RegisterIpcRequestHandler(ipc_mbox_svc_.get());
  }

  ipc_msg_ =
      std::make_unique<machina::IpcMessage>(&service_provider_bridge_, cfg_);
  if (ipc_msg_ == nullptr) {
    FXL_LOG(ERROR) << "Failed to create ipc message";
    return ZX_ERR_NO_MEMORY;
  }

  machina::RprocServiceSyncPtr rproc_svc;
  component::ConnectToEnvironmentService(rproc_svc.NewRequest());
  rproc_client_ = std::make_unique<RprocClient>(
      vmm_cfg.vmid, std::move(rproc_svc), guest_, interrupt_controller_);
  if (rproc_client_ == nullptr) {
    FXL_LOG(ERROR) << "Failed to create rproc client";
    return ZX_ERR_NO_MEMORY;
  }

  if (cfg_.tipc_vq_notifier_irq()) {
    // for yocto vmlog sink driver
    vqueue_notifier_ =
        std::make_unique<TipcVqueueNotifier>(cfg_.tipc_vq_notifier_irq(), guest_.vmid());
    if (vqueue_notifier_ == nullptr) {
      FXL_LOG(ERROR) << "Failed to create vqueue notifier,vmid: "<< guest_.vmid();
      return ZX_ERR_NO_MEMORY;
    }
  } else if (guest_.vmid() == 1) {
    // for tbox vmlog sink driver
    vqueue_notifier_ =
        std::make_unique<TipcVqueueNotifier>(UINT32_MAX, guest_.vmid());
    if (vqueue_notifier_ == nullptr) {
      FXL_LOG(ERROR) << "Failed to create vqueue notifier,vmid: "<< guest_.vmid();
      return ZX_ERR_NO_MEMORY;
    }
  }
  vmlog_sink_->Start(vqueue_notifier_.get());

  if (cfg_.is_dtb_valid()) {
    auto spec = cfg_.dtb();
    status = PatchDeviceTree(spec.base, spec.size);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to patch device-tree: " << status;
      return status;
    }
  } else {
    guest_.RegisterDeviceTreePatcher([this, fd = std::move(alloc_fd)](uint64_t base, uint64_t size) {
    zx_handle_t vmo_handle = ZX_HANDLE_INVALID;
    auto ret = ioctl_get_guest_dtb_memory(fd.get(), NULL, &vmo_handle);
    if (ret < 0) {
      FXL_LOG(ERROR) << "Failed to get sos dtb base and size";
    }
    auto status = PatchDeviceTree(base, size);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Failed to patch device-tree: " << status;
    }
    machina::DeviceTreeSpec dtb{base, size};
    status = guest_.AddVmemDeviceTreeNodes(dtb, cfg_.phys_base());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to add vmem device tree nodes: " << status;
    }
    status = PatchMdDtb(vmo_handle);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to PatchMdDtb " << status;
    }
    });
  }

  return ZX_OK;
}

zx_status_t Vmm::StartPrimaryVcpu(
    std::function<void(zx_status_t)> stop_callback) {
  auto initialize_vcpu = [this](machina::Guest* guest, uintptr_t guest_ip,
                                uint64_t id, machina::Vcpu* vcpu) {
    zx_status_t status =
        vcpu->Create(guest, guest_ip, id, kSosVcpuPriority, kSosVcpuTimeSlice);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create VCPU";
      return status;
    }

    if (cfg_.tipc_vq_notifier_irq())
      vqueue_notifier_->BindVcpu(id, vcpu);

    // Register VCPU with ID 0.
    status = interrupt_controller_.RegisterVcpu(id, vcpu);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to register VCPU with interrupt controller";
      return status;
    }

    // percpu interrupts should be bound in vcpu thread
    vcpu->RegisterStartCallback([vcpu, id, this] {
      auto& redistributors = interrupt_controller_.redistributors();
      zx_status_t status = redistributors[id]->Init(cfg_.vgic().percpu_irqs);
      FXL_CHECK(status == ZX_OK);
      redistributors[id]->BindVcpu(vcpu);
    });

    // Setup initial VCPU state.
    zx_vcpu_state_t vcpu_state = {};
    int idx = 0;
    for (auto value : extra_params_) {
      vcpu_state.x[idx++] = value;
    }

    // Config VCPU group.
    std::vector<uint32_t>& bind_pcpus = cfg_.bind_pcpus();
    if (id < bind_pcpus.size()) {
      vcpu->SetGroup(bind_pcpus[id]);
    } else {
      vcpu->SetGroup(id);
    }

    // Config VCPU budget
    auto budgets = cfg_.budgets();
    if (id < budgets.size()) {
      vcpu->SetBudget(budgets[id]);
    }

    // Begin VCPU execution.
    return vcpu->Start(&vcpu_state);
  };

  guest_.set_stop_callback(std::move(stop_callback));
  guest_.RegisterVcpuFactory(initialize_vcpu);
  auto status = guest_.StartVcpu(guest_ip_, 0 /* id */);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start VCPU-0 " << status;
    return status;
  }
#if 0
  FXL_LOG(INFO) << "Disable output logs to UART";
  if (_trusty_ioctl(NEBULA_GENERIC_FD,
                    SYS_PLATFORM_UART_ACTION_PRINT_LOG_OFF_CMD, nullptr) < 0) {
    FXL_LOG(WARNING) << "platform do not support disable uart action, skip";
  }
#endif
  guest_.create_wdt_fd();
  FXL_LOG(INFO) << "vmid " << guest_.vmid() << " watchdog created";
  wdt_.ReigsterDumpStateCallback([this](uint32_t reason) {
    std::vector<std::string> vcpu_states;
    guest_.Dump(vcpu_states);
    FXL_LOG(INFO) << "Dumping VCPU states:";
    nbl_vmm::Response resp;
    auto guest_state = resp.mutable_guest_state();
    for (auto& state : vcpu_states) {
      auto vcpu_state = guest_state->add_vcpus();
      auto success =
          google::protobuf::TextFormat::ParseFromString(state, vcpu_state);
      FXL_CHECK(success);
    }
    if (guest_.get_wdt_fd() > 0) {
      FXL_LOG(INFO) << "set wdt rst status before dump state. ";
      ioctl_grt_wdt_set_rst_status(guest_.get_wdt_fd());
    }
    bootloader_->DumpStateHandler(resp.guest_state(), reason);
  });
  wdt_.ReigsterStopCallback([this]() { guest_.Stop(ZX_ERR_CANCELED); });
  wdt_.init(guest_.vmid());
  guest_.regerister_watchdog(&wdt_);
  return 0;
}

void Vmm::GetConsole(GetConsoleCallback callback) {
  callback(duplicate(console_socket_, ZX_RIGHT_SAME_RIGHTS));
}

void Vmm::GetHostVsockEndpoint(
    fidl::InterfaceRequest<virtualization::HostVsockEndpoint> request) {
  host_vsock_endpoint_.AddBinding(std::move(request));
}

void Vmm::SetLogLevel(int32_t log_level) {
  printf("Set log level to %d, vmid=%d\n", log_level, guest_.vmid());
  fxl::LogSettings log_settings = fxl::GetLogSettings();
  log_settings.min_log_level = static_cast<fxl::LogSeverity>(log_level);
  fxl::SetLogSettings(log_settings);
}

void Vmm::CreateNestedEnvironment() {
  application_context_->environment()->CreateNestedEnvironment(
      service_provider_bridge_.OpenAsDirectory(), env_.NewRequest(),
      env_controller_.NewRequest(), "nested_vmm");
  env_->GetApplicationLauncher(env_launcher_.NewRequest());

  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return application_context_->environment()->GetDirectory(std::move(h1));
  service_provider_bridge_.set_backing_dir(std::move(h2));

  service_provider_bridge_.AddService<component::ApplicationLoader>(
      [](fidl::InterfaceRequest<component::ApplicationLoader> request) {
        component::ConnectToEnvironmentService(std::move(request));
      });

  service_provider_bridge_.AddService<machina::RprocService>(
      [](fidl::InterfaceRequest<machina::RprocService> request) {
        component::ConnectToEnvironmentService(std::move(request));
      });
}
