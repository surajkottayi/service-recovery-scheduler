// Unit tests for lib_srs/inc/Common.hpp helpers.

#include <sstream>

#include <gtest/gtest.h>

#include "Common.hpp"

using lib_srs::RecoveryState;
using lib_srs::toString;

TEST(CommonRecoveryStateTest, ToStringCoversAllEnumerators)
{
    EXPECT_EQ(toString(RecoveryState::RESTART), "RESTART");
    EXPECT_EQ(toString(RecoveryState::STOP), "STOP");
    EXPECT_EQ(toString(RecoveryState::DISABLE), "DISABLE");
    EXPECT_EQ(toString(RecoveryState::CRASHED), "CRASHED");
    EXPECT_EQ(toString(RecoveryState::UNKNOWN), "UNKNOWN");
}

TEST(CommonRecoveryStateTest, StreamInsertionDelegatesToToString)
{
    std::ostringstream oss;
    oss << RecoveryState::RESTART << ',' << RecoveryState::CRASHED;
    EXPECT_EQ(oss.str(), "RESTART,CRASHED");
}

TEST(CommonServiceNamesTest, ContainsExpectedApps)
{
    EXPECT_EQ(lib_srs::g_MapServiceNames.at(lib_srs::ServiceId::APP_A), "AppA");
    EXPECT_EQ(lib_srs::g_MapServiceNames.at(lib_srs::ServiceId::APP_B), "AppB");
    EXPECT_EQ(lib_srs::g_MapServiceNames.at(lib_srs::ServiceId::APP_T), "AppT");
}
