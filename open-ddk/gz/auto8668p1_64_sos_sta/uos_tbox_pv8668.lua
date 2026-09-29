

print("we are in tbox config lua")

-- product.lua sets SOS DTB at 0x47c80000; TBOX RAM is [0x200000000, 0x240000000).
sos_config:setDeviceTree(0, 0)
-- Drop inherited SOS MMIO fragments so TBOX can use original devicesL/H windows.
sos_config:clearNonPhysVmem()

--  vmid
--sos_config:setVmid(1)
--**************************************************************

--  sched vmid
sos_config:setSchedId(3)

-- pv block architecture validation path
sos_config:setPvblkEnabled(1)

--  cpus
sos_config:setCpus(8)
--**************************************************************

--  physical-cpus
Pcpus = {1, 0, 2, 3, 4, 5, 6, 7}
for i = 1, #Pcpus do
    sos_config:setBindcpus(Pcpus[i])
end

--  vCPU budgets in microsecond
budgets = {2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000}
for i = 1, #budgets do
    sos_config:setBudgets(budgets[i])
end
--**************************************************************

-- vCPU priority
vcpu_priority = {0, 0, 0, 0, 0, 0, 0, 0}
for i = 1, #vcpu_priority do
    sos_config:setVcpuPriority(vcpu_priority[i])
end
--**************************************************************

--  memory
-- getGuestMemSize(int vmid)
--local mem = getGuestMemSize(1)  --unit MB

--if (mem > 0) then
--  sos_config:setMemsize(mem)
--end
--sos_config:setMemsize(4096) --unit MB
--**************************************************************

--  vsock-cid
--sos_config:setVsockCid(4)
--**************************************************************

-- filesystem
-- void setFs(std::string type, std::string path)
--sos_config:setFs("tbox_nvdata", "/tmp/virtiofs_tbox_socket0")
--sos_config:setFs("tbox_media", "/tmp/virtiofs_tbox_socket1")
--**************************************************************

-- tipc
--[[
--void setTipc(std::string path)
--]]
--sos_config:setTipc("/tmp/virtio_tipc")
--**************************************************************

--  vmnet
--[[
--voiid setVmnet(std::string id, std::string ip, std::string netmask)
--void setVmnet(std::string id, std::string ip, std::string netmask, std::string fe_MAC)
--sos_config:setVmnet("sample_eth0_if", "172.16.16.1", "255.255.255.0", "02:22:ee:b9:55:66");
--]]
--sos_config:setVmnet("vmnet-ysyu0", "172.16.17.1", "255.255.255.0", "0a:78:ac:10:11:02");
--sos_config:setVmnet("vmnet-ysyu1", "172.16.33.1", "255.255.255.0", "0a:78:ac:10:21:02");
--sos_config:setVmnet("vmnet-ysyu2", "172.16.34.1", "255.255.255.0", "0a:78:ac:10:22:02");
--**************************************************************

--  vmem_auto // insert memory node & map PA to guest os
--[[
void setVmemAuto(std::string name, std::string node_name,
    uint64_t base_addr, uint64_t size,
    uint8_t policy, uint8_t is_phy_mem)
--]]
sos_config:setVmemAuto("consys_emi_reserved", "", 0x0, 0x00, 1, 0)
sos_config:setVmemAuto("shared-conninfra", "", 0x0, 0x0, 1, 1)
--sos_config:setVmemAuto("atf_log_reserved", "", 0, 0, 1, 1)
--sos_config:setVmemAuto("framebuffer", "", 0, 0, 1, 1)
--sos_config:setVmemAuto("79xx_wifi_vm", "", 0, 0, 1, 1)
--sos_config:setVmemAuto("pstore", "", 0, 0, 1, 1)
--sos_config:setVmemAuto("aee_debug_kinfo", "", 0, 0, 1, 1)
--**************************************************************

--  vmem_auto // insert memory node & map PA to guest os
--[[
void setVmemAuto(std::string name, std::string node_name,
    uint64_t base_addr, uint64_t size,
    uint8_t policy, uint8_t is_phy_mem)
--]]
sos_config:setVmemAuto("pstore", "", 0, 0, 1, 1)
sos_config:setVmemAuto("aee_debug_kinfo", "", 0, 0, 1, 1)
sos_config:setVmemAuto("atf-log-reserved", "", 0, 0, 1, 1)
sos_config:setVmemAuto("vmlog_sink_tbox", "", 0x0, 0x00, 1, 0)
-- sos_config:setVmemAuto("nbl_trace", "", 0, 0, 1, 1)
--**************************************************************

--  vmem // map io address for guest os
--[[
void setVmem(std::string name, uint64_t gpa_base, uint64_t hpa_base,
    uint64_t size,
    uint8_t policy, uint8_t is_phy_mem)
--]]
-- sos_config:setVmem("devices", 0x00000000, 0x00000000, 0x40000000, 1, 0)
sos_config:setVmem("devicesL", 0x00000000, 0x00000000, 0x11001000, 1, 0)
-- sos_config:setVmem("serial@0x11001000", 0x11001000, 0x11001000, 0x1000, 2, 0)
sos_config:setVmem("devicesH", 0x11002000, 0x11002000, 0x2EFFE000, 1, 0)

-- read modem address and size from dtb node
function read_dtb_node(path)
    local file = io.open(path, "rb")
    if not file then
        print("Cannot open file: " .. path)
        return nil
    end

    local content = file:read("*all")
    file:close()

    return content
end

function read_md_config(path)
    local content = read_dtb_node(path)
    if not content then
        print("Failed to read content from: " .. path)
        return nil
    end

    if #content ~= 8 then
        print(path .. " content length is not 8 bytes")
        return nil
    end

    local value = 0
    for i = 1, #content do
        value = value * 256 + string.byte(content, i)
    end
    return value
end

--local dtb_base = "/sys/firmware/devicetree/base/md_smem_addr_node"
--local md_c_mem_addr   = read_md_config(dtb_base .. "/md_c_mem_addr")
--local md_c_mem_size   = read_md_config(dtb_base .. "/md_c_mem_size")
--local md_nc_mem_addr  = read_md_config(dtb_base .. "/md_nc_mem_addr")
--local md_nc_mem_size  = read_md_config(dtb_base .. "/md_nc_mem_size")
--local md_tag_mem_addr = read_md_config(dtb_base .. "/md_tag_mem_addr")
--local md_tag_mem_size = read_md_config(dtb_base .. "/md_tag_mem_size")
local dtb_base = "/md_smem_addr_node"
local md_c_mem_addr   = device_tree:getProp(dtb_base .. "/md_c_mem_addr")
local md_c_mem_size   = device_tree:getProp(dtb_base .. "/md_c_mem_size")
local md_nc_mem_addr  = device_tree:getProp(dtb_base .. "/md_nc_mem_addr")
local md_nc_mem_size  = device_tree:getProp(dtb_base .. "/md_nc_mem_size")
local md_tag_mem_addr = device_tree:getProp(dtb_base .. "/md_tag_mem_addr")
local md_tag_mem_size = device_tree:getProp(dtb_base .. "/md_tag_mem_size")

local function safe_print_mem(desc, addr, size)
    addr = tonumber(addr)
    size = tonumber(size)
    if not addr or not size then
        print(string.format("%s: invalid addr or size! addr=%s, size=%s", desc, tostring(addr), tostring(size)))
    else
        print(string.format("%s: 0x%08x:0x%08x", desc, addr, size))
    end
end

safe_print_mem("md_c_mem",   md_c_mem_addr,   md_c_mem_size)
safe_print_mem("md_nc_mem",  md_nc_mem_addr,  md_nc_mem_size)
safe_print_mem("md_tag_mem", md_tag_mem_addr, md_tag_mem_size)

local function safe_setVmem(name, addr, size, a, b)
    addr = tonumber(addr)
    size = tonumber(size)
    if not addr or not size then
        print(string.format("setVmem %s failed: addr or size invalid! addr=%s, size=%s", name, tostring(addr), tostring(size)))
        return
    end
    sos_config:setVmem(name, addr, addr, size, a, b)
end

safe_setVmem("modem_ccci_tag_mem", md_tag_mem_addr, md_tag_mem_size, 1, 0)
safe_setVmem("modem_ap_md_nc_mem", md_nc_mem_addr,  md_nc_mem_size,  1, 0)
safe_setVmem("modem_ap_md_c_mem",  md_c_mem_addr,   md_c_mem_size,   1, 0)


--**************************************************************

-- setFlag
-- void setFlag(const std::string& name, bool enable)
-- sos_config:setFlag("block", false)
-- sos_config:setFlag("vmctl", true)
-- sos_config:setFlag("sound", true)
-- sos_config:setFlag("vsock", true)
-- sos_config:setFlag("net", true)
-- sos_config:setFlag("i2c", true)
-- sos_config:setFlag("eint", true)
-- sos_config:setFlag("adsp", true)
-- sos_config:setFlag("rtc_alarm", true)
-- sos_config:setFlag("vhost_user_fs", true)
-- sos_config:setFlag("mbrain", true)
-- sos_config:setFlag("rpmb", true)
-- sos_config:setFlag("conninfra_tbox", true)
-- sos_config:setFlag("mape", true)
--**************************************************************
--  gpu-sock
--[[
void setGpuSock(std::string sock_path)
--]]
--sos_config:setGpuSock("/tmp/virtio_gpu_socket")
--**************************************************************

--  gpu-irqs
--[[
void setGpuIrqs(uint32_t gpuirq0, uint32_t gpuirq1)
--]]
--sos_config:setGpuIrqs(176, 177)
--**************************************************************

--  vgic
--  gicd_paddr
--  gicr_paddr
--[[
void setVgicPaddr(size_t gicd, size_t gicr)
--]]
sos_config:setVgicPaddr(0xC400000, 0xC440000)

-- gic_its
-- its_paddr
--[[
void setVgicIts(bool has_its, uint64_t its_paddr)
--]]
--sos_config:setVgicIts(1, 0x910000000)

-- percpu_interrupt
--[[
void setVgicPercpuIrq(uint32_t hwirq)
--]]
percpu_interrupt = {
    }
-- TBOX does not consume the physical ARM PMU overflow PPI23. Keep its
-- per-CPU interrupt set independent from Android's required PPI23 policy.
for i = 1, #percpu_interrupt do
  sos_config:setVgicPercpuIrq(percpu_interrupt[i])
end

-- interrupt
--[[
void setVgicIrq(uint32_t hwirq)
--]]

interrupt = {
    651, 388, 389, 393, 398, 419, --Modem
    539, 540, 541, 542, 543, 549, 289,  --GPS
    211, -- Suspend
    836, --mtk_wakeup_display
    838, --mtk_wakeup_nodisplay
}
for i = 1, #interrupt do
    sos_config:setVgicIrq(interrupt[i])
end

--**************************************************************
-- usb interrupt
-- char *getSysEnv(const char *env, const char *default_value)
local USB_SWITCH_value = getSysEnv("USB_SWITCH", "F")
-- local USB_SWITCH_value = "F"

if USB_SWITCH_value == "T" then
    print("USB0 in Tbox UOS")
    sos_config:setVmem("usb_host",                 0x11200000, 0x11200000, 0x1000, 2, 0)
    sos_config:setVmem("ssusb",                    0x11201000, 0x11201000, 0x3000, 2, 0)
    sos_config:setVmem("u2phy0",                   0x11f40000, 0x11f40000, 0x1000, 2, 0)
    sos_config:setVmem("u3phy0",                   0x16773000, 0x16773000, 0x1000, 2, 0)
    interrupt = {544, 545}
    for i = 1, #interrupt do
         sos_config:setVgicIrq(interrupt[i])
    end
else
    print("USB0 NOT in Tbox UOS")
end

--**************************************************************

--  set cmdline
--sos_config:setCmdline("earlycon=pl011,0xffff32000 console=hvc0 root=/dev/vda ip-addr=192.168.3.2")
--**************************************************************

--  bootloader
--[[
void setBootloader(std::string path)
--]]
--sos_config:setBootloader("/vendor/etc/hyper_tbox/uos_tbox_bootloader_lk2.pb.txt")
--**************************************************************

-- pci-global-irqs
--[[
void setPciIrqs(uint32_t hwirq)
--]]
for i = 992, 1007 do
  sos_config:setPciIrqs(i)
end

-- enable-restartvm log
--[[
void setLogger(bool en)
--]]
--sos_config:setLogger(true)
--**************************************************************

-- vtee-notifier-irq
--[[
void setVteeNotifierIrq(uint32_t hwirq)
--]]
sos_config:setVteeNotifierIrq(203)
--**************************************************************

-- mmio-irq
--[[
void setMmioIrqs(uint32_t irq)

1.The IRQs must be continous.
2.IRQ 180 must be in sync with VIRTIO0_INT_BASE in LK2.
3.IRQ 181 must be in sync with virtio_vmctl in DTS.
--]]
--sos_config:setMmioIrqs(181)
--**************************************************************

-- sched-irq
--[[
void setSchedIrq(uint32_t irq)
--]]
sos_config:setSchedIrq(183)
--**************************************************************

-- smc-irq
--[[
void setSmcIrq(uint32_t irq)
--]]
sos_config:setSmcIrq(323)
--**************************************************************

-- cvm-lock-irq
--[[
void setCvmLockIrq(uint32_t irq)
--]]
sos_config:setCvmLockIrq(324)
--**************************************************************

-- wakeup-irq
--[[
void setWakeupIrq(uint32_t irq)
--]]
Wakeupirqs = {836, 838, 0xdeadbeef}
for i = 1, #Wakeupirqs do
    sos_config:setWakeupIrq(Wakeupirqs[i])
end
--**************************************************************


--  device-lua
--sos_config:setDeviceLua("/data/uos_vhost_dev.lua")
--**************************************************************

print("uos config ending... lua")
