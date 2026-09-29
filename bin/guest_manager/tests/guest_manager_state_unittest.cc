// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest_manager/guest_manager.h"

#include <vector>

#include "gtest/gtest.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/fidl/cpp/binding.h"

namespace {

guest_manager::Config MakeConfig(bool auto_start,
                                 int32_t delayed_launch_ms = 0) {
  guest_manager::Config cfg;
  cfg.set_vmid(1);
  cfg.set_name("test");
  cfg.set_auto_start(auto_start);
  cfg.set_delayed_launch_ms(delayed_launch_ms);
  return cfg;
}

std::unique_ptr<Guest> MakeGuest(async_t* async,
                                 guest_manager::Config cfg) {
  return std::make_unique<Guest>(nullptr, async, std::move(cfg));
}

class TestGuestLifecycle : public virtualization::GuestLifecycle {
 public:
  explicit TestGuestLifecycle(async_t* async) : async_(async), binding_(this) {}

  zx_status_t Bind(zx::channel channel) {
    return binding_.Bind(
        fidl::InterfaceRequest<virtualization::GuestLifecycle>(
            std::move(channel)),
        async_);
  }

  void Create(virtualization::Config, CreateCallback callback) override {
    callback(GuestError::OK);
  }

  void Bind(fidl::InterfaceRequest<virtualization::GuestController>,
            BindCallback callback) override {
    callback(GuestError::OK);
  }

  void Run(RunCallback) override {}

  void Stop(StopCallback callback) override {
    // Keep Stop pending; ForceShutdown completion is driven by guest teardown.
    (void)callback;
    ++stop_count_;
  }

  size_t stop_count() const { return stop_count_; }

 private:
  async_t* const async_;
  fidl::Binding<virtualization::GuestLifecycle> binding_;
  size_t stop_count_ = 0;
};

void BindLifecycle(Guest* guest,
                   TestGuestLifecycle* lifecycle,
                   async_t* async) {
  zx::channel client;
  zx::channel server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &client, &server));
  ASSERT_EQ(ZX_OK, guest->lifecycle_.Bind(std::move(client), async));
  ASSERT_EQ(ZX_OK, lifecycle->Bind(std::move(server)));
}

bool AreGuestsHealthy(const std::vector<Guest*>& guests) {
  for (const auto& guest : guests) {
    if (!guest->is_healthy_for_watchdog()) {
      return false;
    }
  }
  return true;
}

class GuestWatchdogStateTest : public ::testing::Test {
 protected:
  GuestWatchdogStateTest() : loop_(&kAsyncLoopConfigMakeDefault) {}

  async_t* async() { return loop_.async(); }

  async::Loop loop_;
};

TEST_F(GuestWatchdogStateTest,
       AutoStartGuestHasStartupWatchdogGraceByDefault) {
  auto guest = MakeGuest(async(), MakeConfig(true));

  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());

  guest->watchdog_grace_deadline_ = async::Now(async()) - zx::sec(1);
  EXPECT_FALSE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       ManualGuestDoesNotArmBeforeLaunch) {
  auto guest = MakeGuest(async(), MakeConfig(false));

  EXPECT_FALSE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       WatchdogObligationStaysActiveAcrossRelaunchPending) {
  auto guest = MakeGuest(async(), MakeConfig(false));

  guest->watchdog_obligation_active_ = true;
  guest->state_ = GuestStatus::RUNNING;
  guest->HandleGuestStopped(GuestError::INTERNAL_ERROR);

  EXPECT_EQ(GuestStatus::STOPPED, guest->state());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());

  guest->watchdog_grace_deadline_ = async::Now(async()) - zx::sec(1);
  EXPECT_FALSE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       ManualStopCancelsRelaunchPendingWatchdogObligation) {
  auto guest = MakeGuest(async(), MakeConfig(false));

  guest->watchdog_obligation_active_ = true;
  guest->state_ = GuestStatus::RUNNING;
  guest->HandleGuestStopped(GuestError::INTERNAL_ERROR);
  const auto pending_relaunch_generation = guest->supervision_generation_;

  bool callback_called = false;
  guest->ForceShutdown([&callback_called] { callback_called = true; });

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(GuestStatus::STOPPED, guest->state());
  EXPECT_FALSE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
  EXPECT_EQ(zx::time(0), guest->watchdog_grace_deadline_);
  EXPECT_NE(pending_relaunch_generation, guest->supervision_generation_);
}

TEST_F(GuestWatchdogStateTest,
       ManualStopSuppressesWatchdogObligation) {
  auto guest = MakeGuest(async(), MakeConfig(true));

  ASSERT_TRUE(guest->has_watchdog_obligation());
  guest->state_ = GuestStatus::STOPPING;
  guest->manual_stop_requested_ = true;
  guest->watchdog_obligation_active_ = false;
  guest->ClearWatchdogGrace();
  guest->HandleGuestStopped(GuestError::SHUTDOWN);

  EXPECT_EQ(GuestStatus::STOPPED, guest->state());
  EXPECT_FALSE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       AutoStartGuestWithLaunchDelayIsHealthyDuringStartupGrace) {
  auto guest = MakeGuest(async(), MakeConfig(true, 2000));

  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       ManualStopCancelsPendingDelayedLaunchWatchdogObligation) {
  auto guest = MakeGuest(async(), MakeConfig(true, 2000));

  guest->ScheduleAutoLaunch();
  const auto pending_launch_generation = guest->launch_generation_;

  bool callback_called = false;
  guest->ForceShutdown([&callback_called] { callback_called = true; });

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(GuestStatus::NOT_STARTED, guest->state());
  EXPECT_FALSE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
  EXPECT_EQ(zx::time(0), guest->watchdog_grace_deadline_);
  EXPECT_NE(pending_launch_generation, guest->launch_generation_);

  guest->LaunchIfCurrent(pending_launch_generation);
  EXPECT_EQ(GuestStatus::NOT_STARTED, guest->state());
  EXPECT_FALSE(guest->has_watchdog_obligation());
}

TEST_F(GuestWatchdogStateTest,
       RepeatedForceShutdownWhileStoppingQueuesCallbacksUntilLifecycleClose) {
  auto guest = MakeGuest(async(), MakeConfig(false));
  TestGuestLifecycle lifecycle(async());
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));

  guest->state_ = GuestStatus::RUNNING;

  bool first_callback_called = false;
  bool second_callback_called = false;
  guest->ForceShutdown(
      [&first_callback_called] { first_callback_called = true; });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());

  EXPECT_EQ(GuestStatus::STOPPING, guest->state());
  EXPECT_EQ(1u, lifecycle.stop_count());
  EXPECT_FALSE(first_callback_called);

  guest->ForceShutdown(
      [&second_callback_called] { second_callback_called = true; });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());

  EXPECT_EQ(GuestStatus::STOPPING, guest->state());
  EXPECT_EQ(1u, lifecycle.stop_count());
  EXPECT_FALSE(first_callback_called);
  EXPECT_FALSE(second_callback_called);

  guest->HandleLifecycleClosed();

  EXPECT_EQ(GuestStatus::STOPPED, guest->state());
  EXPECT_TRUE(first_callback_called);
  EXPECT_TRUE(second_callback_called);
}

TEST_F(GuestWatchdogStateTest,
       MultiGuestHealthOnlyFailsAfterActiveObligationGraceExpires) {
  auto required = MakeGuest(async(), MakeConfig(true));
  auto idle_manual = MakeGuest(async(), MakeConfig(false));

  EXPECT_TRUE(AreGuestsHealthy({required.get(), idle_manual.get()}));

  required->watchdog_grace_deadline_ = async::Now(async()) - zx::sec(1);
  EXPECT_FALSE(AreGuestsHealthy({required.get(), idle_manual.get()}));
}

}  // namespace
