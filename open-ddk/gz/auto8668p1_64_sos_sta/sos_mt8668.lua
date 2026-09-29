print("we are in sos config lua")

--  cpus
sos_config:setCpus(8)

--  vcpu entry
sos_config:setVcpuEntry(0x40000000)

--  physical-cpus
Pcpus = {0, 1, 2, 3, 4, 5, 6, 7}
for i = 1, #Pcpus do
    sos_config:setBindcpus(Pcpus[i])
end

--  periods
Period = {5000,5000,5000,5000,5000,5000,5000,5000}
for i = 1, #Period do
    sos_config:setPeriods(Period[i])
end

--  budgets
Budget = {2000,2000,2000,2000,3000,1500,1500,2000}
for i = 1, #Budget do
    sos_config:setBudgets(Budget[i])
end

-- sched-priority
SchedPriority = {2,2,4}
for i = 1, #SchedPriority do
    sos_config:setSchedPriority(SchedPriority[i])
end

-- sched-timeslice
SchedTimeslice = {200,200,100}
for i = 1, #SchedTimeslice do
    sos_config:setSchedTimeslice(SchedTimeslice[i])
end

-- vcpus-priority
VcpusPriority = {0,0,0,0,0,0,0,0}
for i = 1, #VcpusPriority do
    sos_config:setVcpuPriority(VcpusPriority[i])
end

-- sched-policy
SchedPolicy = {1,1,1,1,1,1,1,1}
for i = 1, #SchedPolicy do
    sos_config:setSchedPolicy(SchedPolicy[i])
end

-- ex-vcpu-info
sos_config:setExVcpuInfo(1, 0x3)
sos_config:setExVcpuInfo(2, 0x19)
sos_config:setExVcpuInfo(3, 0x0)

-- online-vcpu-info
sos_config:setOnlineVcpuInfo(1, 0x33)
sos_config:setOnlineVcpuInfo(2, 0x1F)
sos_config:setOnlineVcpuInfo(3, 0x11)

--  vgic
--  gicd_paddr
--  gicr_paddr
--[[
void setVgicPaddr(size_t gicd, size_t gicr)
--]]
sos_config:setVgicPaddr(0xc400000, 0xc440000)

-- vgic percpu_interrupt
sos_config:setVgicPercpuIrq(15)

-- vgic interrupts
local interrupts = {}
for i = 32, 854 do
    interrupts[i] = true
end

local exclude = {
    --usb irq
    --set interrupt_movable must exclude
    544, 545,
    -- uos_alps_pv8668.lua
    290, 292, 853,  --BT
    644, 852,  --WIFI
    556, 596, 597,  --UART2
    462, 464, 465, 466, --Display
    210, -- Suspend
    807, -- mtk_wakeup_display
    808, -- mtk_wakeup_nodisplay
    -- uos_tbox_pv8668.lua
    651, 388, 389, 393, 398, 419, --Modem
    539, 540, 541, 542, 543, 549, 289,  --GPS
    555, 594, 595,  --UART1
    211, -- Suspend
    552, -- Audio
    429, 430, 431, 432, 433, 434, -- Mali GPU
    836, -- mtk_wakeup_display
    838, -- mtk_wakeup_nodisplay
    622, -- spi3
    554, -- UART0
    818, -- UFS
    619, 620, 621, 622, 623, 624, 625, 626, -- spi
    562, 565, 572, --i2c2, i2c5, i2c12
    399, -- EINT
}

for _, v in ipairs(exclude) do
    interrupts[v] = nil
end

local exclude_virt = {
    -- sos_mt8668.lua
    179, --vhm
    182, --sched
    184, --dump
    204, 205, 206, 207, 208, 209, --mbox
    245, --tipc
    -- uos_alps_pv8668.lua
    180, 181, --mmio
    186, 188, --apu
    278, 280, --cmdq irq
    190, 192, 194, 196, 198, 200, --vsock
    202,  --vtee
    -- uos_tbox_pv8668.lua
    183, --sched
    203, --vtee
    -- rproc
    212, 215, 216, 217, 218,
    219, 220, 221, 222, 223,
    224, 225, 226, 227, 228,
    229, 230, 231, 232, 233,
    234, 235, 236, 237, 238,
    241, 242,
    261,262,263,264,265,
    266,267,268,269,270,
    271,272,273,274,275,
}

for _, v in ipairs(exclude_virt) do
    interrupts[v] = nil
end

local result = {}
for i = 32, 854 do
    if interrupts[i] then
        table.insert(result, i)
    end
end

for _, irq in ipairs(result) do
    sos_config:setVgicIrq(irq)
end

--set sos usb irq
--can be overridden by uos, which is configured by the interrupt_movable parameter.
local interrupt_movable = {544, 545}
for i = 1, #interrupt_movable do
    sos_config:setVgicIrqsMovable(interrupt_movable[i])
end

-- ipc-mbox
sos_config:setIpcMbox(204, 205)
sos_config:setIpcMbox(206, 207)
sos_config:setIpcMbox(208, 209)

-- sched-irq
sos_config:setSchedIrq(182)

-- sched-id
sos_config:setSchedId(1)

-- dump-irq
sos_config:setDumpIrq(184)

-- uos-exp-irq
--@GRT_CONFIG_UOS_EXP_IRQ_BEGIN@
sos_config:setUosExpIrq(837)
--@GRT_CONFIG_UOS_EXP_IRQ_END@

-- tipc-vq-notifier-irq
sos_config:setTipcVqNotifierIrq(245)

-- vhm-irq
--@GRT_CONFIG_ACRN_VHM_IRQ_BEGIN@
sos_config:setVhmIrq(179)
--@GRT_CONFIG_ACRN_VHM_IRQ_END@

-- guest-reserved-memory
sos_config:setGuestReservedMemory(2560)

sos_config:setPvblkEnabled(1)

-- cpufreq
sos_config:setCpufreq("mt8668")

-- gpu-ping-irq
sos_config:setGpuPingIrq(844)

-- smc-irq
sos_config:setSmcIrq(323)

-- cvm-lock-irq
sos_config:setCvmLockIrq(324)


-- cmdq_irq
CmdqIrqs = {277, 279}
for i = 1, #CmdqIrqs do
    sos_config:setCmdqIrqs(CmdqIrqs[i])
end

-- vsock-irqs
--@GRT_CONFIG_VSOCK_BEGIN@
VsockIrqs = {189, 191, 193, 195, 197, 199}
--@GRT_CONFIG_VSOCK_END@
for i = 1, #VsockIrqs do
    sos_config:setVsockIrqs(VsockIrqs[i])
end

-- pci irqs
for i = 992, 1007 do
  sos_config:setPciIrqs(i)
end

-- apu-irqs
ApuIrqs = {185, 187}
for i = 1, #ApuIrqs do
    sos_config:setApuIrqs(ApuIrqs[i])
end

-- vtee-notifier-irq
sos_config:setVteeNotifierIrq(201)
--**************************************************************

-- wdt-timeout
--[[
void setSosWdtTimeout(uint32_t timeout_s)
--]]
--setSosWdtTimeout second unit, 15 means 15s
--@GRT_CONFIG_SOS_WATCH_DOG_TIMEOUT_BEGIN@
sos_config:setSosWdtTimeout(15)
--@GRT_CONFIG_SOS_WATCH_DOG_TIMEOUT_END@
--**************************************************************

-- irq_monitor
--@GRT_CONFIG_IRQ_MONITOR_BEGIN@
monitor_irqs = {}
sos_config:setMonitorSpec(1, 10000, monitor_irqs)
--@GRT_CONFIG_IRQ_MONITOR_END@

-- nebula trace memory: 0 - minimum memory size, 1 - normal size
--@GRT_CONFIG_NEBULA_TRACE_BEGIN@
sos_config:setNbltraceMemEnable(1)
--@GRT_CONFIG_NEBULA_TRACE_END@'

-- crossvm-times
--@GRT_CONFIG_CROSS_VM_DUMP_BEGIN@
CrossVmTimes = {1000,1000,1000,1000,500,2000,500,500,500,500,500,500,2000,2000,2000,2000,500,500,500,500}
--@GRT_CONFIG_CROSS_VM_DUMP_END@
for i = 1, #CrossVmTimes do
    sos_config:setCrossVmTimes(CrossVmTimes[i])
end

sos_config:setArbiterGpuEnable(true)

local ArbiterGpuRegionConfig = {
    {paddr = 0x3a000000, size = 0x480000, flag = 0x00 },
}
for i = 1, #ArbiterGpuRegionConfig do
    local cfg = ArbiterGpuRegionConfig[i]
    sos_config:setArbiterGpuRegion(cfg.paddr, cfg.size, cfg.flag)
end

-- ArbiterGpuIrqs
local ArbiterGpuConfigs = {
{ irq = 4320, name = "GRANT" },
{ irq = 4321, name = "STOP" },
{ irq = 4322, name = "LOST" },
{ irq = 4323, name = "RCVR" },
{ irq = 432, name = "MALI-EVENT" },
{ irq = 433, name = "MALI-PWR" },
{ irq = 431, name = "MALI-JOB" },
{ irq = 430, name = "MALI-MMU" },
{ irq = 429, name = "MALI-GPU" },
}

for i = 1, #ArbiterGpuConfigs do
    local cfg = ArbiterGpuConfigs[i]
    sos_config:setArbiterGpuIrqs(cfg.irq, cfg.name)
end

print("sos config ending... lua")
