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
const FAST_POLL_INTERVAL=1000;
let tpsHistory=[];
const MAX_TPS_POINTS=60;
let hasActiveDownloads=false;

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

        html+=`<div class="gpu-row">
            <div class="gpu-label"><span>${gpu.name} (${gpu.backend})${gpu.unified_memory?" ⚡ Unified":"" }</span><span>${memLabel}: ${formatMb(memUsed)} / ${formatMb(memTotal)}</span></div>
            <div class="gpu-bar"><div class="gpu-bar-fill gpu-bar-vram" style="width:${memPct.toFixed(1)}%"></div></div>
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

    // Storage (runs in parallel)
    refreshStorage();
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
