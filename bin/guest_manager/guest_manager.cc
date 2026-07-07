// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest_manager/guest_manager.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

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
    auto guest = std::make_unique<Guest>(application_context, async, guest_cfg);
    FXL_CHECK(guest);

    FXL_LOG(INFO) << "add new guest vm,id: "<< guest_cfg.vmid() <<",name:"<<guest_cfg.name();
    manager->guests_.emplace(guest_cfg.vmid(), std::move(guest));
  }
  return manager;
}

void GuestManagerImpl::LaunchGuests() {
  for (auto& it : guests_) {
    auto& guest = it.second;

    if (guest->is_auto_start_enabled()) {
      auto delay_ms = guest->delayed_launch_ms();
      if (delay_ms)
        async::PostDelayedTask(
            async_get_default(), [&guest]() { guest->Launch([](GuestError) {}); },
            zx::msec(delay_ms));
      else
        guest->Launch([](GuestError) {});
    }
  }
}

GuestManagerImpl::GuestManagerImpl(component::ApplicationContext* app_context,
                                   async_t* async)
    : application_context_(app_context), async_(async) {
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

  it->second->ForceShutdown([] {});
  callback();
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