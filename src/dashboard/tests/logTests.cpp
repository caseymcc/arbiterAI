#include "dashboardTest.h"
#include "panels/widgets.h"

#include <gweni/controls/toast.h>
#include <gweni/platforms/platform.h>

using arbiterAI::dashboard::LogPanel;
using arbiterAI::dashboard::formatLogTime;
namespace palette=arbiterAI::dashboard::palette;

class LogTest: public DashboardTest
{
protected:
    LogPanel &logs() { return m_app->logPanel(); }

    std::vector<std::string> rows()
    {
        std::vector<std::string> texts;
        gweni::controls::LogView *view=logs().view();
        if(view->getRowCount()==0)
            return texts;
        for(auto id=view->firstRowId(); id<=view->lastRowId(); ++id)
        {
            if(const gweni::controls::LogRow *row=view->getRow(id))
                texts.push_back(row->text);
        }
        return texts;
    }

    std::string lastToast()
    {
        std::vector<gweni::controls::Toast *> toasts=m_headless->canvas()->toasts();
        return toasts.empty()?std::string():toasts.back()->getText();
    }

    static constexpr const char *Path="/api/logs?count=200";
};

TEST(LogTime, HoursMinutesSecondsAndMilliseconds)
{
    // tests/main.cpp sets TZ=UTC
    EXPECT_EQ(formatLogTime(1795392000123), "00:00:00.123");
    EXPECT_EQ(formatLogTime(1795392000000+3723045), "01:02:03.045");
}

TEST_F(LogTest, LoadedOnlyOnceThePanelIsOpen)
{
    start();
    frames(125);
    EXPECT_EQ(m_data.count(Path), 0u);

    click(logs().header());
    EXPECT_EQ(m_data.count(Path), 1u);
    std::vector<std::string> texts=rows();
    ASSERT_EQ(texts.size(), 8u);
    EXPECT_EQ(texts[0], "00:00:00.000  INFO     log line 0");
    EXPECT_EQ(texts[4], "00:00:01.000  ERROR    log line 4");
}

TEST_F(LogTest, LevelsHaveTheirColours)
{
    start("/dashboard/logs");
    gweni::controls::LogView *view=logs().view();
    const gweni::controls::LogRow *error=view->getRow(view->firstRowId()+4);
    const gweni::controls::LogRow *warning=view->getRow(view->firstRowId()+2);
    ASSERT_NE(error, nullptr);
    ASSERT_NE(warning, nullptr);
    EXPECT_TRUE(error->color==palette::bad);
    EXPECT_TRUE(warning->color==palette::warn);
}

TEST_F(LogTest, EachEntryIsAppendedOnce)
{
    start("/dashboard/logs");
    EXPECT_EQ(m_data.count(Path), 1u);
    frames(125);
    frames(125);
    EXPECT_EQ(m_data.count(Path), 3u);
    EXPECT_EQ(rows().size(), 8u);

    // two more, the first in the same millisecond as the last one shown
    nlohmann::json logsJson=loadFixture("logs.json");
    const int64_t last=logsJson["logs"].back()["epoch_ms"].get<int64_t>();
    logsJson["logs"].push_back({{"epoch_ms", last}, {"level", "info"}, {"message", "same millisecond"}});
    logsJson["logs"].push_back({{"epoch_ms", last+10}, {"level", "info"}, {"message", "later"}});
    m_data.setJson(Path, logsJson);
    frames(125);
    std::vector<std::string> texts=rows();
    ASSERT_EQ(texts.size(), 10u);
    EXPECT_NE(texts[8].find("same millisecond"), std::string::npos);
    EXPECT_NE(texts[9].find("later"), std::string::npos);
}

TEST_F(LogTest, LevelFilterReloadsAtThatLevel)
{
    m_data.setJson("/api/logs?count=200&level=error", {{"logs", {{{"epoch_ms", 1795392000500}, {"level", "error"}, {"message", "only errors"}}}}});
    start("/dashboard/logs");
    ASSERT_EQ(rows().size(), 8u);

    logs().levelBox()->selectItemByName("error");
    frames(2);
    EXPECT_EQ(logs().level(), "error");
    EXPECT_EQ(m_data.count("/api/logs?count=200&level=error"), 1u);
    std::vector<std::string> texts=rows();
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(texts[0], "00:00:00.500  ERROR    only errors");
}

TEST_F(LogTest, LiveOffFreezesTheViewUntilItIsOnAgain)
{
    start("/dashboard/logs");
    logs().liveBox()->checkBox()->setChecked(false);
    frames(2);
    EXPECT_FALSE(logs().live());
    const size_t asked=m_data.count(Path);
    frames(250);
    EXPECT_EQ(m_data.count(Path), asked);

    logs().liveBox()->checkBox()->setChecked(true);
    frames(2);
    EXPECT_EQ(m_data.count(Path), asked+1);
    EXPECT_EQ(rows().size(), 8u);
}

TEST_F(LogTest, CopyPutsTheSelectedLinesOnTheClipboard)
{
    start("/dashboard/logs");
    gweni::platform::setClipboardText("");
    logs().view()->selectRow(logs().view()->firstRowId()+1);
    logs().view()->extendSelectionTo(logs().view()->firstRowId()+2);
    click(logs().copyButton());
    EXPECT_EQ(gweni::platform::getClipboardText(), "00:00:00.250  DEBUG    log line 1\n00:00:00.500  WARNING  log line 2");
    EXPECT_EQ(lastToast(), "Copied 2 lines");
    shot("logs");
}

TEST_F(LogTest, CopyWithNothingSelectedSaysHow)
{
    start("/dashboard/logs");
    gweni::platform::setClipboardText("untouched");
    click(logs().copyButton());
    EXPECT_EQ(gweni::platform::getClipboardText(), "untouched");
    EXPECT_EQ(lastToast(), "Select log lines to copy first (click, Shift+click, Ctrl+A).");
}

TEST_F(LogTest, AnOlderReplyAfterANewerOneAddsNoDuplicates)
{
    start("/dashboard/logs");
    ASSERT_EQ(rows().size(), 8u);

    // two polls in flight, answered newest first
    m_data.hold(true);
    logs().poll();
    nlohmann::json logsJson=loadFixture("logs.json");
    const int64_t last=logsJson["logs"].back()["epoch_ms"].get<int64_t>();
    logsJson["logs"].push_back({{"epoch_ms", last+10}, {"level", "info"}, {"message", "later"}});
    m_data.setJson(Path, logsJson);
    logs().poll();
    ASSERT_EQ(m_data.heldCount(), 2u);
    m_data.answerHeld(1);
    m_data.answerHeld(0);
    m_data.hold(false);
    EXPECT_EQ(rows().size(), 9u);

    logs().poll();
    EXPECT_EQ(rows().size(), 9u);
}
