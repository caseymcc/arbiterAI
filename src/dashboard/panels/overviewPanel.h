#ifndef _ARBITERAI_DASHBOARD_OVERVIEWPANEL_H_
#define _ARBITERAI_DASHBOARD_OVERVIEWPANEL_H_

#include "data/dataSource.h"
#include "panels/vramOverride.h"

#include <gweni/controls.h>
#include <gweni/controls/chart.h>
#include <gweni/controls/dataTable.h>
#include <gweni/controls/dialogs/query.h>
#include <gweni/controls/progressBar.h>
#include <gweni/controls/scrollControl.h>

#include <nlohmann/json.hpp>

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace arbiterAI
{
namespace dashboard
{

/// The Overview tab: performance, system memory, the tokens/s chart, the
/// GPUs (VRAM override), the loaded models (load, unload, pin), the active
/// requests and downloads, and the recent inferences and swaps. Each update*()
/// takes one endpoint's reply; the actions call \p refresh once the server has
/// answered.
class OverviewPanel {
public:
    /// the chart's points, one per poll (two minutes)
    static constexpr size_t ChartPoints=60;

    OverviewPanel(gweni::controls::Base *page, DataSource &data, std::function<void()> refresh);

    /// /api/stats
    void updateStats(const nlohmann::json &stats);
    /// /api/hardware: the GPUs (/api/stats' are used until it answers)
    void updateHardware(const nlohmann::json &hardware);
    /// /api/stats/history?minutes=5
    void updateHistory(const nlohmann::json &history);
    /// /api/stats/swaps
    void updateSwaps(const nlohmann::json &swaps);
    /// /api/scheduler/jobs
    void updateJobs(const nlohmann::json &jobs);
    /// /api/downloads
    void updateDownloads(const nlohmann::json &downloads);

    /// POST /api/models/<name>/<action>: load, unload, pin, unpin
    void modelAction(const std::string &name, const std::string &action);
    /// Asks for a GPU's VRAM in MB, then POSTs /api/hardware/vram-override.
    void promptVramOverride(int gpuIndex, double currentMb);
    void setVramOverride(int gpuIndex, const std::string &input);
    /// DELETE /api/hardware/vram-override/<index>
    void clearVramOverride(int gpuIndex);

    std::string stat(const std::string &name) const;
    gweni::controls::ScrollControl *scroll() const { return m_scroll; }
    gweni::controls::Chart *chart() const { return m_chart; }
    gweni::controls::ProgressBar *cpuBar() const { return m_cpuBar; }
    gweni::controls::DataTable *modelsTable() const { return m_models; }
    gweni::controls::DataTable *jobsTable() const { return m_jobs; }
    gweni::controls::DataTable *inferencesTable() const { return m_inferences; }
    gweni::controls::DataTable *swapsTable() const { return m_swaps; }
    gweni::controls::Base *downloads() const { return m_downloads; }

    struct GpuRow {
        gweni::controls::Base *row=nullptr;
        gweni::controls::Label *name=nullptr;
        gweni::controls::Label *memory=nullptr;
        gweni::controls::ProgressBar *memoryBar=nullptr;
        gweni::controls::Label *utilization=nullptr;
        gweni::controls::Label *models=nullptr;
        gweni::controls::Button *overrideButton=nullptr;
        gweni::controls::Button *clearButton=nullptr;
    };
    const std::vector<GpuRow> &gpus() const { return m_gpuRows; }
    /// the VRAM prompt while it is open
    gweni::dialogs::PromptDialog *vramPrompt() const { return m_vram.dialog(); }

private:
    void renderGpus(const nlohmann::json &gpus);
    void renderModels(const nlohmann::json &models);
    void renderChart();
    /// a failed action's toast; \return whether it failed
    bool reportFailure(const Response &response, const std::string &what);

    DataSource &m_data;
    std::function<void()> m_refresh;
    gweni::controls::Base *m_page;
    gweni::controls::ScrollControl *m_scroll;
    VramOverride m_vram;

    std::vector<std::pair<std::string, gweni::controls::Label *>> m_stats;
    gweni::controls::ProgressBar *m_cpuBar;
    gweni::controls::Chart *m_chart;
    size_t m_promptSeries=0;
    size_t m_generationSeries=0;
    std::deque<double> m_promptHistory;
    std::deque<double> m_generationHistory;

    gweni::controls::Base *m_gpuList;
    gweni::controls::Label *m_noGpus;
    std::vector<GpuRow> m_gpuRows;

    gweni::controls::DataTable *m_models;
    gweni::controls::Base *m_downloads;
    gweni::controls::DataTable *m_jobs;
    gweni::controls::DataTable *m_inferences;
    gweni::controls::DataTable *m_swaps;

    bool m_haveHardware=false;
    nlohmann::json m_lastModels=nlohmann::json::array();
    std::string m_shownModels;
    std::string m_shownGpus;
    std::string m_shownDownloads;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_OVERVIEWPANEL_H_
