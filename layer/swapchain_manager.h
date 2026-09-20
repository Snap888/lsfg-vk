// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// layer/swapchain_manager.h — Swapchain interception and frame ring buffer

#pragma once

#include "layer.h"
#include "frame_generator.h"

#include <unordered_map>
#include <mutex>
#include <memory>

// ─── Per-swapchain state ─────────────────────────────────────────────────────
struct SwapchainState {
    VkDevice               device      = VK_NULL_HANDLE;
    VkSwapchainKHR         swapchain   = VK_NULL_HANDLE;
    LsfgSwapchainConfig    config      = {};

    // Real swapchain images (from vkGetSwapchainImagesKHR)
    std::vector<VkImage>   images;

    // Frame generator (owns optical flow, pipelines, synthesis buffers)
    std::unique_ptr<FrameGenerator> generator;
};

// ─── Singleton SwapchainManager ───────────────────────────────────────────────
class SwapchainManager {
public:
    static SwapchainManager& Instance() {
        static SwapchainManager s_instance;
        return s_instance;
    }

    SwapchainManager(const SwapchainManager&) = delete;
    SwapchainManager& operator=(const SwapchainManager&) = delete;

    // Called after the real swapchain is created
    void OnCreateSwapchain(VkDevice                       device,
                           VkSwapchainKHR                 swapchain,
                           const VkSwapchainCreateInfoKHR& ci);

    // Called before the real swapchain is destroyed
    void OnDestroySwapchain(VkDevice device, VkSwapchainKHR swapchain);

    // Called instead of the app's vkQueuePresentKHR
    VkResult OnQueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

    // Destroy all swapchains associated with a device (on vkDestroyDevice)
    void DestroyAllForDevice(VkDevice device);

private:
    SwapchainManager() = default;

    std::mutex                                            m_lock;
    std::unordered_map<VkSwapchainKHR, SwapchainState>  m_swapchains;

    LsfgSwapchainConfig BuildConfig(const VkSwapchainCreateInfoKHR& ci) const;
};
