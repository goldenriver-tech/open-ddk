// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest_manager/guest_manager.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "lib/fxl/strings/string_printf.h"

constexpr zx::duration Guest::kManagementProbeInterval;
constexpr zx::duration Guest::kManagementProbeRetryInterval;
constexpr zx::duration Guest::kManagementProbeTimeout;
constexpr zx::duration Guest::kWatchdogStartupGrace;
constexpr zx::duration Guest::kWatchdogRecoveryGrace;
constexpr uint32_t Guest::kManagementProbeFailureThreshold;

namespace {

constexpr zx::time kNoActiveProbeDeadline = zx::time(0);

}  // namespace

void Guest::ScheduleManagementProbe() {
  if (!lifecycle_.is_bound() || state_ != GuestStatus::RUNNING) {
    return;
  }

  const uint64_t generation = supervision_generation_;
  async::PostDelayedTask(
      async_, [this, generation]() {
        if (generation != supervision_generation_) {
          return;
        }
        IssueManagementProbe(generation);
      },
      consecutive_probe_failures_ > 0 ? kManagementProbeRetryInterval
                                      : kManagementProbeInterval);
}

void Guest::IssueManagementProbe(uint64_t generation) {
  if (generation != supervision_generation_ || !lifecycle_.is_bound() ||
      state_ != GuestStatus::RUNNING || probe_pending_) {
    return;
  }

  probe_pending_ = true;
  active_probe_deadline_ = async::Now(async_) + kManagementProbeTimeout;
  const uint64_t probe_id = ++next_probe_id_;

  virtualization::GuestControllerPtr probe_controller;
  lifecycle_->Bind(
      probe_controller.NewRequest(),
      [this, generation, probe_id](GuestError status) {
        OnManagementProbeResponse(generation, probe_id, status);
      });

  async::PostTaskForTime(
      async_, [this, generation, probe_id]() {
        OnManagementProbeTimeout(generation, probe_id);
      },
      active_probe_deadline_);
}

void Guest::OnManagementProbeResponse(uint64_t generation,
                                      uint64_t probe_id,
                                      GuestError status) {
  if (generation != supervision_generation_ || !probe_pending_ ||
      probe_id != next_probe_id_ || state_ != GuestStatus::RUNNING) {
    return;
  }

  if (async::Now(async_) > active_probe_deadline_) {
    FXL_LOG(WARNING) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                     << " event=management_probe_late_response generation="
                     << generation << " probe_id=" << probe_id;
    OnManagementProbeFailure(
        generation, probe_id,
        fxl::StringPrintf("response arrived after supervision window for guest %s",
                          cfg_.name().c_str()));
    return;
  }

  if (status != GuestError::OK) {
    FXL_LOG(WARNING) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                     << " event=management_probe_non_ok generation="
                     << generation << " probe_id=" << probe_id
                     << " status=" << GuestErrorToString(status);
    OnManagementProbeFailure(
        generation, probe_id,
        fxl::StringPrintf("probe returned non-OK for guest %s: %d",
                          cfg_.name().c_str(), static_cast<int>(status)));
    return;
  }

  probe_pending_ = false;
  active_probe_deadline_ = kNoActiveProbeDeadline;
  consecutive_probe_failures_ = 0;
  ScheduleManagementProbe();
}

void Guest::OnManagementProbeTimeout(uint64_t generation, uint64_t probe_id) {
  if (generation != supervision_generation_ || !probe_pending_ ||
      probe_id != next_probe_id_ || !lifecycle_.is_bound() ||
      state_ != GuestStatus::RUNNING) {
    return;
  }

  OnManagementProbeFailure(
      generation, probe_id,
      fxl::StringPrintf("probe timed out for guest %s", cfg_.name().c_str()));
}

void Guest::OnManagementProbeFailure(uint64_t generation,
                                     uint64_t probe_id,
                                     std::string reason) {
  if (generation != supervision_generation_ || !probe_pending_ ||
      probe_id != next_probe_id_ || state_ != GuestStatus::RUNNING) {
    return;
  }

  probe_pending_ = false;
  active_probe_deadline_ = kNoActiveProbeDeadline;
  ++consecutive_probe_failures_;

  if (consecutive_probe_failures_ < kManagementProbeFailureThreshold) {
    FXL_LOG(WARNING) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                     << " event=management_probe_failure generation="
                     << generation << " probe_id=" << probe_id
                     << " failures=" << consecutive_probe_failures_
                     << " threshold=" << kManagementProbeFailureThreshold
                     << " reason=" << reason;
    ScheduleManagementProbe();
    return;
  }

  FXL_LOG(ERROR) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                 << " event=management_probe_threshold_reached generation="
                 << generation << " probe_id=" << probe_id
                 << " failures=" << consecutive_probe_failures_
                 << " reason=" << reason;
  RequestStackRebuildForSilentHang();
}

void Guest::RequestStackRebuildForSilentHang() {
  if (!app_controller_.is_bound() || relaunch_after_forced_teardown_) {
    return;
  }

  FXL_LOG(ERROR) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                 << " event=silent_hang_rebuild_requested state="
                 << GuestStatusToString(state_);

  relaunch_after_forced_teardown_ = true;
  state_ = GuestStatus::STOPPING;
  ++supervision_generation_;
  ResetManagementSupervision();
  app_controller_->Kill();
}

void Guest::ResetManagementSupervision() {
  probe_pending_ = false;
  active_probe_deadline_ = kNoActiveProbeDeadline;
  consecutive_probe_failures_ = 0;
}

// static
std::unique_ptr<GuestManagerImpl> GuestManagerImpl::BuildFromFile(
    component::ApplicationContext* application_context,
    async_t* async,
    std::string path) {
  auto fd = open(path.c_str(), O_RDONLY);
  FXL_CHECK(fd > 0);

  google::protobuf::io::FileInputStream fin(fd);
  fin.SetCloseOnDelete(true);
  guest_manager::ManagerConfig cfg;
  auto success = google::protobuf::TextFormat::Parse(&fin, &cfg);
  FXL_CHECK(success);

  return Build(application_context, async, cfg);
}

// static
std::unique_ptr<GuestManagerImpl> GuestManagerImpl::Build(
    component::ApplicationContext* application_context,
    async_t* async,
    guest_manager::ManagerConfig cfg) {
  auto manager = std::make_unique<GuestManagerImpl>(application_context, async);
  FXL_CHECK(manager);

  for (auto& guest_cfg : cfg.guest_configs()) {
    auto guest = std::make_unique<Guest>(
        application_context, async, guest_cfg,
        [manager_ptr = manager.get()]() { manager_ptr->SyncWatchdogReporter(); });
    FXL_CHECK(guest);

    FXL_LOG(INFO) << "add new guest vm,id: "<< guest_cfg.vmid() <<",name:"<<guest_cfg.name();
    manager->guests_.emplace(guest_cfg.vmid(), std::move(guest));
  }
  return manager;
}

void GuestManagerImpl::LaunchGuests() {
  SyncWatchdogReporter();
  for (auto& it : guests_) {
    it.second->ScheduleAutoLaunch();
  }
}

bool GuestManagerImpl::AreWatchdogObligationsHealthy() const {
  for (const auto& it : guests_) {
    if (!it.second->is_healthy_for_watchdog()) {
      return false;
    }
  }
  return true;
}

GuestManagerImpl::GuestManagerImpl(component::ApplicationContext* app_context,
                                   async_t* async)
    : application_context_(app_context),
      async_(async),
      reporter_(async, [this]() { return AreWatchdogObligationsHealthy(); }) {
  application_context_->outgoing_services()->AddService<GuestManager>(
      [this](fidl::InterfaceRequest<GuestManager> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void GuestManagerImpl::Launch(int8_t vmid, LaunchCallback callback) {
  auto it = guests_.find(vmid);
  if (it == guests_.end()) {
    callback(GuestError::NOT_FOUND);
    return;
  }

  SyncWatchdogReporter();
  it->second->Launch([callback = std::move(callback)](GuestError error) {
    callback(error);
  });
}

void GuestManagerImpl::Connect(int8_t vmid,
                               fidl::InterfaceRequest<GuestController> request,
                               ConnectCallback callback) {
  auto it = guests_.find(vmid);
  if (it == guests_.end()) {
    callback(GuestError::NOT_FOUND);
    return;
  }

  it->second->Connect(std::move(request), std::move(callback));
}

void GuestManagerImpl::ForceShutdown(int8_t vmid,
                                     ForceShutdownCallback callback) {
  auto it = guests_.find(vmid);
  if (it == guests_.end()) {
    callback();
    return;
  }

  it->second->ForceShutdown(std::move(callback));
}

void GuestManagerImpl::Show(ShowCallback callback) {
  fidl::VectorPtr<virtualization::GuestInfo> info;
  for (const auto& it : guests_) {
    virtualization::GuestInfo guest_info;
    auto cfg = it.second->cfg();
    guest_info.config.vmid = cfg.vmid();
    guest_info.config.default_cfg = cfg.default_cfg();
    guest_info.config.bootloader = cfg.bootloader();
    guest_info.status = it.second->state();
    guest_info.config.log_level = cfg.log_level();
    info.push_back(std::move(guest_info));
  }
  callback(std::move(info));
}

bool GuestManagerImpl::HasWatchdogObligation() const {
  for (const auto& it : guests_) {
    const auto& guest = it.second;
    if (guest->has_watchdog_obligation()) {
      return true;
    }
  }
  return false;
}

void GuestManagerImpl::SyncWatchdogReporter() {
  if (HasWatchdogObligation()) {
    if (!reporter_.running()) {
      zx_status_t status = reporter_.Start();
      if (status != ZX_OK) {
        FXL_LOG(WARNING)
            << "Failed to start guest_manager watchdog reporter: " << status;
      }
    }
    return;
  }

  reporter_.Stop();
}
