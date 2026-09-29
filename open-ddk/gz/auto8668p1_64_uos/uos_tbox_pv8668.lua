

print("we are in tbox config lua")

--  vmid
uos_config:setVmid(1)
--**************************************************************

--  sched vmid
uos_config:setSchedId(3)

--  cpus
uos_config:setCpus(8)
--**************************************************************

--  physical-cpus
Pcpus = {3, 1, 2, 0, 4, 5, 6, 7}
for i = 1, #Pcpus do
    uos_config:setBindcpus(Pcpus[i])
end

--  vCPU budgets in microsecond
budgets = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000}
for i = 1, #budgets do
    uos_config:setBudgets(budgets[i])
end
--**************************************************************

-- vCPU priority
vcpu_priority = {2, 0, 0, 0, 0, 0, 0, 0}
for i = 1, #vcpu_priority do
    uos_config:setVcpuPriority(vcpu_priority[i])
end
--**************************************************************

--  memory
-- getGuestMemSize(int vmid)
local mem = getGuestMemSize(1)  --unit MB

if (mem > 0) then
  uos_config:setMemsize(mem)
end
--uos_config:setMemsize(4096) --unit MB
--**************************************************************

--  vsock-cid
uos_config:setVsockCid(4)
--**************************************************************

-- filesystem
-- void setFs(std::string type, std::string path)
--@GRT_CONFIG_VIRTIO_FS_BEGIN_0@
uos_config:setFs("tbox_media", "/tmp/virtiofs_tbox_socket0")
--@GRT_CONFIG_VIRTIO_FS_END_0@
--@GRT_CONFIG_VIRTIO_FS_BEGIN_1@
--@GRT_CONFIG_VIRTIO_FS_END_1@
--**************************************************************

-- tipc
--[[
--void setTipc(std::string path)
--]]
--uos_config:setTipc("/tmp/virtio_tipc")
--**************************************************************

--  vmnet
--[[
--voiid setVmnet(std::string id, std::string ip, std::string netmask)
--void setVmnet(std::string id, std::string ip, std::string netmask, std::string fe_MAC)
--uos_config:setVmnet("sample_eth0_if", "172.16.16.1", "255.255.255.0", "02:22:ee:b9:55:66");
--]]
--@GRT_CONFIG_NEBULA_NET_BEGIN_0@
uos_config:setVmnet("vmnet-ysyu0", "172.16.17.1", "255.255.255.0", "0a:78:ac:10:11:02");
--@GRT_CONFIG_NEBULA_NET_END_0@
--@GRT_CONFIG_NEBULA_NET_BEGIN_1@
uos_config:setVmnet("vmnet-ysyu1", "172.16.33.1", "255.255.255.0", "0a:78:ac:10:21:02");
--@GRT_CONFIG_NEBULA_NET_END_1@
--@GRT_CONFIG_NEBULA_NET_BEGIN_2@
uos_config:setVmnet("vmnet-ysyu2", "172.16.34.1", "255.255.255.0", "0a:78:ac:10:22:02");
--@GRT_CONFIG_NEBULA_NET_END_2@
--**************************************************************

--  vmem_auto // insert memory node & map PA to guest os
--[[
void setVmemAuto(std::string name, std::string node_name,
    uint64_t base_addr, uint64_t size,
    uint8_t policy, uint8_t is_phy_mem)
--]]
uos_config:setVmemAuto("consys", "", 0x0, 0x00, 1, 0)
--uos_config:setVmemAuto("atf_log_reserved", "", 0, 0, 1, 1)
--uos_config:setVmemAuto("framebuffer", "", 0, 0, 1, 1)
--uos_config:setVmemAuto("79xx_wifi_vm", "", 0, 0, 1, 1)
--uos_config:setVmemAuto("pstore", "", 0, 0, 1, 1)
--uos_config:setVmemAuto("aee_debug_kinfo", "", 0, 0, 1, 1)
--**************************************************************

--  vmem_auto // insert memory node & map PA to guest os
--[[
void setVmemAuto(std::string name, std::string node_name,
    uint64_t base_addr, uint64_t size,
    uint8_t policy, uint8_t is_phy_mem)
--]]
uos_config:setVmemAuto("pstore", "", 0, 0, 1, 1)
uos_config:setVmemAuto("aee_debug_kinfo", "", 0, 0, 1, 1)
uos_config:setVmemAuto("atf_log_reserved", "", 0, 0, 1, 1)
uos_config:setVmemAuto("vmlog1", "", 0, 0, 1, 1)
uos_config:setVmemAuto("pmic_hyp", "", 0, 0, 1, 1)
uos_config:setVmemAuto("nbl_trace", "", 0, 0, 1, 1)
--**************************************************************

--  vmem // map io address for guest os
--[[
void setVmem(std::string name, uint64_t gpa_base, uint64_t hpa_base,
    uint64_t size,
    uint8_t policy, uint8_t is_phy_mem)
--]]

--@GRT_CONFIG_IOMMU_BEGIN@
uos_config:setVmem("spmi_efuse_csi-data", 0x00000000, 0x00000000, 0x1000, 1, 0)
uos_config:setVmem("qos", 0x0011b000, 0x0011b000, 0x1000, 1, 0)
uos_config:setVmem("csram-sysram", 0x0011f000, 0x0011f000, 0x2000, 1, 0)
uos_config:setVmem("sram", 0x00121000, 0x00121000, 0x1000, 1, 0)
uos_config:setVmem("chipid", 0x00f00000, 0x00f00000, 0x1000, 1, 0)
uos_config:setVmem("fdvfs", 0x0c068000, 0x0c068000, 0x1000, 2, 0)
uos_config:setVmem("cpuhvfs", 0x0c0db000, 0x0c0db000, 0x1000, 2, 0)
uos_config:setVmem("therm_intf-cpuhvfs", 0x0c0dd000, 0x0c0dd000, 0x3000, 2, 0)
uos_config:setVmem("mddriver", 0x0d180000, 0x0d180000, 0x2000, 2, 0)
uos_config:setVmem("topckgen", 0x10000000, 0x10000000, 0x1000, 2, 0)
uos_config:setVmem("infracfg-ao", 0x10001000, 0x10001000, 0x1000, 2, 0)
uos_config:setVmem("gpio", 0x10005000, 0x10005000, 0x1000, 2, 0)
uos_config:setVmem("apmixed", 0x1000c000, 0x1000c000, 0x1000, 2, 0)
uos_config:setVmem("dpmaif_apcldmaout", 0x10014000, 0x10014000, 0x1000, 2, 0)
uos_config:setVmem("modem-temp-share", 0x10018000, 0x10018000, 0x1000, 2, 0)
uos_config:setVmem("devapc-ao-md", 0x10019000, 0x10019000, 0x1000, 2, 0)
uos_config:setVmem("ap-ccif0", 0x10209000, 0x10209000, 0x1000, 2, 0)
uos_config:setVmem("md-ccif0", 0x1020a000, 0x1020a000, 0x1000, 2, 0)
uos_config:setVmem("ap-ccif1", 0x1020b000, 0x1020b000, 0x1000, 2, 0)
uos_config:setVmem("md-ccif1", 0x1020c000, 0x1020c000, 0x1000, 2, 0)
uos_config:setVmem("infracfg", 0x1020e000, 0x1020e000, 0x1000, 2, 0)
uos_config:setVmem("md2md-md1-ccif0", 0x10211000, 0x10211000, 0x1000, 2, 0)
uos_config:setVmem("md2md-md2-ccif0", 0x10213000, 0x10213000, 0x1000, 2, 0)
uos_config:setVmem("emi", 0x10219000, 0x10219000, 0x1000, 2, 0)
uos_config:setVmem("infra-md", 0x1021d000, 0x1021d000, 0x1000, 2, 0)
uos_config:setVmem("emimpu", 0x10226000, 0x10226000, 0x1000, 2, 0)
uos_config:setVmem("infra-dpmaif", 0x1022c000, 0x1022c000, 0x4000, 2, 0)
uos_config:setVmem("dramc@0x10230000", 0x10230000, 0x10230000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10234000", 0x10234000, 0x10234000, 0x1000, 2, 0)
uos_config:setVmem("emichn@0x10235000", 0x10235000, 0x10235000, 0x1000, 2, 0)
uos_config:setVmem("dramc@0x10236000", 0x10236000, 0x10236000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10238000", 0x10238000, 0x10238000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10240000", 0x10240000, 0x10240000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10244000", 0x10244000, 0x10244000, 0x1000, 2, 0)
uos_config:setVmem("emichn@0x10245000", 0x10245000, 0x10245000, 0x1000, 2, 0)
uos_config:setVmem("dramc@0x10246000", 0x10246000, 0x10246000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10248000", 0x10248000, 0x10248000, 0x2000, 2, 0)
uos_config:setVmem("ap-ccif4", 0x1024c000, 0x1024c000, 0x1000, 2, 0)
uos_config:setVmem("md-ccif2", 0x10250000, 0x10250000, 0x1000, 2, 0)
uos_config:setVmem("ap-ccif3", 0x10252000, 0x10252000, 0x1000, 2, 0)
uos_config:setVmem("md-ccif3", 0x10253000, 0x10253000, 0x1000, 2, 0)
uos_config:setVmem("md-ccif4", 0x10255000, 0x10255000, 0x1000, 2, 0)
uos_config:setVmem("md-ccif5", 0x10257000, 0x10257000, 0x1000, 2, 0)
uos_config:setVmem("ap-ccif5", 0x1025c000, 0x1025c000, 0x1000, 2, 0)
uos_config:setVmem("gpufreq@0x10270000", 0x10270000, 0x10270000, 0x1000, 2, 0)
uos_config:setVmem("hwv-syscon", 0x10320000, 0x10320000, 0x2000, 2, 0)
uos_config:setVmem("smpu", 0x10351000, 0x10351000, 0x1000, 2, 0)
uos_config:setVmem("dramc-md32-s0-apb", 0x10900000, 0x10900000, 0x40000, 2, 0)
uos_config:setVmem("dramc-md32-s1-apb", 0x10a00000, 0x10a00000, 0x40000, 2, 0)
uos_config:setVmem("dramc-ch1-rsv@0x10b00000", 0x10b00000, 0x10b00000, 0x40000, 2, 0)
uos_config:setVmem("dramc-ch1-rsv@0x10b40000", 0x10b40000, 0x10b40000, 0xc0000, 2, 0)
uos_config:setVmem("dramc-ch1-rsv@0x10c00000", 0x10c00000, 0x10c00000, 0x40000, 2, 0)
uos_config:setVmem("dramc-ch1-rsv@0x10c40000", 0x10c40000, 0x10c40000, 0xc0000, 2, 0)
uos_config:setVmem("serial@0x11002000", 0x11002000, 0x11002000, 0x1000, 2, 0)
uos_config:setVmem("spi3@0x11013000", 0x11013000, 0x11013000, 0x1000, 2, 0)
uos_config:setVmem("pericfg_ao_reg-syscon", 0x11036000, 0x11036000, 0x1000, 2, 0)
uos_config:setVmem("mt6881-afe-pcm", 0x11050000, 0x11050000, 0x10000, 2, 0)
uos_config:setVmem("audio-sram", 0x11060000, 0x11060000, 0x14000, 2, 0)
uos_config:setVmem("xhci0", 0x11200000, 0x11200000, 0x1000, 2, 0)
uos_config:setVmem("usb0", 0x11201000, 0x11201000, 0x3000, 2, 0)
uos_config:setVmem("ufscfg_ao-syscon", 0x112b8000, 0x112b8000, 0x1000, 2, 0)
uos_config:setVmem("ufscfg_pdn-syscon", 0x112bb000, 0x112bb000, 0x1000, 2, 0)
uos_config:setVmem("pextpcfg_ao-syscon", 0x112e0000, 0x112e0000, 0x1000, 2, 0)
uos_config:setVmem("uart-dma", 0x11300000, 0x11300000, 0x1000, 2, 0)
uos_config:setVmem("pinctrl@0x11b00000", 0x11b00000, 0x11b00000, 0x1000, 2, 0)
uos_config:setVmem("pinctrl@0x11b10000", 0x11b10000, 0x11b10000, 0x1000, 2, 0)
uos_config:setVmem("pinctrl@0x11b20000", 0x11b20000, 0x11b20000, 0x1000, 2, 0)
uos_config:setVmem("pinctrl@0x11c00000", 0x11c00000, 0x11c00000, 0x1000, 2, 0)
uos_config:setVmem("mipi_csi_top_ctrl_0-syscon", 0x11ca0000, 0x11ca0000, 0x1000, 2, 0)
uos_config:setVmem("pinctrl@0x11d40000", 0x11d40000, 0x11d40000, 0x1000, 2, 0)
uos_config:setVmem("pinctrl@0x11d60000", 0x11d60000, 0x11d60000, 0x1000, 2, 0)
uos_config:setVmem("pinctrl@0x11e20000", 0x11e20000, 0x11e20000, 0x1000, 2, 0)
uos_config:setVmem("pinctrl@0x11ec0000", 0x11ec0000, 0x11ec0000, 0x1000, 2, 0)
uos_config:setVmem("pinctrl@0x11f30000", 0x11f30000, 0x11f30000, 0x1000, 2, 0)
uos_config:setVmem("usb2-phy0", 0x11f40000, 0x11f40000, 0x1000, 2, 0)
uos_config:setVmem("usb3-phy0", 0x11f53000, 0x11f53000, 0x1000, 2, 0)
uos_config:setVmem("mmsys0-syscon", 0x14000000, 0x14000000, 0x1000, 2, 0)
uos_config:setVmem("imgsys_main-syscon", 0x15010000, 0x15010000, 0x1000, 2, 0)
uos_config:setVmem("dip_top_dip1-syscon", 0x15100000, 0x15100000, 0x1000, 2, 0)
uos_config:setVmem("dip_nr1_dip1-syscon", 0x15120000, 0x15120000, 0x1000, 2, 0)
uos_config:setVmem("dip_nr2_dip1-syscon", 0x15150000, 0x15150000, 0x1000, 2, 0)
uos_config:setVmem("wpe_eis_dip1-syscon", 0x15200000, 0x15200000, 0x1000, 2, 0)
uos_config:setVmem("wpe_tnr_dip1-syscon", 0x15500000, 0x15500000, 0x1000, 2, 0)
uos_config:setVmem("traw_dip1-syscon", 0x15700000, 0x15700000, 0x1000, 2, 0)
uos_config:setVmem("traw_cap_dip1-syscon", 0x15740000, 0x15740000, 0x1000, 2, 0)
uos_config:setVmem("img_vcore_d1a-syscon", 0x15780000, 0x15780000, 0x1000, 2, 0)
uos_config:setVmem("vdecsys-syscon", 0x1602f000, 0x1602f000, 0x1000, 2, 0)
uos_config:setVmem("vencsys-syscon", 0x17000000, 0x17000000, 0x1000, 2, 0)
uos_config:setVmem("consys", 0x18000000, 0x18000000, 0x100000, 2, 0)
uos_config:setVmem("gps", 0x18c00000, 0x18c00000, 0x100000, 2, 0)
uos_config:setVmem("cam_main_r1a-syscon", 0x1a010000, 0x1a010000, 0x1000, 2, 0)
uos_config:setVmem("camsys_mraw-syscon", 0x1a680000, 0x1a680000, 0x1000, 2, 0)
uos_config:setVmem("camsys_rawa-syscon", 0x1a7d0000, 0x1a7d0000, 0x1000, 2, 0)
uos_config:setVmem("camsys_rmsa-syscon", 0x1a7f0000, 0x1a7f0000, 0x1000, 2, 0)
uos_config:setVmem("camsys_yuva-syscon", 0x1a810000, 0x1a810000, 0x1000, 2, 0)
uos_config:setVmem("camsys_rawb-syscon", 0x1a9d0000, 0x1a9d0000, 0x1000, 2, 0)
uos_config:setVmem("camsys_rmsb-syscon", 0x1a9f0000, 0x1a9f0000, 0x1000, 2, 0)
uos_config:setVmem("camsys_yuvb-syscon", 0x1aa10000, 0x1aa10000, 0x1000, 2, 0)
uos_config:setVmem("cam_vcore_r1a-syscon", 0x1b80d000, 0x1b80d000, 0x1000, 2, 0)
uos_config:setVmem("gps_disp-dpc_apu-top-3", 0x1c000000, 0x1c000000, 0x1000, 2, 0)
uos_config:setVmem("scpsys-syscon", 0x1c001000, 0x1c001000, 0x1000, 2, 0)
uos_config:setVmem("watchdog_consys", 0x1c00a000, 0x1c00a000, 0x1000, 2, 0)
uos_config:setVmem("vlpcfg_reg_bus-syscon", 0x1c00c000, 0x1c00c000, 0x1000, 2, 0)
uos_config:setVmem("srclken-rc", 0x1c00d000, 0x1c00d000, 0x1000, 2, 0)
uos_config:setVmem("dvfsrc_disp-dpc", 0x1c00f000, 0x1c00f000, 0x1000, 2, 0)
uos_config:setVmem("vlp_cksys_top-syscon", 0x1c012000, 0x1c012000, 0x1000, 2, 0)
uos_config:setVmem("gps_thermal-ntc", 0x1c013000, 0x1c013000, 0x1000, 2, 0)
uos_config:setVmem("spmi@0x1cc10000", 0x1cc10000, 0x1cc10000, 0x4000, 2, 0)
uos_config:setVmem("spmi@0x1cc14000", 0x1cc14000, 0x1cc14000, 0x4000, 2, 0)
uos_config:setVmem("spmi@0x1cc18000", 0x1cc18000, 0x1cc18000, 0x7000, 2, 0)
uos_config:setVmem("spmi@0x1cc20000", 0x1cc20000, 0x1cc20000, 0x7000, 2, 0)
uos_config:setVmem("systimer", 0x1cd00000, 0x1cd00000, 0x1000, 2, 0)
uos_config:setVmem("mminfra_config-syscon", 0x1e800000, 0x1e800000, 0x1000, 2, 0)
uos_config:setVmem("mdpsys-syscon", 0x1f000000, 0x1f000000, 0x1000, 2, 0)
uos_config:setVmem("MD_AP_S-9", 0x20900000, 0x20900000, 0x100000, 2, 0)
--@GRT_CONFIG_IOMMU_END@

--  reg-access

--uos_config:addRegAccess("usb0-test1", 0x11201000, 0x11201008)

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

local dtb_base = "/sys/firmware/devicetree/base/md_smem_addr_node"
local md_c_mem_addr   = read_md_config(dtb_base .. "/md_c_mem_addr")
local md_c_mem_size   = read_md_config(dtb_base .. "/md_c_mem_size")
local md_nc_mem_addr  = read_md_config(dtb_base .. "/md_nc_mem_addr")
local md_nc_mem_size  = read_md_config(dtb_base .. "/md_nc_mem_size")
local md_tag_mem_addr = read_md_config(dtb_base .. "/md_tag_mem_addr")
local md_tag_mem_size = read_md_config(dtb_base .. "/md_tag_mem_size")

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
    uos_config:setVmem(name, addr, addr, size, a, b)
end

safe_setVmem("modem_ccci_tag_mem", md_tag_mem_addr, md_tag_mem_size, 1, 0)
safe_setVmem("modem_ap_md_nc_mem", md_nc_mem_addr,  md_nc_mem_size,  1, 0)
safe_setVmem("modem_ap_md_c_mem",  md_c_mem_addr,   md_c_mem_size,   1, 0)


--**************************************************************

-- setFlag
-- void setFlag(const std::string& name, bool enable)
uos_config:setFlag("console", true)
uos_config:setFlag("config_block", true)
uos_config:setFlag("block", true)
uos_config:setFlag("sound", true)
uos_config:setFlag("vsock", true)
uos_config:setFlag("net", true)
uos_config:setFlag("i2c", true)
uos_config:setFlag("sensor", true)
uos_config:setFlag("pinctrl", true)
uos_config:setFlag("conninfra_tbox", true)
uos_config:setFlag("eint", true)
uos_config:setFlag("adsp", true)
uos_config:setFlag("vmctl", true)
uos_config:setFlag("rtc_alarm", true)
uos_config:setFlag("mbrain", true)
--@GRT_CONFIG_VIRTIO_FS_ENABLE_BEGIN@
uos_config:setFlag("vhost_user_fs", true)
--@GRT_CONFIG_VIRTIO_FS_ENABLE_END@
uos_config:setFlag("rpmb", true)
uos_config:setFlag("mape", true)
--**************************************************************

--**************************************************************
-- Per-device vhost affinity inherit from nbl_vm_srv/sync-io (default true).
-- false: this vhost worker does not inherit parent affinity.

-- setInheritParentAffinity
-- void setInheritParentAffinity(const std::string& name, bool inherit)
uos_config:setInheritParentAffinity("adsp", false)
--**************************************************************

--  vgic
--  gicd_paddr
--  gicr_paddr
--[[
void setVgicPaddr(size_t gicd, size_t gicr)
--]]
uos_config:setVgicPaddr(0xC400000, 0xC440000)

-- gic_its
-- its_paddr
--[[
void setVgicIts(bool has_its, uint64_t its_paddr)
--]]
uos_config:setVgicIts(1, 0x910000000)

-- percpu_interrupt
--[[
void setVgicPercpuIrq(uint32_t hwirq)
--]]
percpu_interrupt = {
    }
for i = 1, #percpu_interrupt do
  uos_config:setVgicPercpuIrq(percpu_interrupt[i])
end

-- interrupt
--[[
void setVgicIrq(uint32_t hwirq)
--]]

interrupt = {
    622, --spi3
    651, 388, 389, 393, 398, 419, --Modem
    539, 540, 541, 542, 543, 549, 289,  --GPS
    555, 594, 595,  --UART1
    211, -- Suspend
    836, --mtk_wakeup_display
    838, --mtk_wakeup_nodisplay
}
for i = 1, #interrupt do
    uos_config:setVgicIrq(interrupt[i])
end

--**************************************************************
-- usb interrupt
-- char *getSysEnv(const char *env, const char *default_value)
local USB_SWITCH_value = getSysEnv("USB_SWITCH", "T")

if USB_SWITCH_value == "T" then
    print("USB0 in Tbox UOS")
    -- uos_config:setVmem("usb_host",                 0x11200000, 0x11200000, 0x1000, 2, 0)
    -- uos_config:setVmem("ssusb",                    0x11201000, 0x11201000, 0x3000, 2, 0)
    -- uos_config:setVmem("u2phy0",                   0x11f40000, 0x11f40000, 0x1000, 2, 0)
    -- uos_config:setVmem("u3phy0",                   0x16773000, 0x16773000, 0x1000, 2, 0)
    interrupt = {544, 545}
    for i = 1, #interrupt do
         uos_config:setVgicIrq(interrupt[i])
    end
else
    print("USB0 NOT in Tbox UOS")
end

--**************************************************************

--  set cmdline
--uos_config:setCmdline("earlycon=pl011,0xffff32000 console=hvc0 root=/dev/vda ip-addr=192.168.3.2")
--**************************************************************

--  bootloader
--[[
void setBootloader(std::string path)
--]]
uos_config:setBootloader("/vendor/etc/hyper_tbox/uos_tbox_bootloader_lk2.pb.txt")
--**************************************************************

-- pci-global-irqs
--[[
void setPciIrqs(uint32_t hwirq)
--]]
-- for i = 992, 1023 do
--   uos_config:setPciIrqs(i)
-- end

-- enable-restartvm log
--[[
void setLogger(bool en)
--]]
uos_config:setLogger(true)
--**************************************************************

-- vtee-notifier-irq
--[[
void setVteeNotifierIrq(uint32_t hwirq)
--]]
uos_config:setVteeNotifierIrq(203)
--**************************************************************

-- mmio-irq
--[[
void setMmioIrqs(uint32_t irq)

1.The IRQs must be continous.
2.IRQ 180 must be in sync with VIRTIO0_INT_BASE in LK2.
3.IRQ 181 must be in sync with virtio_vmctl in DTS.
--]]
for i = 180, 181 do
  uos_config:setMmioIrqs(i)
end
--**************************************************************

-- sched-irq
--[[
void setSchedIrq(uint32_t irq)
--]]
uos_config:setSchedIrq(183)
--**************************************************************

-- wakeup-irq
--[[
void setWakeupIrq(uint32_t irq)
--]]
Wakeupirqs = {836, 838, 0xdeadbeef, 651, 388, 389, 398, 419}
for i = 1, #Wakeupirqs do
    uos_config:setWakeupIrq(Wakeupirqs[i])
end
--**************************************************************


--  device-lua
--uos_config:setDeviceLua("/data/uos_vhost_dev.lua")
--**************************************************************

print("uos config ending... lua")
