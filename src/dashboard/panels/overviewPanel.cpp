#include "overviewPanel.h"
#include "widgets.h"

#include <algorithm>
#include <map>

namespace arbiterAI
{
namespace dashboard
{

namespace
{

const int TopRowHeight=150;
const int TableHeight=170;

gweni::Color stateColor(const std::string &state)
{
    if(state=="Loaded")
        return palette::good;
    if(state=="Ready")
        return palette::info;
    if(state=="Downloading")
        return palette::warn;
    return palette::muted;
}

std::string heapToolTip(const nlohmann::json &gpu)
{
    const nlohmann::json heaps=gpu.value("memory_heaps", nlohmann::json::array());
    if(!heaps.is_array() || heaps.empty())
        return std::string();

    const bool budget=gpu.value("has_memory_budget", false);
    std::string tip=budget?"Vulkan Memory Heaps (VK_EXT_memory_budget)":"Vulkan Memory Heaps";
    for(const nlohmann::json &heap:heaps)
    {
        tip+="\nHeap "+std::to_string(static_cast<int>(number(heap, "index")))+" ["
            +(heap.value("device_local", false)?"DEVICE_LOCAL":"host")+"]: size "+formatMb(number(heap, "size_mb"));
        if(budget)
        {
            const double available=std::max(0.0, number(heap, "budget_mb")-number(heap, "usage_mb"));
            tip+=", budget "+formatMb(number(heap, "budget_mb"))+", used "+formatMb(number(heap, "usage_mb"))
                +", avail "+formatMb(available);
        }
    }
    if(!budget)
        tip+="\nVK_EXT_memory_budget not available";
    return tip;
}

std::string allocationToolTip(const nlohmann::json &model)
{
    std::string tip;
    const nlohmann::json allocations=model.value("device_allocations", nlohmann::json::object());

    if(allocations.is_object())
    {
        for(auto it=allocations.begin(); it!=allocations.end(); ++it)
        {
            const nlohmann::json &allocation=it.value();
            if(!tip.empty())
                tip+="\n";
            tip+=text(allocation, "device_name", it.key())+": Model "+formatFixed(number(allocation, "model_buffer_mb"), 0)
                +" MB | KV "+formatFixed(number(allocation, "kv_cache_buffer_mb"), 0)
                +" MB | Compute "+formatFixed(number(allocation, "compute_buffer_mb"), 0)
                +" MB | "+formatFixed(number(allocation, "total_mb"), 0)+" MB total";
        }
    }
    if(number(model, "cpu_mapped_buffer_mb")>0)
        tip+="\nCPU: "+formatFixed(number(model, "cpu_mapped_buffer_mb"), 0)+" MB mapped";
    if(number(model, "graph_splits")>1)
        tip+="\nGraph splits: "+formatFixed(number(model, "graph_splits"), 0);
    return tip;
}

std::string joinIndices(const nlohmann::json &indices)
{
    std::string out;
    if(!indices.is_array())
        return out;
    for(const nlohmann::json &index:indices)
    {
        if(!out.empty())
            out+=", ";
        out+=index.is_number()?std::to_string(index.get<int>()):index.dump();
    }
    return out;
}

std::string stageName(const std::string &stage)
{
    static const std::map<std::string, std::string> names={
        {"queued", "Queued"}, {"tokenizing", "Tokenizing"}, {"waiting", "Waiting"}, {"inferring", "Inferring"}};
    auto name=names.find(stage);
    return name==names.end()?stage:name->second;
}

}//namespace

OverviewPanel::OverviewPanel(gweni::controls::Base *page, DataSource &data, std::function<void()> refresh):
    m_data(data),
    m_refresh(std::move(refresh)),
    m_page(page),
    m_vram(page, data, m_refresh)
{
    m_scroll=page->newChild<gweni::controls::ScrollControl>();
    m_scroll->setDock(gweni::DockPosition::Center);
    m_scroll->setScroll(false, true);

    gweni::controls::Base *content=m_scroll->newChild<gweni::controls::Base>();
    content->setDock(gweni::DockPosition::Top);
    content->setHeight(TopRowHeight+12+170+12+TableHeight+40+12+TableHeight+60+12+TableHeight+40);

    gweni::controls::Base *topRow=content->newChild<gweni::controls::Base>();
    topRow->setDock(gweni::DockPosition::Top);
    topRow->setHeight(TopRowHeight);

    gweni::controls::Base *performance=addCard(topRow, "PERFORMANCE", gweni::DockPosition::Left, 360, 0);
    for(const char *name:{"Prompt Speed", "Generation Speed", "Active Requests", "Total Inferences", "Model Swaps"})
        m_stats.emplace_back(name, addStatRow(performance, name));

    gweni::controls::Base *memory=addCard(topRow, "SYSTEM MEMORY", gweni::DockPosition::Left, 360, 0);
    for(const char *name:{"Total RAM", "Free RAM", "CPU Cores", "CPU Usage"})
        m_stats.emplace_back(name, addStatRow(memory, name));
    m_cpuBar=memory->newChild<gweni::controls::ProgressBar>();
    m_cpuBar->setDock(gweni::DockPosition::Top);
    m_cpuBar->setHeight(10);
    m_cpuBar->setAutoLabel(false);
    m_cpuBar->setProgress(0.0f);

    gweni::controls::Base *speed=addCard(topRow, "PROCESSING SPEED OVER TIME", gweni::DockPosition::Center, 0, 0);
    m_chart=speed->newChild<gweni::controls::Chart>();
    m_chart->setDock(gweni::DockPosition::Center);
    m_promptSeries=m_chart->addSeries("Prompt (tokens in/s)", gweni::controls::ChartSeriesType::Line, gweni::controls::ChartAxisSide::Left);
    m_generationSeries=m_chart->addSeries("Generation (tokens out/s)", gweni::controls::ChartSeriesType::Line, gweni::controls::ChartAxisSide::Right);
    m_chart->setXAxisType(gweni::controls::ChartXAxisType::Value);
    m_chart->setXRange(0.0, static_cast<double>(ChartPoints-1));
    m_chart->setLegendVisible(false);
    gweni::controls::Label *legend=speed->newChild<gweni::controls::Label>();
    legend->setText("Left axis, blue: prompt tokens in/s      Right axis, green: generation tokens out/s");
    legend->setTextColor(palette::muted);
    legend->setDock(gweni::DockPosition::Bottom);

    gweni::controls::Base *gpus=addCard(content, "GPUS", gweni::DockPosition::Top, 0, 170);
    m_gpuList=gpus->newChild<gweni::controls::Base>();
    m_gpuList->setDock(gweni::DockPosition::Center);
    m_noGpus=m_gpuList->newChild<gweni::controls::Label>();
    m_noGpus->setText("No GPUs detected");
    m_noGpus->setTextColor(palette::muted);
    m_noGpus->setDock(gweni::DockPosition::Top);

    gweni::controls::Base *models=addCard(content, "LOADED MODELS", gweni::DockPosition::Top, 0, TableHeight+40);
    m_models=addTable(models, TableHeight, {{"Model", 200}, {"Variant", 90}, {"State", 90}, {"Context", 90}, {"Max Context", 100},
        {"GPU(s)", 70}, {"VRAM (MB)", 90}, {"RAM (MB)", 80}, {"Pinned", 60}, {"Actions", 160}});
    setRows(m_models, {}, "No models loaded");

    gweni::controls::Base *requests=addCard(content, "ACTIVE REQUESTS", gweni::DockPosition::Top, 0, TableHeight+60);
    m_downloads=requests->newChild<gweni::controls::Base>();
    m_downloads->setDock(gweni::DockPosition::Top);
    m_downloads->setHeight(0);
    m_jobs=addTable(requests, TableHeight, {{"Job", 60}, {"Model", 200}, {"Status", 90}, {"Input Tokens", 100}, {"Output Tokens", 100},
        {"Prompt t/s", 80}, {"Gen t/s", 80}, {"Latency", 80}, {"Total Time", 80}});
    setRows(m_jobs, {}, "No active requests");

    gweni::controls::Base *recent=content->newChild<gweni::controls::Base>();
    recent->setDock(gweni::DockPosition::Top);
    recent->setHeight(TableHeight+40);
    gweni::controls::Base *inferences=addCard(recent, "RECENT INFERENCES", gweni::DockPosition::Left, 760, 0);
    m_inferences=addTable(inferences, TableHeight, {{"Job", 60}, {"Status", 90}, {"Model", 200}, {"Prompt t/s", 80},
        {"Gen t/s", 80}, {"Input", 70}, {"Output", 70}, {"Latency", 80}});
    setRows(m_inferences, {}, "No recent inferences");
    gweni::controls::Base *swaps=addCard(recent, "RECENT SWAPS", gweni::DockPosition::Center, 0, 0);
    m_swaps=addTable(swaps, TableHeight, {{"From", 160}, {"To", 160}, {"Time (ms)", 80}});
    setRows(m_swaps, {}, "No recent swaps");

    updateStats(nlohmann::json::object());
}

std::string OverviewPanel::stat(const std::string &name) const
{
    for(const auto &entry:m_stats)
    {
        if(entry.first==name)
            return entry.second->getText();
    }
    return std::string();
}

void OverviewPanel::updateStats(const nlohmann::json &stats)
{
    auto set=[this](const std::string &name, const std::string &value)
    {
        for(auto &entry:m_stats)
        {
            if(entry.first==name)
                entry.second->setText(value);
        }
    };

    const double promptRate=number(stats, "avg_prompt_tokens_per_second");
    const double generationRate=number(stats, "avg_generation_tokens_per_second");
    const double active=number(stats, "active_requests");
    set("Prompt Speed", formatRate(promptRate));
    set("Generation Speed", formatRate(generationRate));
    set("Active Requests", formatFixed(active, 0));

    if(stats.contains("hardware") && stats["hardware"].is_object())
    {
        const nlohmann::json &hardware=stats["hardware"];
        const double cpu=number(hardware, "cpu_utilization_percent");
        set("Total RAM", formatMb(number(hardware, "total_ram_mb")));
        set("Free RAM", formatMb(number(hardware, "free_ram_mb")));
        set("CPU Cores", formatFixed(number(hardware, "cpu_cores"), 0));
        set("CPU Usage", formatFixed(cpu, 1)+"%");
        m_cpuBar->setProgress(static_cast<float>(std::clamp(cpu/100.0, 0.0, 1.0)));
        if(!m_haveHardware && hardware.contains("gpus"))
            renderGpus(hardware["gpus"]);
    }

    if(stats.is_object() && !stats.empty())
    {
        // a rolling average outlives its requests: an idle server charts 0
        const bool idle=active==0;
        m_promptHistory.push_back(idle?0.0:promptRate);
        m_generationHistory.push_back(idle?0.0:generationRate);
        while(m_promptHistory.size()>ChartPoints)
            m_promptHistory.pop_front();
        while(m_generationHistory.size()>ChartPoints)
            m_generationHistory.pop_front();
        renderChart();
    }

    if(stats.contains("models") && stats["models"].is_array())
    {
        m_lastModels=stats["models"];
        renderModels(m_lastModels);
    }
}

void OverviewPanel::updateHardware(const nlohmann::json &hardware)
{
    if(!hardware.is_object() || !hardware.contains("gpus"))
        return;
    m_haveHardware=true;
    renderGpus(hardware["gpus"]);
}

void OverviewPanel::updateHistory(const nlohmann::json &history)
{
    if(!history.is_array())
        return;

    for(auto &entry:m_stats)
    {
        if(entry.first=="Total Inferences")
            entry.second->setText(std::to_string(history.size()));
    }

    std::vector<std::vector<std::string>> rows;
    for(size_t i=0; i<history.size() && i<10; ++i)
    {
        const nlohmann::json &run=history[history.size()-1-i];
        const bool cancelled=run.value("cancelled", false);
        rows.push_back({run.contains("job_id")?"#"+run["job_id"].dump():"-",
            cancelled?"Cancelled":"Complete",
            text(run, "model"),
            formatFixed(number(run, "prompt_tokens_per_second"), 1),
            formatFixed(number(run, "generation_tokens_per_second"), 1),
            formatFixed(number(run, "prompt_tokens"), 0),
            formatFixed(number(run, "completion_tokens"), 0),
            formatFixed(number(run, "latency_ms"), 0)+"ms"});
    }
    setRows(m_inferences, rows, "No recent inferences");
    for(int i=0; i<static_cast<int>(rows.size()); ++i)
        m_inferences->getRow(i)->getCell(1)->setTextColor(rows[i][1]=="Cancelled"?palette::bad:palette::good);
}

void OverviewPanel::updateSwaps(const nlohmann::json &swaps)
{
    if(!swaps.is_array())
        return;

    for(auto &entry:m_stats)
    {
        if(entry.first=="Model Swaps")
            entry.second->setText(std::to_string(swaps.size()));
    }

    std::vector<std::vector<std::string>> rows;
    for(size_t i=0; i<swaps.size() && i<10; ++i)
    {
        const nlohmann::json &swap=swaps[swaps.size()-1-i];
        const std::string from=text(swap, "from");
        rows.push_back({from.empty()?"-":from, text(swap, "to"), formatFixed(number(swap, "time_ms"), 1)});
    }
    setRows(m_swaps, rows, "No recent swaps");
}

void OverviewPanel::updateJobs(const nlohmann::json &jobs)
{
    std::vector<std::vector<std::string>> rows;

    if(jobs.is_array())
    {
        for(const nlohmann::json &job:jobs)
        {
            rows.push_back({job.contains("id")?"#"+job["id"].dump():"-",
                text(job, "model"),
                stageName(text(job, "stage")),
                formatThousands(static_cast<long long>(number(job, "prompt_tokens"))),
                formatThousands(static_cast<long long>(number(job, "completion_tokens"))),
                "-", "-", "-",
                formatFixed(number(job, "elapsed_ms")/1000.0, 1)+"s"});
        }
    }
    setRows(m_jobs, rows, "No active requests");
}

void OverviewPanel::updateDownloads(const nlohmann::json &downloads)
{
    const nlohmann::json list=downloads.is_object()?downloads.value("downloads", nlohmann::json::array()):nlohmann::json::array();
    const std::string shown=list.dump();
    if(shown==m_shownDownloads)
        return;
    m_shownDownloads=shown;
    showDownloads(m_downloads, list);
}

void OverviewPanel::renderChart()
{
    std::vector<gweni::controls::ChartPoint> prompt;
    std::vector<gweni::controls::ChartPoint> generation;

    // the newest point at the right edge, the older ones to its left
    const double first=static_cast<double>(ChartPoints-m_promptHistory.size());
    for(size_t i=0; i<m_promptHistory.size(); ++i)
        prompt.emplace_back(first+static_cast<double>(i), m_promptHistory[i]);
    for(size_t i=0; i<m_generationHistory.size(); ++i)
        generation.emplace_back(first+static_cast<double>(i), m_generationHistory[i]);
    m_chart->setSeriesPoints(m_promptSeries, std::move(prompt));
    m_chart->setSeriesPoints(m_generationSeries, std::move(generation));
}

void OverviewPanel::renderGpus(const nlohmann::json &gpus)
{
    if(!gpus.is_array())
        return;
    const std::string shown=gpus.dump()+m_lastModels.dump();
    if(shown==m_shownGpus)
        return;
    m_shownGpus=shown;

    // the models loaded on each GPU, with their share of its memory
    std::map<int, std::string> gpuModels;
    for(const nlohmann::json &model:m_lastModels)
    {
        if(text(model, "state")!="Loaded" || !model.contains("gpu_indices") || !model["gpu_indices"].is_array())
            continue;
        for(const nlohmann::json &index:model["gpu_indices"])
        {
            if(!index.is_number())
                continue;
            const int gpu=index.get<int>();
            double vram=number(model, "estimated_vram_mb");
            if(model.contains("per_gpu_vram_mb"))
                vram=std::max(vram, number(model["per_gpu_vram_mb"], std::to_string(gpu).c_str()));
            std::string &line=gpuModels[gpu];
            line+=(line.empty()?"":"   ")+text(model, "model")+" "+formatFixed(vram, 0)+"MB";
        }
    }

    if(m_gpuRows.size()!=gpus.size())
    {
        for(GpuRow &row:m_gpuRows)
            row.row->delayedDelete();
        m_gpuRows.clear();
        for(size_t i=0; i<gpus.size(); ++i)
        {
            GpuRow row;
            row.row=m_gpuList->newChild<gweni::controls::Base>();
            row.row->setDock(gweni::DockPosition::Top);
            row.row->setHeight(66);

            gweni::controls::Base *line=row.row->newChild<gweni::controls::Base>();
            line->setDock(gweni::DockPosition::Top);
            line->setHeight(22);
            row.name=line->newChild<gweni::controls::Label>();
            row.name->setDock(gweni::DockPosition::Left);
            row.overrideButton=line->newChild<gweni::controls::Button>();
            row.overrideButton->setText("Override VRAM");
            row.overrideButton->setDock(gweni::DockPosition::Left);
            fixWidth(row.overrideButton, 100);
            row.overrideButton->setMargin(gweni::Margin(8, 1, 0, 1));
            row.clearButton=line->newChild<gweni::controls::Button>();
            row.clearButton->setText("Clear");
            row.clearButton->setDock(gweni::DockPosition::Left);
            fixWidth(row.clearButton, 50);
            row.clearButton->setMargin(gweni::Margin(4, 1, 0, 1));
            row.memory=line->newChild<gweni::controls::Label>();
            row.memory->setDock(gweni::DockPosition::Center);
            row.memory->setTextAlignment(gweni::Alignment::Right | gweni::Alignment::CenterV);

            row.memoryBar=row.row->newChild<gweni::controls::ProgressBar>();
            row.memoryBar->setDock(gweni::DockPosition::Top);
            row.memoryBar->setHeight(10);
            row.memoryBar->setAutoLabel(false);

            row.utilization=row.row->newChild<gweni::controls::Label>();
            row.utilization->setDock(gweni::DockPosition::Top);
            row.models=row.row->newChild<gweni::controls::Label>();
            row.models->setDock(gweni::DockPosition::Top);
            row.models->setTextColor(palette::accent);
            m_gpuRows.push_back(row);
        }
    }
    m_noGpus->setHidden(!gpus.empty());

    for(size_t i=0; i<gpus.size(); ++i)
    {
        const nlohmann::json &gpu=gpus[i];
        GpuRow &row=m_gpuRows[i];
        const int index=static_cast<int>(number(gpu, "index"));
        const std::string backend=text(gpu, "backend");
        const bool unified=gpu.value("unified_memory", false);
        const bool overridden=gpu.value("vram_overridden", false);

        std::string memoryLabel="VRAM";
        double total=number(gpu, "vram_total_mb");
        double used=total-number(gpu, "vram_free_mb");
        if(unified && number(gpu, "gpu_accessible_ram_mb")>0)
        {
            memoryLabel="GPU Memory (unified)";
            total=number(gpu, "gpu_accessible_ram_mb");
            used=total-number(gpu, "gpu_accessible_ram_free_mb");
        }

        row.name->setText(text(gpu, "name")+" ("+backend+")"+(unified?"  Unified":"")+(overridden?"  [VRAM overridden]":""));
        row.memory->setText(memoryLabel+": "+formatMb(used)+" / "+formatMb(total));
        row.memory->setToolTipText(heapToolTip(gpu));
        row.memoryBar->setProgress(total>0?static_cast<float>(std::clamp(used/total, 0.0, 1.0)):0.0f);
        if(backend=="CUDA")
            row.utilization->setText("Utilization  "+formatFixed(number(gpu, "utilization_percent"), 0)+"%");
        else
            row.utilization->setText("Utilization  N/A ("+backend+")");
        row.models->setText(gpuModels[index]);
        row.clearButton->setHidden(!overridden);

        row.overrideButton->onPressCaller.removeHandler(row.overrideButton);
        row.overrideButton->onPressCaller.addCallback(row.overrideButton, [this, index, total](gweni::event::Handler &, gweni::event::Info)
        {
            promptVramOverride(index, total);
        });
        row.clearButton->onPressCaller.removeHandler(row.clearButton);
        row.clearButton->onPressCaller.addCallback(row.clearButton, [this, index](gweni::event::Handler &, gweni::event::Info)
        {
            clearVramOverride(index);
        });
    }
}

void OverviewPanel::renderModels(const nlohmann::json &models)
{
    const std::string shown=models.dump();
    if(shown==m_shownModels)
        return;
    m_shownModels=shown;

    m_models->clear();
    if(models.empty())
    {
        setRows(m_models, {}, "No models loaded");
        return;
    }

    for(const nlohmann::json &model:models)
    {
        const std::string name=text(model, "model");
        const std::string state=text(model, "state");
        const bool pinned=model.value("pinned", false);
        double vram=number(model, "estimated_vram_mb");
        if(vram==0)
            vram=number(model, "vram_usage_mb");

        gweni::controls::DataTableRow *row=m_models->addRow();
        row->setCellText(0, name);
        row->setCellText(1, text(model, "variant", "-"));
        row->setCellText(2, state);
        row->getCell(2)->setTextColor(stateColor(state));
        row->setCellText(3, number(model, "context_size")>0?formatThousands(static_cast<long long>(number(model, "context_size"))):"-");
        row->setCellText(4, number(model, "max_context_size")>0?formatThousands(static_cast<long long>(number(model, "max_context_size"))):"-");
        std::string gpus=joinIndices(model.value("gpu_indices", nlohmann::json::array()));
        row->setCellText(5, gpus.empty()?"-":gpus);
        row->setCellText(6, formatFixed(vram, 0));
        row->getCell(6)->setToolTipText(allocationToolTip(model));
        row->setCellText(7, formatFixed(number(model, "ram_usage_mb"), 0));
        row->setCellText(8, pinned?"Yes":"No");

        gweni::controls::Base *actions=m_models->newChild<gweni::controls::Base>();
        std::vector<std::pair<std::string, std::string>> buttons;
        if(state=="Loaded")
            buttons.push_back({"Unload", "unload"});
        if(state=="Ready" || state=="Unloaded")
            buttons.push_back({"Load", "load"});
        buttons.push_back(pinned?std::make_pair(std::string("Unpin"), std::string("unpin")):std::make_pair(std::string("Pin"), std::string("pin")));
        for(const auto &[caption, action]:buttons)
        {
            gweni::controls::Button *button=actions->newChild<gweni::controls::Button>();
            button->setText(caption);
            button->setName(action);
            button->setDock(gweni::DockPosition::Left);
            fixWidth(button, 56);
            button->setMargin(gweni::Margin(0, 0, 4, 0));
            button->onPressCaller.addCallback(button, [this, name, action=action](gweni::event::Handler &, gweni::event::Info)
            {
                modelAction(name, action);
            });
        }
        row->setCellContents(9, actions);
    }
}

bool OverviewPanel::reportFailure(const Response &response, const std::string &what)
{
    if(response.ok || (response.status>=200 && response.status<300))
        return false;
    notify(m_page, gweni::controls::ToastKind::Negative, what+" failed: "+response.error);
    return true;
}

void OverviewPanel::modelAction(const std::string &name, const std::string &action)
{
    m_data.postJson("/api/models/"+urlEncode(name)+"/"+action, nlohmann::json::object(), [this, name, action](const Response &response)
    {
        if(!reportFailure(response, action+" "+name) && m_refresh)
            m_refresh();
    });
}

void OverviewPanel::promptVramOverride(int gpuIndex, double currentMb)
{
    m_vram.prompt(gpuIndex, currentMb);
}

void OverviewPanel::setVramOverride(int gpuIndex, const std::string &input)
{
    m_vram.set(gpuIndex, input);
}

void OverviewPanel::clearVramOverride(int gpuIndex)
{
    m_vram.clear(gpuIndex);
}

}//namespace dashboard
}//namespace arbiterAI
