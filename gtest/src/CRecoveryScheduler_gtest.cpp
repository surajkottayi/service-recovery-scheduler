// Unit tests for lib_srs::CRecoveryScheduler.
//
// CRecoveryScheduler is a process-wide singleton, so every test cleans up any
// service names it registered in TearDown to keep other tests isolated.
// init() and run() are intentionally NOT invoked here — they open a CommonAPI
// D-Bus connection which is out of scope for unit testing.

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CRecoveryScheduler.hpp"
#include "Common.hpp"

using lib_srs::CRecoveryScheduler;
using lib_srs::RecoveryState;
using lib_srs::SServiceSnapshot;

namespace
{
    class SchedulerTest : public ::testing::Test
    {
    protected:
        std::shared_ptr<CRecoveryScheduler> scheduler;
        std::vector<std::string> registered;

        void SetUp() override
        {
            scheduler = CRecoveryScheduler::getInstance();
        }

        void TearDown() override
        {
            for (const auto &name : registered)
            {
                scheduler->onUnregisterService(name);
            }
            registered.clear();
        }

        // Convenience wrapper that also records the name for TearDown cleanup.
        bool doRegister(const std::string &name,
                        const std::vector<RecoveryState> &actions = {},
                        int interval = -1)
        {
            const bool ok = scheduler->onRegisterService(name, actions, interval, -1);
            if (ok)
            {
                registered.push_back(name);
            }
            return ok;
        }
    };
} // namespace

// -------------------- getInstance --------------------

TEST_F(SchedulerTest, GetInstanceReturnsSameSingleton)
{
    auto a = CRecoveryScheduler::getInstance();
    auto b = CRecoveryScheduler::getInstance();
    EXPECT_EQ(a.get(), b.get());
    EXPECT_EQ(a.get(), scheduler.get());
}

// -------------------- onRegisterService --------------------

TEST_F(SchedulerTest, RegisterValidNameSucceedsAndIsQueryable)
{
    ASSERT_TRUE(doRegister("AppA", {RecoveryState::RESTART}));
    const auto snap = scheduler->getServiceState("AppA");
    EXPECT_TRUE(snap.found);
    EXPECT_TRUE(snap.isOnline);
    EXPECT_EQ(snap.attemptCount, 0);
    EXPECT_EQ(snap.currentAction, RecoveryState::UNKNOWN);
    EXPECT_EQ(snap.lastAction, RecoveryState::UNKNOWN);
    EXPECT_EQ(snap.nextAction, RecoveryState::RESTART);
}

TEST_F(SchedulerTest, RegisterInvalidNameFails)
{
    EXPECT_FALSE(scheduler->onRegisterService("NotInMap", {}, -1, -1));
    EXPECT_FALSE(scheduler->getServiceState("NotInMap").found);
}

TEST_F(SchedulerTest, RegisterEmptyActionsUsesDefaults)
{
    ASSERT_TRUE(doRegister("AppB", {}));
    // Default sequence is {RESTART, RESTART, STOP, DISABLE}; next action index
    // after registration (before any failure) is 0 → RESTART.
    EXPECT_EQ(scheduler->getServiceState("AppB").nextAction, RecoveryState::RESTART);
}

TEST_F(SchedulerTest, ReRegisterAfterCrashMarksOnlineWithoutLosingCounters)
{
    ASSERT_TRUE(doRegister("AppC", {RecoveryState::RESTART, RecoveryState::STOP}));
    scheduler->onServiceFailure("AppC");
    ASSERT_FALSE(scheduler->getServiceState("AppC").isOnline);

    // Second register call is treated as "peer back online".
    EXPECT_TRUE(scheduler->onRegisterService("AppC", {}, -1, -1));
    const auto snap = scheduler->getServiceState("AppC");
    EXPECT_TRUE(snap.isOnline);
    EXPECT_EQ(snap.attemptCount, 1); // crashCount preserved across re-register
    EXPECT_EQ(snap.lastAction, RecoveryState::CRASHED);
}

// -------------------- onUnregisterService --------------------

TEST_F(SchedulerTest, UnregisterKnownServiceSucceeds)
{
    ASSERT_TRUE(doRegister("AppD"));
    // Registered by fixture — pull it back out so we don't double-unregister.
    registered.clear();

    EXPECT_TRUE(scheduler->onUnregisterService("AppD"));
    EXPECT_FALSE(scheduler->getServiceState("AppD").found);
}

TEST_F(SchedulerTest, UnregisterUnknownServiceFails)
{
    EXPECT_FALSE(scheduler->onUnregisterService("AppE"));
}

// -------------------- onReportServiceState --------------------

TEST_F(SchedulerTest, ReportStateUpdatesCurrentAndLast)
{
    ASSERT_TRUE(doRegister("AppF", {RecoveryState::RESTART}));
    EXPECT_TRUE(scheduler->onReportServiceState("AppF", RecoveryState::STOP, RecoveryState::RESTART));

    const auto snap = scheduler->getServiceState("AppF");
    EXPECT_EQ(snap.currentAction, RecoveryState::STOP);
    EXPECT_EQ(snap.lastAction, RecoveryState::RESTART);
}

TEST_F(SchedulerTest, ReportStateWithUnknownLastPreservesSchedulerLastAction)
{
    ASSERT_TRUE(doRegister("AppG", {RecoveryState::RESTART}));
    // Simulate a crash so the scheduler records lastAction = CRASHED.
    scheduler->onServiceFailure("AppG");
    ASSERT_EQ(scheduler->getServiceState("AppG").lastAction, RecoveryState::CRASHED);

    // App pushes an update but doesn't know its previous action.
    EXPECT_TRUE(scheduler->onReportServiceState("AppG", RecoveryState::RESTART, RecoveryState::UNKNOWN));
    const auto snap = scheduler->getServiceState("AppG");
    EXPECT_EQ(snap.currentAction, RecoveryState::RESTART);
    EXPECT_EQ(snap.lastAction, RecoveryState::CRASHED) << "UNKNOWN must not clobber tracked lastAction";
}

TEST_F(SchedulerTest, ReportStateForUnknownServiceFails)
{
    EXPECT_FALSE(scheduler->onReportServiceState("AppH", RecoveryState::RESTART, RecoveryState::UNKNOWN));
}

// -------------------- onServiceFailure --------------------

TEST_F(SchedulerTest, FailureForUnknownServiceIsNoop)
{
    // Must not throw and must not spuriously register anything.
    scheduler->onServiceFailure("AppI");
    EXPECT_FALSE(scheduler->getServiceState("AppI").found);
}

TEST_F(SchedulerTest, FailureMarksOfflineAndRecordsCrashed)
{
    ASSERT_TRUE(doRegister("AppJ", {RecoveryState::RESTART, RecoveryState::STOP}));
    scheduler->onServiceFailure("AppJ");

    const auto snap = scheduler->getServiceState("AppJ");
    EXPECT_FALSE(snap.isOnline);
    EXPECT_EQ(snap.lastAction, RecoveryState::CRASHED);
    EXPECT_EQ(snap.currentAction, RecoveryState::RESTART);
    EXPECT_EQ(snap.nextAction, RecoveryState::STOP);
    EXPECT_EQ(snap.attemptCount, 1);
}

TEST_F(SchedulerTest, FailureRotatesActionsAndWrapsAround)
{
    ASSERT_TRUE(doRegister("AppK", {RecoveryState::RESTART, RecoveryState::STOP}));

    scheduler->onServiceFailure("AppK"); // index 0 -> RESTART
    EXPECT_EQ(scheduler->getServiceState("AppK").currentAction, RecoveryState::RESTART);
    EXPECT_EQ(scheduler->getServiceState("AppK").nextAction, RecoveryState::STOP);

    scheduler->onServiceFailure("AppK"); // index 1 -> STOP
    EXPECT_EQ(scheduler->getServiceState("AppK").currentAction, RecoveryState::STOP);
    // With only 2 actions, next wraps back to index 0.
    EXPECT_EQ(scheduler->getServiceState("AppK").nextAction, RecoveryState::RESTART);

    scheduler->onServiceFailure("AppK"); // wraps to index 0 -> RESTART
    EXPECT_EQ(scheduler->getServiceState("AppK").currentAction, RecoveryState::RESTART);
}

TEST_F(SchedulerTest, CrashCountIsMonotonic)
{
    ASSERT_TRUE(doRegister("AppL", {RecoveryState::RESTART}));

    for (int i = 1; i <= 5; ++i)
    {
        scheduler->onServiceFailure("AppL");
        EXPECT_EQ(scheduler->getServiceState("AppL").attemptCount, i);
    }
}

// -------------------- getServiceState --------------------

TEST_F(SchedulerTest, GetServiceStateForUnknownServiceReturnsNotFound)
{
    const auto snap = scheduler->getServiceState("AppM");
    EXPECT_FALSE(snap.found);
    EXPECT_FALSE(snap.isOnline);
    EXPECT_EQ(snap.currentAction, RecoveryState::UNKNOWN);
    EXPECT_EQ(snap.lastAction, RecoveryState::UNKNOWN);
    EXPECT_EQ(snap.nextAction, RecoveryState::UNKNOWN);
    EXPECT_EQ(snap.attemptCount, 0);
}

TEST_F(SchedulerTest, NextActionReflectsFullSequence)
{
    ASSERT_TRUE(doRegister("AppN",
                           {RecoveryState::RESTART,
                            RecoveryState::RESTART,
                            RecoveryState::STOP,
                            RecoveryState::DISABLE}));
    // Before any failure: next = first action.
    EXPECT_EQ(scheduler->getServiceState("AppN").nextAction, RecoveryState::RESTART);

    scheduler->onServiceFailure("AppN"); // idx 0 -> RESTART; next = RESTART
    EXPECT_EQ(scheduler->getServiceState("AppN").nextAction, RecoveryState::RESTART);

    scheduler->onServiceFailure("AppN"); // idx 1 -> RESTART; next = STOP
    EXPECT_EQ(scheduler->getServiceState("AppN").nextAction, RecoveryState::STOP);

    scheduler->onServiceFailure("AppN"); // idx 2 -> STOP; next = DISABLE
    EXPECT_EQ(scheduler->getServiceState("AppN").nextAction, RecoveryState::DISABLE);

    scheduler->onServiceFailure("AppN"); // idx 3 -> DISABLE; next wraps to RESTART
    EXPECT_EQ(scheduler->getServiceState("AppN").nextAction, RecoveryState::RESTART);
}
