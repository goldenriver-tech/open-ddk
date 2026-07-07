// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_GUEST_CONFIG_H_
#define GARNET_BIN_GUEST_GUEST_CONFIG_H_

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>

#include "garnet/lib/machina/guest_config.h"
#include "third_party/rapidjson/rapidjson/document.h"

class GuestConfig {
 public:
  machina::Kernel kernel() const { return kernel_; }
  const std::string& kernel_path() const { return kernel_path_; }
  const std::string& ramdisk_path() const { return ramdisk_path_; }
  const std::vector<machina::BlockSpec>& block_devices() const {
    return block_specs_;
  }
  const std::string& cmdline() const { return cmdline_; }
  uint8_t num_cpus() const { return num_cpus_; }
  size_t memory() const { return memory_; }
  zx_duration_t balloon_interval() const {
    return ZX_SEC(balloon_interval_seconds_);
  }
  uint32_t balloon_pages_threshold() const { return balloon_pages_threshold_; }
  bool balloon_demand_page() const { return balloon_demand_page_; }
  machina::GuestDisplay display() const { return display_; }
  bool block_wait() const { return block_wait_; }
  machina::Gic gic_version() const { return gic_version_; }
  const machina::VcpuSpec& vcpu() const { return vcpu_spec_; }
  machina::VgicSpec& vgic() { return vgic_spec_; }
  const std::vector<machina::VsmmuSpec>& vsmmus() const { return vsmmu_specs_; }
  std::vector<machina::VsmmuSpec>& vsmmus() { return vsmmu_specs_; }
  const std::vector<machina::VmemSpec>& vmem() { return vmem_spec_; }
  const std::vector<machina::VmemSpec>& vmem_auto() { return vmem_auto_spec_; }
  void set_dtb(uint64_t base, uint64_t size) {
    dtb_spec_.base = base;
    dtb_spec_.size = size;
  }
  void append_vmem(machina::VmemSpec& vmem) {
    vmem_spec_.push_back(std::move(vmem));
  }
  bool is_dtb_valid() const { return dtb_spec_.size != 0; }
  const machina::DeviceTreeSpec dtb() const { return dtb_spec_; }
  const machina::RprocSpec& rproc_spec() const { return rproc_spec_; }
  bool has_rproc_spec() const { return rproc_spec_.ctrl_irq != 0; }
  uint32_t sched_irq() const { return sched_irq_; }
  uint32_t dump_irq() const { return dump_irq_; }
  const std::vector<machina::IpcMboxSpec>& ipc_mbox() const {
    return mbox_spec_;
  }
  uint32_t monitor_irq() const { return monitor_irq_; }
  uint32_t smc_irq() const { return smc_irq_; }
  std::vector<uint32_t>& wakeup_irqs() { return wakeup_irqs_; }
  machina::IrqMonitorSpec& irq_monitor() { return irq_monitor_spec_; }

  uint64_t phys_base() const;
  uint64_t phys_size() const;
  uint32_t tipc_vq_notifier_irq() const { return tipc_vq_notifier_irq_; }
  uint32_t vhm_irq() const { return vhm_irq_; }
  uint32_t gpu_irq() const { return gpu_irq_; }
  const std::vector<uint16_t>& vsock_irqs() const { return vsock_irqs_; }
  const std::vector<uint16_t>& apu_irqs() const { return apu_irqs_; }
  const std::vector<uint16_t>& cmdq_irqs() const { return cmdq_irqs_; }
  uint32_t guest_reserved_memory() const { return guest_reserved_memory_; }
  std::vector<uint32_t>& bind_pcpus() { return bind_pcpus_; }
  std::vector<uint64_t>& budgets() { return budgets_; }
  std::vector<uint64_t>& periods() { return periods_; }
  std::vector<uint64_t>& sched_priority() { return sched_priority_; }
  std::vector<uint64_t>& sched_timeslice() { return sched_timeslice_; }
  const std::vector<machina::ProductSpec>& product_info() const {
    return product_spec_;
  }
  uint32_t vtee_notifier_irq() const { return vtee_notifier_irq_; }
  uint8_t sched_id() const { return sched_id_; }
  uint8_t nbl_trace_mem_enable() const { return nbl_trace_mem_enable_; }
  std::vector<uint32_t>& pci_global_irqs() { return pci_global_irqs_; }

  /*******************************************************************/
  void setCpus(uint8_t num)
  {
    num_cpus_ = num;
  }
  void setVcpuEntry(uint64_t entry)
  {
    vcpu_spec_.entry = entry;
  }
  void setBindcpus(uint32_t num)
  {
    bind_pcpus_.push_back(num);
  }
  void setPeriods(uint64_t num)
  {
    periods_.push_back(num);
  }
  void setBudgets(uint64_t num)
  {
    budgets_.push_back(num);
  }
  void setSchedPriority(uint64_t num)
  {
    sched_priority_.push_back(num);
  }
  void setSchedTimeslice(uint64_t num)
  {
    sched_timeslice_.push_back(num);
  }
  void setVgicPaddr(uint64_t gicd, uint64_t gicr)
  {
      vgic_spec_.gicd_paddr = gicd;
      vgic_spec_.gicr_paddr = gicr;
  }
  void setVgicIrqs(uint16_t num)
  {
    vgic_spec_.irqs.push_back(num);
  }
  void setVgicPercpuIrqs(uint16_t num)
  {
    vgic_spec_.percpu_irqs.push_back(num);
  }
  void setIpcMbox(uint16_t rx_ready_irq, uint16_t tx_done_irq)
  {
    machina::IpcMboxSpec ipcmbox;
    ipcmbox.rx_ready_irq = rx_ready_irq;
    ipcmbox.tx_done_irq = tx_done_irq;
    mbox_spec_.push_back(std::move(ipcmbox));
  }
  void setSchedIrq(uint32_t id)
  {
    sched_irq_ = id;
  }
  void setSchedId(uint8_t id)
  {
    sched_id_ = id;
  }
  void setDumpIrq(uint32_t id)
  {
    dump_irq_ = id;
  }
  void setSmcIrq(uint32_t id)
  {
    smc_irq_ = id;
  }
  void setGpuIrq(uint32_t id)
  {
    gpu_irq_ = id;
  }
  void setVteeNotifierIrq(uint32_t id)
  {
    vtee_notifier_irq_ = id;
  }
  void setVsockIrqs(uint16_t num)
  {
    vsock_irqs_.push_back(num);
  }
  void setWakeupIrq(uint32_t num)
  {
    wakeup_irqs_.push_back(num);
  }
  void setApuIrqs(uint16_t num)
  {
    apu_irqs_.push_back(num);
  }
  void setTipcVqNotifierIrq(uint32_t id)
  {
    tipc_vq_notifier_irq_ = id;
  }
  void setVhmIrq(uint32_t id)
  {
    vhm_irq_ = id;
  }
  void setGuestReservedMemory(uint32_t id)
  {
    guest_reserved_memory_ = id;
  }
  void setCpufreq(std::string freq);
  void setVsmmu(uint64_t paddr, uint16_t irq, std::vector<uint16_t> sids)
  {
    machina::VsmmuSpec spec;
    spec.paddr = paddr;
    spec.irq = irq;
    spec.sids = sids;
    vsmmu_specs_.push_back(std::move(spec));
  }
  void setVmem(std::string const name,
               uint64_t const base_addr,
               uint64_t const size,
               uint8_t policy,
               uint8_t is_physmem) {
    machina::VmemSpec spec;

    spec.name = name;
    spec.hpa_base = base_addr;
    spec.gpa_base = spec.hpa_base;
    spec.size = size;
    spec.policy = (machina::MemoryPolicy)policy;
    spec.is_physmem = is_physmem;
    spec.is_reservedmem = false;

    vmem_spec_.push_back(spec);
  }
  void setVmemAuto(std::string const name,
                   std::string const node_name,
                   uint64_t const base_addr,
                   uint64_t const size,
                   uint8_t policy,
                   uint8_t is_physmem) {
    machina::VmemSpec spec;

    spec.name = name;
    spec.node_name = node_name;
    spec.hpa_base = base_addr;
    spec.gpa_base = spec.hpa_base;
    spec.size = size;
    spec.policy = (machina::MemoryPolicy)policy;
    spec.is_physmem = is_physmem;

    vmem_auto_spec_.push_back(spec);
  }
  void setDeviceTree(uint64_t base, uint64_t size)
  {
    dtb_spec_.base = base;
    dtb_spec_.size = size;
  }
  void setProductSpec(std::string name, std::string file)
  {
    machina::ProductSpec spec;
    spec.name = name;
    spec.file = file;
    product_spec_.push_back(std::move(spec));
  }
  void setCmdqIrqs(uint16_t num)
  {
    cmdq_irqs_.push_back(num);
  }
  void setMonitorSpec(uint8_t enable, uint32_t threshold, std::vector<uint16_t> irqs)
  {
    irq_monitor_spec_.enable = enable;
    irq_monitor_spec_.threshold = threshold;
    irq_monitor_spec_.irqs = irqs;
  }
  void setNbltraceMemEnable(uint8_t enable)
  {
    nbl_trace_mem_enable_ = enable;
  }
  void setCmdline(std::string cmdline)
  {
    cmdline_ = std::move(cmdline);
  }
  void setPciIrqs(uint32_t const hwirq)
  {
    pci_global_irqs_.push_back(hwirq);
  }
  /*******************************************************************/
 private:
  friend class GuestConfigParser;
  machina::Kernel kernel_ = machina::Kernel::ZIRCON;
  std::string kernel_path_;
  std::string ramdisk_path_;
  std::vector<machina::BlockSpec> block_specs_;
  std::string cmdline_;
  uint8_t num_cpus_ = zx_system_get_num_cpus();
  size_t memory_ = 1 << 28;
  uint32_t balloon_interval_seconds_ = 0;
  uint32_t balloon_pages_threshold_ = 0;
  bool balloon_demand_page_ = false;
  machina::GuestDisplay display_ = machina::GuestDisplay::SCENIC;
  bool block_wait_ = false;
  machina::Gic gic_version_ = machina::Gic::V3;
  machina::VcpuSpec vcpu_spec_;
  machina::VgicSpec vgic_spec_;
  machina::RprocSpec rproc_spec_ = {};
  std::vector<machina::VsmmuSpec> vsmmu_specs_;
  machina::IrqMonitorSpec irq_monitor_spec_;
  machina::DeviceTreeSpec dtb_spec_{0};
  uint32_t tipc_vq_notifier_irq_;
  uint32_t vhm_irq_;
  uint32_t gpu_irq_{0};
  std::vector<uint16_t> vsock_irqs_;
  std::vector<uint16_t> apu_irqs_;
  std::vector<uint16_t> cmdq_irqs_;
  std::vector<machina::VmemSpec> vmem_spec_;
  std::vector<machina::VmemSpec> vmem_auto_spec_;
  uint32_t guest_reserved_memory_ = 0;
  uint32_t sched_irq_;
  uint8_t sched_id_ = 0;
  uint32_t dump_irq_;
  uint32_t monitor_irq_;
  uint32_t smc_irq_;
  std::vector<uint32_t> wakeup_irqs_;
  uint8_t nbl_trace_mem_enable_ = 0;
  std::vector<machina::IpcMboxSpec> mbox_spec_;
  std::string cpufreq_;
  std::vector<uint32_t> bind_pcpus_;
  std::vector<machina::ProductSpec> product_spec_;
  std::vector<uint64_t> periods_;
  std::vector<uint64_t> budgets_;
  std::vector<uint64_t> sched_priority_;
  std::vector<uint64_t> sched_timeslice_;
  uint32_t vtee_notifier_irq_;
  std::vector<uint32_t> pci_global_irqs_;
};

class GuestConfigParser {
 public:
  using OptionHandler = std::function<zx_status_t(const std::string& name,
                                                  const std::string& value)>;
  using OptionMap = std::unordered_map<std::string, OptionHandler>;

  GuestConfigParser(GuestConfig* config);
  ~GuestConfigParser();

  zx_status_t ParseArgcArgv(int argc, char** argv);
  zx_status_t ParseConfig(const std::string& data);
  zx_status_t ParseProductConfig(const std::string& data);
  zx_status_t ParseConfigFromLua(const std::string& cfg_path, GuestConfig* cfg);
  zx_status_t ParseProductConfigFromLua(const std::string& cfg_path, GuestConfig* cfg);
 private:
  OptionMap GetOptionHandlers(const std::string& name);
  OptionMap GetRootOptionHandlers();
  OptionMap GetVcpuOptionHandlers();
  OptionMap GetVgicOptionHandlers();
  OptionMap GetVsmmuOptionHandlers(machina::VsmmuSpec& spec);
  OptionMap GetVmemOptionHandlers();
  OptionMap GetRprocOptionHandlers();
  zx_status_t ParseVmemsConfig(const rapidjson::Value& vmem_array);
  OptionMap GetIrqMonitorOptionHandlers();

  GuestConfig* cfg_;
  int current_vsmmu_spec_index_;
  std::string current_vsmmu_name_;

  const std::string kRootOptionName = "/";
  const std::string kVcpuOptionName = "vcpu";
  const std::string kVgicOptionName = "vgic";
  const std::string kRprocOptionName = "rproc";
  const std::string kVsmmuptionName = "vsmmu_";
  const std::string kIrqMonitorOptionName = "irq_monitor";
};

#endif  // GARNET_BIN_GUEST_GUEST_CONFIG_H_
