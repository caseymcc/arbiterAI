#include "dashboardTest.h"

#include <gweni/controls/textBox.h>
#include <gweni/controls/toast.h>

using arbiterAI::dashboard::OverviewPanel;

namespace
{

/// The control named \p name under \p parent, depth first.
gweni::controls::Base *findNamed(gweni::controls::Base *parent, const std::string &name)
{
    for(gweni::controls::Base *child:parent->getChildren())
    {
        if(child->getName()==name)
            return child;
        if(gweni::controls::Base *found=findNamed(child, name))
            return found;
    }
    return nullptr;
}

std::vector<std::string> rowCells(gweni::controls::DataTable *table, int row)
{
    std::vector<std::string> cells;
    gweni::controls::DataTableRow *tableRow=table->getRow(row);
    if(!tableRow)
        return cells;
    for(int i=0; i<tableRow->getCellCount(); ++i)
        cells.push_back(tableRow->getCellText(i));
    return cells;
}

/// The text of row 0's one filled cell: an empty table's placeholder.
std::string placeholder(gweni::controls::DataTable *table)
{
    std::string shown;
    for(const std::string &cell:rowCells(table, 0))
    {
        if(!cell.empty())
            shown=cell;
    }
    return table->rowCount()==1?shown:std::string();
}

}//namespace

class OverviewTest: public DashboardTest
{
protected:
    OverviewPanel &overview() { return m_app->overview(); }

    /// the toasts' texts, oldest first
    std::vector<std::string> toasts()
    {
        std::vector<std::string> texts;
        for(gweni::controls::Toast *toast:m_headless->canvas()->toasts())
            texts.push_back(toast->getText());
        return texts;
    }
};

TEST_F(OverviewTest, PerformanceAndMemoryCardsFromStatsHistoryAndSwaps)
{
    start();
    EXPECT_EQ(overview().stat("Prompt Speed"), "812.5 t/s");
    EXPECT_EQ(overview().stat("Total Inferences"), "12");
    EXPECT_EQ(overview().stat("Model Swaps"), "3");
    EXPECT_EQ(overview().stat("Total RAM"), "121.2 GB");
    EXPECT_EQ(overview().stat("Free RAM"), "57.9 GB");
    EXPECT_EQ(overview().stat("CPU Cores"), "32");
    EXPECT_EQ(overview().stat("CPU Usage"), "2.1%");
    EXPECT_NEAR(overview().cpuBar()->getProgress(), 0.0207f, 0.001f);
}

TEST_F(OverviewTest, ChartHasAPointPerPollForBothSeries)
{
    start();
    nlohmann::json stats=loadFixture("stats.json");
    stats["avg_prompt_tokens_per_second"]=900.0;
    stats["avg_generation_tokens_per_second"]=55.0;
    m_data.setJson("/api/stats", stats);
    frames(125);

    const auto &prompt=overview().chart()->getSeriesPoints(0);
    const auto &generation=overview().chart()->getSeriesPoints(1);
    ASSERT_EQ(prompt.size(), 2u);
    ASSERT_EQ(generation.size(), 2u);
    EXPECT_DOUBLE_EQ(prompt[0].y, 812.5);
    EXPECT_DOUBLE_EQ(prompt[1].y, 900.0);
    EXPECT_DOUBLE_EQ(generation[1].y, 55.0);
    // the newest point is at the right edge
    EXPECT_DOUBLE_EQ(prompt[1].x, static_cast<double>(OverviewPanel::ChartPoints-1));
    EXPECT_DOUBLE_EQ(prompt[0].x, static_cast<double>(OverviewPanel::ChartPoints-2));
}

TEST_F(OverviewTest, ChartShowsZeroWhileIdleAndKeepsTwoMinutes)
{
    start();
    nlohmann::json stats=loadFixture("stats.json");
    stats["active_requests"]=0;
    m_data.setJson("/api/stats", stats);
    frames(125);
    EXPECT_DOUBLE_EQ(overview().chart()->getSeriesPoints(0).back().y, 0.0);
    EXPECT_DOUBLE_EQ(overview().chart()->getSeriesPoints(1).back().y, 0.0);

    for(size_t i=0; i<OverviewPanel::ChartPoints+5; ++i)
        overview().updateStats(stats);
    EXPECT_EQ(overview().chart()->getSeriesPoints(0).size(), OverviewPanel::ChartPoints);
}

TEST_F(OverviewTest, GpusFromHardwareWithHeapsOverridesAndModels)
{
    start();
    const auto &gpus=overview().gpus();
    ASSERT_EQ(gpus.size(), 2u);

    EXPECT_EQ(gpus[0].name->getText(), "AMD Radeon Graphics (RADV GFX1151) (Vulkan)  Unified");
    EXPECT_EQ(gpus[0].memory->getText(), "GPU Memory (unified): 32.8 GB / 120.0 GB");
    EXPECT_NE(gpus[0].memory->getToolTipText().find("Heap 1 [DEVICE_LOCAL]: size 86.7 GB, budget 53.9 GB"), std::string::npos)
        <<gpus[0].memory->getToolTipText();
    EXPECT_EQ(gpus[0].utilization->getText(), "Utilization  N/A (Vulkan)");
    EXPECT_EQ(gpus[0].models->getText(), "gpt-oss-120b 65536MB");
    EXPECT_TRUE(gpus[0].clearButton->hidden());

    EXPECT_EQ(gpus[1].name->getText(), "NVIDIA GeForce RTX 4090 (CUDA)  [VRAM overridden]");
    EXPECT_EQ(gpus[1].memory->getText(), "VRAM: 18.0 GB / 24.0 GB");
    EXPECT_NEAR(gpus[1].memoryBar->getProgress(), 0.75f, 0.001f);
    EXPECT_EQ(gpus[1].utilization->getText(), "Utilization  87%");
    EXPECT_EQ(gpus[1].models->getText(), "llama-3.1-8b 6100MB");
    EXPECT_FALSE(gpus[1].clearButton->hidden());
}

TEST_F(OverviewTest, VramOverridePromptPostsTheValueAndRefreshes)
{
    m_data.setJson("/api/hardware/vram-override", {{"success", true}}, "POST");
    start();
    click(overview().gpus()[0].overrideButton);
    gweni::dialogs::PromptDialog *dialog=overview().vramPrompt();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->getField()->getText(), "122880");
    shot("overview-vram-prompt");

    const size_t polls=m_data.count("/api/stats");
    dialog->getField()->setText("65536");
    dialog->close(gweni::dialogs::Result::Ok);
    frames(2);

    ASSERT_EQ(m_data.count("/api/hardware/vram-override", "POST"), 1u);
    bool found=false;
    for(const auto &request:m_data.requests())
    {
        if(request.method=="POST" && request.path=="/api/hardware/vram-override")
        {
            EXPECT_EQ(request.body, nlohmann::json({{"gpu_index", 0}, {"vram_mb", 65536}}));
            found=true;
        }
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(m_data.count("/api/stats"), polls+1);
}

TEST_F(OverviewTest, InvalidVramValueWarnsAndPostsNothing)
{
    start();
    overview().setVramOverride(0, "12abc");
    overview().setVramOverride(0, "-5");
    frames(2);
    EXPECT_EQ(m_data.count("/api/hardware/vram-override", "POST"), 0u);
    std::vector<std::string> texts=toasts();
    ASSERT_FALSE(texts.empty());
    EXPECT_EQ(texts.back(), "Invalid value: enter a positive whole number of MB.");
}

TEST_F(OverviewTest, ClearButtonDeletesTheOverride)
{
    start();
    click(overview().gpus()[1].clearButton);
    EXPECT_EQ(m_data.count("/api/hardware/vram-override/1", "DELETE"), 1u);
}

TEST_F(OverviewTest, ModelsTableRowsAndTheirActions)
{
    start();
    gweni::controls::DataTable *models=overview().modelsTable();
    ASSERT_EQ(models->rowCount(), 3);
    std::vector<std::string> first=rowCells(models, 0);
    EXPECT_EQ(first[0], "gpt-oss-120b");
    EXPECT_EQ(first[2], "Loaded");
    EXPECT_EQ(first[3], "32,768");
    EXPECT_EQ(first[4], "131,072");
    EXPECT_EQ(first[5], "0");
    EXPECT_EQ(first[6], "65536");
    EXPECT_EQ(first[8], "Yes");
    EXPECT_NE(models->getRow(0)->getCell(6)->getToolTipText().find("AMD Radeon Graphics: Model 60000 MB | KV 4096 MB"), std::string::npos);
    EXPECT_EQ(rowCells(models, 1)[3], "-");

    // a Loaded, pinned model offers Unload and Unpin; a Ready one Load and Pin
    gweni::controls::Base *loadedActions=models->getRow(0)->getCell(9);
    EXPECT_NE(findNamed(loadedActions, "unload"), nullptr);
    EXPECT_NE(findNamed(loadedActions, "unpin"), nullptr);
    EXPECT_EQ(findNamed(loadedActions, "load"), nullptr);
    gweni::controls::Base *readyActions=models->getRow(1)->getCell(9);
    EXPECT_NE(findNamed(readyActions, "load"), nullptr);
    EXPECT_NE(findNamed(readyActions, "pin"), nullptr);

    gweni::controls::Base *unload=findNamed(loadedActions, "unload");
    ASSERT_NE(unload, nullptr);
    click(unload);
    EXPECT_EQ(m_data.count("/api/models/gpt-oss-120b/unload", "POST"), 1u);
}

TEST_F(OverviewTest, ModelActionEncodesTheName)
{
    start();
    overview().modelAction("org/model name", "load");
    EXPECT_EQ(m_data.count("/api/models/org%2Fmodel%20name/load", "POST"), 1u);
}

TEST_F(OverviewTest, FailedModelActionShowsAToast)
{
    m_data.setFailure("/api/models/gpt-oss-120b/unload", 409, "model is busy", "POST");
    start();
    overview().modelAction("gpt-oss-120b", "unload");
    frames(2);
    std::vector<std::string> texts=toasts();
    ASSERT_FALSE(texts.empty());
    EXPECT_EQ(texts.back(), "unload gpt-oss-120b failed: model is busy");
    EXPECT_TRUE(m_app->connected());
}

TEST_F(OverviewTest, ActiveRequestsAndDownloads)
{
    start();
    gweni::controls::DataTable *jobs=overview().jobsTable();
    ASSERT_EQ(jobs->rowCount(), 2);
    EXPECT_EQ(rowCells(jobs, 0), std::vector<std::string>({"#113", "gpt-oss-120b", "Inferring", "15,234", "412", "-", "-", "-", "8.3s"}));
    EXPECT_EQ(rowCells(jobs, 1)[2], "Queued");

    gweni::controls::Base::List downloads=overview().downloads()->getChildren();
    ASSERT_EQ(downloads.size(), 1u);
    std::vector<std::string> texts;
    for(gweni::controls::Base *child:downloads.front()->getChildren())
    {
        if(auto *bar=gweni::gweni_cast<gweni::controls::ProgressBar>(child))
            EXPECT_NEAR(bar->getProgress(), 0.425f, 0.001f);
        else if(auto *label=gweni::gweni_cast<gweni::controls::Label>(child))
            texts.push_back(label->getText());
    }
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0], "qwen3-32b Q4_K_M  Downloading");
    EXPECT_EQ(texts[1], "42.5%   8.0 GB / 18.8 GB   95.3 MB/s   ~122s left");
}

TEST_F(OverviewTest, RecentInferencesAndSwapsNewestFirst)
{
    start();
    gweni::controls::DataTable *inferences=overview().inferencesTable();
    ASSERT_EQ(inferences->rowCount(), 10);
    EXPECT_EQ(rowCells(inferences, 0), std::vector<std::string>({"#111", "Cancelled", "gpt-oss-120b", "811.0", "41.1", "1310", "311", "361ms"}));
    EXPECT_EQ(rowCells(inferences, 9)[0], "#102");

    gweni::controls::DataTable *swaps=overview().swapsTable();
    ASSERT_EQ(swaps->rowCount(), 3);
    EXPECT_EQ(rowCells(swaps, 0), std::vector<std::string>({"qwen3-8b", "gpt-oss-120b", "1236.5"}));
    EXPECT_EQ(rowCells(swaps, 2)[0], "-");
}

TEST_F(OverviewTest, EmptyListsShowTheirPlaceholders)
{
    nlohmann::json stats=loadFixture("stats.json");
    stats["models"]=nlohmann::json::array();
    m_data.setJson("/api/stats", stats);
    m_data.setJson("/api/stats/history?minutes=5", nlohmann::json::array());
    m_data.setJson("/api/stats/swaps", nlohmann::json::array());
    m_data.setJson("/api/scheduler/jobs", nlohmann::json::array());
    m_data.setJson("/api/downloads", {{"downloads", nlohmann::json::array()}});
    start();

    EXPECT_EQ(placeholder(overview().modelsTable()), "No models loaded");
    EXPECT_EQ(placeholder(overview().jobsTable()), "No active requests");
    EXPECT_EQ(placeholder(overview().inferencesTable()), "No recent inferences");
    EXPECT_EQ(placeholder(overview().swapsTable()), "No recent swaps");
    EXPECT_TRUE(overview().downloads()->getChildren().empty());
}

TEST_F(OverviewTest, WritesTheWholeOverview)
{
    start();
    shot("overview-top");
    overview().scroll()->scrollToBottom();
    frames(2);
    shot("overview-bottom");
}

TEST_F(OverviewTest, ASplitModelShowsItsShareOnEachGpu)
{
    nlohmann::json stats=loadFixture("stats.json");
    nlohmann::json &split=stats["models"][2];
    split["gpu_indices"]={0, 1};
    split["per_gpu_vram_mb"]={{"0", 4000}, {"1", 2100}};
    m_data.setJson("/api/stats", stats);
    start();

    const auto &gpus=overview().gpus();
    ASSERT_EQ(gpus.size(), 2u);
    EXPECT_EQ(gpus[0].models->getText(), "gpt-oss-120b 65536MB   llama-3.1-8b 4000MB");
    EXPECT_EQ(gpus[1].models->getText(), "llama-3.1-8b 2100MB");
    // without device_allocations the VRAM cell lists the shares itself
    EXPECT_EQ(rowCells(overview().modelsTable(), 2)[6], "6100 (GPU0:4000, GPU1:2100)");
    EXPECT_EQ(rowCells(overview().modelsTable(), 0)[6], "65536");
}
