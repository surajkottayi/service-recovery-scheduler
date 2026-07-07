// Unit tests for lib_srs::CRecoverySchedulerStubImpl.
//
// Only the parts that don't require a live CommonAPI/D-Bus session are covered
// here:
//   * setCallbacks + method delegation (registerService, unregisterService,
//     reportServiceState) with a null ClientId
//   * enum translation between the generated interface and lib_srs
//   * extractDbusUniqueName on a null client
//
// The D-Bus-facing pieces (queryUnixPidForBusName, handleNameOwnerChanged, run,
// notifyStateChanged fireEvent) are intentionally NOT exercised — they need a
// real DBusProxyConnection which is out of scope for unit testing.

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CRecoverySchedulerStubImpl.hpp"
#include "Common.hpp"

using lib_srs::CRecoverySchedulerStubImpl;
using lib_srs::GeneratedIface;
using lib_srs::RecoveryState;

namespace
{
    class StubTest : public ::testing::Test
    {
    protected:
        std::shared_ptr<CRecoverySchedulerStubImpl> stub;

        // Captured arguments from whichever callback fired.
        std::string capturedName;
        std::vector<RecoveryState> capturedActions;
        int capturedInterval{0};
        RecoveryState capturedCurrent{RecoveryState::UNKNOWN};
        RecoveryState capturedLast{RecoveryState::UNKNOWN};

        void SetUp() override
        {
            stub = std::make_shared<CRecoverySchedulerStubImpl>();
        }

        void installAllCallbacks(bool registerReturn, bool unregisterReturn, bool reportReturn)
        {
            stub->setCallbacks(
                [this, registerReturn](const std::string &name,
                                       const std::vector<RecoveryState> &actions,
                                       int interval, const pid_t &)
                {
                    capturedName = name;
                    capturedActions = actions;
                    capturedInterval = interval;
                    return registerReturn;
                },
                [this, unregisterReturn](const std::string &name)
                {
                    capturedName = name;
                    return unregisterReturn;
                },
                [this](const std::string &name)
                { capturedName = name; },
                [this, reportReturn](const std::string &name, RecoveryState current, RecoveryState last)
                {
                    capturedName = name;
                    capturedCurrent = current;
                    capturedLast = last;
                    return reportReturn;
                });
        }
    };
} // namespace

// -------------------- extractDbusUniqueName --------------------

TEST_F(StubTest, ExtractDbusUniqueNameOnNullClientReturnsEmpty)
{
    EXPECT_EQ(stub->extractDbusUniqueName(nullptr), std::string());
}

// -------------------- reportServiceState --------------------

TEST_F(StubTest, ReportServiceStateForwardsAndRepliesOkOnCallbackTrue)
{
    installAllCallbacks(true, true, true);

    GeneratedIface::QueryResult replied;
    stub->reportServiceState(nullptr, "AppA",
                             GeneratedIface::RecoveryState::CRASHED,
                             GeneratedIface::RecoveryState::RESTART,
                             [&](GeneratedIface::QueryResult r)
                             { replied = r; });

    EXPECT_EQ(capturedName, "AppA");
    EXPECT_EQ(capturedCurrent, RecoveryState::CRASHED);
    EXPECT_EQ(capturedLast, RecoveryState::RESTART);
    EXPECT_TRUE(replied == GeneratedIface::QueryResult::OK);
}

TEST_F(StubTest, ReportServiceStateRepliesNotFoundOnCallbackFalse)
{
    installAllCallbacks(true, true, false);

    GeneratedIface::QueryResult replied;
    stub->reportServiceState(nullptr, "AppB",
                             GeneratedIface::RecoveryState::STOP,
                             GeneratedIface::RecoveryState::UNKNOWN,
                             [&](GeneratedIface::QueryResult r)
                             { replied = r; });

    EXPECT_TRUE(replied == GeneratedIface::QueryResult::NOT_FOUND);
    EXPECT_EQ(capturedLast, RecoveryState::UNKNOWN);
}

TEST_F(StubTest, ReportServiceStateWithoutCallbackRepliesNotFound)
{
    GeneratedIface::QueryResult replied;
    stub->reportServiceState(nullptr, "AppC",
                             GeneratedIface::RecoveryState::RESTART,
                             GeneratedIface::RecoveryState::UNKNOWN,
                             [&](GeneratedIface::QueryResult r)
                             { replied = r; });

    EXPECT_TRUE(replied == GeneratedIface::QueryResult::NOT_FOUND);
    EXPECT_TRUE(capturedName.empty());
}

// -------------------- unregisterService --------------------

TEST_F(StubTest, UnregisterServiceRepliesOkOnCallbackTrue)
{
    installAllCallbacks(true, true, true);

    GeneratedIface::UnregisterResult replied;
    stub->unregisterService(nullptr, "AppD",
                            [&](GeneratedIface::UnregisterResult r)
                            { replied = r; });

    EXPECT_EQ(capturedName, "AppD");
    EXPECT_TRUE(replied == GeneratedIface::UnregisterResult::OK);
}

TEST_F(StubTest, UnregisterServiceRepliesNotFoundOnCallbackFalse)
{
    installAllCallbacks(true, false, true);

    GeneratedIface::UnregisterResult replied;
    stub->unregisterService(nullptr, "AppE",
                            [&](GeneratedIface::UnregisterResult r)
                            { replied = r; });

    EXPECT_TRUE(replied == GeneratedIface::UnregisterResult::NOT_FOUND);
}

TEST_F(StubTest, UnregisterServiceWithoutCallbackRepliesNotFound)
{
    GeneratedIface::UnregisterResult replied;
    stub->unregisterService(nullptr, "AppF",
                            [&](GeneratedIface::UnregisterResult r)
                            { replied = r; });

    EXPECT_TRUE(replied == GeneratedIface::UnregisterResult::NOT_FOUND);
}

// -------------------- registerService --------------------

TEST_F(StubTest, RegisterServiceTranslatesEnumsAndRepliesOk)
{
    installAllCallbacks(true, true, true);

    std::vector<GeneratedIface::RecoveryState> wire{
        GeneratedIface::RecoveryState::RESTART,
        GeneratedIface::RecoveryState::STOP,
        GeneratedIface::RecoveryState::DISABLE};

    GeneratedIface::RegisterResult replied;
    stub->registerService(nullptr, "AppG", wire, 42,
                          [&](GeneratedIface::RegisterResult r)
                          { replied = r; });

    EXPECT_EQ(capturedName, "AppG");
    EXPECT_EQ(capturedInterval, 42);
    ASSERT_EQ(capturedActions.size(), wire.size());
    EXPECT_EQ(capturedActions[0], RecoveryState::RESTART);
    EXPECT_EQ(capturedActions[1], RecoveryState::STOP);
    EXPECT_EQ(capturedActions[2], RecoveryState::DISABLE);
    EXPECT_TRUE(replied == GeneratedIface::RegisterResult::OK);
}

TEST_F(StubTest, RegisterServiceRepliesInvalidNameOnCallbackFalse)
{
    installAllCallbacks(false, true, true);

    GeneratedIface::RegisterResult replied;
    stub->registerService(nullptr, "AppH", {}, -1,
                          [&](GeneratedIface::RegisterResult r)
                          { replied = r; });

    EXPECT_TRUE(replied == GeneratedIface::RegisterResult::INVALID_NAME);
}

TEST_F(StubTest, RegisterServiceWithoutCallbackRepliesInvalidName)
{
    GeneratedIface::RegisterResult replied;
    stub->registerService(nullptr, "AppI", {}, -1,
                          [&](GeneratedIface::RegisterResult r)
                          { replied = r; });

    EXPECT_TRUE(replied == GeneratedIface::RegisterResult::INVALID_NAME);
    EXPECT_TRUE(capturedName.empty());
}
