#include <gtest/gtest.h>

#include "ClimatologyThreadGuard.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

class ClimatologyThreadGuardTests : public ::testing::Test {
protected:
  void SetUp() override { ClimatologyThreadGuard::Reset(); }
};

TEST_F(ClimatologyThreadGuardTests, MainThreadMayInitializeAnyService) {
  EXPECT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Wind,
                                                true));
  EXPECT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                true));
}

TEST_F(ClimatologyThreadGuardTests, WorkerIsBlockedUntilServicePrepared) {
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                 false));
  ClimatologyThreadGuard::MarkPrepared(ClimatologyService::Current);
  EXPECT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                false));
}

TEST_F(ClimatologyThreadGuardTests, PreparationIsIsolatedPerService) {
  ClimatologyThreadGuard::MarkPrepared(ClimatologyService::Wind);
  EXPECT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Wind,
                                                false));
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                 false));
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(
      ClimatologyService::WindAtlas, false));
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(
      ClimatologyService::CycloneTracks, false));
}

TEST_F(ClimatologyThreadGuardTests, BlockDiagnosticIsLoggedOncePerService) {
  EXPECT_TRUE(
      ClimatologyThreadGuard::ShouldLogBlocked(ClimatologyService::Current));
  EXPECT_FALSE(
      ClimatologyThreadGuard::ShouldLogBlocked(ClimatologyService::Current));
  EXPECT_TRUE(
      ClimatologyThreadGuard::ShouldLogBlocked(ClimatologyService::Wind));
}

TEST_F(ClimatologyThreadGuardTests, ResetRevokesWorkerAccess) {
  ClimatologyThreadGuard::MarkPrepared(ClimatologyService::Current);
  ASSERT_TRUE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                false));
  ClimatologyThreadGuard::Reset();
  EXPECT_FALSE(ClimatologyThreadGuard::CanInvoke(ClimatologyService::Current,
                                                 false));
}

TEST_F(ClimatologyThreadGuardTests,
       LegacyProviderInvocationsAreSerializedAcrossWorkers) {
  std::atomic<int> active{0};
  std::atomic<int> maximumActive{0};
  auto invoke = [&] {
    std::lock_guard<std::recursive_mutex> lock(
        ClimatologyThreadGuard::InvocationMutex());
    const int now = active.fetch_add(1) + 1;
    int observed = maximumActive.load();
    while (observed < now &&
           !maximumActive.compare_exchange_weak(observed, now)) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    active.fetch_sub(1);
  };

  std::vector<std::future<void>> workers;
  for (int i = 0; i < 8; ++i)
    workers.push_back(std::async(std::launch::async, invoke));
  for (auto& worker : workers) worker.get();

  EXPECT_EQ(maximumActive.load(), 1);
}
