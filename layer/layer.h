// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// layer/layer.h — Internal layer state, dispatch tables, utilities

#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <unordered_map>
#include <mutex>
#include <memory>
#include <cstring>

#include "lsfg_vk.h"

// ─── Logging ─────────────────────────────────────────────────────────────────
#ifdef ANDROID
#include <android/log.h>
#define LSFG_TAG "LSFG_VK"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LSFG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LSFG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LSFG_TAG, __VA_ARGS__)
#define LOGD(...) do { if (g_debug) __android_log_print(ANDROID_LOG_DEBUG, LSFG_TAG, __VA_ARGS__); } while(0)
#else
#include <cstdio>
#define LOGI(...) fprintf(stdout, "[LSFG INFO]  " __VA_ARGS__)
#define LOGW(...) fprintf(stderr, "[LSFG WARN]  " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[LSFG ERROR] " __VA_ARGS__)
#define LOGD(...) do { if (g_debug) fprintf(stdout, "[LSFG DEBUG] " __VA_ARGS__); } while(0)
#endif

extern bool g_debug;

// ─── Dispatch key helper ──────────────────────────────────────────────────────
// The dispatch key is the first sizeof(void*) bytes of the object (loader
// puts a pointer to the dispatch table there).
static inline void* GetDispatchKey(const void* obj) {
    return *reinterpret_cast<void* const*>(obj);
}

// ─── Instance dispatch table ─────────────────────────────────────────────────
struct InstanceDispatch {
    PFN_vkGetInstanceProcAddr       GetInstanceProcAddr;
    PFN_vkDestroyInstance           DestroyInstance;
    PFN_vkEnumeratePhysicalDevices  EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
};

// ─── Device dispatch table ────────────────────────────────────────────────────
struct DeviceDispatch {
    PFN_vkGetDeviceProcAddr         GetDeviceProcAddr;
    PFN_vkDestroyDevice             DestroyDevice;

    // Queues
    PFN_vkGetDeviceQueue            GetDeviceQueue;
    PFN_vkQueueSubmit               QueueSubmit;
    PFN_vkQueuePresentKHR           QueuePresentKHR;
    PFN_vkQueueWaitIdle             QueueWaitIdle;
    PFN_vkDeviceWaitIdle            DeviceWaitIdle;

    // Memory
    PFN_vkAllocateMemory            AllocateMemory;
    PFN_vkFreeMemory                FreeMemory;
    PFN_vkMapMemory                 MapMemory;
    PFN_vkUnmapMemory               UnmapMemory;
    PFN_vkBindImageMemory           BindImageMemory;
    PFN_vkBindBufferMemory          BindBufferMemory;

    // Images
    PFN_vkCreateImage               CreateImage;
    PFN_vkDestroyImage              DestroyImage;
    PFN_vkCreateImageView           CreateImageView;
    PFN_vkDestroyImageView          DestroyImageView;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;

    // Buffers
    PFN_vkCreateBuffer              CreateBuffer;
    PFN_vkDestroyBuffer             DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;

    // Samplers
    PFN_vkCreateSampler             CreateSampler;
    PFN_vkDestroySampler            DestroySampler;

    // Descriptors
    PFN_vkCreateDescriptorPool      CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool     DestroyDescriptorPool;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkAllocateDescriptorSets    AllocateDescriptorSets;
    PFN_vkFreeDescriptorSets        FreeDescriptorSets;
    PFN_vkUpdateDescriptorSets      UpdateDescriptorSets;

    // Pipelines
    PFN_vkCreateComputePipelines    CreateComputePipelines;
    PFN_vkDestroyPipeline           DestroyPipeline;
    PFN_vkCreatePipelineLayout      CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout     DestroyPipelineLayout;

    // Shaders
    PFN_vkCreateShaderModule        CreateShaderModule;
    PFN_vkDestroyShaderModule       DestroyShaderModule;

    // Command buffers
    PFN_vkCreateCommandPool         CreateCommandPool;
    PFN_vkDestroyCommandPool        DestroyCommandPool;
    PFN_vkAllocateCommandBuffers    AllocateCommandBuffers;
    PFN_vkFreeCommandBuffers        FreeCommandBuffers;
    PFN_vkBeginCommandBuffer        BeginCommandBuffer;
    PFN_vkEndCommandBuffer          EndCommandBuffer;
    PFN_vkResetCommandBuffer        ResetCommandBuffer;

    // Commands
    PFN_vkCmdPipelineBarrier        CmdPipelineBarrier;
    PFN_vkCmdCopyImage              CmdCopyImage;
    PFN_vkCmdBlitImage              CmdBlitImage;
    PFN_vkCmdBindPipeline           CmdBindPipeline;
    PFN_vkCmdBindDescriptorSets     CmdBindDescriptorSets;
    PFN_vkCmdDispatch               CmdDispatch;
    PFN_vkCmdPushConstants          CmdPushConstants;

    // Sync
    PFN_vkCreateSemaphore           CreateSemaphore;
    PFN_vkDestroySemaphore          DestroySemaphore;
    PFN_vkCreateFence               CreateFence;
    PFN_vkDestroyFence              DestroyFence;
    PFN_vkWaitForFences             WaitForFences;
    PFN_vkResetFences               ResetFences;

    // Swapchain
    PFN_vkCreateSwapchainKHR        CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR       DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR     GetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR       AcquireNextImageKHR;
};

// ─── Per-device context ───────────────────────────────────────────────────────
struct DeviceContext {
    VkDevice                        device          = VK_NULL_HANDLE;
    VkPhysicalDevice                physicalDevice  = VK_NULL_HANDLE;
    DeviceDispatch                  dispatch        = {};

    // Compute queue for optical flow
    VkQueue                         computeQueue    = VK_NULL_HANDLE;
    uint32_t                        computeQueueFamily = UINT32_MAX;

    // Physical device memory properties (for allocation helpers)
    VkPhysicalDeviceMemoryProperties memProps       = {};
};

// ─── Global registry maps (keyed by dispatch key) ─────────────────────────────
extern std::mutex                                       g_instanceLock;
extern std::unordered_map<void*, InstanceDispatch>      g_instanceDispatch;

extern std::mutex                                       g_deviceLock;
extern std::unordered_map<void*, DeviceContext>         g_deviceCtx;

// ─── Helpers ──────────────────────────────────────────────────────────────────
DeviceContext* GetDeviceContext(VkDevice device);
DeviceContext* GetDeviceContextFromQueue(VkQueue queue);

// Find a memory type satisfying required flags
uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& props,
                        uint32_t                               typeBits,
                        VkMemoryPropertyFlags                  required);

// Simple VkResult check that logs and returns on failure
#define VK_CHECK(expr)                                              \
    do {                                                            \
        VkResult _r = (expr);                                       \
        if (_r != VK_SUCCESS) {                                     \
            LOGE("%s failed with VkResult %d at %s:%d",            \
                 #expr, (int)_r, __FILE__, __LINE__);               \
            return _r;                                              \
        }                                                           \
    } while(0)

#define VK_CHECK_VOID(expr)                                         \
    do {                                                            \
        VkResult _r = (expr);                                       \
        if (_r != VK_SUCCESS) {                                     \
            LOGE("%s failed with VkResult %d at %s:%d",            \
                 #expr, (int)_r, __FILE__, __LINE__);               \
            return;                                                  \
        }                                                           \
    } while(0)
