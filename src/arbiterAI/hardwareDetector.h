#ifndef _ARBITERAI_HARDWAREDETECTOR_H_
#define _ARBITERAI_HARDWAREDETECTOR_H_

#include <string>
#include <vector>
#include <mutex>

namespace arbiterAI
{

enum class GpuBackend {
    None,
    CUDA,
    Vulkan
};

struct GpuInfo {
    int index=0;
    std::string name;
    GpuBackend backend=GpuBackend::None;
    int vramTotalMb=0;
    int vramFreeMb=0;
    float computeCapability=0.0f; // CUDA only, 0.0 for Vulkan
    float utilizationPercent=0.0f;
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

    // NVML dlopen handles
    bool loadNvml();
    void unloadNvml();

    // Vulkan dlopen handles
    bool loadVulkan();
    void unloadVulkan();

    SystemInfo m_systemInfo;
    mutable std::mutex m_mutex;

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

    // CPU utilization tracking (for delta calculation)
    long long m_prevCpuIdle=0;
    long long m_prevCpuTotal=0;
};

} // namespace arbiterAI

#endif//_ARBITERAI_HARDWAREDETECTOR_H_
