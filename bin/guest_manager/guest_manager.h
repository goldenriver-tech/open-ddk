// SPDX-License-Identifier: BSD-3-Clause

#include <fuchsia/cpp/virtualization.h>

#include "garnet/bin/guest_manager/proto/config.pb.h"
#include "garnet/public/lib/app/cpp/application_context.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/nebula/base/macros.h"
#include "lib/nebula/base/thread_annotations.h"

constexpr char kVmmPackagePath[] = "file://sos";

using virtualization::GuestController;
using virtualization::GuestError;
using virtualization::GuestLifecyclePtr;
using virtualization::GuestManager;
using virtualization::GuestStatus;

class Guest {
 public:
  Guest(component::ApplicationContext* application_context,
        async_t* async,
        guest_manager::Config cfg)
      : app_ctx_(application_context),
        async_(async),
        state_(GuestStatus::NOT_STARTED),
        cfg_(std::move(cfg)) {}

  void Launch(GuestManager::LaunchCallback callback) {
    if (is_guest_started()) {
      FXL_LOG(WARNING) << "Guest is already started.";
      callback(GuestError::ALREADY_RUNNING);
      return;
    }

    // if VMM app is not started, start it
    if (!lifecycle_.is_bound()) {
      component::ApplicationLaunchInfo launch_info;

      zx::channel h1, h2;
      FXL_CHECK(ZX_OK == zx::channel::create(0, &h1, &h2));
      lifecycle_.Bind(std::move(h1));

      lifecycle_.set_error_handler([this] {
        FXL_LOG(WARNING) << "VMM component terminated unexpectedly,"
                         << fxl::StringPrintf("vmid: %d", cfg_.vmid());
        state_ = GuestStatus::VMM_UNEXPECTED_TERMINATION;
        OnGuestStopped();
      });

      launch_info.url = kVmmPackagePath;
      launch_info.ta_session_request = std::move(h2);

      int log_level = cfg_.log_level();
      if (log_level < 0) {
        std::string log_level_arg = fxl::StringPrintf("--verbose=%d", -log_level);
        launch_info.arguments.push_back(log_level_arg);
      } else {
        std::string log_level_arg = fxl::StringPrintf("--quite=%d", log_level);
        launch_info.arguments.push_back(log_level_arg);
      }

      FXL_LOG(INFO) << "Launching VMM component for guest: " << cfg_.name();
      app_ctx_->launcher()->CreateApplication(
          std::move(launch_info), app_controller_.NewRequest(async_));
    }

    state_ = GuestStatus::STARTING;
    virtualization::Config cfg;
    cfg.default_cfg = cfg_.default_cfg();
    cfg.bootloader = cfg_.bootloader();
    cfg.memory = cfg_.memory();
    cfg.memory_path = cfg_.memory_path();
    cfg.vmid = cfg_.vmid();
    cfg.log_level = cfg_.log_level();
    lifecycle_->Create(std::move(cfg), [this, callback = std::move(callback)](
                                           GuestError status) {
      this->HandleCreateResult(status, std::move(callback));
    });
  }

  void ForceShutdown(GuestManager::ForceShutdownCallback callback) {
    if (!lifecycle_.is_bound() || !is_guest_started()) {
      // VMM component isn't running.
      callback();
      return;
    }

    state_ = GuestStatus::STOPPING;
    pending_force_shutdowns_.push_back(std::move(callback));
    lifecycle_->Stop([]() {});
  }

  void Connect(fidl::InterfaceRequest<virtualization::GuestController> request,
               GuestManager::ConnectCallback callback) {
    if (!lifecycle_.is_bound() || !is_guest_started()) {
      // VMM component isn't running.
      callback(GuestError::NOT_FOUND);
      return;
    }

    lifecycle_->Bind(std::move(request),
                     [callback = std::move(callback)](GuestError status) {
                       callback(status);
                     });
  }

  void HandleCreateResult(GuestError status,
                          GuestManager::LaunchCallback callback) {
    if (status != GuestError::OK) {
      HandleGuestStopped(status);
    } else {
      state_ = GuestStatus::RUNNING;
      lifecycle_->Run(
          [this](GuestError status) { this->HandleGuestStopped(status); });
      OnGuestLaunched();
    }
    callback(status);
  }

  void HandleGuestStopped(GuestError status) {
    state_ = GuestStatus::STOPPED;

    for (auto& pending_force_shutdown : pending_force_shutdowns_) {
      auto callback = std::move(pending_force_shutdown);
      callback();
    }
    pending_force_shutdowns_.clear();
    OnGuestStopped();

    if (status == GuestError::REBOOT_REQUIRED) {
      Launch([](GuestError) {});
    }
  }

  void OnGuestLaunched() {
    FXL_LOG(INFO) << cfg_.name() << " VM has launched.";
  }

  void OnGuestStopped() {
    FXL_LOG(INFO) << cfg_.name() << " VM has stopped.";
    lifecycle_.Unbind();
  }

  GuestStatus state() const { return state_; }
  guest_manager::Config cfg() const { return cfg_; }

  ~Guest() = default;

  bool is_guest_started() const {
    switch (state_) {
      case GuestStatus::STARTING:
      case GuestStatus::RUNNING:
      case GuestStatus::STOPPING:
        return true;
      default:
        return false;
    }
  }

  bool is_auto_start_enabled() const { return cfg_.auto_start(); }
  uint32_t delayed_launch_ms() const { return cfg_.delayed_launch_ms(); }

 private:
  component::ApplicationContext* const app_ctx_;
  async_t* async_;
  component::ApplicationControllerPtr app_controller_;
  GuestLifecyclePtr lifecycle_;
  std::vector<GuestManager::ForceShutdownCallback> pending_force_shutdowns_;
  GuestStatus state_;
  guest_manager::Config cfg_;
};

class GuestManagerImpl : public GuestManager {
 public:
  static std::unique_ptr<GuestManagerImpl> BuildFromFile(
      component::ApplicationContext* application_context,
      async_t* async,
      std::string path);

  GuestManagerImpl(component::ApplicationContext* app_context, async_t* async);

  void LaunchGuests();

  // |virtualization::GuestManager|
  void Launch(int8_t vmid, LaunchCallback callback) override;
  void Connect(int8_t vmid,
               fidl::InterfaceRequest<GuestController> request,
               ConnectCallback) override;
  void ForceShutdown(int8_t vmid, ForceShutdownCallback callback) override;
  void Show(ShowCallback callback) override;

 private:
  component::ApplicationContext* const application_context_;
  async_t* const async_ __UNUSED;

  fidl::BindingSet<GuestManager> bindings_;

  std::unordered_map<int8_t, std::unique_ptr<Guest>> guests_;

  static std::unique_ptr<GuestManagerImpl> Build(
      component::ApplicationContext* application_context,
      async_t* async,
      guest_manager::ManagerConfig cfg);
};