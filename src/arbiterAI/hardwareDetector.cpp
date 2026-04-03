#include "arbiterAI/hardwareDetector.h"

#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <cstring>

#ifdef __linux__
    #include <dlfcn.h>
#endif

// NVML type definitions (mirrors nvml.h without requiring the header)
namespace
{

typedef enum
{
    NVML_SUCCESS=0
} NvmlReturn;

struct NvmlDevice;
typedef NvmlDevice *NvmlDeviceHandle;

struct NvmlMemory
{
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

struct NvmlUtilization
{
    unsigned int gpu;
    unsigned int memory;
};

typedef NvmlReturn (*NvmlInitFunc)();
typedef NvmlReturn (*NvmlShutdownFunc)();
typedef NvmlReturn (*NvmlDeviceGetCountFunc)(unsigned int *);
typedef NvmlReturn (*NvmlDeviceGetHandleByIndexFunc)(unsigned int, NvmlDeviceHandle *);
typedef NvmlReturn (*NvmlDeviceGetNameFunc)(NvmlDeviceHandle, char *, unsigned int);
typedef NvmlReturn (*NvmlDeviceGetMemoryInfoFunc)(NvmlDeviceHandle, NvmlMemory *);
typedef NvmlReturn (*NvmlDeviceGetUtilizationRatesFunc)(NvmlDeviceHandle, NvmlUtilization *);
typedef NvmlReturn (*NvmlDeviceGetCudaComputeCapabilityFunc)(NvmlDeviceHandle, int *, int *);

// Vulkan type definitions (mirrors vulkan_core.h without requiring the header)
typedef enum
{
    VK_SUCCESS=0
} VkResult;

typedef enum
{
    VK_STRUCTURE_TYPE_APPLICATION_INFO=0,
    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO=1,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2=1000059006,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT=1000237000
} VkStructureType;

typedef enum
{
    VK_PHYSICAL_DEVICE_TYPE_OTHER=0,
    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU=1,
    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU=2,
    VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU=3,
    VK_PHYSICAL_DEVICE_TYPE_CPU=4
} VkPhysicalDeviceType;

typedef enum
{
    VK_MEMORY_HEAP_DEVICE_LOCAL_BIT=0x00000001
} VkMemoryHeapFlagBits;

typedef struct VkApplicationInfo {
    VkStructureType sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    VkStructureType sType;
    const void *pNext;
    uint32_t flags;
    const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;

struct VkInstance_T;
typedef VkInstance_T *VkInstance;

struct VkPhysicalDevice_T;
typedef VkPhysicalDevice_T *VkPhysicalDevice;

typedef struct alignas(8) VkPhysicalDeviceLimits {
    char padding[504]; // exact size and alignment of VkPhysicalDeviceLimits
} VkPhysicalDeviceLimits;

typedef struct VkPhysicalDeviceSparseProperties {
    uint32_t residencyStandard2DBlockShape;
    uint32_t residencyStandard2DMultisampleBlockShape;
    uint32_t residencyStandard3DBlockShape;
    uint32_t residencyAlignedMipSize;
    uint32_t residencyNonResidentStrict;
} VkPhysicalDeviceSparseProperties;

typedef struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType; // VkPhysicalDeviceType
    char deviceName[256]; // VK_MAX_PHYSICAL_DEVICE_NAME_SIZE
    uint8_t pipelineCacheUUID[16]; // VK_UUID_SIZE
    VkPhysicalDeviceLimits limits;
    VkPhysicalDeviceSparseProperties sparseProperties;
} VkPhysicalDeviceProperties;

typedef struct VkMemoryHeap {
    uint64_t size;
    uint32_t flags; // VkMemoryHeapFlags
} VkMemoryHeap;

typedef struct VkMemoryType {
    uint32_t propertyFlags;
    uint32_t heapIndex;
} VkMemoryType;

typedef struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32]; // VK_MAX_MEMORY_TYPES
    uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[16]; // VK_MAX_MEMORY_HEAPS
} VkPhysicalDeviceMemoryProperties;

typedef void *AllocationCallbacks;

typedef VkResult (*VkCreateInstanceFunc)(const VkInstanceCreateInfo *, const AllocationCallbacks *, VkInstance *);
typedef void (*VkDestroyInstanceFunc)(VkInstance, const AllocationCallbacks *);
typedef VkResult (*VkEnumeratePhysicalDevicesFunc)(VkInstance, uint32_t *, VkPhysicalDevice *);
typedef void (*VkGetPhysicalDevicePropertiesFunc)(VkPhysicalDevice, VkPhysicalDeviceProperties *);
typedef void (*VkGetPhysicalDeviceMemoryPropertiesFunc)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *);

// Vulkan 1.1+ / VK_EXT_memory_budget structures and function pointers

typedef struct VkPhysicalDeviceMemoryProperties2 {
    VkStructureType sType;
    void *pNext;
    VkPhysicalDeviceMemoryProperties memoryProperties;
} VkPhysicalDeviceMemoryProperties2;

typedef struct VkPhysicalDeviceMemoryBudgetPropertiesEXT {
    VkStructureType sType;
    void *pNext;
    uint64_t heapBudget[16]; // VK_MAX_MEMORY_HEAPS
    uint64_t heapUsage[16];
} VkPhysicalDeviceMemoryBudgetPropertiesEXT;

typedef struct VkExtensionProperties {
    char extensionName[256]; // VK_MAX_EXTENSION_NAME_SIZE
    uint32_t specVersion;
} VkExtensionProperties;

typedef void (*VkGetPhysicalDeviceMemoryProperties2Func)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2 *);
typedef VkResult (*VkEnumerateDeviceExtensionPropertiesFunc)(VkPhysicalDevice, const char *, uint32_t *, VkExtensionProperties *);

const int NVML_DEVICE_NAME_BUFFER_SIZE=96;

} // anonymous namespace

namespace arbiterAI
{

HardwareDetector &HardwareDetector::instance()
{
    static HardwareDetector detector;
    return detector;
}

HardwareDetector::HardwareDetector()
{
    loadNvml();
    loadVulkan();
    refresh();
}

HardwareDetector::~HardwareDetector()
{
    unloadNvml();
    unloadVulkan();
}

void HardwareDetector::refresh()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_systemInfo.gpus.clear();
    detectSystemRam();
    detectCpuInfo();
    detectCpuUtilization();
    detectNvmlGpus();
    detectVulkanGpus();
    detectUnifiedMemory();
    applyVramOverrides();

    m_firstRefreshDone=true;
}

SystemInfo HardwareDetector::getSystemInfo() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_systemInfo;
}

std::vector<GpuInfo> HardwareDetector::getGpus() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_systemInfo.gpus;
}

int HardwareDetector::getTotalFreeVramMb() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    int total=0;
    for(const GpuInfo &gpu:m_systemInfo.gpus)
    {
        total+=gpu.vramFreeMb;
    }
    return total;
}

int HardwareDetector::getTotalFreeRamMb() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_systemInfo.freeRamMb;
}

bool HardwareDetector::isNvmlAvailable() const
{
    return m_nvmlLoaded;
}

bool HardwareDetector::isVulkanAvailable() const
{
    return m_vulkanLoaded;
}

void HardwareDetector::setVramOverride(int gpuIndex, int vramMb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_vramOverrides[gpuIndex]=vramMb;

    // Apply immediately to current data
    for(GpuInfo &gpu:m_systemInfo.gpus)
    {
        if(gpu.index==gpuIndex)
        {
            // Preserve the actual used amount: free = newTotal - used
            int usedMb=gpu.vramTotalMb-gpu.vramFreeMb;
            gpu.vramTotalMb=vramMb;
            gpu.vramFreeMb=std::max(0, vramMb-usedMb);
            gpu.vramOverridden=true;

            if(gpu.unifiedMemory)
            {
                int accessibleUsedMb=gpu.gpuAccessibleRamMb-gpu.gpuAccessibleRamFreeMb;
                gpu.gpuAccessibleRamMb=vramMb;
                gpu.gpuAccessibleRamFreeMb=std::max(0, vramMb-accessibleUsedMb);
            }

            spdlog::info("VRAM override applied to GPU {}: {} MB", gpuIndex, vramMb);
            break;
        }
    }
}

void HardwareDetector::clearVramOverride(int gpuIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_vramOverrides.erase(gpuIndex);
    spdlog::info("VRAM override cleared for GPU {}", gpuIndex);
}

void HardwareDetector::clearAllVramOverrides()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_vramOverrides.clear();
    spdlog::info("All VRAM overrides cleared");
}

bool HardwareDetector::hasVramOverride(int gpuIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_vramOverrides.find(gpuIndex)!=m_vramOverrides.end();
}

int HardwareDetector::getVramOverride(int gpuIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it=m_vramOverrides.find(gpuIndex);

    if(it!=m_vramOverrides.end())
    {
        return it->second;
    }
    return 0;
}

void HardwareDetector::applyVramOverrides()
{
    for(GpuInfo &gpu:m_systemInfo.gpus)
    {
        auto it=m_vramOverrides.find(gpu.index);

        if(it==m_vramOverrides.end())
        {
            continue;
        }

        int overrideMb=it->second;

        // Preserve the actual used amount: free = newTotal - used
        int usedMb=gpu.vramTotalMb-gpu.vramFreeMb;
        gpu.vramTotalMb=overrideMb;
        gpu.vramFreeMb=std::max(0, overrideMb-usedMb);
        gpu.vramOverridden=true;

        if(gpu.unifiedMemory)
        {
            int accessibleUsedMb=gpu.gpuAccessibleRamMb-gpu.gpuAccessibleRamFreeMb;
            gpu.gpuAccessibleRamMb=overrideMb;
            gpu.gpuAccessibleRamFreeMb=std::max(0, overrideMb-accessibleUsedMb);
        }
    }
}

// --- System RAM detection ---

void HardwareDetector::detectSystemRam()
{
#ifdef __linux__
    std::ifstream meminfo("/proc/meminfo");
    if(!meminfo.is_open())
    {
        spdlog::warn("Failed to open /proc/meminfo");
        return;
    }

    std::string line;
    while(std::getline(meminfo, line))
    {
        long long value=0;
        if(line.find("MemTotal:")==0)
        {
            std::istringstream iss(line.substr(9));
            iss >> value; // in kB
            m_systemInfo.totalRamMb=static_cast<int>(value/1024);
        }
        else if(line.find("MemAvailable:")==0)
        {
            std::istringstream iss(line.substr(13));
            iss >> value; // in kB
            m_systemInfo.freeRamMb=static_cast<int>(value/1024);
        }
    }
#endif
}

// --- CPU info detection ---

void HardwareDetector::detectCpuInfo()
{
    m_systemInfo.cpuCores=static_cast<int>(std::thread::hardware_concurrency());
}

void HardwareDetector::detectCpuUtilization()
{
#ifdef __linux__
    std::ifstream stat("/proc/stat");
    if(!stat.is_open())
    {
        spdlog::warn("Failed to open /proc/stat");
        return;
    }

    std::string line;
    std::getline(stat, line);

    // Format: cpu user nice system idle iowait irq softirq steal
    if(line.find("cpu ")!=0)
    {
        return;
    }

    std::istringstream iss(line.substr(4));
    long long user=0, nice=0, system=0, idle=0, iowait=0, irq=0, softirq=0, steal=0;

    iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

    long long totalIdle=idle+iowait;
    long long total=user+nice+system+idle+iowait+irq+softirq+steal;

    if(m_prevCpuTotal>0)
    {
        long long totalDelta=total-m_prevCpuTotal;
        long long idleDelta=totalIdle-m_prevCpuIdle;

        if(totalDelta>0)
        {
            m_systemInfo.cpuUtilizationPercent=static_cast<float>(totalDelta-idleDelta)*100.0f/static_cast<float>(totalDelta);
        }
    }

    m_prevCpuIdle=totalIdle;
    m_prevCpuTotal=total;
#endif
}

// --- NVML GPU detection ---

bool HardwareDetector::loadNvml()
{
#ifdef __linux__
    m_nvmlLib=dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if(!m_nvmlLib)
    {
        m_nvmlLib=dlopen("libnvidia-ml.so", RTLD_LAZY);
    }
    if(!m_nvmlLib)
    {
        spdlog::debug("NVML not available: {}", dlerror());
        return false;
    }

    m_nvmlInit=dlsym(m_nvmlLib, "nvmlInit_v2");
    m_nvmlShutdown=dlsym(m_nvmlLib, "nvmlShutdown");
    m_nvmlDeviceGetCount=dlsym(m_nvmlLib, "nvmlDeviceGetCount_v2");
    m_nvmlDeviceGetHandleByIndex=dlsym(m_nvmlLib, "nvmlDeviceGetHandleByIndex_v2");
    m_nvmlDeviceGetName=dlsym(m_nvmlLib, "nvmlDeviceGetName");
    m_nvmlDeviceGetMemoryInfo=dlsym(m_nvmlLib, "nvmlDeviceGetMemoryInfo");
    m_nvmlDeviceGetUtilizationRates=dlsym(m_nvmlLib, "nvmlDeviceGetUtilizationRates");
    m_nvmlDeviceGetCudaComputeCapability=dlsym(m_nvmlLib, "nvmlDeviceGetCudaComputeCapability");

    if(!m_nvmlInit||!m_nvmlShutdown||!m_nvmlDeviceGetCount||
        !m_nvmlDeviceGetHandleByIndex||!m_nvmlDeviceGetName||
        !m_nvmlDeviceGetMemoryInfo)
    {
        spdlog::warn("NVML loaded but missing required symbols");
        unloadNvml();
        return false;
    }

    m_nvmlLoaded=true;
    spdlog::info("NVML loaded successfully");
    return true;
#else
    return false;
#endif
}

void HardwareDetector::unloadNvml()
{
#ifdef __linux__
    if(m_nvmlLib)
    {
        dlclose(m_nvmlLib);
        m_nvmlLib=nullptr;
    }
    m_nvmlLoaded=false;
    m_nvmlInit=nullptr;
    m_nvmlShutdown=nullptr;
    m_nvmlDeviceGetCount=nullptr;
    m_nvmlDeviceGetHandleByIndex=nullptr;
    m_nvmlDeviceGetName=nullptr;
    m_nvmlDeviceGetMemoryInfo=nullptr;
    m_nvmlDeviceGetUtilizationRates=nullptr;
    m_nvmlDeviceGetCudaComputeCapability=nullptr;
#endif
}

void HardwareDetector::detectNvmlGpus()
{
    if(!m_nvmlLoaded)
    {
        return;
    }

    NvmlInitFunc init=reinterpret_cast<NvmlInitFunc>(m_nvmlInit);
    NvmlShutdownFunc shutdown=reinterpret_cast<NvmlShutdownFunc>(m_nvmlShutdown);
    NvmlDeviceGetCountFunc getCount=reinterpret_cast<NvmlDeviceGetCountFunc>(m_nvmlDeviceGetCount);
    NvmlDeviceGetHandleByIndexFunc getHandle=reinterpret_cast<NvmlDeviceGetHandleByIndexFunc>(m_nvmlDeviceGetHandleByIndex);
    NvmlDeviceGetNameFunc getName=reinterpret_cast<NvmlDeviceGetNameFunc>(m_nvmlDeviceGetName);
    NvmlDeviceGetMemoryInfoFunc getMemory=reinterpret_cast<NvmlDeviceGetMemoryInfoFunc>(m_nvmlDeviceGetMemoryInfo);
    NvmlDeviceGetUtilizationRatesFunc getUtilization=reinterpret_cast<NvmlDeviceGetUtilizationRatesFunc>(m_nvmlDeviceGetUtilizationRates);
    NvmlDeviceGetCudaComputeCapabilityFunc getComputeCap=reinterpret_cast<NvmlDeviceGetCudaComputeCapabilityFunc>(m_nvmlDeviceGetCudaComputeCapability);

    if(init()!=NVML_SUCCESS)
    {
        spdlog::warn("NVML initialization failed");
        return;
    }

    unsigned int deviceCount=0;
    if(getCount(&deviceCount)!=NVML_SUCCESS)
    {
        spdlog::warn("Failed to get NVML device count");
        shutdown();
        return;
    }

    for(unsigned int i=0; i<deviceCount; ++i)
    {
        NvmlDeviceHandle device=nullptr;
        if(getHandle(i, &device)!=NVML_SUCCESS)
        {
            continue;
        }

        GpuInfo gpu;
        gpu.index=static_cast<int>(i);
        gpu.backend=GpuBackend::CUDA;

        char name[NVML_DEVICE_NAME_BUFFER_SIZE];
        if(getName(device, name, NVML_DEVICE_NAME_BUFFER_SIZE)==NVML_SUCCESS)
        {
            gpu.name=name;
        }

        NvmlMemory memory;
        if(getMemory(device, &memory)==NVML_SUCCESS)
        {
            gpu.vramTotalMb=static_cast<int>(memory.total/(1024*1024));
            gpu.vramFreeMb=static_cast<int>(memory.free/(1024*1024));
        }

        if(getUtilization)
        {
            NvmlUtilization utilization;
            if(getUtilization(device, &utilization)==NVML_SUCCESS)
            {
                gpu.utilizationPercent=static_cast<float>(utilization.gpu);
            }
        }

        if(getComputeCap)
        {
            int major=0, minor=0;
            if(getComputeCap(device, &major, &minor)==NVML_SUCCESS)
            {
                gpu.computeCapability=static_cast<float>(major)+static_cast<float>(minor)*0.1f;
            }
        }

        spdlog::info("NVML GPU {}: {} ({}MB VRAM, {}MB free, CC {:.1f})",
            gpu.index, gpu.name, gpu.vramTotalMb, gpu.vramFreeMb, gpu.computeCapability);

        m_systemInfo.gpus.push_back(gpu);
    }

    shutdown();
}

// --- Vulkan GPU detection ---

bool HardwareDetector::loadVulkan()
{
#ifdef __linux__
    m_vulkanLib=dlopen("libvulkan.so.1", RTLD_LAZY);
    if(!m_vulkanLib)
    {
        m_vulkanLib=dlopen("libvulkan.so", RTLD_LAZY);
    }
    if(!m_vulkanLib)
    {
        spdlog::debug("Vulkan not available: {}", dlerror());
        return false;
    }

    m_vkCreateInstance=dlsym(m_vulkanLib, "vkCreateInstance");
    m_vkDestroyInstance=dlsym(m_vulkanLib, "vkDestroyInstance");
    m_vkEnumeratePhysicalDevices=dlsym(m_vulkanLib, "vkEnumeratePhysicalDevices");
    m_vkGetPhysicalDeviceProperties=dlsym(m_vulkanLib, "vkGetPhysicalDeviceProperties");
    m_vkGetPhysicalDeviceMemoryProperties=dlsym(m_vulkanLib, "vkGetPhysicalDeviceMemoryProperties");

    // Vulkan 1.1+ optional symbols for memory budget queries
    m_vkGetPhysicalDeviceMemoryProperties2=dlsym(m_vulkanLib, "vkGetPhysicalDeviceMemoryProperties2");
    m_vkEnumerateDeviceExtensionProperties=dlsym(m_vulkanLib, "vkEnumerateDeviceExtensionProperties");

    if(!m_vkCreateInstance||!m_vkDestroyInstance||
        !m_vkEnumeratePhysicalDevices||!m_vkGetPhysicalDeviceProperties||
        !m_vkGetPhysicalDeviceMemoryProperties)
    {
        spdlog::warn("Vulkan loaded but missing required symbols");
        unloadVulkan();
        return false;
    }

    m_vulkanLoaded=true;
    spdlog::info("Vulkan loaded successfully");
    return true;
#else
    return false;
#endif
}

void HardwareDetector::unloadVulkan()
{
#ifdef __linux__
    if(m_vulkanLib)
    {
        dlclose(m_vulkanLib);
        m_vulkanLib=nullptr;
    }
    m_vulkanLoaded=false;
    m_vkCreateInstance=nullptr;
    m_vkDestroyInstance=nullptr;
    m_vkEnumeratePhysicalDevices=nullptr;
    m_vkGetPhysicalDeviceProperties=nullptr;
    m_vkGetPhysicalDeviceMemoryProperties=nullptr;
    m_vkGetPhysicalDeviceMemoryProperties2=nullptr;
    m_vkEnumerateDeviceExtensionProperties=nullptr;
#endif
}

void HardwareDetector::detectVulkanGpus()
{
    if(!m_vulkanLoaded)
    {
        return;
    }

    VkCreateInstanceFunc createInstance=reinterpret_cast<VkCreateInstanceFunc>(m_vkCreateInstance);
    VkDestroyInstanceFunc destroyInstance=reinterpret_cast<VkDestroyInstanceFunc>(m_vkDestroyInstance);
    VkEnumeratePhysicalDevicesFunc enumDevices=reinterpret_cast<VkEnumeratePhysicalDevicesFunc>(m_vkEnumeratePhysicalDevices);
    VkGetPhysicalDevicePropertiesFunc getProperties=reinterpret_cast<VkGetPhysicalDevicePropertiesFunc>(m_vkGetPhysicalDeviceProperties);
    VkGetPhysicalDeviceMemoryPropertiesFunc getMemProperties=reinterpret_cast<VkGetPhysicalDeviceMemoryPropertiesFunc>(m_vkGetPhysicalDeviceMemoryProperties);

    // Optional Vulkan 1.1+ function for memory budget queries
    VkGetPhysicalDeviceMemoryProperties2Func getMemProperties2=nullptr;
    VkEnumerateDeviceExtensionPropertiesFunc enumDeviceExtensions=nullptr;

    if(m_vkGetPhysicalDeviceMemoryProperties2)
    {
        getMemProperties2=reinterpret_cast<VkGetPhysicalDeviceMemoryProperties2Func>(m_vkGetPhysicalDeviceMemoryProperties2);
    }
    if(m_vkEnumerateDeviceExtensionProperties)
    {
        enumDeviceExtensions=reinterpret_cast<VkEnumerateDeviceExtensionPropertiesFunc>(m_vkEnumerateDeviceExtensionProperties);
    }

    // Create a Vulkan instance — request API 1.1 for vkGetPhysicalDeviceMemoryProperties2
    VkApplicationInfo appInfo{};
    appInfo.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName="ArbiterAI HardwareDetector";
    appInfo.applicationVersion=1;
    appInfo.apiVersion=(1u<<22)|(1u<<12)|0u; // VK_MAKE_API_VERSION(0,1,1,0)

    VkInstanceCreateInfo createInfo{};
    createInfo.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo=&appInfo;

    VkInstance vkInstance=nullptr;
    if(createInstance(&createInfo, nullptr, &vkInstance)!=VK_SUCCESS)
    {
        spdlog::warn("Failed to create Vulkan instance for hardware detection");
        return;
    }

    // Enumerate physical devices
    uint32_t deviceCount=0;
    enumDevices(vkInstance, &deviceCount, nullptr);
    if(deviceCount==0)
    {
        spdlog::info("No Vulkan physical devices found");
        destroyInstance(vkInstance, nullptr);
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    enumDevices(vkInstance, &deviceCount, devices.data());

    int gpuOffset=static_cast<int>(m_systemInfo.gpus.size());

    for(uint32_t i=0; i<deviceCount; ++i)
    {
        VkPhysicalDeviceProperties props{};
        getProperties(devices[i], &props);

        // Skip CPU-based software renderers (e.g. llvmpipe) — they report
        // system RAM as VRAM and are not useful for model inference.
        if(props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)
        {
            spdlog::debug("Skipping Vulkan software renderer: {}", props.deviceName);
            continue;
        }

        // Skip devices already detected via NVML (match by name)
        bool alreadyDetected=false;
        for(const GpuInfo &existing:m_systemInfo.gpus)
        {
            if(existing.name==props.deviceName)
            {
                alreadyDetected=true;
                break;
            }
        }
        if(alreadyDetected)
        {
            continue;
        }

        // Check if VK_EXT_memory_budget is supported on this device
        bool hasBudgetExt=false;
        if(enumDeviceExtensions)
        {
            uint32_t extCount=0;
            if(enumDeviceExtensions(devices[i], nullptr, &extCount, nullptr)==VK_SUCCESS&&extCount>0)
            {
                std::vector<VkExtensionProperties> exts(extCount);
                if(enumDeviceExtensions(devices[i], nullptr, &extCount, exts.data())==VK_SUCCESS)
                {
                    for(uint32_t e=0; e<extCount; ++e)
                    {
                        if(std::strcmp(exts[e].extensionName, "VK_EXT_memory_budget")==0)
                        {
                            hasBudgetExt=true;
                            break;
                        }
                    }
                }
            }
        }

        bool isIntegrated=(props.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
        GpuInfo gpu;
        gpu.index=gpuOffset+static_cast<int>(i);
        gpu.name=props.deviceName;
        gpu.backend=GpuBackend::Vulkan;
        gpu.unifiedMemory=isIntegrated;

        // Use VK_EXT_memory_budget for accurate per-heap budget/usage data.
        // This is the authoritative runtime signal for how much memory
        // the GPU can actually use, especially on UMA/APU systems where
        // heap sizes alone can be misleading.
        if(hasBudgetExt&&getMemProperties2)
        {
            VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps{};
            budgetProps.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

            VkPhysicalDeviceMemoryProperties2 memProps2{};
            memProps2.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
            memProps2.pNext=&budgetProps;

            getMemProperties2(devices[i], &memProps2);

            const VkPhysicalDeviceMemoryProperties &mp=memProps2.memoryProperties;

            // Sum DEVICE_LOCAL heaps — on discrete GPUs this is dedicated VRAM,
            // on UMA systems this is the GPU-accessible portion of system RAM.
            uint64_t deviceLocalBudgetBytes=0;
            uint64_t deviceLocalUsageBytes=0;
            uint64_t deviceLocalSizeBytes=0;

            for(uint32_t h=0; h<mp.memoryHeapCount; ++h)
            {
                bool deviceLocal=(mp.memoryHeaps[h].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)!=0;

                spdlog::debug("Vulkan GPU {}: heap {} — {} size={:.0f}MB budget={:.0f}MB usage={:.0f}MB",
                    gpu.index, h,
                    deviceLocal ? "DEVICE_LOCAL" : "host",
                    static_cast<double>(mp.memoryHeaps[h].size)/(1024.0*1024.0),
                    static_cast<double>(budgetProps.heapBudget[h])/(1024.0*1024.0),
                    static_cast<double>(budgetProps.heapUsage[h])/(1024.0*1024.0));

                MemoryHeapInfo heapInfo;
                heapInfo.index=static_cast<int>(h);
                heapInfo.deviceLocal=deviceLocal;
                heapInfo.sizeMb=static_cast<int>(mp.memoryHeaps[h].size/(1024ULL*1024ULL));
                heapInfo.budgetMb=static_cast<int>(budgetProps.heapBudget[h]/(1024ULL*1024ULL));
                heapInfo.usageMb=static_cast<int>(budgetProps.heapUsage[h]/(1024ULL*1024ULL));
                gpu.memoryHeaps.push_back(heapInfo);

                if(deviceLocal)
                {
                    deviceLocalSizeBytes+=mp.memoryHeaps[h].size;
                    deviceLocalBudgetBytes+=budgetProps.heapBudget[h];
                    deviceLocalUsageBytes+=budgetProps.heapUsage[h];
                }
            }

            gpu.hasMemoryBudget=true;

            // Budget is the best estimate of how much this process can allocate.
            // On UMA, budget may be significantly larger than the raw heap size
            // (driver exposes most of system RAM as available to the GPU).
            uint64_t budgetTotalMb=deviceLocalBudgetBytes/(1024ULL*1024ULL);
            uint64_t budgetUsedMb=deviceLocalUsageBytes/(1024ULL*1024ULL);
            uint64_t heapSizeMb=deviceLocalSizeBytes/(1024ULL*1024ULL);

            // Use the larger of heap size and budget for total — on some UMA
            // drivers the budget exceeds the reported heap size.
            uint64_t effectiveTotalMb=(budgetTotalMb>heapSizeMb) ? budgetTotalMb : heapSizeMb;
            uint64_t effectiveFreeMb=(deviceLocalBudgetBytes>deviceLocalUsageBytes)
                ? (deviceLocalBudgetBytes-deviceLocalUsageBytes)/(1024ULL*1024ULL)
                : 0;

            gpu.vramTotalMb=static_cast<int>(effectiveTotalMb);
            gpu.vramFreeMb=static_cast<int>(effectiveFreeMb);

            if(isIntegrated)
            {
                // For UMA, the GPU-accessible pool is the DEVICE_LOCAL budget —
                // this is the closest Vulkan gives to "how much memory the GPU
                // can actually use right now" on unified-memory systems.
                gpu.gpuAccessibleRamMb=static_cast<int>(effectiveTotalMb);
                gpu.gpuAccessibleRamFreeMb=static_cast<int>(effectiveFreeMb);
            }

            spdlog::log(m_firstRefreshDone ? spdlog::level::debug : spdlog::level::info,
                "Vulkan GPU {}: {} (budget: {}MB total, {}MB free, "
                "heap size: {}MB, integrated={}, memoryBudget=true)",
                gpu.index, gpu.name,
                gpu.vramTotalMb, gpu.vramFreeMb,
                static_cast<int>(heapSizeMb), gpu.unifiedMemory);
        }
        else
        {
            // Fallback: no memory budget extension — use raw heap sizes.
            // Free VRAM is unknown, approximate as total.
            VkPhysicalDeviceMemoryProperties memProps{};
            getMemProperties(devices[i], &memProps);

            int vramTotalMb=0;
            for(uint32_t h=0; h<memProps.memoryHeapCount; ++h)
            {
                bool deviceLocal=(memProps.memoryHeaps[h].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)!=0;

                MemoryHeapInfo heapInfo;
                heapInfo.index=static_cast<int>(h);
                heapInfo.deviceLocal=deviceLocal;
                heapInfo.sizeMb=static_cast<int>(memProps.memoryHeaps[h].size/(1024ULL*1024ULL));
                gpu.memoryHeaps.push_back(heapInfo);

                if(deviceLocal)
                {
                    vramTotalMb+=static_cast<int>(memProps.memoryHeaps[h].size/(1024*1024));
                }
            }

            gpu.vramTotalMb=vramTotalMb;
            gpu.vramFreeMb=vramTotalMb;

            spdlog::log(m_firstRefreshDone ? spdlog::level::debug : spdlog::level::info,
                "Vulkan GPU {}: {} ({}MB VRAM, integrated={}, memoryBudget=false)",
                gpu.index, gpu.name, gpu.vramTotalMb, gpu.unifiedMemory);
        }

        m_systemInfo.gpus.push_back(gpu);
    }

    destroyInstance(vkInstance, nullptr);
}

// --- Unified memory detection via sysfs (amdgpu) ---
// Supplements Vulkan memory budget data with amdgpu kernel-side diagnostics.
// If VK_EXT_memory_budget already provided gpuAccessibleRam, sysfs is used
// only for logging correlation. If budget was not available, sysfs provides
// the fallback for GPU-accessible memory detection.

void HardwareDetector::detectUnifiedMemory()
{
#ifdef __linux__
    for(GpuInfo &gpu:m_systemInfo.gpus)
    {
        if(!gpu.unifiedMemory)
        {
            continue;
        }

        // If Vulkan budget already set gpuAccessibleRamMb, we have good data.
        // Still attempt sysfs for diagnostic logging, but don't overwrite.
        bool hasBudgetData=(gpu.gpuAccessibleRamMb>0);

        // Find the matching DRM card by scanning /sys/class/drm/card*
        // Match by reading mem_info_vram_total from sysfs and comparing
        // to the basic Vulkan heap size. Note: when budget is available,
        // gpu.vramTotalMb may reflect the budget (much larger on UMA),
        // so we read the raw sysfs VRAM and look for any reasonable match.
        std::string matchedCardPath;

        for(int card=0; card<16; ++card)
        {
            std::string cardPath="/sys/class/drm/card"+std::to_string(card)+"/device";
            std::string vramTotalPath=cardPath+"/mem_info_vram_total";

            std::ifstream testFile(vramTotalPath);
            if(!testFile.is_open())
            {
                continue;
            }

            // On UMA APUs, the sysfs VRAM is typically a small carveout
            // (e.g. 512MB) that doesn't match the Vulkan budget-derived
            // vramTotalMb. Accept the match if this is the only amdgpu card
            // with sysfs data available, or if the values are close.
            long long vramBytes=0;
            testFile >> vramBytes;

            // Just accept the first card that has sysfs data for UMA —
            // multi-GPU UMA systems are extremely rare.
            matchedCardPath=cardPath;
            break;
        }

        if(matchedCardPath.empty())
        {
            if(!hasBudgetData)
            {
                // No sysfs and no budget — fall back to system RAM
                gpu.gpuAccessibleRamMb=m_systemInfo.totalRamMb;
                gpu.gpuAccessibleRamFreeMb=m_systemInfo.freeRamMb;

                spdlog::log(m_firstRefreshDone ? spdlog::level::debug : spdlog::level::info,
                    "Unified memory GPU {}: {} — no sysfs or budget data, "
                    "falling back to system RAM ({}MB total, {}MB free)",
                    gpu.index, gpu.name,
                    gpu.gpuAccessibleRamMb, gpu.gpuAccessibleRamFreeMb);
            }
            continue;
        }

        // Read sysfs VRAM and GTT for diagnostic logging
        long long sysfsVramTotal=0, sysfsVramUsed=0;
        long long gttTotalBytes=0, gttUsedBytes=0;

        {
            std::ifstream file(matchedCardPath+"/mem_info_vram_total");
            if(file.is_open()) file >> sysfsVramTotal;
        }
        {
            std::ifstream file(matchedCardPath+"/mem_info_vram_used");
            if(file.is_open()) file >> sysfsVramUsed;
        }
        {
            std::ifstream file(matchedCardPath+"/mem_info_gtt_total");
            if(file.is_open()) file >> gttTotalBytes;
        }
        {
            std::ifstream file(matchedCardPath+"/mem_info_gtt_used");
            if(file.is_open()) file >> gttUsedBytes;
        }

        int sysfsVramTotalMb=static_cast<int>(sysfsVramTotal/(1024LL*1024LL));
        int sysfsVramUsedMb=static_cast<int>(sysfsVramUsed/(1024LL*1024LL));
        int gttTotalMb=static_cast<int>(gttTotalBytes/(1024LL*1024LL));
        int gttUsedMb=static_cast<int>(gttUsedBytes/(1024LL*1024LL));
        int gttFreeMb=gttTotalMb-gttUsedMb;

        if(gttFreeMb<0) gttFreeMb=0;

        spdlog::log(m_firstRefreshDone ? spdlog::level::debug : spdlog::level::info,
            "Unified memory GPU {}: {} — sysfs: VRAM {}MB ({}MB used), "
            "GTT {}MB ({}MB used), budgetAlreadySet={}",
            gpu.index, gpu.name,
            sysfsVramTotalMb, sysfsVramUsedMb,
            gttTotalMb, gttUsedMb, hasBudgetData);

        if(hasBudgetData)
        {
            // Vulkan budget is authoritative for allocation decisions.
            // Log sysfs as a diagnostic side channel only.
            continue;
        }

        // No Vulkan budget — use sysfs data for GPU-accessible memory.
        // Refine VRAM free from sysfs (more accurate than "assume all free").
        if(sysfsVramTotal>0)
        {
            gpu.vramTotalMb=sysfsVramTotalMb;
            gpu.vramFreeMb=sysfsVramTotalMb-sysfsVramUsedMb;
            if(gpu.vramFreeMb<0) gpu.vramFreeMb=0;
        }

        if(gttTotalBytes>0)
        {
            // GPU-accessible memory = VRAM + GTT (system RAM mapped to GPU)
            gpu.gpuAccessibleRamMb=gpu.vramTotalMb+gttTotalMb;
            gpu.gpuAccessibleRamFreeMb=gpu.vramFreeMb+gttFreeMb;

            spdlog::log(m_firstRefreshDone ? spdlog::level::debug : spdlog::level::info,
                "Unified memory GPU {}: {} — sysfs fallback: "
                "total accessible {}MB ({}MB free)",
                gpu.index, gpu.name,
                gpu.gpuAccessibleRamMb, gpu.gpuAccessibleRamFreeMb);
        }
        else
        {
            // No GTT info — fall back to system RAM
            gpu.gpuAccessibleRamMb=m_systemInfo.totalRamMb;
            gpu.gpuAccessibleRamFreeMb=m_systemInfo.freeRamMb;

            spdlog::log(m_firstRefreshDone ? spdlog::level::debug : spdlog::level::info,
                "Unified memory GPU {}: {} — no GTT info, "
                "falling back to system RAM ({}MB total, {}MB free)",
                gpu.index, gpu.name,
                gpu.gpuAccessibleRamMb, gpu.gpuAccessibleRamFreeMb);
        }
    }
#endif
}

} // namespace arbiterAI
