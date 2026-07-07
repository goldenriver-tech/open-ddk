// SPDX-License-Identifier: BSD-3-Clause

#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/string_buffer.h>
#include <libfdt.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>
#include <lib/zx/resource.h>

#include "garnet/lib/machina/watchdog.h"
#include "lib/fxl/logging.h"

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
#define GUEST_PM_SYSTEM_OFF          0
#define GUEST_PM_RUNNING             1
#define GUEST_PM_SUSPEND_TO_RAM      2
#define GUEST_PM_SUSPEND_TO_IDLE     3

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

namespace machina {
constexpr uint32_t kVmControlWdtCheckInterval = 30000;

Watchdog::~Watchdog() {
    {
        std::unique_lock<std::mutex> lk(mutex_);
        stop_thread_ = true;
    }
    wdt_cv_.notify_all();
    if (wdt_thread_.joinable()) {
        wdt_thread_.join();
    }
}

void Watchdog::init(int32_t vmid) {
    zx_status_t status = get_root_resource(&root_resource);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to get hypervisor resource";
        return;
    }

    FXL_LOG(ERROR) << "Watchdog init for vmid: " << vmid;
    wdt_thread_ = std::thread([this, vmid]() {
        while (true) {
            int32_t pm_state;
            std::unique_lock<std::mutex> lk(mutex_);
            auto start = std::chrono::steady_clock::now();
            auto status = wdt_cv_.wait_for(
              lk, std::chrono::milliseconds(kVmControlWdtCheckInterval));
            if (stop_thread_) {
                break;
            }
            if (!wdt_kick_start_)
                continue;

            // TODO: Need to consider the scenario where HWT (Hardware Watchdog Trigger) occurs
            // during the period from the suspension of Yocto WDT (Watchdog Timer) thread to
            // the complete success of Yocto system suspension.
            // Currently, we only simply stop the WDT without handling this scenario.
            PowerState state = GetPowerState();
            if (state == PowerState::SUSPENDED)
                continue;

            zx_get_pmstate_form_vmid(root_resource.get(), vmid,  &pm_state);
            if (pm_state == GUEST_PM_SUSPEND_TO_RAM ||
                pm_state == GUEST_PM_SUSPEND_TO_IDLE) {
                FXL_LOG(INFO) << "VM is in suspend state, skip watchdog check";
                continue;
            }

            if (status == std::cv_status::timeout) {
                if ((std::chrono::steady_clock::now() - start) <
                std::chrono::milliseconds(kVmControlWdtCheckInterval)) {
                    FXL_LOG(INFO) << "false alarm occured. (this might caused by ntpdate)";
                    continue;
                }
                FXL_LOG(INFO) << "detected VM watchdog timeout, try to reboot VM";
                if (dump_state_callback_)
                    dump_state_callback_(1);
                if (stop_callback_) {
                    stop_callback_();
                    FXL_LOG(INFO) << "notify VM controller to reset VM";
                }

                wdt_kick_start_ = false;
            }
        }
    });
}

void Watchdog::notify() {
    FXL_LOG(INFO) << "kick watchdog to prevent VM reset";
    SetPowerState(PowerState::RUNNING);
    wdt_cv_.notify_all();
}

void Watchdog::notify_suspend() {
    FXL_LOG(INFO) << "set watchdog suspend to prevent VM reset";
    SetPowerState(PowerState::SUSPENDED);
    wdt_cv_.notify_all();
}

void Watchdog::notify_resume() {
    FXL_LOG(INFO) << "set watchdog resume to prevent VM reset";
    SetPowerState(PowerState::RUNNING);
    wdt_cv_.notify_all();
}

void Watchdog::kick_start() {
    FXL_LOG(INFO) << "kick watchdog to start";
    wdt_kick_start_ = true;
}
}  // namespace machina