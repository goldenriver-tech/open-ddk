// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "garnet/bin/guest/vmm.h"
#include "garnet/bin/guest/vmm_controller_teardown.h"

#include <fuchsia/cpp/machina.h>
#include <fuchsia/cpp/virtualization.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <functional>

using virtualization::GuestController;
using virtualization::GuestLifecycle;
using virtualization::GuestError;

class VmmController : public GuestLifecycle {
 public:
  VmmController(component::ApplicationContext* app_context,
                async_t* async,
                std::function<void()> stop_callback);
  ~VmmController() override = default;

  void AddBinding(fidl::InterfaceRequest<GuestLifecycle> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  void LifecycleChannelClosed();

  // |virtualization::GuestLifecycle|
  void Create(virtualization::Config config, CreateCallback callback) override;
  void Run(RunCallback callback) override;
  void Bind(fidl::InterfaceRequest<GuestController> request,
            BindCallback callback) override;
  void Stop(StopCallback callback) override;

  void ScheduleVmmTeardown(GuestError status);
  void DestroyAndRespond(GuestError status);
  void FinishVmmTeardown(GuestError status);

 private:
  void MaybeStopComponent();

  component::ApplicationContext* application_context_;
  async_t* async_;

  std::unique_ptr<Vmm> vmm_;

  fidl::BindingSet<GuestLifecycle> bindings_;

  RunCallback run_callback_;
  std::function<void()> stop_component_callback_;
  VmmTeardownLifecycle teardown_;
};
