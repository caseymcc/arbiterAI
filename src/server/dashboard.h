#ifndef _ARBITERAI_SERVER_DASHBOARD_H_
#define _ARBITERAI_SERVER_DASHBOARD_H_

#include <string>

namespace arbiterAI
{
namespace server
{

const std::string DASHBOARD_HTML=R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ArbiterAI Dashboard</title>
<style>
*
{
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}
body
{
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: #0f1117;
    color: #e0e0e0;
    line-height: 1.6;
}
.header
{
    background: #1a1d27;
    border-bottom: 1px solid #2a2d3a;
    padding: 16px 24px;
    display: flex;
    align-items: center;
    justify-content: space-between;
}
.header h1
{
    font-size: 20px;
    color: #7c8aff;
}
.header .status
{
    font-size: 13px;
    color: #888;
}
.header .status .dot
{
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #4caf50;
    margin-right: 6px;
    vertical-align: middle;
}
.version-badge
{
    font-size: 12px;
    color: #888;
    background: #2a2d3a;
    padding: 2px 8px;
    border-radius: 4px;
    margin-left: 8px;
    font-weight: normal;
    vertical-align: middle;
}
.container
{
    max-width: 1400px;
    margin: 0 auto;
    padding: 20px 24px;
}
.grid
{
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
    gap: 16px;
    margin-bottom: 20px;
}
.card
{
    background: #1a1d27;
    border: 1px solid #2a2d3a;
    border-radius: 8px;
    padding: 16px;
}
.card h2
{
    font-size: 14px;
    color: #888;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-bottom: 12px;
}
.stat-row
{
    display: flex;
    justify-content: space-between;
    padding: 6px 0;
    border-bottom: 1px solid #1f2230;
}
.stat-row:last-child
{
    border-bottom: none;
}
.stat-label
{
    color: #999;
    font-size: 13px;
}
.stat-value
{
    color: #e0e0e0;
    font-size: 13px;
    font-weight: 500;
}
.stat-big
{
    font-size: 28px;
    font-weight: 600;
    color: #7c8aff;
    margin-bottom: 4px;
}
table
{
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
}
th
{
    text-align: left;
    padding: 8px 10px;
    color: #888;
    font-weight: 500;
    border-bottom: 1px solid #2a2d3a;
}
td
{
    padding: 8px 10px;
    border-bottom: 1px solid #1f2230;
}
.badge
{
    display: inline-block;
    padding: 2px 8px;
    border-radius: 4px;
    font-size: 11px;
    font-weight: 600;
    text-transform: uppercase;
}
.badge-loaded
{
    background: #1b3a2a;
    color: #4caf50;
}
.badge-ready
{
    background: #2a3040;
    color: #7c8aff;
}
.badge-unloaded
{
    background: #2a2020;
    color: #888;
}
.badge-downloading
{
    background: #2a2a10;
    color: #f0c040;
}
.btn
{
    padding: 4px 12px;
    border: 1px solid #2a2d3a;
    background: #1a1d27;
    color: #ccc;
    border-radius: 4px;
    cursor: pointer;
    font-size: 12px;
    margin-right: 4px;
}
.btn:hover
{
    background: #252838;
    color: #fff;
}
.btn-danger
{
    border-color: #5a2020;
}
.btn-danger:hover
{
    background: #3a1515;
    color: #ff6060;
}
.chart-container
{
    height: 120px;
    position: relative;
    margin-top: 8px;
}
.chart-svg
{
    width: 100%;
    height: 100%;
}
.gpu-bar
{
    background: #1f2230;
    border-radius: 4px;
    height: 20px;
    margin: 4px 0;
    overflow: hidden;
}
.gpu-bar-fill
{
    height: 100%;
    border-radius: 4px;
    transition: width 0.5s ease;
}
.gpu-bar-vram
{
    background: linear-gradient(90deg, #4a6cf7, #7c8aff);
}
.gpu-bar-util
{
    background: linear-gradient(90deg, #4caf50, #66cc66);
}
.gpu-row
{
    margin-bottom: 8px;
}
.gpu-label
{
    display: flex;
    justify-content: space-between;
    font-size: 12px;
    color: #999;
    margin-bottom: 2px;
}
#tpsChart
{
    stroke: #7c8aff;
    fill: none;
    stroke-width: 2;
}
.chart-grid line
{
    stroke: #1f2230;
    stroke-width: 1;
}
.chart-label
{
    fill: #666;
    font-size: 10px;
}
</style>
</head>
<body>
<div class="header">
    <h1>ArbiterAI Dashboard <span id="versionBadge" class="version-badge"></span></h1>
    <div class="status"><span class="dot" id="statusDot"></span><span id="statusText">Connected</span></div>
</div>
<div class="container">
    <div class="grid">
        <div class="card">
            <h2>Performance</h2>
            <div class="stat-big" id="avgTps">0.0</div>
            <div style="color:#888;font-size:12px;margin-bottom:8px;">tokens/sec (avg)</div>
            <div class="stat-row"><span class="stat-label">Active Requests</span><span class="stat-value" id="activeReqs">0</span></div>
            <div class="stat-row"><span class="stat-label">Total Inferences</span><span class="stat-value" id="totalInferences">0</span></div>
            <div class="stat-row"><span class="stat-label">Model Swaps</span><span class="stat-value" id="totalSwaps">0</span></div>
        </div>
        <div class="card">
            <h2>System Memory</h2>
            <div class="stat-row"><span class="stat-label">Total RAM</span><span class="stat-value" id="totalRam">-</span></div>
            <div class="stat-row"><span class="stat-label">Free RAM</span><span class="stat-value" id="freeRam">-</span></div>
            <div class="stat-row"><span class="stat-label">CPU Cores</span><span class="stat-value" id="cpuCores">-</span></div>
            <div class="stat-row"><span class="stat-label">CPU Usage</span><span class="stat-value" id="cpuUsage">-</span></div>
        </div>
        <div class="card">
            <h2>Tokens/sec Over Time</h2>
            <div class="chart-container">
                <svg class="chart-svg" id="tpsChartSvg" viewBox="0 0 400 120" preserveAspectRatio="none">
                    <g class="chart-grid" id="chartGrid"></g>
                    <polyline id="tpsChart" points=""></polyline>
                </svg>
            </div>
        </div>
    </div>
    <div class="grid">
        <div class="card" style="grid-column: 1 / -1;">
            <h2>GPUs</h2>
            <div id="gpuList"><span style="color:#666;">No GPUs detected</span></div>
        </div>
    </div>
    <div class="card" style="margin-bottom:20px;">
        <h2>Loaded Models</h2>
        <table>
            <thead>
                <tr>
                    <th>Model</th>
                    <th>Variant</th>
                    <th>State</th>
                    <th>Context</th>
                    <th>VRAM (MB)</th>
                    <th>RAM (MB)</th>
                    <th>Pinned</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody id="modelTable">
                <tr><td colspan="8" style="color:#666;text-align:center;">No models loaded</td></tr>
            </tbody>
        </table>
    </div>
    <div class="grid">
        <div class="card">
            <h2>Recent Inferences</h2>
            <table>
                <thead><tr><th>Model</th><th>TPS</th><th>Prompt</th><th>Completion</th><th>Latency</th></tr></thead>
                <tbody id="inferenceTable">
                    <tr><td colspan="5" style="color:#666;text-align:center;">No recent inferences</td></tr>
                </tbody>
            </table>
        </div>
        <div class="card">
            <h2>Recent Swaps</h2>
            <table>
                <thead><tr><th>From</th><th>To</th><th>Time (ms)</th></tr></thead>
                <tbody id="swapTable">
                    <tr><td colspan="3" style="color:#666;text-align:center;">No recent swaps</td></tr>
                </tbody>
            </table>
        </div>
    </div>
</div>
<script>
const POLL_INTERVAL=2000;
let tpsHistory=[];
const MAX_TPS_POINTS=60;

function formatMb(mb)
{
    if(mb>=1024) return (mb/1024).toFixed(1)+" GB";
    return mb+" MB";
}

function stateClass(state)
{
    const map={"Loaded":"loaded", "Ready":"ready", "Unloaded":"unloaded", "Downloading":"downloading", "Unloading":"unloaded"};
    return "badge-"+(map[state]||"unloaded");
}

async function fetchJson(url)
{
    try
    {
        const resp=await fetch(url);
        if(!resp.ok) return null;
        return await resp.json();
    }
    catch(e)
    {
        return null;
    }
}

async function modelAction(name, action)
{
    try
    {
        await fetch("/api/models/"+encodeURIComponent(name)+"/"+action, {method: "POST"});
        await refresh();
    }
    catch(e)
    {
        console.error("Action failed:", e);
    }
}

function updateTpsChart()
{
    const svg=document.getElementById("tpsChartSvg");
    const polyline=document.getElementById("tpsChart");
    const grid=document.getElementById("chartGrid");

    if(tpsHistory.length<2)
    {
        polyline.setAttribute("points", "");
        return;
    }

    const w=400, h=120, pad=5;
    const maxTps=Math.max(...tpsHistory, 1);

    let points=[];
    for(let i=0; i<tpsHistory.length; i++)
    {
        const x=pad+(i/(Math.max(tpsHistory.length-1, 1)))*(w-2*pad);
        const y=h-pad-(tpsHistory[i]/maxTps)*(h-2*pad);
        points.push(x.toFixed(1)+","+y.toFixed(1));
    }
    polyline.setAttribute("points", points.join(" "));

    grid.innerHTML="";
    for(let i=0; i<=4; i++)
    {
        const y=pad+i*((h-2*pad)/4);
        const val=((maxTps*(4-i)/4)).toFixed(0);
        grid.innerHTML+=`<line x1="${pad}" y1="${y}" x2="${w-pad}" y2="${y}"/>`;
        grid.innerHTML+=`<text class="chart-label" x="${w-pad+4}" y="${y+3}">${val}</text>`;
    }
}

function renderGpus(gpus)
{
    const el=document.getElementById("gpuList");

    if(!gpus||gpus.length===0)
    {
        el.innerHTML='<span style="color:#666;">No GPUs detected</span>';
        return;
    }

    let html="";
    for(const gpu of gpus)
    {
        const usedVram=gpu.vram_total_mb-gpu.vram_free_mb;
        const vramPct=gpu.vram_total_mb>0?(usedVram/gpu.vram_total_mb*100):0;
        const utilPct=gpu.utilization_percent||0;

        html+=`<div class="gpu-row">
            <div class="gpu-label"><span>${gpu.name} (${gpu.backend})</span><span>${formatMb(usedVram)} / ${formatMb(gpu.vram_total_mb)}</span></div>
            <div class="gpu-bar"><div class="gpu-bar-fill gpu-bar-vram" style="width:${vramPct.toFixed(1)}%"></div></div>
            <div class="gpu-label"><span>Utilization</span><span>${utilPct.toFixed(0)}%</span></div>
            <div class="gpu-bar"><div class="gpu-bar-fill gpu-bar-util" style="width:${utilPct.toFixed(1)}%"></div></div>
        </div>`;
    }
    el.innerHTML=html;
}

function renderModels(models)
{
    const el=document.getElementById("modelTable");

    if(!models||models.length===0)
    {
        el.innerHTML='<tr><td colspan="8" style="color:#666;text-align:center;">No models loaded</td></tr>';
        return;
    }

    let html="";
    for(const m of models)
    {
        const actions=[];
        if(m.state==="Loaded") actions.push(`<button class="btn btn-danger" onclick="modelAction('${m.model}','unload')">Unload</button>`);
        if(m.state==="Ready"||m.state==="Unloaded") actions.push(`<button class="btn" onclick="modelAction('${m.model}','load')">Load</button>`);
        if(!m.pinned) actions.push(`<button class="btn" onclick="modelAction('${m.model}','pin')">Pin</button>`);
        else actions.push(`<button class="btn" onclick="modelAction('${m.model}','unpin')">Unpin</button>`);

        html+=`<tr>
            <td>${m.model}</td>
            <td>${m.variant||"-"}</td>
            <td><span class="badge ${stateClass(m.state)}">${m.state}</span></td>
            <td>${m.context_size||"-"}</td>
            <td>${m.vram_usage_mb||0}</td>
            <td>${m.ram_usage_mb||0}</td>
            <td>${m.pinned?"Yes":"No"}</td>
            <td>${actions.join("")}</td>
        </tr>`;
    }
    el.innerHTML=html;
}

function renderInferences(history)
{
    const el=document.getElementById("inferenceTable");

    if(!history||history.length===0)
    {
        el.innerHTML='<tr><td colspan="5" style="color:#666;text-align:center;">No recent inferences</td></tr>';
        return;
    }

    const recent=history.slice(-10).reverse();
    let html="";
    for(const s of recent)
    {
        html+=`<tr>
            <td>${s.model}</td>
            <td>${s.tokens_per_second.toFixed(1)}</td>
            <td>${s.prompt_tokens}</td>
            <td>${s.completion_tokens}</td>
            <td>${s.latency_ms.toFixed(0)}ms</td>
        </tr>`;
    }
    el.innerHTML=html;
}

function renderSwaps(swaps)
{
    const el=document.getElementById("swapTable");

    if(!swaps||swaps.length===0)
    {
        el.innerHTML='<tr><td colspan="3" style="color:#666;text-align:center;">No recent swaps</td></tr>';
        return;
    }

    const recent=swaps.slice(-10).reverse();
    let html="";
    for(const s of recent)
    {
        html+=`<tr>
            <td>${s.from||"-"}</td>
            <td>${s.to}</td>
            <td>${s.time_ms.toFixed(1)}</td>
        </tr>`;
    }
    el.innerHTML=html;
}

async function refresh()
{
    const [stats, history, swaps, hw]=await Promise.all([
        fetchJson("/api/stats"),
        fetchJson("/api/stats/history?minutes=5"),
        fetchJson("/api/stats/swaps"),
        fetchJson("/api/hardware")
    ]);

    const dot=document.getElementById("statusDot");
    const statusText=document.getElementById("statusText");

    if(!stats)
    {
        dot.style.background="#ff4444";
        statusText.textContent="Disconnected";
        return;
    }

    dot.style.background="#4caf50";
    statusText.textContent="Connected";

    // Performance card
    document.getElementById("avgTps").textContent=(stats.avg_tokens_per_second||0).toFixed(1);
    document.getElementById("activeReqs").textContent=stats.active_requests||0;
    document.getElementById("totalInferences").textContent=history?history.length:0;
    document.getElementById("totalSwaps").textContent=swaps?swaps.length:0;

    // System memory card
    if(stats.hardware)
    {
        document.getElementById("totalRam").textContent=formatMb(stats.hardware.total_ram_mb);
        document.getElementById("freeRam").textContent=formatMb(stats.hardware.free_ram_mb);
        document.getElementById("cpuCores").textContent=stats.hardware.cpu_cores;
        document.getElementById("cpuUsage").textContent=stats.hardware.cpu_utilization_percent.toFixed(1)+"%";
    }

    // TPS chart
    if(stats.avg_tokens_per_second!==undefined)
    {
        tpsHistory.push(stats.avg_tokens_per_second||0);
        if(tpsHistory.length>MAX_TPS_POINTS) tpsHistory.shift();
        updateTpsChart();
    }

    // GPUs
    if(hw&&hw.gpus) renderGpus(hw.gpus);
    else if(stats.hardware&&stats.hardware.gpus) renderGpus(stats.hardware.gpus);

    // Models
    if(stats.models) renderModels(stats.models);

    // Inference history
    if(history) renderInferences(history);

    // Swaps
    if(swaps) renderSwaps(swaps);
}

async function loadVersion()
{
    const data=await fetchJson("/api/version");
    if(data)
    {
        document.getElementById("versionBadge").textContent="v"+data.version;
    }
}

loadVersion();
refresh();
setInterval(refresh, POLL_INTERVAL);
</script>
</body>
</html>)HTML";

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_DASHBOARD_H_
