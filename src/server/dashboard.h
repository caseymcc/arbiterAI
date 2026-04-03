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
.btn-disabled
{
    opacity: 0.4;
    cursor: not-allowed;
}
.btn-toggle
{
    padding: 2px 8px;
    font-size: 11px;
}
.btn-toggle.active
{
    background: #1b3a2a;
    border-color: #4caf50;
    color: #4caf50;
}
.storage-bar-outer
{
    background: #1f2230;
    border-radius: 4px;
    height: 24px;
    margin: 8px 0;
    overflow: hidden;
    position: relative;
}
.storage-bar-fill
{
    height: 100%;
    border-radius: 4px;
    background: linear-gradient(90deg, #4a6cf7, #7c8aff);
    transition: width 0.5s ease;
}
.storage-bar-text
{
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    height: 100%;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 12px;
    color: #e0e0e0;
    font-weight: 500;
}
.storage-info
{
    display: flex;
    justify-content: space-between;
    font-size: 12px;
    color: #888;
    margin-bottom: 4px;
}
.row-fresh
{
    border-left: 3px solid #4caf50;
}
.row-stale
{
    border-left: 3px solid #f0c040;
}
.row-old
{
    border-left: 3px solid #ff4444;
}
.progress-inline
{
    display: inline-block;
    width: 120px;
    height: 12px;
    background: #1f2230;
    border-radius: 3px;
    overflow: hidden;
    vertical-align: middle;
    margin-right: 6px;
}
.progress-inline-fill
{
    height: 100%;
    background: linear-gradient(90deg, #4a6cf7, #7c8aff);
    transition: width 0.3s ease;
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
.cpu-bar-fill
{
    background: linear-gradient(90deg, #f0a030, #f0c040);
}
.gpu-row
{
    margin-bottom: 8px;
}
.heap-tooltip-wrap
{
    position: relative;
    display: inline;
    cursor: help;
    border-bottom: 1px dotted #666;
}
.heap-tooltip-wrap .heap-tooltip
{
    display: none;
    position: absolute;
    bottom: 120%;
    right: 0;
    background: #1a1d2e;
    border: 1px solid #2a2d3e;
    border-radius: 6px;
    padding: 8px 10px;
    white-space: nowrap;
    font-size: 11px;
    color: #ccc;
    z-index: 100;
    box-shadow: 0 4px 12px rgba(0,0,0,0.4);
    line-height: 1.6;
}
.heap-tooltip-wrap:hover .heap-tooltip
{
    display: block;
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
#promptChart
{
    stroke: #4caf50;
    fill: none;
    stroke-width: 2;
}
#genChart
{
    stroke: #f0c040;
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
.chart-legend
{
    display: flex;
    gap: 16px;
    margin-top: 6px;
    font-size: 11px;
    color: #999;
}
.chart-legend-dot
{
    display: inline-block;
    width: 10px;
    height: 10px;
    border-radius: 2px;
    margin-right: 4px;
    vertical-align: middle;
}
.log-panel
{
    background: #0d0f14;
    border: 1px solid #2a2d3a;
    border-radius: 6px;
    font-family: 'Menlo', 'Consolas', 'Courier New', monospace;
    font-size: 12px;
    line-height: 1.5;
    max-height: 400px;
    overflow-y: auto;
    padding: 8px 0;
}
.log-panel.collapsed
{
    max-height: 0;
    padding: 0;
    border: none;
    overflow: hidden;
}
.log-entry
{
    padding: 1px 12px;
    white-space: pre-wrap;
    word-break: break-all;
}
.log-entry:hover
{
    background: #161922;
}
.log-ts
{
    color: #555;
    margin-right: 8px;
}
.log-level
{
    display: inline-block;
    width: 52px;
    font-weight: 600;
    margin-right: 6px;
}
.log-level-trace { color: #666; }
.log-level-debug { color: #888; }
.log-level-info  { color: #4caf50; }
.log-level-warning { color: #f0c040; }
.log-level-error { color: #ff4444; }
.log-level-critical { color: #ff2222; font-weight: 800; }
.log-msg
{
    color: #ccc;
}
.log-toolbar
{
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 8px;
}
.log-toolbar select
{
    background: #1a1d27;
    color: #ccc;
    border: 1px solid #2a2d3a;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
}
.log-toolbar label
{
    color: #888;
    font-size: 12px;
    display: flex;
    align-items: center;
    gap: 4px;
}
.log-toolbar input[type="checkbox"]
{
    accent-color: #7c8aff;
}
.card-header-row
{
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 12px;
    cursor: pointer;
}
.card-header-row h2
{
    margin-bottom: 0;
}
.collapse-icon
{
    color: #888;
    font-size: 16px;
    transition: transform 0.2s;
    user-select: none;
}
.collapse-icon.open
{
    transform: rotate(180deg);
}
</style>
</head>
<body>
<div class="header">
    <h1>ArbiterAI Dashboard <span id="versionBadge" class="version-badge"></span><span id="llamaBadge" class="version-badge"></span></h1>
    <div class="status"><span class="dot" id="statusDot"></span><span id="statusText">Connected</span></div>
</div>
<div class="container">
    <div class="grid">
        <div class="card">
            <h2>Performance</h2>
            <div class="stat-row"><span class="stat-label">Prompt Speed</span><span class="stat-value" id="avgPromptTps">0.0 t/s</span></div>
            <div class="stat-row"><span class="stat-label">Generation Speed</span><span class="stat-value" id="avgGenTps">0.0 t/s</span></div>
            <div class="stat-row"><span class="stat-label">Active Requests</span><span class="stat-value" id="activeReqs">0</span></div>
            <div class="stat-row"><span class="stat-label">Total Inferences</span><span class="stat-value" id="totalInferences">0</span></div>
            <div class="stat-row"><span class="stat-label">Model Swaps</span><span class="stat-value" id="totalSwaps">0</span></div>
        </div>
        <div class="card">
            <h2>System Memory</h2>
            <div class="stat-row"><span class="stat-label">Total RAM</span><span class="stat-value" id="totalRam">-</span></div>
            <div class="stat-row"><span class="stat-label">Free RAM</span><span class="stat-value" id="freeRam">-</span></div>
            <div class="stat-row"><span class="stat-label">CPU Cores</span><span class="stat-value" id="cpuCores">-</span></div>
            <div class="gpu-label" style="margin-top:6px;"><span>CPU Usage</span><span id="cpuUsage">-</span></div>
            <div class="gpu-bar"><div class="gpu-bar-fill cpu-bar-fill" id="cpuBar" style="width:0%"></div></div>
        </div>
        <div class="card">
            <h2>Processing Speed Over Time</h2>
            <div class="chart-container">
                <svg class="chart-svg" id="tpsChartSvg" viewBox="0 0 430 120" preserveAspectRatio="none">
                    <g class="chart-grid" id="chartGrid"></g>
                    <g id="chartYLabelsLeft"></g>
                    <g id="chartYLabelsRight"></g>
                    <polyline id="promptChart" points=""></polyline>
                    <polyline id="genChart" points=""></polyline>
                </svg>
            </div>
            <div class="chart-legend">
                <span><span class="chart-legend-dot" style="background:#4caf50;"></span>Prompt (tokens in/s)</span>
                <span><span class="chart-legend-dot" style="background:#f0c040;"></span>Generation (tokens out/s)</span>
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
                    <th>Max Context</th>
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
    <div class="card" style="margin-bottom:20px;">
        <h2>Downloaded Models</h2>
        <div id="storageBarSection">
            <div class="storage-info">
                <span id="storageUsedLabel">Used: -</span>
                <span id="storageLimitLabel">Limit: -</span>
            </div>
            <div class="storage-bar-outer">
                <div class="storage-bar-fill" id="storageBarFill" style="width:0%"></div>
                <div class="storage-bar-text" id="storageBarText">-</div>
            </div>
            <div class="storage-info">
                <span id="storageCleanupLabel">Auto-cleanup: -</span>
                <span id="storageCandidatesLabel"></span>
            </div>
        </div>
        <div id="downloadProgressSection" style="margin:8px 0;"></div>
        <table>
            <thead>
                <tr>
                    <th>Model</th>
                    <th>Variant</th>
                    <th>Size</th>
                    <th>Downloaded</th>
                    <th>Last Used</th>
                    <th>Uses</th>
                    <th>State</th>
                    <th>Hot Ready</th>
                    <th>Protected</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody id="downloadedModelTable">
                <tr><td colspan="10" style="color:#666;text-align:center;">No downloaded models</td></tr>
            </tbody>
        </table>
    </div>
    <div class="grid">
        <div class="card">
            <h2>Recent Inferences</h2>
            <table>
                <thead><tr><th>Model</th><th>Prompt t/s</th><th>Gen t/s</th><th>Prompt</th><th>Completion</th><th>Latency</th></tr></thead>
                <tbody id="inferenceTable">
                    <tr><td colspan="6" style="color:#666;text-align:center;">No recent inferences</td></tr>
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
    <div class="card" style="margin-bottom:20px;">
        <div class="card-header-row" onclick="toggleLogPanel()">
            <h2>Server Log</h2>
            <span class="collapse-icon open" id="logCollapseIcon">&#x25BC;</span>
        </div>
        <div class="log-toolbar" id="logToolbar">
            <select id="logLevelFilter" onchange="refreshLogs(true)">
                <option value="">All levels</option>
                <option value="trace">Trace</option>
                <option value="debug">Debug</option>
                <option value="info">Info</option>
                <option value="warning">Warning</option>
                <option value="error">Error</option>
                <option value="critical">Critical</option>
            </select>
            <label><input type="checkbox" id="logAutoScroll" checked onchange="refreshLogs(true)"> Live</label>
        </div>
        <div class="log-panel" id="logPanel">
            <div class="log-entry" style="color:#666;">Loading logs...</div>
        </div>
    </div>
</div>
<script>
const POLL_INTERVAL=2000;
const FAST_POLL_INTERVAL=1000;
let promptTpsHistory=[];
let genTpsHistory=[];
const MAX_TPS_POINTS=60;
let hasActiveDownloads=false;
let logPanelOpen=true;
let lastLogEpoch=0;

function toggleLogPanel()
{
    logPanelOpen=!logPanelOpen;
    const panel=document.getElementById("logPanel");
    const toolbar=document.getElementById("logToolbar");
    const icon=document.getElementById("logCollapseIcon");

    if(logPanelOpen)
    {
        panel.classList.remove("collapsed");
        toolbar.style.display="flex";
        icon.classList.add("open");
    }
    else
    {
        panel.classList.add("collapsed");
        toolbar.style.display="none";
        icon.classList.remove("open");
    }
}

function logLevelClass(level)
{
    const map={"trace":"trace", "debug":"debug", "info":"info", "warning":"warning", "error":"error", "critical":"critical"};
    return "log-level-"+(map[level]||"info");
}

function formatLogTime(isoStr)
{
    if(!isoStr) return "";
    const d=new Date(isoStr);
    const h=String(d.getHours()).padStart(2, "0");
    const m=String(d.getMinutes()).padStart(2, "0");
    const s=String(d.getSeconds()).padStart(2, "0");
    const ms=String(d.getMilliseconds()).padStart(3, "0");
    return h+":"+m+":"+s+"."+ms;
}

function escapeHtml(text)
{
    const el=document.createElement("span");
    el.textContent=text;
    return el.innerHTML;
}

async function refreshLogs(force)
{
    if(!logPanelOpen) return;

    const autoScroll=document.getElementById("logAutoScroll").checked;

    // When live mode is off, freeze the view entirely so the user
    // can select and copy text without the DOM changing underneath.
    // A forced refresh (filter change, re-enabling live) always runs.
    if(!autoScroll&&!force) return;

    const level=document.getElementById("logLevelFilter").value;
    let url="/api/logs?count=200";
    if(level) url+="&level="+encodeURIComponent(level);

    const data=await fetchJson(url);
    if(!data||!data.logs) return;

    const panel=document.getElementById("logPanel");

    let html="";
    for(const entry of data.logs)
    {
        const lvl=entry.level||"info";
        html+=`<div class="log-entry"><span class="log-ts">${formatLogTime(entry.timestamp)}</span><span class="log-level ${logLevelClass(lvl)}">${lvl.toUpperCase().padEnd(8)}</span><span class="log-msg">${escapeHtml(entry.message)}</span></div>`;
    }

    if(data.logs.length===0)
    {
        html='<div class="log-entry" style="color:#666;">No log entries</div>';
    }

    panel.innerHTML=html;
    panel.scrollTop=panel.scrollHeight;
}

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

async function promptVramOverride(gpuIndex, currentMb)
{
    const input=prompt("Enter VRAM override in MB for GPU "+gpuIndex+" (current: "+currentMb+" MB):", currentMb);

    if(input===null) return;

    const vramMb=parseInt(input, 10);

    if(isNaN(vramMb)||vramMb<=0)
    {
        alert("Invalid value. Please enter a positive integer in MB.");
        return;
    }

    try
    {
        const res=await fetch("/api/hardware/vram-override", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify({gpu_index: gpuIndex, vram_mb: vramMb})
        });

        if(!res.ok)
        {
            const err=await res.json();
            alert("Failed: "+(err.error||"Unknown error"));
            return;
        }
        await refresh();
    }
    catch(e)
    {
        console.error("VRAM override failed:", e);
    }
}

async function clearVramOverride(gpuIndex)
{
    try
    {
        const res=await fetch("/api/hardware/vram-override/"+gpuIndex, {method: "DELETE"});

        if(!res.ok)
        {
            const err=await res.json();
            alert("Failed: "+(err.error||"Unknown error"));
            return;
        }
        await refresh();
    }
    catch(e)
    {
        console.error("Clear VRAM override failed:", e);
    }
}

function updateTpsChart()
{
    const promptLine=document.getElementById("promptChart");
    const genLine=document.getElementById("genChart");
    const grid=document.getElementById("chartGrid");
    const yLabelsL=document.getElementById("chartYLabelsLeft");
    const yLabelsR=document.getElementById("chartYLabelsRight");

    const nPoints=Math.max(promptTpsHistory.length, genTpsHistory.length);

    if(nPoints<2)
    {
        promptLine.setAttribute("points", "");
        genLine.setAttribute("points", "");
        return;
    }

    const w=430, h=120, padL=32, padR=32, padT=5, padB=5;

    const maxPrompt=Math.max(...promptTpsHistory, 1);
    const maxGen=Math.max(...genTpsHistory, 1);
    const niceMaxPrompt=niceNum(maxPrompt);
    const niceMaxGen=niceNum(maxGen);

    function buildPoints(data, niceMax)
    {
        let pts=[];
        for(let i=0; i<data.length; i++)
        {
            const x=padL+(i/(Math.max(data.length-1, 1)))*(w-padL-padR);
            const y=h-padB-(data[i]/niceMax)*(h-padT-padB);
            pts.push(x.toFixed(1)+","+y.toFixed(1));
        }
        return pts.join(" ");
    }

    promptLine.setAttribute("points", buildPoints(promptTpsHistory, niceMaxPrompt));
    genLine.setAttribute("points", buildPoints(genTpsHistory, niceMaxGen));

    // Grid lines (based on prompt/left axis)
    grid.innerHTML="";
    yLabelsL.innerHTML="";
    yLabelsR.innerHTML="";
    const steps=4;

    for(let i=0; i<=steps; i++)
    {
        const y=padT+i*((h-padT-padB)/steps);
        const promptVal=((niceMaxPrompt*(steps-i)/steps)).toFixed(0);
        const genVal=((niceMaxGen*(steps-i)/steps)).toFixed(0);
        grid.innerHTML+=`<line x1="${padL}" y1="${y}" x2="${w-padR}" y2="${y}"/>`;
        yLabelsL.innerHTML+=`<text class="chart-label" x="${padL-4}" y="${y+3}" text-anchor="end" fill="#4caf50">${promptVal}</text>`;
        yLabelsR.innerHTML+=`<text class="chart-label" x="${w-padR+4}" y="${y+3}" text-anchor="start" fill="#f0c040">${genVal}</text>`;
    }
}

function niceNum(val)
{
    if(val<=0) return 1;
    const exp=Math.floor(Math.log10(val));
    const frac=val/Math.pow(10, exp);
    let nice;

    if(frac<=1.5) nice=1.5;
    else if(frac<=2) nice=2;
    else if(frac<=3) nice=3;
    else if(frac<=5) nice=5;
    else nice=10;

    return nice*Math.pow(10, exp);
}

function buildHeapTooltip(gpu)
{
    const heaps=gpu.memory_heaps;

    if(!heaps||heaps.length===0) return "";

    let lines=[];

    if(gpu.has_memory_budget)
    {
        lines.push("<b>Vulkan Memory Heaps (VK_EXT_memory_budget)</b>");
        for(const h of heaps)
        {
            const tag=h.device_local?"DEVICE_LOCAL":"host";
            const avail=h.budget_mb-h.usage_mb;
            lines.push(`Heap ${h.index} [${tag}]: size ${formatMb(h.size_mb)}, budget ${formatMb(h.budget_mb)}, used ${formatMb(h.usage_mb)}, avail ${formatMb(avail>0?avail:0)}`);
        }
    }
    else
    {
        lines.push("<b>Vulkan Memory Heaps</b>");
        for(const h of heaps)
        {
            const tag=h.device_local?"DEVICE_LOCAL":"host";
            lines.push(`Heap ${h.index} [${tag}]: size ${formatMb(h.size_mb)}`);
        }
        lines.push("<i>VK_EXT_memory_budget not available</i>");
    }

    return lines.join("<br>");
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

        let memLabel="VRAM";
        let memTotal=gpu.vram_total_mb;
        let memUsed=usedVram;
        let memPct=vramPct;

        if(gpu.unified_memory&&gpu.gpu_accessible_ram_mb>0)
        {
            memLabel="GPU Memory (unified)";
            memTotal=gpu.gpu_accessible_ram_mb;
            const memFree=gpu.gpu_accessible_ram_free_mb||0;
            memUsed=memTotal-memFree;
            memPct=memTotal>0?(memUsed/memTotal*100):0;
        }

        const hasUtilData=(gpu.backend==="CUDA");
        let utilHtml;

        if(hasUtilData)
        {
            utilHtml=`<div class="gpu-label"><span>Utilization</span><span>${utilPct.toFixed(0)}%</span></div>
            <div class="gpu-bar"><div class="gpu-bar-fill gpu-bar-util" style="width:${utilPct.toFixed(1)}%"></div></div>`;
        }
        else
        {
            utilHtml=`<div class="gpu-label"><span>Utilization</span><span style="color:#555;">N/A (${gpu.backend})</span></div>`;
        }

        const tooltip=buildHeapTooltip(gpu);
        let memSpan;

        if(tooltip)
        {
            memSpan=`<span class="heap-tooltip-wrap">${memLabel}: ${formatMb(memUsed)} / ${formatMb(memTotal)}<span class="heap-tooltip">${tooltip}</span></span>`;
        }
        else
        {
            memSpan=`<span>${memLabel}: ${formatMb(memUsed)} / ${formatMb(memTotal)}</span>`;
        }

        const overrideTag=gpu.vram_overridden?` <span style="color:#f0a;font-size:0.8em;" title="VRAM overridden by user">⚙ Override</span>`:"";
        const overrideBtn=`<button class="btn" style="font-size:0.7em;padding:2px 6px;margin-left:6px;" onclick="promptVramOverride(${gpu.index}, ${memTotal})" title="Override reported VRAM">✏</button>`;
        const clearBtn=gpu.vram_overridden?`<button class="btn btn-danger" style="font-size:0.7em;padding:2px 6px;margin-left:2px;" onclick="clearVramOverride(${gpu.index})" title="Clear VRAM override">✕</button>`:"";

        html+=`<div class="gpu-row">
            <div class="gpu-label"><span>${gpu.name} (${gpu.backend})${gpu.unified_memory?" ⚡ Unified":""}${overrideTag}${overrideBtn}${clearBtn}</span>${memSpan}</div>
            <div class="gpu-bar"><div class="gpu-bar-fill gpu-bar-vram" style="width:${memPct.toFixed(1)}%"></div></div>
            ${utilHtml}
        </div>`;
    }
    el.innerHTML=html;
}

function renderModels(models)
{
    const el=document.getElementById("modelTable");

    if(!models||models.length===0)
    {
        el.innerHTML='<tr><td colspan="9" style="color:#666;text-align:center;">No models loaded</td></tr>';
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

        const ctxDisplay=m.context_size? m.context_size.toLocaleString() : "-";
        const maxCtxDisplay=m.max_context_size? m.max_context_size.toLocaleString() : "-";

        html+=`<tr>
            <td>${m.model}</td>
            <td>${m.variant||"-"}</td>
            <td><span class="badge ${stateClass(m.state)}">${m.state}</span></td>
            <td>${ctxDisplay}</td>
            <td>${maxCtxDisplay}</td>
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
        el.innerHTML='<tr><td colspan="6" style="color:#666;text-align:center;">No recent inferences</td></tr>';
        return;
    }

    const recent=history.slice(-10).reverse();
    let html="";
    for(const s of recent)
    {
        const promptTps=s.prompt_tokens_per_second||0;
        const genTps=s.generation_tokens_per_second||0;

        html+=`<tr>
            <td>${s.model}</td>
            <td>${promptTps.toFixed(1)}</td>
            <td>${genTps.toFixed(1)}</td>
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

function formatBytesJs(bytes)
{
    if(bytes>=1073741824) return (bytes/1073741824).toFixed(1)+" GB";
    if(bytes>=1048576) return (bytes/1048576).toFixed(1)+" MB";
    return bytes+" B";
}

function formatDate(isoStr)
{
    if(!isoStr) return "-";
    const d=new Date(isoStr);
    return d.toLocaleDateString();
}

function daysSince(isoStr)
{
    if(!isoStr) return 999;
    const d=new Date(isoStr);
    const now=new Date();
    return Math.floor((now-d)/(1000*60*60*24));
}

function rowAgeClass(lastUsed)
{
    const days=daysSince(lastUsed);
    if(days>30) return "row-old";
    if(days>14) return "row-stale";
    return "row-fresh";
}

async function toggleHotReady(name, variant, currentlyHotReady)
{
    const method=currentlyHotReady?"DELETE":"POST";
    const url="/api/models/"+encodeURIComponent(name)+"/variants/"+encodeURIComponent(variant)+"/hot-ready";
    await fetch(url, {method});
    await refreshStorage();
}

async function toggleProtected(name, variant, currentlyProtected)
{
    const method=currentlyProtected?"DELETE":"POST";
    const url="/api/models/"+encodeURIComponent(name)+"/variants/"+encodeURIComponent(variant)+"/protected";
    await fetch(url, {method});
    await refreshStorage();
}

async function deleteModelFile(name, variant)
{
    if(!confirm("Delete "+name+" "+variant+"? This cannot be undone.")) return;
    const url="/api/models/"+encodeURIComponent(name)+"/files"+(variant?"?variant="+encodeURIComponent(variant):"");
    const resp=await fetch(url, {method:"DELETE"});
    if(resp.status===409)
    {
        const data=await resp.json();
        alert(data.error?.message||"Cannot delete: variant is guarded");
    }
    await refreshStorage();
}

function renderStorageBar(storage)
{
    if(!storage) return;

    const used=storage.used_by_models_bytes||0;
    const limit=storage.storage_limit_bytes;
    const free=storage.free_disk_bytes||0;
    const total=limit>0?limit:(used+free);
    const pct=total>0?(used/total*100):0;

    document.getElementById("storageUsedLabel").textContent="Used: "+formatBytesJs(used);
    document.getElementById("storageLimitLabel").textContent=limit>0?"Limit: "+formatBytesJs(limit):"Limit: All free space";
    document.getElementById("storageBarFill").style.width=pct.toFixed(1)+"%";
    document.getElementById("storageBarText").textContent=formatBytesJs(used)+" / "+formatBytesJs(total)+" ("+pct.toFixed(1)+"%)";
    document.getElementById("storageCleanupLabel").textContent="Auto-cleanup: "+(storage.cleanup_enabled?"ON":"OFF");
}

function renderDownloadProgress(downloads)
{
    const el=document.getElementById("downloadProgressSection");
    if(!downloads||downloads.length===0)
    {
        el.innerHTML="";
        hasActiveDownloads=false;
        return;
    }

    hasActiveDownloads=true;
    let html="";
    for(const dl of downloads)
    {
        const pct=dl.percent_complete||0;
        const downloaded=dl.bytes_downloaded||0;
        const total=dl.total_bytes||0;
        const speed=dl.speed_mbps||0;
        const eta=dl.eta_seconds||0;

        html+=`<div style="padding:6px 0;border-bottom:1px solid #1f2230;">
            <span style="font-weight:500;">${dl.model}</span>
            <span style="color:#888;margin-left:4px;">${dl.variant||""}</span>
            <span class="badge badge-downloading" style="margin-left:8px;">Downloading</span>
            <div style="margin-top:4px;">
                <div class="progress-inline"><div class="progress-inline-fill" style="width:${pct.toFixed(1)}%"></div></div>
                <span style="font-size:12px;color:#ccc;">${pct.toFixed(1)}%</span>
                ${total>0?`<span style="font-size:12px;color:#888;margin-left:8px;">${formatBytesJs(downloaded)} / ${formatBytesJs(total)}</span>`:""}
                ${speed>0?`<span style="font-size:12px;color:#888;margin-left:8px;">${speed.toFixed(1)} MB/s</span>`:""}
                ${eta>0?`<span style="font-size:12px;color:#888;margin-left:8px;">~${eta}s left</span>`:""}
            </div>
        </div>`;
    }
    el.innerHTML=html;
}

function renderDownloadedModels(models)
{
    const el=document.getElementById("downloadedModelTable");

    if(!models||models.length===0)
    {
        el.innerHTML='<tr><td colspan="10" style="color:#666;text-align:center;">No downloaded models</td></tr>';
        return;
    }

    let html="";
    for(const m of models)
    {
        const ageClass=rowAgeClass(m.last_used_at);
        const guarded=m.hot_ready||m.protected;
        const hrClass=m.hot_ready?"btn-toggle active":"btn-toggle";
        const prClass=m.protected?"btn-toggle active":"btn-toggle";
        const deleteDisabled=guarded?"btn-disabled":"";
        const deleteTitle=guarded?"Clear hot_ready and protected first":"Delete model file";

        html+=`<tr class="${ageClass}">
            <td>${m.model}</td>
            <td>${m.variant||"-"}</td>
            <td>${m.file_size_display||formatBytesJs(m.file_size_bytes)}</td>
            <td>${formatDate(m.downloaded_at)}</td>
            <td>${formatDate(m.last_used_at)}</td>
            <td>${m.usage_count||0}</td>
            <td><span class="badge ${stateClass(m.runtime_state)}">${m.runtime_state||"Unloaded"}</span></td>
            <td><button class="btn ${hrClass}" onclick="toggleHotReady('${m.model}','${m.variant}',${m.hot_ready})">${m.hot_ready?"ON":"OFF"}</button></td>
            <td><button class="btn ${prClass}" onclick="toggleProtected('${m.model}','${m.variant}',${m.protected})">${m.protected?"ON":"OFF"}</button></td>
            <td><button class="btn btn-danger ${deleteDisabled}" title="${deleteTitle}" onclick="${guarded?"":`deleteModelFile('${m.model}','${m.variant}')`}" ${guarded?"disabled":""}>Delete</button></td>
        </tr>`;
    }
    el.innerHTML=html;
}

async function refreshStorage()
{
    const [storage, storageModels, downloads, cleanupPreview]=await Promise.all([
        fetchJson("/api/storage"),
        fetchJson("/api/storage/models"),
        fetchJson("/api/downloads"),
        fetchJson("/api/storage/cleanup/preview")
    ]);

    renderStorageBar(storage);

    if(downloads&&downloads.downloads) renderDownloadProgress(downloads.downloads);
    else renderDownloadProgress([]);

    if(storageModels&&storageModels.models) renderDownloadedModels(storageModels.models);
    else renderDownloadedModels([]);

    if(cleanupPreview)
    {
        const count=cleanupPreview.candidate_count||0;
        document.getElementById("storageCandidatesLabel").textContent=count>0?count+" cleanup candidate"+(count>1?"s":""):"";
    }
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
    document.getElementById("avgPromptTps").textContent=(stats.avg_prompt_tokens_per_second||0).toFixed(1)+" t/s";
    document.getElementById("avgGenTps").textContent=(stats.avg_generation_tokens_per_second||0).toFixed(1)+" t/s";
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
        document.getElementById("cpuBar").style.width=stats.hardware.cpu_utilization_percent.toFixed(1)+"%";
    }

    // TPS chart
    if(stats.avg_prompt_tokens_per_second!==undefined||stats.avg_generation_tokens_per_second!==undefined)
    {
        promptTpsHistory.push(stats.avg_prompt_tokens_per_second||0);
        genTpsHistory.push(stats.avg_generation_tokens_per_second||0);
        if(promptTpsHistory.length>MAX_TPS_POINTS) promptTpsHistory.shift();
        if(genTpsHistory.length>MAX_TPS_POINTS) genTpsHistory.shift();
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

    // Storage (runs in parallel)
    refreshStorage();
}

async function loadVersion()
{
    const data=await fetchJson("/api/version");
    if(data)
    {
        document.getElementById("versionBadge").textContent="v"+data.version;
        if(data.llamaCppBuild)
        {
            document.getElementById("llamaBadge").textContent="llama.cpp "+data.llamaCppBuild;
        }
    }
}

loadVersion();
refresh();
refreshLogs();
setInterval(refresh, POLL_INTERVAL);
setInterval(refreshLogs, POLL_INTERVAL);
</script>
</body>
</html>)HTML";

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_DASHBOARD_H_
