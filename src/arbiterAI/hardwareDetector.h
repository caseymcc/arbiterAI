#ifndef _ARBITERAI_HARDWAREDETECTOR_H_
#define _ARBITERAI_HARDWAREDETECTOR_H_

#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace arbiterAI
{

enum class GpuBackend {
    None,
    CUDA,
    Vulkan
};

struct MemoryHeapInfo {
    int index=0;
    bool deviceLocal=false;
    int sizeMb=0;
    int budgetMb=0;    // 0 if VK_EXT_memory_budget not available
    int usageMb=0;     // 0 if VK_EXT_memory_budget not available
};

struct GpuInfo {
    int index=0;
    std::string name;
    GpuBackend backend=GpuBackend::None;
    int vramTotalMb=0;
    int vramFreeMb=0;
    float computeCapability=0.0f; // CUDA only, 0.0 for Vulkan
    float utilizationPercent=0.0f;
    bool unifiedMemory=false;     // true for APUs/iGPUs sharing system RAM
    int gpuAccessibleRamMb=0;     // total RAM the GPU can access (GTT+VRAM on APUs, 0 for discrete)
    int gpuAccessibleRamFreeMb=0; // free RAM the GPU can access
    bool hasMemoryBudget=false;   // true if VK_EXT_memory_budget was used
    bool vramOverridden=false;    // true if VRAM values were overridden by user
    std::vector<MemoryHeapInfo> memoryHeaps; // per-heap details from Vulkan
};

struct SystemInfo {
    int totalRamMb=0;
    int freeRamMb=0;
    int cpuCores=0;
    float cpuUtilizationPercent=0.0f;
    std::vector<GpuInfo> gpus;
};

class HardwareDetector {
public:
    static HardwareDetector &instance();

    /// Refresh all hardware info (call periodically or on demand)
    void refresh();

    /// Get a snapshot of all system hardware info
    SystemInfo getSystemInfo() const;

    /// Get detected GPU list
    std::vector<GpuInfo> getGpus() const;

    /// Get total free VRAM across all GPUs in MB
    int getTotalFreeVramMb() const;

    /// Get free system RAM in MB
    int getTotalFreeRamMb() const;

    /// Check whether NVML (NVIDIA) is available at runtime
    bool isNvmlAvailable() const;

    /// Check whether Vulkan is available at runtime
    bool isVulkanAvailable() const;

    /// Override the reported VRAM for a GPU (persists across refresh cycles).
    /// For unified memory devices, also overrides gpuAccessibleRamMb.
    void setVramOverride(int gpuIndex, int vramMb);

    /// Clear a VRAM override for a specific GPU
    void clearVramOverride(int gpuIndex);

    /// Clear all VRAM overrides
    void clearAllVramOverrides();

    /// Check whether a GPU has a VRAM override set
    bool hasVramOverride(int gpuIndex) const;

    /// Get the VRAM override value for a GPU (0 if not set)
    int getVramOverride(int gpuIndex) const;

private:
    HardwareDetector();
    ~HardwareDetector();

    HardwareDetector(const HardwareDetector &)=delete;
    HardwareDetector &operator=(const HardwareDetector &)=delete;

    void detectSystemRam();
    void detectCpuInfo();
    void detectCpuUtilization();
    void detectNvmlGpus();
    void detectVulkanGpus();
    void detectUnifiedMemory();
    void applyVramOverrides();

    // NVML dlopen handles
    bool loadNvml();
    void unloadNvml();

    // Vulkan dlopen handles
    bool loadVulkan();
    void unloadVulkan();

    SystemInfo m_systemInfo;
    mutable std::mutex m_mutex;
    std::map<int, int> m_vramOverrides; // gpuIndex → vramMb

    // Runtime library handles
    void *m_nvmlLib=nullptr;
    void *m_vulkanLib=nullptr;
    bool m_nvmlLoaded=false;
    bool m_vulkanLoaded=false;

    // NVML function pointers
    void *m_nvmlInit=nullptr;
    void *m_nvmlShutdown=nullptr;
    void *m_nvmlDeviceGetCount=nullptr;
    void *m_nvmlDeviceGetHandleByIndex=nullptr;
    void *m_nvmlDeviceGetName=nullptr;
    void *m_nvmlDeviceGetMemoryInfo=nullptr;
    void *m_nvmlDeviceGetUtilizationRates=nullptr;
    void *m_nvmlDeviceGetCudaComputeCapability=nullptr;

    // Vulkan function pointers
    void *m_vkCreateInstance=nullptr;
    void *m_vkDestroyInstance=nullptr;
    void *m_vkEnumeratePhysicalDevices=nullptr;
    void *m_vkGetPhysicalDeviceProperties=nullptr;
    void *m_vkGetPhysicalDeviceMemoryProperties=nullptr;
    void *m_vkGetPhysicalDeviceMemoryProperties2=nullptr;
    void *m_vkEnumerateDeviceExtensionProperties=nullptr;

    // CPU utilization tracking (for delta calculation)
    long long m_prevCpuIdle=0;
    long long m_prevCpuTotal=0;

    // First refresh logs at info, subsequent refreshes at debug
    bool m_firstRefreshDone=false;
};

} // namespace arbiterAI

#endif//_ARBITERAI_HARDWAREDETECTOR_H_
