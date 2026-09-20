// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// layer/frame_generator.h — Frame generation orchestrator

#pragma once

#include "layer.h"
#include "optical_flow.h"
#include "present_scheduler.h"
#include "lsfg_vk.h"

#include <vector>
#include <array>
#include <deque>
#include <mutex>
#include <atomic>
#include <chrono>

// ─── Ring buffer slot for a real captured frame ───────────────────────────────
struct CapturedFrame {
    VkImage        image      = VK_NULL_HANDLE;  // copy of swapchain image
    VkImageView    view       = VK_NULL_HANDLE;
    VkDeviceMemory memory     = VK_NULL_HANDLE;
    bool           valid      = false;
};

// ─── Synthesised frame slot ───────────────────────────────────────────────────
struct SynthFrame {
    VkImage        image      = VK_NULL_HANDLE;
    VkImageView    view       = VK_NULL_HANDLE;
    VkDeviceMemory memory     = VK_NULL_HANDLE;

    // Motion-vector image (RG16F)
    VkImage        mvImage    = VK_NULL_HANDLE;
    VkImageView    mvView     = VK_NULL_HANDLE;
    VkDeviceMemory mvMemory   = VK_NULL_HANDLE;

    VkSemaphore    readySem   = VK_NULL_HANDLE;  // signaled when synthesis done
    VkFence        fence      = VK_NULL_HANDLE;
    VkCommandBuffer cmd       = VK_NULL_HANDLE;

    bool           inFlight   = false;
    bool           valid      = false;  // true after AllocateSynthSlot succeeds
};

// ─── FrameGenerator ──────────────────────────────────────────────────────────
//
// One instance per swapchain. Owns the optical flow engine, ring buffers,
// synthesis pipeline, and present scheduler for that swapchain.
//
class FrameGenerator {
public:
    FrameGenerator() = default;
    ~FrameGenerator() { Destroy(); }

    VkResult Init(DeviceContext*            ctx,
                  VkSwapchainKHR           swapchain,
                  const LsfgSwapchainConfig& config,
                  const std::vector<VkImage>& swapchainImages);

    void Destroy();

    // Called once per real present. Triggers synthesis and returns
    // the modified VkPresentInfoKHR chain (possibly presenting multiple frames).
    VkResult OnPresent(VkQueue                queue,
                       const VkPresentInfoKHR* pPresentInfo);

    const LsfgStats& GetStats() const { return m_stats; }

private:
    DeviceContext*      m_ctx       = nullptr;
    VkSwapchainKHR      m_swapchain = VK_NULL_HANDLE;
    LsfgSwapchainConfig m_config    = {};

    // ── Command pool ─────────────────────────────────────────────────────────
    VkCommandPool       m_cmdPool   = VK_NULL_HANDLE;

    // ── Real-frame ring buffer (capacity = 2: prev + curr) ───────────────────
    static constexpr int REAL_RING = 2;
    std::array<CapturedFrame, REAL_RING> m_realFrames;
    int m_prevIdx = 0;
    int m_currIdx = 1;

    // ── Synthesised frame slots (up to multiplier-1 = 3 slots) ───────────────
    static constexpr int MAX_SYNTH = 3;
    std::array<SynthFrame, MAX_SYNTH> m_synthFrames;

    // ── Optical flow engine ───────────────────────────────────────────────────
    OpticalFlowEngine   m_opticalFlow;

    // ── Present scheduler ─────────────────────────────────────────────────────
    PresentScheduler    m_scheduler;

    // ── Stats ─────────────────────────────────────────────────────────────────
    LsfgStats           m_stats    = {};
    std::chrono::steady_clock::time_point m_lastPresentTime;
    uint64_t            m_frameCount = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────
    VkResult AllocateImageSlot(CapturedFrame& slot);
    VkResult AllocateSynthSlot(SynthFrame& slot);
    void     FreeSynthSlot(SynthFrame& slot);

    // Copy the current swapchain image into our ring buffer slot
    VkResult CaptureSwapchainImage(VkCommandBuffer  cmd,
                                   VkImage          swapchainImage,
                                   CapturedFrame&   dst);

    // Run synthesis for one interpolation time t
    VkResult SynthesiseAt(float t, int slotIdx,
                          VkCommandBuffer cmd);

    // Build a VkPresentInfoKHR that presents synthesised frames before the real one
    void BuildPresentChain(const VkPresentInfoKHR*  original,
                           std::vector<VkPresentInfoKHR>& chain,
                           std::vector<VkSemaphore>&      extraWaitSems);
};
