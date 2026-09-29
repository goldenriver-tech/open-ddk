

print("we are in uos config lua")

--  vmid
uos_config:setVmid(0)
--**************************************************************

--  sched-vmid
uos_config:setSchedId(2)
--**************************************************************

-- The standalone sink configuration uses the PVBlock backend.
uos_config:setPvblkEnabled(1)

--  cpus
uos_config:setCpus(8)
--**************************************************************

--  lkbootbindcpus
--@GRT_CONFIG_UOS_LKBOOT_BIND_CPUS_BEGIN@
uos_config:lkBootBindCpu(7)
--@GRT_CONFIG_UOS_LKBOOT_BIND_CPUS_END@

--  physical-cpus
Pcpus = {2, 3, 5, 6, 7, 0, 1, 4}
for i = 1, #Pcpus do
    uos_config:setBindcpus(Pcpus[i])
end

--  vCPU budgets in microsecond
budgets = {2000, 3000, 2500, 2500, 2000, 2000, 2000, 2000}
for i = 1, #budgets do
    uos_config:setBudgets(budgets[i])
end
--**************************************************************

-- vCPU priority
vcpu_priority = {0, 0, 0, 0, 0, 0, 0, 0}
for i = 1, #vcpu_priority do
    uos_config:setVcpuPriority(vcpu_priority[i])
end
--**************************************************************

--  memory
-- getGuestMemSize(int vmid)
local mem = getGuestMemSize(0)  --unit MB

if (mem > 0) then
  uos_config:setMemsize(mem)
end
--uos_config:setMemsize(4096) --unit MB
--**************************************************************

-- setFlag
-- void setFlag(const std::string& name, bool enable)
uos_config:setFlag("console", true)
uos_config:setFlag("config_block", true)
uos_config:setFlag("vdmabuf", true)
uos_config:setFlag("block", false)               -- bring up shell
-- uos_config:setFlag("input", true)
-- uos_config:setFlag("keyboard", true)
uos_config:setFlag("sound", true)
uos_config:setFlag("vsock", true)
uos_config:setFlag("net", true)
uos_config:setFlag("i2c", true)
-- uos_config:setFlag("spi", true)
uos_config:setFlag("sensor", true)
-- uos_config:setFlag("leds", true)
uos_config:setFlag("pinctrl", true)
uos_config:setFlag("wmt", true)
uos_config:setFlag("eint", false)
uos_config:setFlag("mmdvfs", true)
uos_config:setFlag("adsp", true)
uos_config:setFlag("smi", true)                  -- bring up home
uos_config:setFlag("iommu", true)                -- bring up home
-- uos_config:setFlag("apu", true)
uos_config:setFlag("cmdq", true)             -- bring up home
uos_config:setFlag("disp", true)             -- bring up home
uos_config:setFlag("vmctl", true)               -- bring up shell
uos_config:setFlag("vhost_user_video", true)
uos_config:setFlag("timesync", true)
-- uos_config:setFlag("slbc", true)
uos_config:setFlag("rtc_alarm", true)
uos_config:setFlag("proxy_disp", true)
uos_config:setFlag("camera", true)
uos_config:setFlag("msched", true)
uos_config:setFlag("mbrain", true)
uos_config:setFlag("touch", true)            -- bring up home
--@GRT_CONFIG_VIRTIO_FS_ENABLE_BEGIN@
uos_config:setFlag("vhost_user_fs", true)
--@GRT_CONFIG_VIRTIO_FS_ENABLE_END@
uos_config:setFlag("rpmb", true)
--**************************************************************

--**************************************************************
-- Per-device vhost affinity inherit from nbl_vm_srv/sync-io (default true).
-- false: this vhost worker does not inherit parent affinity.

-- setInheritParentAffinity
-- void setInheritParentAffinity(const std::string& name, bool inherit)
uos_config:setInheritParentAffinity("adsp", false)
--**************************************************************

--  display-type
--[[
void setDispType( uint8_t type)
type: NONE:0, QT:1, SDL:2, GBM:3, DUMMY:4
--]]
uos_config:setDispType(4)
--**************************************************************

--  display
--[[
void setDisplayParam(int id, int x, int y,int has_touch, int type)
type: VIRTIO_INPUT_MOUSE:1; VIRTIO_INPUT_SINGLE_TOUCH:2; VIRTIO_INPUT_MULTI_TOUCH:3
--]]
uos_config:setDisplayParam(0,720,800,1,2)
uos_config:setDisplayParam(0,720,800,1,2)
uos_config:setDisplayParam(0,800,1280,1,2)
uos_config:setDisplayParam(0,1920,1080,1,2)
--**************************************************************

--  video
--[[
void setVideoParam(std::string type, std::string path)
--]]
uos_config:setVideoParam("decoder","/tmp/virtio_video_decoder")
uos_config:setVideoParam("encoder","/tmp/virtio_video_encoder")
--**************************************************************

--  vsock-cid
uos_config:setVsockCid(3)
--**************************************************************

--  input
uos_config:setInput("/dev/input/event4")
uos_config:setInput("/dev/input/event5")
uos_config:setInput("/dev/input/event6")
uos_config:setInput("/dev/input/event7")
--**************************************************************

--  filesystem
--[[
void setFs(std::string type, std::string path)
--]]
--@GRT_CONFIG_VIRTIO_FS_BEGIN_0@
uos_config:setFs("media", "/tmp/virtiofs_socket0")
--@GRT_CONFIG_VIRTIO_FS_END_0@

--**************************************************************

-- tipc
--[[
void setTipc(std::string path)
--]]
uos_config:setTipc("/tmp/virtio_tipc")
--**************************************************************

--  vmnet
--[[
void setVmnet(std::string id, std::string ip, std::string netmask)
void setVmnet(std::string id, std::string ip, std::string netmask, std::string fe_MAC)
uos_config:setVmnet("sample_eth0_if", "172.16.16.1", "255.255.255.0", "02:22:ee:b9:55:66");
--]]
--@GRT_CONFIG_NEBULA_NET_BEGIN_0@
uos_config:setVmnet("vmnet-ysau0", "172.16.16.1", "255.255.255.0", "0a:78:ac:10:10:02");
--@GRT_CONFIG_NEBULA_NET_END_0@
--@GRT_CONFIG_NEBULA_NET_BEGIN_1@
uos_config:setVmnet("vmnet-ysau1", "172.16.30.1", "255.255.255.0", "0a:78:ac:10:1e:02");
--@GRT_CONFIG_NEBULA_NET_END_1@
--@GRT_CONFIG_NEBULA_NET_BEGIN_2@
uos_config:setVmnet("vmnet-ysau2", "172.16.31.1", "255.255.255.0", "0a:78:ac:10:1f:02");
--@GRT_CONFIG_NEBULA_NET_END_2@
--@GRT_CONFIG_NEBULA_NET_BEGIN_3@
uos_config:setVmnet("vmnet-ysau3", "172.16.50.1", "255.255.255.0", "0a:78:ac:10:32:02");
--@GRT_CONFIG_NEBULA_NET_END_3@
--**************************************************************

--  vmem_auto // insert memory node & map PA to guest os
--[[
void setVmemAuto(std::string name, std::string node_name,
    uint64_t base_addr, uint64_t size,
    uint8_t policy, uint8_t is_phy_mem)
--]]
uos_config:setVmemAuto("consys", "", 0x0, 0x00, 1, 0)
uos_config:setVmemAuto("wifi_vm", "mblock-shared-dma-pool_wifi-reserve-memory_dma", 0x0, 0x00, 1, 0)
uos_config:setVmemAuto("atf_log_reserved", "", 0, 0, 1, 1)
uos_config:setVmemAuto("pstore", "", 0, 0, 1, 1)
uos_config:setVmemAuto("aee_debug_kinfo", "", 0, 0, 1, 1)
uos_config:setVmemAuto("vmlog_sink_alps", "", 0, 0, 1, 1)
uos_config:setVmemAuto("pmic_hyp", "", 0, 0, 1, 1)
uos_config:setVmemAuto("nbl_trace", "", 0, 0, 1, 1)
uos_config:setVmemAuto("gpueb_shared", "", 0, 0, 1, 0)
uos_config:setVmemAuto("mfg_dfd", "", 0, 0, 1, 0)
--**************************************************************

--  vmem // map io address for guest os
--[[
void setVmem(std::string name, uint64_t gpa_base, uint64_t hpa_base,
    uint64_t size,
    uint8_t policy, uint8_t is_phy_mem)
--]]

--@GRT_CONFIG_IOMMU_BEGIN@
uos_config:setVmem("spmi_efuse_csi-data", 0x00000000, 0x00000000, 0x1000, 1, 0)
uos_config:setVmem("therm-intf", 0x0011a000, 0x0011a000, 0x1000, 1, 0)
uos_config:setVmem("qos", 0x0011b000, 0x0011b000, 0x1000, 1, 0)
uos_config:setVmem("csram-sysram", 0x0011f000, 0x0011f000, 0x2000, 1, 0)
uos_config:setVmem("gpu-fdvfs_qos", 0x00121000, 0x00121000, 0x2000, 1, 0)
uos_config:setVmem("chipid", 0x00f00000, 0x00f00000, 0x1000, 1, 0)
uos_config:setVmem("fdvfs", 0x0c068000, 0x0c068000, 0x1000, 2, 0)
uos_config:setVmem("cpuhvfs", 0x0c0db000, 0x0c0db000, 0x1000, 2, 0)
uos_config:setVmem("therm_intf-cpuhvfs", 0x0c0dc000, 0x0c0dc000, 0x4000, 2, 0)
uos_config:setVmem("topckgen", 0x10000000, 0x10000000, 0x1000, 2, 0)
uos_config:setVmem("infracfg-ao", 0x10001000, 0x10001000, 0x1000, 2, 0)
uos_config:setVmem("gpio", 0x10005000, 0x10005000, 0x1000, 2, 0)
uos_config:setVmem("apmixed", 0x1000c000, 0x1000c000, 0x1000, 2, 0)
uos_config:setVmem("lastbus@0x10023000", 0x10023000, 0x10023000, 0x1000, 2, 0)
uos_config:setVmem("gpufreq@0x1002b000", 0x1002b000, 0x1002b000, 0x1000, 2, 0)
uos_config:setVmem("gpufreq@0x10042000", 0x10042000, 0x10042000, 0x1000, 2, 0)
uos_config:setVmem("gpufreq@0x1021c000", 0x1021c000, 0x1021c000, 0x1000, 2, 0)
uos_config:setVmem("dramc@0x10230000", 0x10230000, 0x10230000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10234000", 0x10234000, 0x10234000, 0x1000, 2, 0)
uos_config:setVmem("dramc@0x10236000", 0x10236000, 0x10236000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10238000", 0x10238000, 0x10238000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10240000", 0x10240000, 0x10240000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10244000", 0x10244000, 0x10244000, 0x1000, 2, 0)
uos_config:setVmem("dramc@0x10246000", 0x10246000, 0x10246000, 0x2000, 2, 0)
uos_config:setVmem("dramc@0x10248000", 0x10248000, 0x10248000, 0x2000, 2, 0)
uos_config:setVmem("dramc_ch1_top2", 0x10254000, 0x10254000, 0x1000, 2, 0)
uos_config:setVmem("dramc_ch1_rsv", 0x10256000, 0x10256000, 0x1000, 2, 0)
uos_config:setVmem("gpufreq@0x1025f000", 0x1025f000, 0x1025f000, 0x1000, 2, 0)
uos_config:setVmem("gpufreq@0x10270000", 0x10270000, 0x10270000, 0x1000, 2, 0)
uos_config:setVmem("gpufreq@0x10276000", 0x10276000, 0x10276000, 0x1000, 2, 0)
uos_config:setVmem("gpufreq@0x1030c000", 0x1030c000, 0x1030c000, 0x1000, 2, 0)
uos_config:setVmem("lvts@0x10315000", 0x10315000, 0x10315000, 0x1000, 2, 0)
uos_config:setVmem("lvts@0x10316000", 0x10316000, 0x10316000, 0x1000, 2, 0)
uos_config:setVmem("hwv-syscon", 0x10320000, 0x10320000, 0x2000, 2, 0)
uos_config:setVmem("dramc-md32-s0-apb", 0x10900000, 0x10900000, 0x40000, 2, 0)
uos_config:setVmem("dramc-md32-s1-apb", 0x10a00000, 0x10a00000, 0x40000, 2, 0)
uos_config:setVmem("dramc-ch1-rsv@0x10b00000", 0x10b00000, 0x10b00000, 0x40000, 2, 0)
uos_config:setVmem("dramc-ch1-rsv@0x10b40000", 0x10b40000, 0x10b40000, 0xc0000, 2, 0)
uos_config:setVmem("dramc-ch1-rsv@0x10c00000", 0x10c00000, 0x10c00000, 0x40000, 2, 0)
uos_config:setVmem("dramc-ch1-rsv@0x10c40000", 0x10c40000, 0x10c40000, 0xc0000, 2, 0)
uos_config:setVmem("uarthub", 0x11000000, 0x11000000, 0x1000, 2, 0)
uos_config:setVmem("serial@0x11003000", 0x11003000, 0x11003000, 0x1000, 2, 0)
uos_config:setVmem("spi4@0x11014000", 0x11014000, 0x11014000, 0x1000, 2, 0)
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
uos_config:setVmem("disp-sync_disp-mutex0", 0x14001000, 0x14001000, 0x1000, 2, 0)
uos_config:setVmem("disp-ovl0-2l", 0x14002000, 0x14002000, 0x1000, 2, 0)
uos_config:setVmem("disp-ovl1-2l", 0x14003000, 0x14003000, 0x1000, 2, 0)
uos_config:setVmem("disp-ovl2-2l", 0x14004000, 0x14004000, 0x1000, 2, 0)
uos_config:setVmem("disp-ovl3-2l", 0x14005000, 0x14005000, 0x1000, 2, 0)
uos_config:setVmem("disp-rsz1", 0x14007000, 0x14007000, 0x1000, 2, 0)
uos_config:setVmem("disp-rsz0", 0x14008000, 0x14008000, 0x1000, 2, 0)
uos_config:setVmem("disp-postmask0", 0x14010000, 0x14010000, 0x1000, 2, 0)
uos_config:setVmem("disp-splitter0", 0x14018000, 0x14018000, 0x1000, 2, 0)
uos_config:setVmem("disp-wdma1", 0x1401c000, 0x1401c000, 0x1000, 2, 0)
uos_config:setVmem("smi-larb0", 0x1401e000, 0x1401e000, 0x1000, 2, 0)
uos_config:setVmem("smi-larb1", 0x1401f000, 0x1401f000, 0x1000, 2, 0)
uos_config:setVmem("disp-ovl0-1l", 0x14020000, 0x14020000, 0x1000, 2, 0)
uos_config:setVmem("disp-ovl1-1l", 0x14021000, 0x14021000, 0x1000, 2, 0)
uos_config:setVmem("disp-dpc@0x14028000", 0x14028000, 0x14028000, 0x1000, 2, 0)
uos_config:setVmem("reserved@0x14029000", 0x14029000, 0x14029000, 0x1000, 2, 0)
uos_config:setVmem("reserved@0x1402a000", 0x1402a000, 0x1402a000, 0x1000, 2, 0)
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
uos_config:setVmem("consys", 0x18000000, 0x18000000, 0x700000, 2, 0)
uos_config:setVmem("bt@0x18800000", 0x18800000, 0x18800000, 0x1000, 2, 0)
uos_config:setVmem("bt@0x18812000", 0x18812000, 0x18812000, 0x1000, 2, 0)
uos_config:setVmem("conn2btsys@0x18820000", 0x18820000, 0x18820000, 0x1000, 2, 0)
uos_config:setVmem("mtk-btcvsd-snd@0x18830000", 0x18830000, 0x18830000, 0x2000, 2, 0)
uos_config:setVmem("conn2btsys@0x18834000", 0x18834000, 0x18834000, 0x1000, 2, 0)
uos_config:setVmem("mtk-btcvsd-snd@0x18840000", 0x18840000, 0x18840000, 0x20000, 2, 0)
-- uos_config:setVmem("apu-top-3@0x190e1000", 0x190e1000, 0x190e1000, 0x2000, 2, 0)
-- uos_config:setVmem("apu-top-3@0x190f0000", 0x190f0000, 0x190f0000, 0x1000, 2, 0)
uos_config:setVmem("cam_main_r1a-syscon", 0x1a010000, 0x1a010000, 0x1000, 2, 0)
uos_config:setVmem("camsys_mraw-syscon", 0x1a680000, 0x1a680000, 0x1000, 2, 0)
uos_config:setVmem("camsys_rawa-syscon", 0x1a7d0000, 0x1a7d0000, 0x1000, 2, 0)
uos_config:setVmem("camsys_rmsa-syscon", 0x1a7f0000, 0x1a7f0000, 0x1000, 2, 0)
uos_config:setVmem("camsys_yuva-syscon", 0x1a810000, 0x1a810000, 0x1000, 2, 0)
uos_config:setVmem("camsys_rawb-syscon", 0x1a9d0000, 0x1a9d0000, 0x1000, 2, 0)
uos_config:setVmem("camsys_rmsb-syscon", 0x1a9f0000, 0x1a9f0000, 0x1000, 2, 0)
uos_config:setVmem("camsys_yuvb-syscon", 0x1aa10000, 0x1aa10000, 0x1000, 2, 0)
uos_config:setVmem("cam_vcore_r1a-syscon", 0x1b80d000, 0x1b80d000, 0x1000, 2, 0)
-- uos_config:setVmem("gps_disp-dpc_apu-top-3", 0x1c000000, 0x1c000000, 0x1000, 2, 0)
uos_config:setVmem("scpsys-syscon", 0x1c001000, 0x1c001000, 0x1000, 2, 0)
uos_config:setVmem("watchdog_consys", 0x1c00a000, 0x1c00a000, 0x1000, 2, 0)
uos_config:setVmem("vlpcfg_reg_bus-syscon", 0x1c00c000, 0x1c00c000, 0x1000, 2, 0)
uos_config:setVmem("srclken-rc", 0x1c00d000, 0x1c00d000, 0x1000, 2, 0)
uos_config:setVmem("dvfsrc_disp-dpc", 0x1c00f000, 0x1c00f000, 0x1000, 2, 0)
uos_config:setVmem("vlp_cksys_top-syscon", 0x1c012000, 0x1c012000, 0x1000, 2, 0)
uos_config:setVmem("gps_thermal-ntc", 0x1c013000, 0x1c013000, 0x1000, 2, 0)
uos_config:setVmem("ssram1", 0x1c350000, 0x1c350000, 0x1000, 2, 0)
uos_config:setVmem("tinysys-mbox@0x1c351000", 0x1c351000, 0x1c351000, 0x1000, 2, 0)
uos_config:setVmem("spmi@0x1cc10000", 0x1cc10000, 0x1cc10000, 0x4000, 2, 0)
uos_config:setVmem("spmi@0x1cc14000", 0x1cc14000, 0x1cc14000, 0x4000, 2, 0)
uos_config:setVmem("spmi@0x1cc18000", 0x1cc18000, 0x1cc18000, 0x7000, 2, 0)
uos_config:setVmem("spmi@0x1cc20000", 0x1cc20000, 0x1cc20000, 0x7000, 2, 0)
uos_config:setVmem("systimer", 0x1cd00000, 0x1cd00000, 0x1000, 2, 0)
uos_config:setVmem("security-ao", 0x1e20d000, 0x1e20d000, 0x1000, 2, 0)
uos_config:setVmem("masp", 0x1e20e000, 0x1e20e000, 0x1000, 2, 0)
uos_config:setVmem("mminfra_config-syscon", 0x1e800000, 0x1e800000, 0x1000, 2, 0)
uos_config:setVmem("smi-disp-common", 0x1e801000, 0x1e801000, 0x1000, 2, 0)
uos_config:setVmem("smi-ssc0-smi-sub-common", 0x1e807000, 0x1e807000, 0x1000, 2, 0)
uos_config:setVmem("smi-ssc1-smi-sub-common", 0x1e808000, 0x1e808000, 0x1000, 2, 0)
uos_config:setVmem("smi-mdp-sub-comm0", 0x1e809000, 0x1e809000, 0x1000, 2, 0)
uos_config:setVmem("mminfra-imax@0x1e80b000", 0x1e80b000, 0x1e80b000, 0x1000, 2, 0)
uos_config:setVmem("gce@0x1e980000", 0x1e980000, 0x1e980000, 0x4000, 2, 0)
uos_config:setVmem("mmqos", 0x1ec2e000, 0x1ec2e000, 0x1000, 2, 0)
uos_config:setVmem("disp-dpc@0x1ec35000", 0x1ec35000, 0x1ec35000, 0x1000, 2, 0)
uos_config:setVmem("mdpsys-syscon", 0x1f000000, 0x1f000000, 0x1000, 2, 0)
uos_config:setVmem("mml-mutex0", 0x1f001000, 0x1f001000, 0x1000, 2, 0)
uos_config:setVmem("mminfra-imax@0x1f002000", 0x1f002000, 0x1f002000, 0x1000, 2, 0)
uos_config:setVmem("mml-rdma0", 0x1f003000, 0x1f003000, 0x1000, 2, 0)
uos_config:setVmem("mml-rdma1", 0x1f004000, 0x1f004000, 0x1000, 2, 0)
uos_config:setVmem("mml-hdr0", 0x1f005000, 0x1f005000, 0x1000, 2, 0)
uos_config:setVmem("mml-hdr1", 0x1f006000, 0x1f006000, 0x1000, 2, 0)
uos_config:setVmem("mml-aal0", 0x1f007000, 0x1f007000, 0x1000, 2, 0)
uos_config:setVmem("mml-rsz0", 0x1f009000, 0x1f009000, 0x1000, 2, 0)
uos_config:setVmem("mml-rsz1", 0x1f00a000, 0x1f00a000, 0x1000, 2, 0)
uos_config:setVmem("mml-tdshp0", 0x1f00b000, 0x1f00b000, 0x1000, 2, 0)
uos_config:setVmem("mml-wrot0", 0x1f00f000, 0x1f00f000, 0x1000, 2, 0)
uos_config:setVmem("mml-wrot1", 0x1f010000, 0x1f010000, 0x1000, 2, 0)
uos_config:setVmem("mml-fg1", 0x1f012000, 0x1f012000, 0x1000, 2, 0)
uos_config:setVmem("mali", 0x3a000000, 0x3a000000, 0x480000, 2, 0)
uos_config:setVmem("g3d-config@0x3a500000", 0x3a500000, 0x3a500000, 0x10000, 2, 0)
uos_config:setVmem("g3d-config@0x3a510000", 0x3a510000, 0x3a510000, 0x10000, 2, 0)
uos_config:setVmem("g3d-dvfs", 0x3a530000, 0x3a530000, 0x1000, 2, 0)
uos_config:setVmem("g3d-mpu", 0x3a590000, 0x3a590000, 0x1000, 2, 0)
uos_config:setVmem("gpueb;g3d-gpueb", 0x3d000000, 0x3d000000, 0xa0000, 2, 0)
uos_config:setVmem("gpueb@0x3d120000", 0x3d120000, 0x3d120000, 0x1000, 2, 0)
uos_config:setVmem("gpueb@0x3d130000", 0x3d130000, 0x3d130000, 0x1000, 2, 0)
uos_config:setVmem("gpueb@0x3d140000", 0x3d140000, 0x3d140000, 0x1000, 2, 0)
uos_config:setVmem("gpueb@0x3d170000", 0x3d170000, 0x3d170000, 0x1000, 2, 0)
uos_config:setVmem("gpufreq@0x3d1a0000", 0x3d1a0000, 0x3d1a0000, 0x10000, 2, 0)
uos_config:setVmem("lvts@0x3d400000", 0x3d400000, 0x3d400000, 0x1000, 2, 0)
uos_config:setVmem("g3d-config@0x3d800000", 0x3d800000, 0x3d800000, 0x10000, 2, 0)
uos_config:setVmem("g3d-config@0x3d810000", 0x3d810000, 0x3d810000, 0x1000, 2, 0)
uos_config:setVmem("dfd@0x3d820000", 0x3d820000, 0x3d820000, 0x4000, 2, 0)
uos_config:setVmem("gpufreq@0x3d860000", 0x3d860000, 0x3d860000, 0x10000, 2, 0)
uos_config:setVmem("gpufreq@0x3d910000", 0x3d910000, 0x3d910000, 0x10000, 2, 0)
--@GRT_CONFIG_IOMMU_END@

--  reg-access

--uos_config:addRegAccess("usb0-test1", 0x11201000, 0x11201008)

--**************************************************************

gpu_interrupt = {243, 244}
for i = 1, #gpu_interrupt do
  uos_config:setGpuIrqs(i, gpu_interrupt[i])
end

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
    13,
    23}
for i = 1, #percpu_interrupt do
  uos_config:setVgicPercpuIrq(percpu_interrupt[i])
end

-- interrupt
--[[
void setVgicIrq(uint32_t hwirq)
--]]
interrupt = {
    292, 853, 290,  --BT
    644, 852,  --WIFI
    556, 596, 597,  --UART2
    462, --Display
    210, -- Suspend
    807, --mtk_wakeup_display
    808, --mtk_wakeup_nodisplay
}
for i = 1, #interrupt do
    uos_config:setVgicIrq(interrupt[i])
end

--**************************************************************
-- usb interrupt
-- char *getSysEnv(const char *env, const char *default_value)
local USB_SWITCH_value = getSysEnv("USB_SWITCH", "T")

if USB_SWITCH_value == "A" then
    print("USB0 in Android UOS")
    -- uos_config:setVmem("usb_host",                 0x11200000, 0x11200000, 0x1000, 2, 0)
    -- uos_config:setVmem("ssusb",                    0x11201000, 0x11201000, 0x3000, 2, 0)
    -- uos_config:setVmem("u2phy0",                   0x11f40000, 0x11f40000, 0x1000, 2, 0)
    -- uos_config:setVmem("u3phy0",                   0x16773000, 0x16773000, 0x1000, 2, 0)
    interrupt = {544, 545}
    for i = 1, #interrupt do
         uos_config:setVgicIrq(interrupt[i])
    end
else
    print("USB0 NOT in Android UOS")
end

--**************************************************************

--  bootloader
--[[
void setBootloader(std::string path)
--]]
uos_config:setBootloader("/vendor/etc/hyper_android/uos_alps_bootloader_lk2.pb.txt")
--**************************************************************

-- pci-global-irqs
--[[
void setPciIrqs(uint32_t hwirq)
--]]
for i = 992, 1007 do
   uos_config:setPciIrqs(i)
end

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

uos_config:setVteeNotifierIrq(202)
--**************************************************************

-- mmio-irq
--[[
void setMmioIrqs(uint32_t irq)

1.The IRQs must be continous.
2.IRQ 180 must be in sync with VIRTIO0_INT_BASE in LK2.
3.IRQ 181 must be in sync with virtio_vmctl in DTS.
--]]
uos_config:setMmioIrqs(181)

--**************************************************************

-- sched-irq
--[[
void setSchedIrq(uint32_t irq)
--]]
uos_config:setSchedIrq(182)
--**************************************************************

-- dump-irq
--[[
void setDumpIrq(uint32_t irq)
--]]
uos_config:setDumpIrq(184)
--**************************************************************

-- smc-irq
--[[
void setSmcIrq(uint32_t irq)
--]]
uos_config:setSmcIrq(323)
--**************************************************************

-- cvm-lock-irq
--[[
void setCvmLockIrq(uint32_t irq)
--]]
uos_config:setCvmLockIrq(324)
--**************************************************************

-- wakeup-irq
--[[
void setWakeupIrq(uint32_t irq)
--]]
Wakeupirqs = {807, 808, 0xdeadbeef}
for i = 1, #Wakeupirqs do
    uos_config:setWakeupIrq(Wakeupirqs[i])
end
--**************************************************************

--  apu-irqs
--[[
void setApuIrq(uint32_t hwirq)
--]]
for i = 185, 188 do
    uos_config:setApuIrq(i)
end

-- cmdq-irqs (same as auto8668p1_64_sos dummy SWA)
--[[
void setCmdqIrqs(uint32_t hwirq)
--]]
CmdqIrqs = {277, 278, 279, 280}
for i = 1, #CmdqIrqs do
    uos_config:setCmdqIrqs(CmdqIrqs[i])
end

--  device-lua
--uos_config:setDeviceLua("/data/uos_vhost_dev.lua")
--**************************************************************

-- vsock-irqs
--[[
void setVsockIrqs(uint32_t hwirq)
--]]
--@GRT_CONFIG_VSOCK_BEGIN@
for i = 189, 200 do
--@GRT_CONFIG_VSOCK_END@
  uos_config:setVsockIrqs(i)
end
--**************************************************************

-- vsmmu config
-- uos_config:addVsmmu(0x30800000, 509)  --add mm_smmu config:paddr, irq
-- mm_sids = {                           --add sid for mm_smmu
--   164,601,602,603,604
-- }
-- for i = 1, #mm_sids do
-- uos_config:addSidToVsmmu(mm_sids[i])
-- end

-- uos_config:addVsmmu(0x4c000000, 683)  --add apu_smmu config:paddr, irq
-- for i = 41, 50 do                     --add sid for mm_smmu
--   uos_config:addSidToVsmmu(i)
-- end

print("uos config ending... lua")
