// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

using DumpStateCallback = std::function<void(uint32_t reason)>;
using StopCallback = std::function<void(void)>;
namespace machina {
class Watchdog {
 public:
  Watchdog() = default;;
  ~Watchdog();
  void init(int32_t vmid);
  void notify();
  void notify_suspend();
  void notify_resume();
  void kick_start();
  void ReigsterDumpStateCallback(DumpStateCallback callback) {
    dump_state_callback_ = std::move(callback);
  }
  void ReigsterStopCallback(StopCallback callback) {
    stop_callback_ = std::move(callback);
  }
  DumpStateCallback dump_state_callback_;

  enum class PowerState : uint8_t {
    RUNNING,
    SUSPENDING,
    SUSPENDED,
    RESUMING,
  };

  void SetPowerState(PowerState newState) {
    power_state_.store(static_cast<uint8_t>(newState));
  }

  PowerState GetPowerState() const {
    uint8_t value = power_state_.load();
    return static_cast<PowerState>(value);
  }

 private:
  zx::resource root_resource;
  std::thread wdt_thread_;
  std::condition_variable wdt_cv_;
  std::mutex mutex_;
  bool wdt_kick_start_ = false;
  bool stop_thread_ = false;
  StopCallback stop_callback_;
  std::atomic<uint8_t> power_state_;
};
}  // namespace machina
