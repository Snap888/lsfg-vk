// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// layer/swapchain_manager.cpp

#include "swapchain_manager.h"
#include "layer.h"

#include <cstdlib>

// ─── BuildConfig ─────────────────────────────────────────────────────────────
LsfgSwapchainConfig SwapchainManager::BuildConfig(
        const VkSwapchainCreateInfoKHR& ci) const
{
    LsfgSwapchainConfig cfg = {};

    // Frame multiplier from env (default 2x)
    const char* multStr = getenv("LSFG_MULTIPLIER");
    int mult = multStr ? atoi(multStr) : 2;
    if (mult < 2 || mult > 4) mult = 2;
    cfg.multiplier   = static_cast<LsfgMultiplier>(mult);

    // UI mask (default on)
    const char* maskStr = getenv("LSFG_UI_MASK");
    cfg.ui_mask      = (!maskStr || atoi(maskStr) != 0) ? VK_TRUE : VK_FALSE;

    // VRStereo awareness (default on)
    const char* stereoStr = getenv("LSFG_STEREO_AWARE");
    cfg.stereo_aware = (!stereoStr || atoi(stereoStr) != 0) ? VK_TRUE : VK_FALSE;

    cfg.width  = ci.imageExtent.width;
    cfg.height = ci.imageExtent.height;
    cfg.format = ci.imageFormat;

    LOGI("SwapchainManager: config multiplier=%dx ui_mask=%d stereo=%d %ux%u fmt=%d",
         mult, cfg.ui_mask, cfg.stereo_aware, cfg.width, cfg.height, (int)cfg.format);
    return cfg;
}

// ─── OnCreateSwapchain ───────────────────────────────────────────────────────
void SwapchainManager::OnCreateSwapchain(
        VkDevice                       device,
        VkSwapchainKHR                 swapchain,
        const VkSwapchainCreateInfoKHR& ci)
{
    DeviceContext* ctx = GetDeviceContext(device);
    if (!ctx) {
        LOGE("OnCreateSwapchain: no DeviceContext for device");
        return;
    }

    SwapchainState state = {};
    state.device    = device;
    state.swapchain = swapchain;
    state.config    = BuildConfig(ci);

    // Retrieve swapchain images
    uint32_t imageCount = 0;
    ctx->dispatch.GetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    state.images.resize(imageCount);
    ctx->dispatch.GetSwapchainImagesKHR(device, swapchain, &imageCount, state.images.data());

    LOGI("OnCreateSwapchain: %u swapchain images", imageCount);

    // Create frame generator
    state.generator = std::make_unique<FrameGenerator>();
    VkResult result = state.generator->Init(ctx, swapchain, state.config, state.images);
    if (result != VK_SUCCESS) {
        LOGE("FrameGenerator::Init failed: %d — frame generation disabled for this swapchain", result);
        state.generator.reset();
    }

    std::lock_guard<std::mutex> lk(m_lock);
    m_swapchains[swapchain] = std::move(state);
}

// ─── OnDestroySwapchain ──────────────────────────────────────────────────────
void SwapchainManager::OnDestroySwapchain(VkDevice device, VkSwapchainKHR swapchain) {
    std::lock_guard<std::mutex> lk(m_lock);
    auto it = m_swapchains.find(swapchain);
    if (it != m_swapchains.end()) {
        // FrameGenerator destructor calls Destroy()
        m_swapchains.erase(it);
    }
}

// ─── OnQueuePresent ──────────────────────────────────────────────────────────
VkResult SwapchainManager::OnQueuePresent(
        VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    DeviceContext* ctx = GetDeviceContextFromQueue(queue);
    if (!ctx) {
        LOGE("OnQueuePresent: no DeviceContext for queue — passthrough");
        // Passthrough — find any dispatch and forward
        std::lock_guard<std::mutex> lk(g_deviceLock);
        for (auto& [key, dc] : g_deviceCtx) {
            return dc.dispatch.QueuePresentKHR(queue, pPresentInfo);
        }
        return VK_ERROR_DEVICE_LOST;
    }

    // For each swapchain in the present info, route through its generator
    // (Usually there is only one swapchain per present, but handle N).
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        VkSwapchainKHR sc = pPresentInfo->pSwapchains[i];

        SwapchainState* state = nullptr;
        {
            std::lock_guard<std::mutex> lk(m_lock);
            auto it = m_swapchains.find(sc);
            if (it != m_swapchains.end()) state = &it->second;
        }

        if (state && state->generator) {
            VkResult r = state->generator->OnPresent(queue, pPresentInfo);
            if (r != VK_SUCCESS) {
                LOGW("FrameGenerator::OnPresent returned %d — passthrough", r);
                return ctx->dispatch.QueuePresentKHR(queue, pPresentInfo);
            }
            return r;
        }
    }

    // No generator found — plain passthrough
    return ctx->dispatch.QueuePresentKHR(queue, pPresentInfo);
}

// ─── DestroyAllForDevice ─────────────────────────────────────────────────────
void SwapchainManager::DestroyAllForDevice(VkDevice device) {
    std::lock_guard<std::mutex> lk(m_lock);
    for (auto it = m_swapchains.begin(); it != m_swapchains.end(); ) {
        if (it->second.device == device) {
            it = m_swapchains.erase(it);
        } else {
            ++it;
        }
    }
}
