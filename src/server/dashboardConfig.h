#ifndef _ARBITERAI_SERVER_DASHBOARDCONFIG_H_
#define _ARBITERAI_SERVER_DASHBOARDCONFIG_H_

#include <string>

namespace arbiterAI
{
namespace server
{

const std::string DASHBOARD_CONFIG_HTML=R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ArbiterAI - Configuration</title>
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
    gap: 16px;
}
.header-left
{
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
}
.header-title
{
    font-size: 20px;
    color: #7c8aff;
    font-weight: 600;
}
.header-link
{
    color: #7c8aff;
    text-decoration: none;
    font-size: 13px;
}
.header-link:hover
{
    text-decoration: underline;
}
.status
{
    font-size: 13px;
    color: #888;
}
.status .dot
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
    font-weight: normal;
    vertical-align: middle;
}
.container
{
    max-width: 1440px;
    margin: 0 auto;
    padding: 24px;
}
.card
{
    background: #1a1d27;
    border: 1px solid #2a2d3a;
    border-radius: 10px;
    padding: 18px;
    margin-bottom: 20px;
}
.card h2
{
    font-size: 14px;
    color: #888;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-bottom: 10px;
}
.card p
{
    color: #b8bcc8;
    font-size: 14px;
}
.hero-title
{
    font-size: 24px;
    color: #f3f5fb;
    margin-bottom: 8px;
}
.hero-subtitle
{
    color: #9aa3b8;
    font-size: 14px;
    max-width: 880px;
}
.grid
{
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
    gap: 18px;
}
.startup-card
{
    display: flex;
    flex-direction: column;
    gap: 14px;
    min-height: 420px;
}
.startup-header
{
    display: flex;
    justify-content: space-between;
    gap: 12px;
    align-items: flex-start;
}
.startup-title
{
    font-size: 18px;
    color: #f3f5fb;
    margin-bottom: 4px;
}
.startup-subtitle
{
    color: #8891a4;
    font-size: 13px;
}
.startup-memory
{
    color: #9aa3b8;
    font-size: 12px;
    text-align: right;
}
.startup-field-label
{
    display: block;
    font-size: 12px;
    color: #7f8799;
    margin-bottom: 6px;
    text-transform: uppercase;
    letter-spacing: 0.4px;
}
.context-slider
{
    -webkit-appearance: none;
    appearance: none;
    width: 100%;
    height: 8px;
    border-radius: 4px;
    background: #32384b;
    outline: none;
    cursor: pointer;
}
.context-slider::-webkit-slider-thumb
{
    -webkit-appearance: none;
    appearance: none;
    width: 18px;
    height: 18px;
    border-radius: 50%;
    background: #7c8aff;
    cursor: pointer;
    border: 2px solid #1a1f2e;
}
.context-slider::-moz-range-thumb
{
    width: 18px;
    height: 18px;
    border-radius: 50%;
    background: #7c8aff;
    cursor: pointer;
    border: 2px solid #1a1f2e;
}
.context-slider:disabled
{
    opacity: 0.3;
    cursor: not-allowed;
}
.context-slider:disabled::-webkit-slider-thumb
{
    cursor: not-allowed;
}
.context-slider-value
{
    float: right;
    font-size: 13px;
    color: #f1f4ff;
    font-weight: 600;
}
.vram-override-row
{
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 10px;
    border: 1px solid #32384b;
    border-radius: 8px;
    margin-bottom: 8px;
    background: #111520;
}
.vram-override-row .override-tag
{
    color: #f0a;
    font-size: 0.85em;
}
.picker
{
    border: 1px solid #32384b;
    border-radius: 8px;
    background: #111520;
    overflow: hidden;
}
.picker summary
{
    list-style: none;
    cursor: pointer;
    padding: 12px;
}
.picker summary::-webkit-details-marker
{
    display: none;
}
.picker-summary-row
{
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
}
.picker-summary-title
{
    font-size: 14px;
    color: #f1f4ff;
    font-weight: 500;
}
.picker-summary-meta
{
    color: #8e96a9;
    font-size: 12px;
    margin-top: 4px;
}
.picker-chevron
{
    color: #7c8aff;
    font-size: 11px;
}
.picker-menu
{
    border-top: 1px solid #23293a;
    max-height: 330px;
    overflow-y: auto;
    padding: 8px;
    display: grid;
    gap: 8px;
}
.picker-option
{
    width: 100%;
    border: 1px solid #2f3648;
    background: #171c28;
    color: #e8ecf7;
    border-radius: 8px;
    text-align: left;
    padding: 10px;
    cursor: pointer;
}
.picker-option:hover
{
    background: #1d2332;
}
.picker-option.selected
{
    border-color: #7c8aff;
    box-shadow: inset 0 0 0 1px rgba(124, 138, 255, 0.35);
}
.picker-option-top
{
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 12px;
    margin-bottom: 6px;
}
.picker-option-title
{
    font-size: 14px;
    font-weight: 500;
    color: #f1f4ff;
}
.picker-option-meta
{
    font-size: 12px;
    color: #9aa3b8;
}
.picker-empty
{
    color: #8c93a5;
    font-size: 13px;
    padding: 12px;
}
.compat-badge
{
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 3px 9px;
    border-radius: 999px;
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.45px;
}
.compat-likely
{
    background: rgba(76, 175, 80, 0.12);
    color: #6fdd7b;
    border-color: rgba(76, 175, 80, 0.28);
}
.compat-tight
{
    background: rgba(240, 192, 64, 0.12);
    color: #f0c040;
    border-color: rgba(240, 192, 64, 0.28);
}
.compat-unlikely
{
    background: rgba(255, 96, 96, 0.12);
    color: #ff7d7d;
    border-color: rgba(255, 96, 96, 0.28);
}
.compat-cloud
{
    background: rgba(124, 138, 255, 0.14);
    color: #9eb0ff;
    border-color: rgba(124, 138, 255, 0.28);
}
.compat-undetected
{
    background: rgba(138, 146, 167, 0.14);
    color: #bac2d8;
    border-color: rgba(138, 146, 167, 0.28);
}
.compat-outline
{
    border: 1px solid transparent;
}
.startup-note
{
    margin-top: auto;
    padding: 12px;
    background: #121723;
    border: 1px solid #23293a;
    border-radius: 8px;
    color: #aeb6ca;
    font-size: 13px;
    min-height: 84px;
}
.runtime-opts-section
{
    border: 1px solid #23293a;
    border-radius: 8px;
    overflow: hidden;
}
.runtime-opts-toggle
{
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 12px;
    background: #111520;
    cursor: pointer;
    font-size: 12px;
    color: #8891a4;
    text-transform: uppercase;
    letter-spacing: 0.4px;
    border: none;
    width: 100%;
}
.runtime-opts-toggle:hover
{
    background: #161c2b;
}
.runtime-opts-body
{
    display: none;
    padding: 12px;
    background: #111520;
    border-top: 1px solid #23293a;
}
.runtime-opts-body.open
{
    display: block;
}
.runtime-opts-row
{
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 10px;
}
.runtime-opts-row:last-child
{
    margin-bottom: 0;
}
.runtime-opts-label
{
    font-size: 13px;
    color: #b5bdd1;
    white-space: nowrap;
}
.runtime-opts-select
{
    border: 1px solid #32384b;
    border-radius: 6px;
    background: #171c28;
    color: #e0e0e0;
    padding: 6px 10px;
    font-size: 13px;
    min-width: 120px;
}
.summary-line
{
    display: flex;
    justify-content: space-between;
    gap: 12px;
    color: #b5bdd1;
    font-size: 13px;
    margin-top: 6px;
}
.save-row
{
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    flex-wrap: wrap;
}
.btn
{
    padding: 10px 16px;
    border: 1px solid #2d3450;
    background: #202845;
    color: #eef2ff;
    border-radius: 8px;
    cursor: pointer;
    font-size: 14px;
    font-weight: 600;
}
.btn:hover
{
    background: #273154;
}
.btn:disabled
{
    opacity: 0.45;
    cursor: not-allowed;
}
.settings-message
{
    min-height: 20px;
    font-size: 13px;
    color: #97a0b5;
}
.settings-message.success
{
    color: #6fdd7b;
}
.settings-message.error
{
    color: #ff8d8d;
}
.effective-banner
{
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    flex-wrap: wrap;
}
.effective-copy
{
    color: #c7d0e4;
    font-size: 14px;
}
.effective-copy strong
{
    color: #f3f5fb;
}
@media (max-width: 760px)
{
    .header,
    .effective-banner,
    .startup-header,
    .save-row
    {
        flex-direction: column;
        align-items: flex-start;
    }

    .startup-memory
    {
        text-align: left;
    }
}
</style>
</head>
<body>
<div class="header">
    <div class="header-left">
        <a href="/dashboard" class="header-link">&larr; Dashboard</a>
        <a href="/dashboard/storage" class="header-link">Downloaded Models</a>
        <span class="header-title">Configuration</span>
        <span id="versionBadge" class="version-badge"></span>
    </div>
    <div class="status"><span class="dot" id="statusDot"></span><span id="statusText">Connected</span></div>
</div>
<div class="container">
    <div class="card">
        <div class="hero-title">Startup Configuration</div>
        <div class="hero-subtitle">This is the server configuration. Models listed here are loaded on server startup. Adding a model saves it to the config and loads it immediately. Removing a model unloads it and removes it from the config.</div>
    </div>

    <!-- ── New Startup Models UI ────────────────────────── -->
    <div id="startupModelsSection">
        <div class="card">
            <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;">
                <div>
                    <div class="hero-title" style="font-size:16px;">Startup Models</div>
                    <div class="hero-subtitle">Select models to load on startup and assign compute devices.</div>
                </div>
                <button class="btn" onclick="addStartupModel()">+ Add Model</button>
            </div>
            <div id="startupModelsList"></div>
        </div>
    </div>

    <!-- ── Legacy Accelerator UI (hidden when startup_models active) ── -->
    <div id="legacyAcceleratorSection" style="display:none;">
    <div class="grid">
        <div class="card startup-card">
            <div class="startup-header">
                <div>
                    <div class="startup-title">CPU</div>
                    <div class="startup-subtitle" id="startupSubtitleCpu">Loading hardware status...</div>
                </div>
                <div class="startup-memory" id="startupMemoryCpu"></div>
            </div>
            <div>
                <label class="startup-field-label">Model</label>
                <details class="picker" id="startupPickerCpu">
                    <summary id="startupSummaryCpu"></summary>
                    <div class="picker-menu" id="startupMenuCpu"></div>
                </details>
            </div>
            <div>
                <label class="startup-field-label">Context Size <span class="context-slider-value" id="startupContextLabelCpu">&mdash;</span></label>
                <input type="range" class="context-slider" min="4096" max="131072" step="1024" value="4096" id="startupContextCpu" oninput="updateContextLabel('cpu')" onchange="refreshAcceleratorOptions('cpu')" disabled>
            </div>
            <div class="runtime-opts-section" id="runtimeOptsSectionCpu">
                <button type="button" class="runtime-opts-toggle" onclick="toggleRuntimeOpts('cpu')">Runtime Options <span id="runtimeOptsChevronCpu">&#9654;</span></button>
                <div class="runtime-opts-body" id="runtimeOptsBodyCpu">
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">KV Cache (K)</span>
                        <select class="runtime-opts-select" id="runtimeKvkCpu">
                            <option value="">Default</option>
                            <option value="f16">f16</option>
                            <option value="q8_0">q8_0</option>
                            <option value="q4_0">q4_0</option>
                        </select>
                    </div>
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">KV Cache (V)</span>
                        <select class="runtime-opts-select" id="runtimeKvvCpu">
                            <option value="">Default</option>
                            <option value="f16">f16</option>
                            <option value="q8_0">q8_0</option>
                            <option value="q4_0">q4_0</option>
                        </select>
                    </div>
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">Flash Attention</span>
                        <select class="runtime-opts-select" id="runtimeFlashAttnCpu">
                            <option value="">Default</option>
                            <option value="true">Enabled</option>
                            <option value="false">Disabled</option>
                        </select>
                    </div>
                </div>
            </div>
            <div class="startup-note" id="startupNoteCpu"></div>
        </div>

        <div class="card startup-card">
            <div class="startup-header">
                <div>
                    <div class="startup-title">CUDA GPU</div>
                    <div class="startup-subtitle" id="startupSubtitleCuda">Loading hardware status...</div>
                </div>
                <div class="startup-memory" id="startupMemoryCuda"></div>
            </div>
            <div>
                <label class="startup-field-label">Model</label>
                <details class="picker" id="startupPickerCuda">
                    <summary id="startupSummaryCuda"></summary>
                    <div class="picker-menu" id="startupMenuCuda"></div>
                </details>
            </div>
            <div>
                <label class="startup-field-label">Context Size <span class="context-slider-value" id="startupContextLabelCuda">&mdash;</span></label>
                <input type="range" class="context-slider" min="4096" max="131072" step="1024" value="4096" id="startupContextCuda" oninput="updateContextLabel('cuda')" onchange="refreshAcceleratorOptions('cuda')" disabled>
            </div>
            <div class="runtime-opts-section" id="runtimeOptsSectionCuda">
                <button type="button" class="runtime-opts-toggle" onclick="toggleRuntimeOpts('cuda')">Runtime Options <span id="runtimeOptsChevronCuda">&#9654;</span></button>
                <div class="runtime-opts-body" id="runtimeOptsBodyCuda">
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">KV Cache (K)</span>
                        <select class="runtime-opts-select" id="runtimeKvkCuda">
                            <option value="">Default</option>
                            <option value="f16">f16</option>
                            <option value="q8_0">q8_0</option>
                            <option value="q4_0">q4_0</option>
                        </select>
                    </div>
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">KV Cache (V)</span>
                        <select class="runtime-opts-select" id="runtimeKvvCuda">
                            <option value="">Default</option>
                            <option value="f16">f16</option>
                            <option value="q8_0">q8_0</option>
                            <option value="q4_0">q4_0</option>
                        </select>
                    </div>
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">Flash Attention</span>
                        <select class="runtime-opts-select" id="runtimeFlashAttnCuda">
                            <option value="">Default</option>
                            <option value="true">Enabled</option>
                            <option value="false">Disabled</option>
                        </select>
                    </div>
                </div>
            </div>
            <div class="startup-note" id="startupNoteCuda"></div>
        </div>

        <div class="card startup-card">
            <div class="startup-header">
                <div>
                    <div class="startup-title">Vulkan GPU</div>
                    <div class="startup-subtitle" id="startupSubtitleVulkan">Loading hardware status...</div>
                </div>
                <div class="startup-memory" id="startupMemoryVulkan"></div>
            </div>
            <div>
                <label class="startup-field-label">Model</label>
                <details class="picker" id="startupPickerVulkan">
                    <summary id="startupSummaryVulkan"></summary>
                    <div class="picker-menu" id="startupMenuVulkan"></div>
                </details>
            </div>
            <div>
                <label class="startup-field-label">Context Size <span class="context-slider-value" id="startupContextLabelVulkan">&mdash;</span></label>
                <input type="range" class="context-slider" min="4096" max="131072" step="1024" value="4096" id="startupContextVulkan" oninput="updateContextLabel('vulkan')" onchange="refreshAcceleratorOptions('vulkan')" disabled>
            </div>
            <div class="runtime-opts-section" id="runtimeOptsSectionVulkan">
                <button type="button" class="runtime-opts-toggle" onclick="toggleRuntimeOpts('vulkan')">Runtime Options <span id="runtimeOptsChevronVulkan">&#9654;</span></button>
                <div class="runtime-opts-body" id="runtimeOptsBodyVulkan">
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">KV Cache (K)</span>
                        <select class="runtime-opts-select" id="runtimeKvkVulkan">
                            <option value="">Default</option>
                            <option value="f16">f16</option>
                            <option value="q8_0">q8_0</option>
                            <option value="q4_0">q4_0</option>
                        </select>
                    </div>
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">KV Cache (V)</span>
                        <select class="runtime-opts-select" id="runtimeKvvVulkan">
                            <option value="">Default</option>
                            <option value="f16">f16</option>
                            <option value="q8_0">q8_0</option>
                            <option value="q4_0">q4_0</option>
                        </select>
                    </div>
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">Flash Attention</span>
                        <select class="runtime-opts-select" id="runtimeFlashAttnVulkan">
                            <option value="">Default</option>
                            <option value="true">Enabled</option>
                            <option value="false">Disabled</option>
                        </select>
                    </div>
                    <div class="runtime-opts-row">
                        <span class="runtime-opts-label">No Host-Visible VRAM</span>
                        <select class="runtime-opts-select" id="runtimeVkNoHostVisVulkan" title="GGML_VK_DISABLE_HOST_VISIBLE_VIDMEM: Skip BAR-mapped heap, force device-local only. Useful for GPUs without Resizable BAR where the driver reports incorrect VRAM budgets.">
                            <option value="">Default</option>
                            <option value="true">Enabled</option>
                            <option value="false">Disabled</option>
                        </select>
                    </div>
                </div>
            </div>
            <div class="startup-note" id="startupNoteVulkan"></div>
        </div>
    </div>
    </div><!-- end legacyAcceleratorSection -->

    <div class="card" id="vramOverrideCard">
        <h2>VRAM Overrides</h2>
        <div class="hero-subtitle" style="margin-bottom:14px;">Override the reported VRAM for each GPU. Useful when the driver reports incorrect values or to simulate different hardware. Changes take effect immediately for fit calculations.</div>
        <div id="vramOverrideList"><span style="color:#666;">Loading GPU info...</span></div>
    </div>

    <div class="card save-row">
        <div class="settings-message" id="startupDefaultsMessage"></div>
    </div>
</div>

<script>
const STARTUP_ACCELERATORS=[
    {key: "cpu", label: "CPU", contextId: "startupContextCpu", contextLabelId: "startupContextLabelCpu", summaryId: "startupSummaryCpu", menuId: "startupMenuCpu", noteId: "startupNoteCpu", subtitleId: "startupSubtitleCpu", memoryId: "startupMemoryCpu", pickerId: "startupPickerCpu", kvkId: "runtimeKvkCpu", kvvId: "runtimeKvvCpu", flashAttnId: "runtimeFlashAttnCpu", runtimeBodyId: "runtimeOptsBodyCpu", runtimeChevronId: "runtimeOptsChevronCpu"},
    {key: "cuda", label: "CUDA GPU", contextId: "startupContextCuda", contextLabelId: "startupContextLabelCuda", summaryId: "startupSummaryCuda", menuId: "startupMenuCuda", noteId: "startupNoteCuda", subtitleId: "startupSubtitleCuda", memoryId: "startupMemoryCuda", pickerId: "startupPickerCuda", kvkId: "runtimeKvkCuda", kvvId: "runtimeKvvCuda", flashAttnId: "runtimeFlashAttnCuda", runtimeBodyId: "runtimeOptsBodyCuda", runtimeChevronId: "runtimeOptsChevronCuda"},
    {key: "vulkan", label: "Vulkan GPU", contextId: "startupContextVulkan", contextLabelId: "startupContextLabelVulkan", summaryId: "startupSummaryVulkan", menuId: "startupMenuVulkan", noteId: "startupNoteVulkan", subtitleId: "startupSubtitleVulkan", memoryId: "startupMemoryVulkan", pickerId: "startupPickerVulkan", kvkId: "runtimeKvkVulkan", kvvId: "runtimeKvvVulkan", flashAttnId: "runtimeFlashAttnVulkan", vkNoHostVisId: "runtimeVkNoHostVisVulkan", runtimeBodyId: "runtimeOptsBodyVulkan", runtimeChevronId: "runtimeOptsChevronVulkan"}
];

let serverConfigState=null;
let useStartupModels=false;
let startupModelsState=[];
let availableGpus=[];
let availableModelOptions=[];
const startupState={
    cpu: {selected: {model: "", variant: ""}, contextSize: 0, runtimeOptions: {}, options: [], detected: true, availableVramMb: 0, availableRamMb: 0},
    cuda: {selected: {model: "", variant: ""}, contextSize: 0, runtimeOptions: {}, options: [], detected: false, availableVramMb: 0, availableRamMb: 0},
    vulkan: {selected: {model: "", variant: ""}, contextSize: 0, runtimeOptions: {}, options: [], detected: false, availableVramMb: 0, availableRamMb: 0}
};

function escapeHtml(text)
{
    const el=document.createElement("span");
    el.textContent=text||"";
    return el.innerHTML;
}

function toggleRuntimeOpts(accelerator)
{
    const meta=getAcceleratorMeta(accelerator);
    const body=document.getElementById(meta.runtimeBodyId);
    const chevron=document.getElementById(meta.runtimeChevronId);
    const isOpen=body.classList.toggle("open");
    chevron.innerHTML=isOpen?"&#9660;":"&#9654;";
}

function readRuntimeOptsFromUI(accelerator)
{
    const meta=getAcceleratorMeta(accelerator);
    const opts={};
    const kvk=document.getElementById(meta.kvkId).value;
    const kvv=document.getElementById(meta.kvvId).value;
    const fa=document.getElementById(meta.flashAttnId).value;
    if(kvk) opts.kv_cache_type_k=kvk;
    if(kvv) opts.kv_cache_type_v=kvv;
    if(fa==="true") opts.flash_attn=true;
    else if(fa==="false") opts.flash_attn=false;
    if(meta.vkNoHostVisId)
    {
        const vkNhv=document.getElementById(meta.vkNoHostVisId).value;
        if(vkNhv==="true") opts.vulkan_no_host_visible_vram=true;
        else if(vkNhv==="false") opts.vulkan_no_host_visible_vram=false;
    }
    return opts;
}

function applyRuntimeOptsToUI(accelerator, opts)
{
    const meta=getAcceleratorMeta(accelerator);
    document.getElementById(meta.kvkId).value=(opts&&opts.kv_cache_type_k)||"";
    document.getElementById(meta.kvvId).value=(opts&&opts.kv_cache_type_v)||"";
    if(opts&&opts.flash_attn===true) document.getElementById(meta.flashAttnId).value="true";
    else if(opts&&opts.flash_attn===false) document.getElementById(meta.flashAttnId).value="false";
    else document.getElementById(meta.flashAttnId).value="";
    if(meta.vkNoHostVisId)
    {
        if(opts&&opts.vulkan_no_host_visible_vram===true) document.getElementById(meta.vkNoHostVisId).value="true";
        else if(opts&&opts.vulkan_no_host_visible_vram===false) document.getElementById(meta.vkNoHostVisId).value="false";
        else document.getElementById(meta.vkNoHostVisId).value="";
    }
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

function showMessage(text, state)
{
    const el=document.getElementById("startupDefaultsMessage");
    el.textContent=text||"";
    el.className="settings-message";
    if(state==="success") el.classList.add("success");
    if(state==="error") el.classList.add("error");
}

function formatMb(value)
{
    if(!value||value<=0) return "-";
    if(value>=1024) return (value/1024).toFixed(1)+" GB";
    return value.toFixed(0)+" MB";
}

function formatContextSize(value)
{
    if(!value||value<=0) return "\u2014";
    if(value>=1024) return (value/1024).toFixed(0)+"K";
    return value+"";
}

function updateContextLabel(accelerator)
{
    const meta=getAcceleratorMeta(accelerator);
    const slider=document.getElementById(meta.contextId);
    const label=document.getElementById(meta.contextLabelId);
    const value=parseInt(slider.value, 10);
    label.textContent=formatContextSize(value);
    startupState[accelerator].contextSize=value;
}

function updateSliderGradient(accelerator)
{
    const meta=getAcceleratorMeta(accelerator);
    const slider=document.getElementById(meta.contextId);
    const state=startupState[accelerator];
    const selected=findSelectedOption(accelerator);

    if(!selected||!selected.memory_per_1k_context_mb||selected.memory_per_1k_context_mb<=0)
    {
        slider.style.background="#32384b";
        return;
    }

    const min=parseInt(slider.min);
    const max=parseInt(slider.max);
    const range=max-min;

    if(range<=0)
    {
        slider.style.background="#32384b";
        return;
    }

    const baseMemory=selected.base_memory_mb||0;
    const baseContext=selected.base_context_size||0;
    const memPer1k=selected.memory_per_1k_context_mb;
    const availableMemory=accelerator==="cpu"?state.availableRamMb:state.availableVramMb;

    if(availableMemory<=0)
    {
        slider.style.background="#32384b";
        return;
    }

    const likelyCtx=baseContext+((0.85*availableMemory-baseMemory)/memPer1k)*1024;
    const tightCtx=baseContext+((availableMemory-baseMemory)/memPer1k)*1024;

    const likelyPct=Math.max(0, Math.min(100, ((likelyCtx-min)/range)*100));
    const tightPct=Math.max(0, Math.min(100, ((tightCtx-min)/range)*100));

    if(likelyPct>=100)
    {
        slider.style.background="linear-gradient(to right, rgba(76,175,80,0.35) 0%, rgba(76,175,80,0.35) 100%)";
    }
    else if(tightPct<=0)
    {
        slider.style.background="linear-gradient(to right, rgba(255,96,96,0.35) 0%, rgba(255,96,96,0.35) 100%)";
    }
    else
    {
        slider.style.background="linear-gradient(to right, rgba(76,175,80,0.35) 0%, rgba(76,175,80,0.35) "+likelyPct+"%, rgba(240,192,64,0.35) "+likelyPct+"%, rgba(240,192,64,0.35) "+tightPct+"%, rgba(255,96,96,0.35) "+tightPct+"%, rgba(255,96,96,0.35) 100%)";
    }
}

async function loadGpuOverrides()
{
    const data=await fetchJson("/api/hardware");

    if(!data||!data.gpus)
    {
        document.getElementById("vramOverrideList").innerHTML='<span style="color:#666;">No GPU data available.</span>';
        return;
    }

    renderGpuOverrides(data.gpus);
}

function renderGpuOverrides(gpus)
{
    const el=document.getElementById("vramOverrideList");

    if(!gpus||gpus.length===0)
    {
        el.innerHTML='<span style="color:#666;">No GPUs detected.</span>';
        return;
    }

    let html="";

    for(const gpu of gpus)
    {
        const overrideTag=gpu.vram_overridden?' <span class="override-tag">\u2699 Overridden</span>':"";
        const memTotal=gpu.vram_total_mb;
        const clearBtn=gpu.vram_overridden
            ?'<button class="btn" style="font-size:12px;padding:6px 10px;border-color:#ff6060;color:#ff8d8d;" onclick="clearVramOverrideConfig('+gpu.index+')">\u2715 Clear</button>'
            :"";
        html+='<div class="vram-override-row">'
            +'<div>'
            +'<div style="color:#f1f4ff;font-size:14px;">'+escapeHtml(gpu.name)+' ('+escapeHtml(gpu.backend)+')'+overrideTag+'</div>'
            +'<div style="color:#8e96a9;font-size:12px;margin-top:4px;">VRAM: '+formatMb(memTotal)+' total \u2022 Free: '+formatMb(gpu.vram_free_mb)+'</div>'
            +'</div>'
            +'<div style="display:flex;gap:6px;">'
            +'<button class="btn" style="font-size:12px;padding:6px 10px;" onclick="promptVramOverrideConfig('+gpu.index+', '+memTotal+')">\u270F Override</button>'
            +clearBtn
            +'</div>'
            +'</div>';
    }

    el.innerHTML=html;
}

async function promptVramOverrideConfig(gpuIndex, currentMb)
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

        await loadGpuOverrides();
        await refreshAllStartupOptions();
    }
    catch(e)
    {
        console.error("VRAM override failed:", e);
    }
}

async function clearVramOverrideConfig(gpuIndex)
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

        await loadGpuOverrides();
        await refreshAllStartupOptions();
    }
    catch(e)
    {
        console.error("Clear VRAM override failed:", e);
    }
}

function formatStartupModelLabel(model, variant)
{
    return variant?model+" ("+variant+")":model;
}

function encodeStartupModelValue(model, variant)
{
    return encodeURIComponent(JSON.stringify({model: model||"", variant: variant||""}));
}

function decodeStartupModelValue(value)
{
    if(!value) return {model: "", variant: ""};

    try
    {
        return JSON.parse(decodeURIComponent(value));
    }
    catch(e)
    {
        return {model: "", variant: ""};
    }
}

function getAcceleratorMeta(accelerator)
{
    return STARTUP_ACCELERATORS.find((item) => item.key===accelerator);
}

function normalizeContextSize(value)
{
    const parsed=parseInt(value, 10);
    if(Number.isNaN(parsed)||parsed<=0)
    {
        return 0;
    }

    return parsed;
}

function formatCompatibilityClass(compatibility)
{
    return "compat-"+(compatibility||"unlikely");
}

function formatOptionMemoryLabel(accelerator, option)
{
    if(option.compatibility==="cloud")
    {
        return "Cloud provider";
    }

    if(accelerator==="cpu")
    {
        return "Needs ~"+formatMb(option.required_ram_mb)+" RAM";
    }

    return "Needs ~"+formatMb(option.required_vram_mb)+" VRAM";
}

function getStartupEntry(accelerator)
{
    if(!serverConfigState||!serverConfigState.startup_defaults)
    {
        return {model: "", variant: "", context_size: 0, runtime_options: {}};
    }

    const entry=serverConfigState.startup_defaults[accelerator]||{};
    return {
        model: entry.model||"",
        variant: entry.variant||"",
        context_size: entry.context_size||0,
        runtime_options: entry.runtime_options||{}
    };
}

function findSelectedOption(accelerator)
{
    const state=startupState[accelerator];
    return state.options.find((option) => option.model===state.selected.model&&option.variant===state.selected.variant)||null;
}

function buildSummaryHtml(accelerator)
{
    const state=startupState[accelerator];
    const selected=findSelectedOption(accelerator);

    if(!state.selected.model)
    {
        return '<div class="picker-summary-row"><div><div class="picker-summary-title">Do not auto-load</div><div class="picker-summary-meta">Nothing will be preloaded for this startup slot.</div></div><span class="picker-chevron">Open</span></div>';
    }

    if(!selected)
    {
        const title=escapeHtml(formatStartupModelLabel(state.selected.model, state.selected.variant));
        return '<div class="picker-summary-row"><div><div class="picker-summary-title">'+title+'</div><div class="picker-summary-meta">Saved selection is missing from the current model catalog.</div></div><span class="compat-badge compat-outline compat-unlikely">Missing</span></div>';
    }

    const title=escapeHtml(formatStartupModelLabel(selected.model, selected.variant));
    const meta=escapeHtml(formatOptionMemoryLabel(accelerator, selected)+" • "+selected.compatibility_reason);
    const compatClass=formatCompatibilityClass(selected.compatibility);
    return '<div class="picker-summary-row"><div><div class="picker-summary-title">'+title+'</div><div class="picker-summary-meta">'+meta+'</div></div><span class="compat-badge compat-outline '+compatClass+'">'+escapeHtml(selected.compatibility_label)+'</span></div>';
}

function renderOptionButton(accelerator, option)
{
    const state=startupState[accelerator];
    const selected=state.selected.model===option.model&&state.selected.variant===option.variant;
    const compatClass=formatCompatibilityClass(option.compatibility);
    const value=encodeStartupModelValue(option.model, option.variant);
    const title=escapeHtml(formatStartupModelLabel(option.model, option.variant));
    const meta=escapeHtml(formatOptionMemoryLabel(accelerator, option)+" • max context "+(option.max_context_size||"-")+" • "+option.compatibility_reason);
    return '<button type="button" class="picker-option '+(selected?'selected ':'')+compatClass+'" onclick="selectStartupOption(\''+accelerator+'\', \''+value+'\')">'
        +'<div class="picker-option-top"><span class="picker-option-title">'+title+'</span><span class="compat-badge compat-outline '+compatClass+'">'+escapeHtml(option.compatibility_label)+'</span></div>'
        +'<div class="picker-option-meta">'+meta+'</div>'
        +'</button>';
}

function renderAccelerator(accelerator)
{
    const meta=getAcceleratorMeta(accelerator);
    const state=startupState[accelerator];
    const summaryEl=document.getElementById(meta.summaryId);
    const menuEl=document.getElementById(meta.menuId);
    const noteEl=document.getElementById(meta.noteId);
    const subtitleEl=document.getElementById(meta.subtitleId);
    const memoryEl=document.getElementById(meta.memoryId);
    const selected=findSelectedOption(accelerator);

    subtitleEl.textContent=state.detected
        ? (accelerator==="cpu"?"Uses system RAM for CPU startup.":"Hardware detected and startup fit checked against total VRAM.")
        : (accelerator==="cpu"?"CPU startup is always available if enough RAM remains.":"No compatible hardware is currently detected for this slot.");

    memoryEl.textContent=accelerator==="cpu"
        ? (state.availableRamMb?"Total RAM: "+formatMb(state.availableRamMb):"RAM unavailable")
        : (state.detected?"Total VRAM: "+formatMb(state.availableVramMb):"Device not detected");

    summaryEl.innerHTML=buildSummaryHtml(accelerator);

    let html='<button type="button" class="picker-option '+(!state.selected.model?'selected ':'')+'" onclick="selectStartupOption(\''+accelerator+'\', \'\')">'
        +'<div class="picker-option-top"><span class="picker-option-title">Do not auto-load</span><span class="compat-badge compat-outline compat-cloud">Disabled</span></div>'
        +'<div class="picker-option-meta">Leave this startup slot empty.</div>'
        +'</button>';

    if(state.options.length===0)
    {
        html+='<div class="picker-empty">No startup options are available right now.</div>';
    }
    else
    {
        html+=state.options.map((option) => renderOptionButton(accelerator, option)).join('');
    }

    menuEl.innerHTML=html;

    if(!state.selected.model)
    {
        noteEl.textContent='No model will be preloaded for '+meta.label+'.';
        const slider=document.getElementById(meta.contextId);
        const label=document.getElementById(meta.contextLabelId);
        slider.disabled=true;
        slider.style.background='#32384b';
        label.textContent='\u2014';
        return;
    }

    if(!selected)
    {
        noteEl.textContent='The saved selection is no longer present in the live model catalog. Save a replacement if you want this slot to preload on restart.';
        const slider=document.getElementById(meta.contextId);
        const label=document.getElementById(meta.contextLabelId);
        slider.disabled=true;
        slider.style.background='#32384b';
        label.textContent='\u2014';
        return;
    }

    const slider=document.getElementById(meta.contextId);
    const label=document.getElementById(meta.contextLabelId);
    const maxCtx=selected.max_context_size||131072;
    slider.disabled=false;
    slider.max=maxCtx;
    slider.min=4096;

    if(state.contextSize>0)
    {
        slider.value=Math.min(Math.max(state.contextSize, 4096), maxCtx);
    }
    else
    {
        slider.value=selected.effective_context_size||4096;
    }

    label.textContent=formatContextSize(parseInt(slider.value, 10));
    updateSliderGradient(accelerator);

    noteEl.textContent=selected.compatibility_reason+' Requested context: '+(selected.effective_context_size||'model default')+'.';
}

function renderEffectiveStartup()
{
    const el=document.getElementById('effectiveStartupCopy');
    if(!serverConfigState)
    {
        el.textContent='Loading startup config...';
        return;
    }

    // When using new startup_models mode, show summary from startupModelsState
    if(useStartupModels)
    {
        const active=startupModelsState.filter(e=>e.model);
        if(active.length===0)
        {
            el.innerHTML='Next restart: <strong>no startup models configured</strong>. Add a model above.';
            return;
        }

        let html='Next restart will load: ';
        const parts=active.map(e=>{
            const label=formatStartupModelLabel(e.model, e.variant);
            const ctx=e.context_size>0?formatContextSize(e.context_size):'auto';
            const devs=e.devices&&e.devices.length>0?'GPU '+e.devices.join(','):'auto';
            return '<strong>'+escapeHtml(label)+'</strong> (ctx: '+escapeHtml(ctx)+', devices: '+escapeHtml(devs)+')';
        });
        el.innerHTML=html+parts.join('; ')+'.';
        return;
    }

    // Legacy mode
    const effective=serverConfigState.effective_startup_default||{};
    if(!effective.model)
    {
        el.innerHTML='Next restart: <strong>no startup model configured</strong>.';
        return;
    }

    const label=formatStartupModelLabel(effective.model, effective.variant||'');
    const acceleratorLabel=(effective.accelerator||'legacy').toUpperCase();
    const contextLabel=effective.context_size&&effective.context_size>0
        ? 'context '+effective.context_size
        : 'model default context';

    let optsLabel='';
    const ro=effective.runtime_options;
    if(ro)
    {
        const parts=[];
        if(ro.kv_cache_type_k) parts.push('KV-K: '+ro.kv_cache_type_k);
        if(ro.kv_cache_type_v) parts.push('KV-V: '+ro.kv_cache_type_v);
        if(ro.flash_attn===true) parts.push('flash_attn');
        if(parts.length>0) optsLabel=' ('+parts.join(', ')+')';
    }

    el.innerHTML='Next restart: <strong>'+escapeHtml(acceleratorLabel)+'</strong> will try to load <strong>'+escapeHtml(label)+'</strong> using '+escapeHtml(contextLabel)+escapeHtml(optsLabel)+'.';
}

function applyServerConfigToState()
{
    for(const accelerator of STARTUP_ACCELERATORS)
    {
        const entry=getStartupEntry(accelerator.key);
        startupState[accelerator.key].selected={model: entry.model, variant: entry.variant};
        startupState[accelerator.key].contextSize=entry.context_size||0;
        startupState[accelerator.key].runtimeOptions=entry.runtime_options||{};
        const slider=document.getElementById(accelerator.contextId);

        if(entry.context_size>0)
        {
            slider.value=entry.context_size;
        }

        applyRuntimeOptsToUI(accelerator.key, entry.runtime_options);
    }
}

async function refreshAcceleratorOptions(accelerator)
{
    const meta=getAcceleratorMeta(accelerator);
    const contextSize=normalizeContextSize(document.getElementById(meta.contextId).value);
    startupState[accelerator].contextSize=contextSize;

    const data=await fetchJson('/api/server/startup-options?accelerator='+encodeURIComponent(accelerator)+'&context_size='+encodeURIComponent(contextSize));
    if(!data)
    {
        document.getElementById('statusDot').style.background='#ff4444';
        document.getElementById('statusText').textContent='Disconnected';
        return;
    }

    document.getElementById('statusDot').style.background='#4caf50';
    document.getElementById('statusText').textContent='Connected';

    startupState[accelerator].options=data.options||[];
    startupState[accelerator].detected=!!data.detected;
    startupState[accelerator].availableVramMb=data.available_vram_mb||0;
    startupState[accelerator].availableRamMb=data.available_ram_mb||0;
    renderAccelerator(accelerator);
}

async function refreshAllStartupOptions()
{
    await Promise.all(STARTUP_ACCELERATORS.map((accelerator) => refreshAcceleratorOptions(accelerator.key)));
}

function selectStartupOption(accelerator, encodedValue)
{
    const value=decodeStartupModelValue(encodedValue);
    startupState[accelerator].selected={model: value.model||'', variant: value.variant||''};
    startupState[accelerator].contextSize=0;
    document.getElementById(getAcceleratorMeta(accelerator).pickerId).open=false;
    renderAccelerator(accelerator);
}

async function saveStartupDefaults()
{
    const saveButton=document.getElementById('saveStartupDefaultsBtn');
    const startupDefaults={};

    for(const accelerator of STARTUP_ACCELERATORS)
    {
        startupDefaults[accelerator.key]={
            model: startupState[accelerator.key].selected.model||'',
            variant: startupState[accelerator.key].selected.variant||'',
            context_size: normalizeContextSize(document.getElementById(accelerator.contextId).value),
            runtime_options: readRuntimeOptsFromUI(accelerator.key)
        };
    }

    saveButton.disabled=true;
    showMessage('Saving startup defaults...', '');

    try
    {
        const response=await fetch('/api/server/config', {
            method: 'PUT',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({startup_defaults: startupDefaults})
        });
        const data=await response.json();

        if(!response.ok)
        {
            showMessage((data.error&&data.error.message)||'Failed to save startup defaults.', 'error');
            return;
        }

        serverConfigState=data;
        applyServerConfigToState();
        await refreshAllStartupOptions();
        showMessage('Startup defaults saved. The next restart will auto-download missing startup models and load them when ready.', 'success');
    }
    catch(e)
    {
        console.error('Saving startup defaults failed:', e);
        showMessage('Failed to save startup defaults.', 'error');
    }
    finally
    {
        saveButton.disabled=false;
    }
}

// ── New Startup Models Functions ──────────────────────────────

async function loadAvailableGpus()
{
    const data=await fetchJson('/api/hardware');
    if(data&&data.gpus)
    {
        availableGpus=data.gpus;
    }
}

async function loadAvailableModels()
{
    const data=await fetchJson('/api/server/startup-options?accelerator=vulkan');
    if(data&&data.options)
    {
        availableModelOptions=data.options;
    }

    // Also try cuda
    const cudaData=await fetchJson('/api/server/startup-options?accelerator=cuda');
    if(cudaData&&cudaData.options)
    {
        for(const opt of cudaData.options)
        {
            if(!availableModelOptions.find(o=>o.model===opt.model&&o.variant===opt.variant))
            {
                availableModelOptions.push(opt);
            }
        }
    }
}

function addStartupModel()
{
    startupModelsState.push({
        model: '',
        variant: '',
        context_size: 0,
        runtime_options: {},
        devices: [],
        api_format: ''
    });
    renderStartupModels();
}

async function removeStartupModel(index)
{
    const entry=startupModelsState[index];
    const modelName=entry.model;
    startupModelsState.splice(index, 1);
    renderStartupModels();

    // Unload the model if it was loaded
    if(modelName)
    {
        try
        {
            await fetch('/api/models/'+encodeURIComponent(modelName)+'/unload', {method: 'POST'});
        }
        catch(e) {}
    }

    // Save config without this model
    await saveAllStartupModels();
}

function updateStartupModelField(index, field, value)
{
    startupModelsState[index][field]=value;
    if(field==='modelVariant')
    {
        const parts=value.split('||');
        startupModelsState[index].model=parts[0]||'';
        startupModelsState[index].variant=parts[1]||'';
        delete startupModelsState[index].modelVariant;

        // Set default context from model options
        const opt=availableModelOptions.find(o=>o.model===parts[0]&&o.variant===(parts[1]||''));
        if(opt&&opt.effective_context_size)
        {
            startupModelsState[index].context_size=opt.effective_context_size;
        }
        // Set default api_format from model options
        if(opt&&opt.api_format)
        {
            startupModelsState[index].api_format=opt.api_format;
        }
        else
        {
            startupModelsState[index].api_format='';
        }
        renderStartupModels();
    }
}

function updateStartupModelDevice(index, gpuIndex, checked)
{
    const entry=startupModelsState[index];
    if(checked)
    {
        if(!entry.devices.includes(gpuIndex))
            entry.devices.push(gpuIndex);
        entry.devices.sort();
    }
    else
    {
        entry.devices=entry.devices.filter(d=>d!==gpuIndex);
    }
    updateStartupModelSliderGradient(index);
}

function updateStartupModelContext(index, value)
{
    const parsed=parseInt(value, 10)||0;
    startupModelsState[index].context_size=parsed;
    const label=document.getElementById('smCtxLabel_'+index);
    if(label) label.textContent=parsed===0?'Auto':formatContextSize(parsed);
    const autoInfo=document.getElementById('smCtxAutoInfo_'+index);
    if(autoInfo) autoInfo.style.display=parsed===0?'block':'none';
    updateStartupModelSliderGradient(index);
}

function getSelectedDevicesVram(index)
{
    const entry=startupModelsState[index];
    if(!entry||!entry.devices||entry.devices.length===0)
    {
        // No devices selected: sum all GPU VRAM
        let total=0;
        for(const gpu of availableGpus) total+=gpu.vram_total_mb||0;
        return total;
    }
    let total=0;
    for(const gpuIdx of entry.devices)
    {
        const gpu=availableGpus.find(g=>g.index===gpuIdx);
        if(gpu) total+=gpu.vram_total_mb||0;
    }
    return total;
}

function updateStartupModelSliderGradient(index)
{
    const slider=document.getElementById('smCtxSlider_'+index);
    if(!slider) return;
    const entry=startupModelsState[index];
    const modelOpt=availableModelOptions.find(o=>o.model===entry.model&&o.variant===entry.variant);

    if(!modelOpt||!modelOpt.memory_per_1k_context_mb||modelOpt.memory_per_1k_context_mb<=0)
    {
        slider.style.background='#32384b';
        return;
    }

    const min=parseInt(slider.min);
    const max=parseInt(slider.max);
    const range=max-min;
    if(range<=0){ slider.style.background='#32384b'; return; }

    const baseMemory=modelOpt.base_memory_mb||0;
    const baseContext=modelOpt.base_context_size||0;
    const memPer1k=modelOpt.memory_per_1k_context_mb;
    const availableMemory=getSelectedDevicesVram(index);
    if(availableMemory<=0){ slider.style.background='#32384b'; return; }

    const likelyCtx=baseContext+((0.85*availableMemory-baseMemory)/memPer1k)*1024;
    const tightCtx=baseContext+((availableMemory-baseMemory)/memPer1k)*1024;

    // Hard max: clamp slider to the tight max
    const hardMaxCtx=Math.floor(tightCtx/1024)*1024;
    if(hardMaxCtx>0&&hardMaxCtx<max)
    {
        slider.max=Math.max(hardMaxCtx, min);
        if(parseInt(slider.value)>parseInt(slider.max))
        {
            slider.value=slider.max;
            updateStartupModelContext(index, slider.value);
            return;
        }
    }

    const likelyPct=Math.max(0, Math.min(100, ((likelyCtx-min)/range)*100));
    const tightPct=Math.max(0, Math.min(100, ((tightCtx-min)/range)*100));

    if(likelyPct>=100)
    {
        slider.style.background='linear-gradient(to right, rgba(76,175,80,0.35) 0%, rgba(76,175,80,0.35) 100%)';
    }
    else if(tightPct<=0)
    {
        slider.style.background='linear-gradient(to right, rgba(255,96,96,0.35) 0%, rgba(255,96,96,0.35) 100%)';
    }
    else
    {
        slider.style.background='linear-gradient(to right, rgba(76,175,80,0.35) 0%, rgba(76,175,80,0.35) '+likelyPct+'%, rgba(240,192,64,0.35) '+likelyPct+'%, rgba(240,192,64,0.35) '+tightPct+'%, rgba(255,96,96,0.35) '+tightPct+'%, rgba(255,96,96,0.35) 100%)';
    }
}

function readStartupModelRuntimeOpts(index)
{
    const opts={};
    const kvk=document.getElementById('smKvk_'+index);
    const kvv=document.getElementById('smKvv_'+index);
    const fa=document.getElementById('smFlashAttn_'+index);
    const vkNhv=document.getElementById('smVkNoHostVis_'+index);
    if(kvk&&kvk.value) opts.kv_cache_type_k=kvk.value;
    if(kvv&&kvv.value) opts.kv_cache_type_v=kvv.value;
    if(fa&&fa.value==='true') opts.flash_attn=true;
    else if(fa&&fa.value==='false') opts.flash_attn=false;
    if(vkNhv&&vkNhv.value==='true') opts.vulkan_no_host_visible_vram=true;
    else if(vkNhv&&vkNhv.value==='false') opts.vulkan_no_host_visible_vram=false;
    return opts;
}

function toggleStartupModelRuntime(index)
{
    const body=document.getElementById('smRuntimeBody_'+index);
    const chevron=document.getElementById('smRuntimeChevron_'+index);
    const isOpen=body.classList.toggle('open');
    chevron.innerHTML=isOpen?'&#9660;':'&#9654;';
}

function renderStartupModels()
{
    const el=document.getElementById('startupModelsList');

    if(startupModelsState.length===0)
    {
        el.innerHTML='<div style="color:#666;text-align:center;padding:16px;">No startup models configured. Click "+ Add Model" to add one.</div>';
        return;
    }

    let html='';
    for(let i=0; i<startupModelsState.length; i++)
    {
        const entry=startupModelsState[i];
        const selectedValue=entry.model+'||'+entry.variant;

        // Model selector
        let modelOptions='<option value="">-- Select Model --</option>';
        for(const opt of availableModelOptions)
        {
            const val=opt.model+'||'+(opt.variant||'');
            const label=formatStartupModelLabel(opt.model, opt.variant);
            const sel=val===selectedValue?' selected':'';
            modelOptions+='<option value="'+escapeHtml(val)+'"'+sel+'>'+escapeHtml(label)+'</option>';
        }

        // Context slider
        const modelOpt=availableModelOptions.find(o=>o.model===entry.model&&o.variant===entry.variant);
        const maxCtx=modelOpt?modelOpt.max_context_size||131072:131072;
        const ctxVal=entry.context_size>0?Math.min(entry.context_size, maxCtx):0;
        const ctxLabel=ctxVal===0?'Auto':formatContextSize(ctxVal);
        const isAutoCtx=ctxVal===0;

        // Device checkboxes
        let devicesHtml='';
        for(const gpu of availableGpus)
        {
            const checked=entry.devices.includes(gpu.index)?'checked':'';
            devicesHtml+='<label style="display:inline-flex;align-items:center;gap:4px;margin-right:12px;cursor:pointer;">'
                +'<input type="checkbox" '+checked+' onchange="updateStartupModelDevice('+i+', '+gpu.index+', this.checked)">'
                +'<span style="font-size:13px;">GPU'+gpu.index+': '+escapeHtml(gpu.name)+' ('+formatMb(gpu.vram_total_mb)+')</span>'
                +'</label>';
        }

        // Runtime options
        const ro=entry.runtime_options||{};

        // API format (from model catalog or overridden)
        const currentApiFormat=entry.api_format||(modelOpt&&modelOpt.api_format?modelOpt.api_format:'')||'';

        html+='<div style="border:1px solid #2a2d3a;border-radius:8px;padding:14px;margin-bottom:10px;background:#13151d;">'
            +'<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;">'
            +'<span style="font-weight:600;color:#f1f4ff;">Model '+(i+1)+'</span>'
            +'<div style="display:flex;gap:6px;">'
            +'<button class="btn" style="font-size:12px;padding:4px 10px;" id="smSaveBtn_'+i+'" onclick="saveStartupModelEntry('+i+')">Save &amp; Load</button>'
            +'<button class="btn btn-danger" style="font-size:12px;padding:4px 10px;" onclick="removeStartupModel('+i+')">Remove</button>'
            +'</div>'
            +'</div>'
            +'<div class="settings-message" id="smStatus_'+i+'" style="margin-bottom:8px;"></div>'
            +'<div style="margin-bottom:10px;">'
            +'<label class="startup-field-label">Model &amp; Variant</label>'
            +'<select style="width:100%;padding:8px;background:#1a1d27;color:#e0e0e0;border:1px solid #32384b;border-radius:6px;font-size:14px;" '
            +'onchange="updateStartupModelField('+i+', \'modelVariant\', this.value)">'+modelOptions+'</select>'
            +'</div>'
            +'<div style="margin-bottom:10px;">'
            +'<label class="startup-field-label">Context Size <span class="context-slider-value" id="smCtxLabel_'+i+'">'+ctxLabel+'</span></label>'
            +'<input type="range" class="context-slider" id="smCtxSlider_'+i+'" min="0" max="'+maxCtx+'" step="1024" value="'+ctxVal+'" '
            +'oninput="updateStartupModelContext('+i+', this.value)">'
            +'<div id="smCtxAutoInfo_'+i+'" style="display:'+(isAutoCtx?'block':'none')+';margin-top:6px;padding:8px 10px;background:rgba(124,138,255,0.08);border:1px solid rgba(124,138,255,0.2);border-radius:6px;font-size:12px;color:#9eb0ff;">'
            +'\u2139\uFE0F <strong>Auto:</strong> The server will select the largest context size that fits in the available VRAM of the assigned device(s).'
            +'</div>'
            +'</div>'
            +'<div style="margin-bottom:10px;">'
            +'<label class="startup-field-label">Compute Devices</label>'
            +'<div style="display:flex;flex-wrap:wrap;gap:4px;">'+devicesHtml+'</div>'
            +(entry.devices.length===0?'<div style="color:#ff9800;font-size:12px;margin-top:4px;">No devices selected &mdash; auto-assignment will be used.</div>':'')
            +'</div>'
            +'<div style="margin-bottom:10px;">'
            +'<label class="startup-field-label">API Format</label>'
            +'<select class="runtime-opts-select" style="width:100%;" id="smApiFormat_'+i+'" onchange="updateStartupModelField('+i+', \'api_format\', this.value)">'
            +'<option value=""'+(currentApiFormat===''?' selected':'')+'>Standard (OpenAI)</option>'
            +'<option value="harmony"'+(currentApiFormat==='harmony'?' selected':'')+'>Harmony (auto-convert to OpenAI)</option>'
            +'</select>'
            +'<div style="margin-top:4px;font-size:12px;color:#8891a4;">When set to Harmony, the server parses channel tags from model output and converts to standard OpenAI format.</div>'
            +'</div>'
            +'<div class="runtime-opts-section">'
            +'<button type="button" class="runtime-opts-toggle" onclick="toggleStartupModelRuntime('+i+')">Runtime Options <span id="smRuntimeChevron_'+i+'">&#9654;</span></button>'
            +'<div class="runtime-opts-body" id="smRuntimeBody_'+i+'">'
            +'<div class="runtime-opts-row"><span class="runtime-opts-label">KV Cache (K)</span>'
            +'<select class="runtime-opts-select" id="smKvk_'+i+'"><option value="">Default</option><option value="f16"'+(ro.kv_cache_type_k==='f16'?' selected':'')+'>f16</option><option value="q8_0"'+(ro.kv_cache_type_k==='q8_0'?' selected':'')+'>q8_0</option><option value="q4_0"'+(ro.kv_cache_type_k==='q4_0'?' selected':'')+'>q4_0</option></select></div>'
            +'<div class="runtime-opts-row"><span class="runtime-opts-label">KV Cache (V)</span>'
            +'<select class="runtime-opts-select" id="smKvv_'+i+'"><option value="">Default</option><option value="f16"'+(ro.kv_cache_type_v==='f16'?' selected':'')+'>f16</option><option value="q8_0"'+(ro.kv_cache_type_v==='q8_0'?' selected':'')+'>q8_0</option><option value="q4_0"'+(ro.kv_cache_type_v==='q4_0'?' selected':'')+'>q4_0</option></select></div>'
            +'<div class="runtime-opts-row"><span class="runtime-opts-label">Flash Attention</span>'
            +'<select class="runtime-opts-select" id="smFlashAttn_'+i+'"><option value="">Default</option><option value="true"'+(ro.flash_attn===true?' selected':'')+'>Enabled</option><option value="false"'+(ro.flash_attn===false?' selected':'')+'>Disabled</option></select></div>'
            +'<div class="runtime-opts-row"><span class="runtime-opts-label">No Host-Visible VRAM</span>'
            +'<select class="runtime-opts-select" id="smVkNoHostVis_'+i+'"><option value="">Default</option><option value="true"'+(ro.vulkan_no_host_visible_vram===true?' selected':'')+'>Enabled</option><option value="false"'+(ro.vulkan_no_host_visible_vram===false?' selected':'')+'>Disabled</option></select></div>'
            +'</div></div>'
            +'</div>';
    }

    el.innerHTML=html;

    // Apply VRAM color gradients after DOM is updated
    for(let i=0; i<startupModelsState.length; i++)
    {
        updateStartupModelSliderGradient(i);
    }
}

async function saveAllStartupModels()
{
    // Collect runtime options from the UI before saving
    for(let i=0; i<startupModelsState.length; i++)
    {
        startupModelsState[i].runtime_options=readStartupModelRuntimeOpts(i);
    }

    const models=startupModelsState.filter(e=>e.model).map(e=>({
        model: e.model,
        variant: e.variant||'',
        context_size: e.context_size||0,
        runtime_options: e.runtime_options||{},
        devices: e.devices||[],
        api_format: e.api_format||''
    }));

    try
    {
        const response=await fetch('/api/server/config', {
            method: 'PUT',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({startup_models: models})
        });
        const data=await response.json();
        if(response.ok) serverConfigState=data;
        return response.ok;
    }
    catch(e)
    {
        return false;
    }
}

function showModelStatus(index, text, state)
{
    const el=document.getElementById('smStatus_'+index);
    if(!el) return;
    el.textContent=text||'';
    el.className='settings-message';
    if(state==='success') el.classList.add('success');
    if(state==='error') el.classList.add('error');
}

async function saveStartupModelEntry(index)
{
    const entry=startupModelsState[index];
    if(!entry.model)
    {
        showModelStatus(index, 'Select a model first.', 'error');
        return;
    }

    const btn=document.getElementById('smSaveBtn_'+index);
    btn.disabled=true;

    // Collect this entry's runtime opts from UI
    entry.runtime_options=readStartupModelRuntimeOpts(index);

    // Read api_format from UI
    const apiFormatEl=document.getElementById('smApiFormat_'+index);
    if(apiFormatEl) entry.api_format=apiFormatEl.value||'';

    showModelStatus(index, 'Saving config...', '');

    // Update model catalog api_format if changed
    if(entry.model&&entry.api_format!==undefined)
    {
        try
        {
            await fetch('/api/models/config', {
                method: 'PUT',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({model: entry.model, api_format: entry.api_format})
            });
        }
        catch(e) {}
    }

    // Save all models to config
    const saved=await saveAllStartupModels();
    if(!saved)
    {
        showModelStatus(index, 'Failed to save config.', 'error');
        btn.disabled=false;
        return;
    }

    // Now load the model immediately
    showModelStatus(index, 'Loading model...', '');

    try
    {
        const body={
            variant: entry.variant||'',
            context_size: entry.context_size||0,
            runtime_options: entry.runtime_options||{},
            devices: entry.devices||[]
        };

        const resp=await fetch('/api/models/'+encodeURIComponent(entry.model)+'/load', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(body)
        });
        const data=await resp.json();

        if(resp.ok)
        {
            if(data.status==='downloading')
            {
                showModelStatus(index, 'Model is downloading. It will load automatically when ready.', 'success');
            }
            else
            {
                showModelStatus(index, 'Model loaded successfully.', 'success');
            }
        }
        else
        {
            const errMsg=(data.error&&data.error.message)||'Load failed (HTTP '+resp.status+')';
            showModelStatus(index, errMsg, 'error');
        }
    }
    catch(e)
    {
        showModelStatus(index, 'Network error loading model.', 'error');
    }
    finally
    {
        btn.disabled=false;
    }
}

// ── End New Startup Models Functions ──────────────────────────

async function loadVersion()
{
    const data=await fetchJson('/api/version');
    if(data)
    {
        document.getElementById('versionBadge').textContent='v'+data.version;
    }
}

async function initializePage()
{
    await loadVersion();
    loadGpuOverrides();
    await loadAvailableGpus();

    const config=await fetchJson('/api/server/config');
    if(!config)
    {
        document.getElementById('statusDot').style.background='#ff4444';
        document.getElementById('statusText').textContent='Disconnected';
        showMessage('Failed to load server configuration.', 'error');
        return;
    }

    document.getElementById('statusDot').style.background='#4caf50';
    document.getElementById('statusText').textContent='Connected';

    serverConfigState=config;

    useStartupModels=true;
    document.getElementById('startupModelsSection').style.display='';
    document.getElementById('legacyAcceleratorSection').style.display='none';

    await loadAvailableModels();

    // Show exactly what's in the server config file
    if(config.startup_models&&Array.isArray(config.startup_models))
    {
        startupModelsState=config.startup_models.map(e=>({
            model: e.model||'',
            variant: e.variant||'',
            context_size: e.context_size||0,
            runtime_options: e.runtime_options||{},
            devices: e.devices||[],
            api_format: e.api_format||''
        }));
    }
    else
    {
        startupModelsState=[];
    }

    renderStartupModels();
}

initializePage();
</script>
</body>
</html>)HTML";

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_DASHBOARDCONFIG_H_