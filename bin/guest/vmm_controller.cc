// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest/vmm_controller.h"

#include <zircon/processargs.h>

VmmController::VmmController(component::ApplicationContext* app_context,
                             async_t* async,
                             std::function<void()> stop_callback)
    : application_context_(app_context),
      async_(async),
      stop_component_callback_(std::move(stop_callback)) {
  FXL_CHECK(stop_component_callback_);
  bindings_.set_empty_set_handler([this]() { LifecycleChannelClosed(); });

  auto server_channel = zx::channel(zx_get_startup_handle(PA_HND(PA_USER0, 0)));
  fidl::InterfaceRequest<GuestLifecycle> request(std::move(server_channel));
  bindings_.AddBinding(this, std::move(request));
}

void VmmController::Create(virtualization::Config config,
                           CreateCallback callback) {
  if (run_callback_) {
    callback(GuestError::ALREADY_RUNNING);
    return;
  }

  if (vmm_)
    vmm_.reset();

  auto vmm = std::make_unique<Vmm>(application_context_);
  if (!vmm) {
    callback(GuestError::OUT_OF_MEMORY);
    return;
  }

  auto status = vmm->Initialize(std::move(config));
  if (status != ZX_OK) {
    callback(GuestError::INITIALIZATION_FAILED);
    return;
  }

  vmm_ = std::move(vmm);
  callback(GuestError::OK);
}

void VmmController::Run(RunCallback callback) {
  if (!vmm_) {
    callback(GuestError::NOT_FOUND);
    return;
  }

  if (run_callback_) {
    callback(GuestError::ALREADY_RUNNING);
    return;
  }

  auto status = vmm_->StartPrimaryVcpu([this](zx_status_t result) {
    GuestError error;

    if (result == ZX_ERR_CANCELED) {
      error = GuestError::REBOOT_REQUIRED;
    } else if (result == ZX_ERR_UNAVAILABLE) {
      error = GuestError::SHUTDOWN;
    } else {
      error = GuestError::INTERNAL_ERROR;
    }
    ScheduleVmmTeardown(error);
  });
  if (status != ZX_OK) {
    vmm_.reset();
    callback(GuestError::START_VCPU_FAILED);
    return;
  }

  run_callback_ = std::move(callback);
}

void VmmController::Bind(fidl::InterfaceRequest<GuestController> request,
                         BindCallback callback) {
  if (vmm_) {
    vmm_->AddBinding(std::move(request));
    callback(GuestError::OK);
  } else {
    callback(GuestError::NOT_FOUND);
  }
}

void VmmController::Stop(StopCallback callback) {
  FXL_LOG(INFO) << "Received Stop request from client";
  ScheduleVmmTeardown(GuestError::FORCE_STOPPED);
  callback();
}

void VmmController::LifecycleChannelClosed() {
  FXL_LOG(INFO) << "A client closed the lifecycle channel, shutting down the "
                   "VMM component";
  stop_component_callback_();
}

void VmmController::ScheduleVmmTeardown(GuestError status) {
  auto result =
      async::PostTask(async_, [this, status]() { DestroyAndRespond(status); });

  // If ZX_OK, the task was successfully scheduled. If ZX_ERR_BAD_STATE, the
  // component is already shutting down so there is nothing to do.
  if (result != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to schedule a VMM teardown, so shutting down "
                        "the component instead: "
                     << status;
    stop_component_callback_();
  }
}

void VmmController::DestroyAndRespond(GuestError status) {
  if (vmm_) {
    vmm_->NotifyClientsShutdown();
  }

  vmm_.reset();

  if (run_callback_) {
    RunCallback callback = std::move(run_callback_);
    callback(status);
  }
}