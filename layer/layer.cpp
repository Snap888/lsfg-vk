// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// layer/layer.cpp — Entry point, dispatch table wiring, function overrides

#include "layer.h"
#include "swapchain_manager.h"
#include "frame_generator.h"

#include <vulkan/vk_layer.h>
#include <string.h>
#include <stdlib.h>

// ─── Globals ──────────────────────────────────────────────────────────────────
bool                                        g_debug = false;
std::mutex                                  g_instanceLock;
std::unordered_map<void*, InstanceDispatch> g_instanceDispatch;
std::mutex                                  g_deviceLock;
std::unordered_map<void*, DeviceContext>    g_deviceCtx;

// Map VkQueue → dispatch key of its device (needed for QueuePresent intercept)
static std::mutex                           g_queueLock;
static std::unordered_map<void*, void*>     g_queueToDeviceKey;

// ─── Helpers ──────────────────────────────────────────────────────────────────
DeviceContext* GetDeviceContext(VkDevice device) {
    std::lock_guard<std::mutex> lk(g_deviceLock);
    auto it = g_deviceCtx.find(GetDispatchKey(device));
    return (it != g_deviceCtx.end()) ? &it->second : nullptr;
}

DeviceContext* GetDeviceContextFromQueue(VkQueue queue) {
    void* devKey = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_queueLock);
        auto it = g_queueToDeviceKey.find(GetDispatchKey(queue));
        if (it == g_queueToDeviceKey.end()) return nullptr;
        devKey = it->second;
    }
    std::lock_guard<std::mutex> lk(g_deviceLock);
    auto it = g_deviceCtx.find(devKey);
    return (it != g_deviceCtx.end()) ? &it->second : nullptr;
}

uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& props,
                        uint32_t typeBits, VkMemoryPropertyFlags required) {
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    return UINT32_MAX;
}

// ─── vkCreateInstance intercept ──────────────────────────────────────────────
static VkResult VKAPI_CALL lsfg_CreateInstance(
        const VkInstanceCreateInfo*  pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkInstance*                  pInstance)
{
    // Read env config
    if (getenv("LSFG_DEBUG"))   g_debug = true;
    if (getenv("LSFG_DISABLE")) {
        LOGI("Layer disabled via LSFG_DISABLE env var");
    }

    auto* layerInfo = reinterpret_cast<const VkLayerInstanceCreateInfo*>(
        pCreateInfo->pNext);
    while (layerInfo &&
           !(layerInfo->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
             layerInfo->function == VK_LAYER_LINK_INFO)) {
        layerInfo = reinterpret_cast<const VkLayerInstanceCreateInfo*>(layerInfo->pNext);
    }
    if (!layerInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = layerInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    // Advance chain
    const_cast<VkLayerInstanceCreateInfo*>(layerInfo)->u.pLayerInfo =
        layerInfo->u.pLayerInfo->pNext;

    PFN_vkCreateInstance createInstanceFn =
        reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!createInstanceFn) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = createInstanceFn(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) return result;

    // Build instance dispatch
    InstanceDispatch dispatch = {};
    dispatch.GetInstanceProcAddr = gipa;
#define LOAD_INST(fn) dispatch.fn = reinterpret_cast<PFN_vk##fn>(gipa(*pInstance, "vk"#fn))
    LOAD_INST(DestroyInstance);
    LOAD_INST(EnumeratePhysicalDevices);
    LOAD_INST(GetPhysicalDeviceProperties);
    LOAD_INST(GetPhysicalDeviceMemoryProperties);
    LOAD_INST(GetPhysicalDeviceQueueFamilyProperties);
#undef LOAD_INST

    {
        std::lock_guard<std::mutex> lk(g_instanceLock);
        g_instanceDispatch[GetDispatchKey(*pInstance)] = dispatch;
    }

    LOGI("vkCreateInstance: layer installed (v%d.%d.%d)",
         LSFG_VK_VERSION_MAJOR, LSFG_VK_VERSION_MINOR, LSFG_VK_VERSION_PATCH);
    return VK_SUCCESS;
}

// ─── vkDestroyInstance intercept ─────────────────────────────────────────────
static void VKAPI_CALL lsfg_DestroyInstance(
        VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    void* key = GetDispatchKey(instance);
    PFN_vkDestroyInstance fn = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_instanceLock);
        auto it = g_instanceDispatch.find(key);
        if (it != g_instanceDispatch.end()) {
            fn = it->second.DestroyInstance;
            g_instanceDispatch.erase(it);
        }
    }
    if (fn) fn(instance, pAllocator);
}

// ─── vkCreateDevice intercept ────────────────────────────────────────────────
static VkResult VKAPI_CALL lsfg_CreateDevice(
        VkPhysicalDevice             physicalDevice,
        const VkDeviceCreateInfo*    pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDevice*                    pDevice)
{
    auto* layerInfo = reinterpret_cast<const VkLayerDeviceCreateInfo*>(
        pCreateInfo->pNext);
    while (layerInfo &&
           !(layerInfo->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
             layerInfo->function == VK_LAYER_LINK_INFO)) {
        layerInfo = reinterpret_cast<const VkLayerDeviceCreateInfo*>(layerInfo->pNext);
    }
    if (!layerInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = layerInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   gdpa = layerInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    const_cast<VkLayerDeviceCreateInfo*>(layerInfo)->u.pLayerInfo =
        layerInfo->u.pLayerInfo->pNext;

    // We need a compute queue — check if one is requested already
    bool hasComputeQueue = false;
    uint32_t computeFamily = UINT32_MAX;

    // Find a compute-capable queue family on this physical device
    {
        void* instKey = nullptr;
        // Get instance from physicalDevice (workaround: walk instance map)
        uint32_t qfCount = 0;
        // We'll ask via the instance dispatch that we stored; iterate to find
        std::lock_guard<std::mutex> lk(g_instanceLock);
        for (auto& [key, disp] : g_instanceDispatch) {
            if (disp.GetPhysicalDeviceQueueFamilyProperties) {
                disp.GetPhysicalDeviceQueueFamilyProperties(
                    physicalDevice, &qfCount, nullptr);
                std::vector<VkQueueFamilyProperties> qfProps(qfCount);
                disp.GetPhysicalDeviceQueueFamilyProperties(
                    physicalDevice, &qfCount, qfProps.data());
                for (uint32_t i = 0; i < qfCount; i++) {
                    if (qfProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                        computeFamily = i;
                        break;
                    }
                }
                break;
            }
        }
    }

    // Check if the app already requests a queue from the compute family
    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
        if (pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex == computeFamily) {
            hasComputeQueue = true;
            break;
        }
    }

    // If not, inject an extra QueueCreateInfo for compute
    std::vector<VkDeviceQueueCreateInfo> queueInfos(
        pCreateInfo->pQueueCreateInfos,
        pCreateInfo->pQueueCreateInfos + pCreateInfo->queueCreateInfoCount);
    static const float qPriority = 0.5f;
    VkDeviceCreateInfo modifiedCI = *pCreateInfo;

    if (!hasComputeQueue && computeFamily != UINT32_MAX) {
        VkDeviceQueueCreateInfo cqci = {};
        cqci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        cqci.queueFamilyIndex = computeFamily;
        cqci.queueCount       = 1;
        cqci.pQueuePriorities = &qPriority;
        queueInfos.push_back(cqci);
        modifiedCI.queueCreateInfoCount = (uint32_t)queueInfos.size();
        modifiedCI.pQueueCreateInfos    = queueInfos.data();
    }

    PFN_vkCreateDevice createDeviceFn =
        reinterpret_cast<PFN_vkCreateDevice>(gipa(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!createDeviceFn) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = createDeviceFn(physicalDevice, &modifiedCI, pAllocator, pDevice);
    if (result != VK_SUCCESS) return result;

    // Build device dispatch table
    DeviceContext ctx = {};
    ctx.device         = *pDevice;
    ctx.physicalDevice = physicalDevice;
    DeviceDispatch& d  = ctx.dispatch;
    d.GetDeviceProcAddr = gdpa;

#define LOAD_DEV(fn) d.fn = reinterpret_cast<PFN_vk##fn>(gdpa(*pDevice, "vk"#fn))
    LOAD_DEV(DestroyDevice);
    LOAD_DEV(GetDeviceQueue);
    LOAD_DEV(QueueSubmit);
    LOAD_DEV(QueuePresentKHR);
    LOAD_DEV(QueueWaitIdle);
    LOAD_DEV(DeviceWaitIdle);
    LOAD_DEV(AllocateMemory);
    LOAD_DEV(FreeMemory);
    LOAD_DEV(MapMemory);
    LOAD_DEV(UnmapMemory);
    LOAD_DEV(BindImageMemory);
    LOAD_DEV(BindBufferMemory);
    LOAD_DEV(CreateImage);
    LOAD_DEV(DestroyImage);
    LOAD_DEV(CreateImageView);
    LOAD_DEV(DestroyImageView);
    LOAD_DEV(GetImageMemoryRequirements);
    LOAD_DEV(CreateBuffer);
    LOAD_DEV(DestroyBuffer);
    LOAD_DEV(GetBufferMemoryRequirements);
    LOAD_DEV(CreateSampler);
    LOAD_DEV(DestroySampler);
    LOAD_DEV(CreateDescriptorPool);
    LOAD_DEV(DestroyDescriptorPool);
    LOAD_DEV(CreateDescriptorSetLayout);
    LOAD_DEV(DestroyDescriptorSetLayout);
    LOAD_DEV(AllocateDescriptorSets);
    LOAD_DEV(FreeDescriptorSets);
    LOAD_DEV(UpdateDescriptorSets);
    LOAD_DEV(CreateComputePipelines);
    LOAD_DEV(DestroyPipeline);
    LOAD_DEV(CreatePipelineLayout);
    LOAD_DEV(DestroyPipelineLayout);
    LOAD_DEV(CreateShaderModule);
    LOAD_DEV(DestroyShaderModule);
    LOAD_DEV(CreateCommandPool);
    LOAD_DEV(DestroyCommandPool);
    LOAD_DEV(AllocateCommandBuffers);
    LOAD_DEV(FreeCommandBuffers);
    LOAD_DEV(BeginCommandBuffer);
    LOAD_DEV(EndCommandBuffer);
    LOAD_DEV(ResetCommandBuffer);
    LOAD_DEV(CmdPipelineBarrier);
    LOAD_DEV(CmdCopyImage);
    LOAD_DEV(CmdBlitImage);
    LOAD_DEV(CmdBindPipeline);
    LOAD_DEV(CmdBindDescriptorSets);
    LOAD_DEV(CmdDispatch);
    LOAD_DEV(CmdPushConstants);
    LOAD_DEV(CreateSemaphore);
    LOAD_DEV(DestroySemaphore);
    LOAD_DEV(CreateFence);
    LOAD_DEV(DestroyFence);
    LOAD_DEV(WaitForFences);
    LOAD_DEV(ResetFences);
    LOAD_DEV(CreateSwapchainKHR);
    LOAD_DEV(DestroySwapchainKHR);
    LOAD_DEV(GetSwapchainImagesKHR);
    LOAD_DEV(AcquireNextImageKHR);
#undef LOAD_DEV

    // Retrieve compute queue handle
    if (computeFamily != UINT32_MAX) {
        d.GetDeviceQueue(*pDevice, computeFamily, 0, &ctx.computeQueue);
        ctx.computeQueueFamily = computeFamily;
    }

    // Cache physical device memory properties
    {
        std::lock_guard<std::mutex> lk(g_instanceLock);
        for (auto& [key, disp] : g_instanceDispatch) {
            if (disp.GetPhysicalDeviceMemoryProperties) {
                disp.GetPhysicalDeviceMemoryProperties(physicalDevice, &ctx.memProps);
                break;
            }
        }
    }

    void* devKey = GetDispatchKey(*pDevice);
    {
        std::lock_guard<std::mutex> lk(g_deviceLock);
        g_deviceCtx[devKey] = ctx;
    }

    // Register compute queue→device mapping
    if (ctx.computeQueue) {
        std::lock_guard<std::mutex> lk(g_queueLock);
        g_queueToDeviceKey[GetDispatchKey(ctx.computeQueue)] = devKey;
    }

    LOGI("vkCreateDevice: device context created, computeQueueFamily=%u", computeFamily);
    return VK_SUCCESS;
}

// ─── vkDestroyDevice intercept ───────────────────────────────────────────────
static void VKAPI_CALL lsfg_DestroyDevice(
        VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    void* key = GetDispatchKey(device);

    // Destroy frame generators for all swapchains on this device
    SwapchainManager::Instance().DestroyAllForDevice(device);

    PFN_vkDestroyDevice fn = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_deviceLock);
        auto it = g_deviceCtx.find(key);
        if (it != g_deviceCtx.end()) {
            fn = it->second.dispatch.DestroyDevice;
            g_deviceCtx.erase(it);
        }
    }
    if (fn) fn(device, pAllocator);
}

// ─── vkCreateSwapchainKHR intercept ──────────────────────────────────────────
static VkResult VKAPI_CALL lsfg_CreateSwapchainKHR(
        VkDevice                       device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*   pAllocator,
        VkSwapchainKHR*                pSwapchain)
{
    DeviceContext* ctx = GetDeviceContext(device);
    if (!ctx) return VK_ERROR_DEVICE_LOST;

    // Forward to driver
    VkResult result = ctx->dispatch.CreateSwapchainKHR(
        device, pCreateInfo, pAllocator, pSwapchain);
    if (result != VK_SUCCESS) return result;

    // Register with our swapchain manager
    if (!getenv("LSFG_DISABLE")) {
        SwapchainManager::Instance().OnCreateSwapchain(
            device, *pSwapchain, *pCreateInfo);
    }

    return VK_SUCCESS;
}

// ─── vkDestroySwapchainKHR intercept ─────────────────────────────────────────
static void VKAPI_CALL lsfg_DestroySwapchainKHR(
        VkDevice                     device,
        VkSwapchainKHR               swapchain,
        const VkAllocationCallbacks* pAllocator)
{
    SwapchainManager::Instance().OnDestroySwapchain(device, swapchain);

    DeviceContext* ctx = GetDeviceContext(device);
    if (ctx) ctx->dispatch.DestroySwapchainKHR(device, swapchain, pAllocator);
}

// ─── vkQueuePresentKHR intercept ─────────────────────────────────────────────
static VkResult VKAPI_CALL lsfg_QueuePresentKHR(
        VkQueue                  queue,
        const VkPresentInfoKHR*  pPresentInfo)
{
    if (getenv("LSFG_DISABLE")) {
        DeviceContext* ctx = GetDeviceContextFromQueue(queue);
        if (ctx) return ctx->dispatch.QueuePresentKHR(queue, pPresentInfo);
        return VK_ERROR_DEVICE_LOST;
    }

    return SwapchainManager::Instance().OnQueuePresent(queue, pPresentInfo);
}

// ─── vkGetDeviceProcAddr intercept ───────────────────────────────────────────
static PFN_vkVoidFunction VKAPI_CALL lsfg_GetDeviceProcAddr(
        VkDevice device, const char* pName)
{
#define HOOK(fn) if (strcmp(pName, "vk"#fn) == 0) return (PFN_vkVoidFunction)lsfg_##fn
    HOOK(DestroyDevice);
    HOOK(CreateSwapchainKHR);
    HOOK(DestroySwapchainKHR);
    HOOK(QueuePresentKHR);
#undef HOOK

    DeviceContext* ctx = GetDeviceContext(device);
    if (ctx) return ctx->dispatch.GetDeviceProcAddr(device, pName);
    return nullptr;
}

// ─── vkGetInstanceProcAddr intercept ─────────────────────────────────────────
static PFN_vkVoidFunction VKAPI_CALL lsfg_GetInstanceProcAddr(
        VkInstance instance, const char* pName)
{
#define HOOK(fn) if (strcmp(pName, "vk"#fn) == 0) return (PFN_vkVoidFunction)lsfg_##fn
    HOOK(GetInstanceProcAddr);
    HOOK(GetDeviceProcAddr);
    HOOK(CreateInstance);
    HOOK(DestroyInstance);
    HOOK(CreateDevice);
    HOOK(DestroyDevice);
    HOOK(CreateSwapchainKHR);
    HOOK(DestroySwapchainKHR);
    HOOK(QueuePresentKHR);
#undef HOOK

    if (instance == VK_NULL_HANDLE) return nullptr;
    std::lock_guard<std::mutex> lk(g_instanceLock);
    auto it = g_instanceDispatch.find(GetDispatchKey(instance));
    if (it == g_instanceDispatch.end()) return nullptr;
    return it->second.GetInstanceProcAddr(instance, pName);
}

// ─── Loader negotiation ───────────────────────────────────────────────────────
extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct)
{
    if (pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (pVersionStruct->loaderLayerInterfaceVersion > 2)
        pVersionStruct->loaderLayerInterfaceVersion = 2;

    pVersionStruct->pfnGetInstanceProcAddr = lsfg_GetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr   = lsfg_GetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
