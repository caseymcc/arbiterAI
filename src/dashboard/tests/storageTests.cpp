#include "dashboardTest.h"
#include "panels/widgets.h"

#include <gweni/controls/toast.h>

using arbiterAI::dashboard::Age;
using arbiterAI::dashboard::StoragePanel;
using arbiterAI::dashboard::Tab;
using arbiterAI::dashboard::ageOf;
using arbiterAI::dashboard::dateOf;
namespace palette=arbiterAI::dashboard::palette;

namespace
{

gweni::controls::Button *cellButton(gweni::controls::DataTable *table, int row, int column)
{
    gweni::controls::Label *cell=table->getRow(row)->getCell(column);
    for(gweni::controls::Base *child:cell->getChildren())
    {
        if(auto *button=gweni::gweni_cast<gweni::controls::Button>(child))
            return button;
    }
    return nullptr;
}

}//namespace

class StorageTest: public DashboardTest
{
protected:
    StoragePanel &storage() { return m_app->storage(); }

    std::string lastToast()
    {
        std::vector<gweni::controls::Toast *> toasts=m_headless->canvas()->toasts();
        return toasts.empty()?std::string():toasts.back()->getText();
    }
};

TEST(StorageAge, FreshUpToFourteenDaysStaleToThirtyThenOld)
{
    const std::time_t now=FixtureNow;
    EXPECT_EQ(ageOf("2026-11-22T23:00:00Z", now), Age::Fresh);
    EXPECT_EQ(ageOf("2026-11-09T00:00:00Z", now), Age::Fresh);
    EXPECT_EQ(ageOf("2026-11-08T00:00:00Z", now), Age::Stale);
    EXPECT_EQ(ageOf("2026-10-24T00:00:00Z", now), Age::Stale);
    EXPECT_EQ(ageOf("2026-10-23T00:00:00Z", now), Age::Old);
    EXPECT_EQ(ageOf("", now), Age::Old);
    EXPECT_EQ(ageOf("yesterday", now), Age::Old);
    EXPECT_EQ(dateOf("2026-10-01T09:00:00Z"), "2026-10-01");
    EXPECT_EQ(dateOf(""), "-");
}

TEST_F(StorageTest, StorageBarIsTheModelsShareOfTheFreeSpace)
{
    start("/dashboard/storage");
    EXPECT_EQ(storage().usedText(), "Used: 100.0 GB");
    EXPECT_EQ(storage().limitText(), "Limit: All free space");
    EXPECT_EQ(storage().storageBar()->getText(), "100.0 GB / 500.0 GB (20.0%)");
    EXPECT_NEAR(storage().storageBar()->getProgress(), 0.2f, 0.0001f);
    EXPECT_EQ(storage().cleanupText(), "Auto-cleanup: ON");
    EXPECT_EQ(storage().candidatesText(), "2 cleanup candidates, 12.7 GB freeable");
}

TEST_F(StorageTest, StorageLimitIsTheBarsWhole)
{
    nlohmann::json storageJson=loadFixture("storage.json");
    storageJson["storage_limit_bytes"]=214748364800.0;
    storageJson["cleanup_enabled"]=false;
    m_data.setJson("/api/storage", storageJson);
    start("/dashboard/storage");
    EXPECT_EQ(storage().limitText(), "Limit: 200.0 GB");
    EXPECT_EQ(storage().storageBar()->getText(), "100.0 GB / 200.0 GB (50.0%)");
    EXPECT_EQ(storage().cleanupText(), "Auto-cleanup: OFF");
}

TEST_F(StorageTest, PollsOnlyWhileItsTabShows)
{
    start("/dashboard");
    frames(125);
    EXPECT_EQ(m_data.count("/api/storage"), 0u);

    click(m_app->tabButton(Tab::Storage));
    EXPECT_EQ(m_data.count("/api/storage"), 1u);
    EXPECT_EQ(m_data.count("/api/storage/models"), 1u);
    frames(125);
    EXPECT_EQ(m_data.count("/api/storage"), 2u);
}

TEST_F(StorageTest, ModelFilesWithDatesUsesAndAgeColours)
{
    start("/dashboard/storage");
    gweni::controls::DataTable *files=storage().modelsTable();
    ASSERT_EQ(files->rowCount(), 4);

    gweni::controls::DataTableRow *first=files->getRow(0);
    EXPECT_EQ(first->getCellText(0), "gpt-oss-120b");
    EXPECT_EQ(first->getCellText(2), "61.0 GB");
    EXPECT_EQ(first->getCellText(3), "2026-10-01");
    EXPECT_EQ(first->getCellText(4), "2026-11-22");
    EXPECT_EQ(first->getCellText(5), "152");
    EXPECT_EQ(first->getCellText(6), "Loaded");
    EXPECT_EQ(files->getRow(3)->getCellText(4), "-");

    EXPECT_TRUE(files->getRow(0)->getCell(0)->getTextColor()==palette::good);
    EXPECT_TRUE(files->getRow(1)->getCell(0)->getTextColor()==palette::warn);
    EXPECT_TRUE(files->getRow(2)->getCell(0)->getTextColor()==palette::bad);
    EXPECT_TRUE(files->getRow(3)->getCell(0)->getTextColor()==palette::bad);
}

TEST_F(StorageTest, FlagButtonsShowAndToggleTheFlags)
{
    start("/dashboard/storage");
    gweni::controls::DataTable *files=storage().modelsTable();
    gweni::controls::Button *hotReady=cellButton(files, 0, 7);
    gweni::controls::Button *isProtected=cellButton(files, 0, 8);
    ASSERT_NE(hotReady, nullptr);
    ASSERT_NE(isProtected, nullptr);
    EXPECT_EQ(hotReady->getText(), "ON");
    EXPECT_EQ(isProtected->getText(), "OFF");

    click(hotReady);
    EXPECT_EQ(m_data.count("/api/models/gpt-oss-120b/variants/Q4_K_M/hot-ready", "DELETE"), 1u);
    click(isProtected);
    EXPECT_EQ(m_data.count("/api/models/gpt-oss-120b/variants/Q4_K_M/protected", "POST"), 1u);
}

TEST_F(StorageTest, GuardedFilesCannotBeDeleted)
{
    start("/dashboard/storage");
    gweni::controls::DataTable *files=storage().modelsTable();
    EXPECT_TRUE(cellButton(files, 0, 9)->isDisabled());
    EXPECT_TRUE(cellButton(files, 2, 9)->isDisabled());
    EXPECT_FALSE(cellButton(files, 1, 9)->isDisabled());
    EXPECT_EQ(cellButton(files, 0, 9)->getToolTipText(), "Clear hot ready and protected first");

    click(cellButton(files, 0, 9));
    EXPECT_EQ(storage().deleteConfirm(), nullptr);
}

TEST_F(StorageTest, DeleteAsksFirstAndDeletesOnlyOnOk)
{
    start("/dashboard/storage");
    click(cellButton(storage().modelsTable(), 1, 9));
    ASSERT_NE(storage().deleteConfirm(), nullptr);
    shot("storage-delete-confirm");
    storage().deleteConfirm()->close(gweni::dialogs::Result::Cancel);
    frames(2);
    EXPECT_EQ(m_data.count("/api/models/qwen3-8b/files?variant=Q8_0", "DELETE"), 0u);

    click(cellButton(storage().modelsTable(), 1, 9));
    ASSERT_NE(storage().deleteConfirm(), nullptr);
    // the fixture answers before the call returns: the refresh follows at once
    const size_t polls=m_data.count("/api/storage/models");
    storage().deleteConfirm()->close(gweni::dialogs::Result::Ok);
    EXPECT_EQ(m_data.count("/api/models/qwen3-8b/files?variant=Q8_0", "DELETE"), 1u);
    EXPECT_EQ(m_data.count("/api/storage/models"), polls+1);
}

TEST_F(StorageTest, DeleteEncodesTheNameAndVariant)
{
    start("/dashboard/storage");
    storage().deleteFiles("org/odd model", "Q2_K");
    EXPECT_EQ(m_data.count("/api/models/org%2Fodd%20model/files?variant=Q2_K", "DELETE"), 1u);
}

TEST_F(StorageTest, RefusedDeleteShowsTheServersReason)
{
    m_data.setFailure("/api/models/qwen3-8b/files?variant=Q8_0", 409, "variant is protected", "DELETE");
    start("/dashboard/storage");
    storage().deleteFiles("qwen3-8b", "Q8_0");
    frames(2);
    EXPECT_EQ(lastToast(), "variant is protected");
}

TEST_F(StorageTest, DownloadsCardShowsOnlyWhileDownloading)
{
    start("/dashboard/storage");
    EXPECT_FALSE(storage().downloadsCard()->hidden());
    EXPECT_EQ(storage().downloads()->getChildren().size(), 1u);

    m_data.setJson("/api/downloads", {{"downloads", nlohmann::json::array()}});
    frames(125);
    EXPECT_TRUE(storage().downloadsCard()->hidden());
}

TEST_F(StorageTest, NoFilesShowsThePlaceholder)
{
    m_data.setJson("/api/storage/models", {{"models", nlohmann::json::array()}});
    start("/dashboard/storage");
    ASSERT_EQ(storage().modelsTable()->rowCount(), 1);
    bool found=false;
    for(int i=0; i<storage().modelsTable()->getRow(0)->getCellCount(); ++i)
        found=found || storage().modelsTable()->getRow(0)->getCellText(i)=="No downloaded models";
    EXPECT_TRUE(found);
}
