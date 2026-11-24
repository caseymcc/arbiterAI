#include "processEnd.h"

#include <gtest/gtest.h>

namespace
{

using arbiterAI::server::EndAction;
using arbiterAI::server::ProcessEnd;

TEST(ProcessEndTest, AStopDuringAnUpdateRestartExitsInsteadOfRestarting)
{
    ProcessEnd end;
    EXPECT_EQ(end.afterRestartTimeout(true), EndAction::Exit);
}

TEST(ProcessEndTest, AnUpdateRestartWithoutAStopRestarts)
{
    ProcessEnd end;
    EXPECT_EQ(end.afterRestartTimeout(false), EndAction::Restart);
}

TEST(ProcessEndTest, TheStopHandlersExitLeavesTheOthersNothingToDo)
{
    ProcessEnd end;
    EXPECT_EQ(end.afterStopTimeout(), EndAction::Exit);
    EXPECT_EQ(end.afterRestartTimeout(false), EndAction::None);
    EXPECT_EQ(end.afterListen(true, false), EndAction::None);
}

TEST(ProcessEndTest, TheRestartWorkerLeavesTheStopHandlerAndMainNothingToDo)
{
    ProcessEnd end;
    EXPECT_EQ(end.afterRestartTimeout(false), EndAction::Restart);
    EXPECT_EQ(end.afterStopTimeout(), EndAction::None);
    EXPECT_EQ(end.afterListen(true, false), EndAction::None);
}

TEST(ProcessEndTest, MainRestartsUnlessAStopCameIn)
{
    ProcessEnd restart;
    EXPECT_EQ(restart.afterListen(true, false), EndAction::Restart);
    ProcessEnd stopped;
    EXPECT_EQ(stopped.afterListen(true, true), EndAction::Exit);
    ProcessEnd plain;
    EXPECT_EQ(plain.afterListen(false, false), EndAction::Exit);
}

TEST(ProcessEndTest, MainLeavesAnEndAlreadyTakenAlone)
{
    ProcessEnd end;
    EXPECT_EQ(end.afterListen(false, false), EndAction::Exit);
    EXPECT_EQ(end.afterStopTimeout(), EndAction::None);
    EXPECT_EQ(end.afterRestartTimeout(true), EndAction::None);
}

TEST(ProcessEndTest, OnlyTheFirstCallerStopsTheListener)
{
    // once listen() returned, wait_until_ready() would spin and a second stop() asserts
    ProcessEnd end;
    EXPECT_TRUE(end.claimListenerStop());
    EXPECT_FALSE(end.claimListenerStop());
}

} // anonymous namespace
