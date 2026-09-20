// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// layer/frame_generator.cpp — Frame generation orchestrator

#include "frame_generator.h"
#include "layer.h"

#include <chrono>
#include <cstring>
#include <vector>
#include <algorithm>

// ─── Init ────────────────────────────────────────────────────────────────────
VkResult FrameGenerator::Init(
        DeviceContext*             ctx,
        VkSwapchainKHR             swapchain,
        const LsfgSwapchainConfig& config,
        const std::vector<VkImage>& swapchainImages)
{
    m_ctx       = ctx;
    m_swapchain = swapchain;
    m_config    = config;

    // ── Command pool ─────────────────────────────────────────────────────────
    VkCommandPoolCreateInfo poolCI = {};
    poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = ctx->computeQueueFamily;
    poolCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(ctx->dispatch.CreateCommandPool(ctx->device, &poolCI, nullptr, &m_cmdPool));

    // ── Allocate real-frame ring buffer ───────────────────────────────────────
    for (auto& slot : m_realFrames) {
        VK_CHECK(AllocateImageSlot(slot));
    }

    // ── Allocate synthesis slots ───────────────────────────────────────────────
    int synthCount = (int)config.multiplier - 1;
    for (int i = 0; i < synthCount; i++) {
        VK_CHECK(AllocateSynthSlot(m_synthFrames[i]));
    }

    // ── Initialise optical flow engine ────────────────────────────────────────
    VK_CHECK(m_opticalFlow.Init(ctx, config.width, config.height, config.format));

    LOGI("FrameGenerator: init OK [%ux%u %dx multiplier]",
         config.width, config.height, (int)config.multiplier);
    return VK_SUCCESS;
}

// ─── Destroy ─────────────────────────────────────────────────────────────────
void FrameGenerator::Destroy() {
    if (!m_ctx) return;

    VkDevice dev = m_ctx->device;
    const DeviceDispatch& d = m_ctx->dispatch;

    // Wait for all in-flight work
    d.DeviceWaitIdle(dev);

    // Destroy real frame slots
    for (auto& slot : m_realFrames) {
        if (slot.view)   d.DestroyImageView(dev, slot.view, nullptr);
        if (slot.image)  d.DestroyImage(dev, slot.image, nullptr);
        if (slot.memory) d.FreeMemory(dev, slot.memory, nullptr);
    }

    // Destroy synth slots
    for (auto& slot : m_synthFrames) {
        if (slot.valid) FreeSynthSlot(slot);
    }

    // Destroy optical flow engine
    m_opticalFlow.Destroy();

    // Destroy command pool
    if (m_cmdPool) {
        d.DestroyCommandPool(dev, m_cmdPool, nullptr);
        m_cmdPool = VK_NULL_HANDLE;
    }

    m_ctx = nullptr;
}

// ─── AllocateImageSlot ───────────────────────────────────────────────────────
VkResult FrameGenerator::AllocateImageSlot(CapturedFrame& slot) {
    VkDevice dev = m_ctx->device;
    const DeviceDispatch& d = m_ctx->dispatch;

    VkImageCreateInfo imgCI = {};
    imgCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCI.imageType     = VK_IMAGE_TYPE_2D;
    imgCI.format        = m_config.format;
    imgCI.extent        = { m_config.width, m_config.height, 1 };
    imgCI.mipLevels     = 1;
    imgCI.arrayLayers   = 1;
    imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK(d.CreateImage(dev, &imgCI, nullptr, &slot.image));

    VkMemoryRequirements memReq;
    d.GetImageMemoryRequirements(dev, slot.image, &memReq);
    uint32_t memType = FindMemoryType(m_ctx->memProps, memReq.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == UINT32_MAX) return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = memType;
    VK_CHECK(d.AllocateMemory(dev, &allocInfo, nullptr, &slot.memory));
    VK_CHECK(d.BindImageMemory(dev, slot.image, slot.memory, 0));

    VkImageViewCreateInfo viewCI = {};
    viewCI.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image      = slot.image;
    viewCI.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format     = m_config.format;
    viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(d.CreateImageView(dev, &viewCI, nullptr, &slot.view));

    slot.valid = false;
    return VK_SUCCESS;
}

// ─── AllocateSynthSlot ───────────────────────────────────────────────────────
VkResult FrameGenerator::AllocateSynthSlot(SynthFrame& slot) {
    VkDevice dev = m_ctx->device;
    const DeviceDispatch& d = m_ctx->dispatch;

    // ── Synthesised frame image ───────────────────────────────────────────────
    VkImageCreateInfo imgCI = {};
    imgCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCI.imageType     = VK_IMAGE_TYPE_2D;
    imgCI.format        = m_config.format;
    imgCI.extent        = { m_config.width, m_config.height, 1 };
    imgCI.mipLevels     = 1;
    imgCI.arrayLayers   = 1;
    imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage         = VK_IMAGE_USAGE_STORAGE_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(d.CreateImage(dev, &imgCI, nullptr, &slot.image));

    VkMemoryRequirements memReq;
    d.GetImageMemoryRequirements(dev, slot.image, &memReq);
    uint32_t memType = FindMemoryType(m_ctx->memProps, memReq.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == UINT32_MAX) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = memType;
    VK_CHECK(d.AllocateMemory(dev, &allocInfo, nullptr, &slot.memory));
    VK_CHECK(d.BindImageMemory(dev, slot.image, slot.memory, 0));

    VkImageViewCreateInfo viewCI = {};
    viewCI.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image      = slot.image;
    viewCI.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format     = m_config.format;
    viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(d.CreateImageView(dev, &viewCI, nullptr, &slot.view));

    // ── Motion-vector image (RG16F) ──────────────────────────────────────────
    imgCI.format = VK_FORMAT_R16G16_SFLOAT;
    imgCI.usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VK_CHECK(d.CreateImage(dev, &imgCI, nullptr, &slot.mvImage));
    d.GetImageMemoryRequirements(dev, slot.mvImage, &memReq);
    memType = FindMemoryType(m_ctx->memProps, memReq.memoryTypeBits,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == UINT32_MAX) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = memType;
    VK_CHECK(d.AllocateMemory(dev, &allocInfo, nullptr, &slot.mvMemory));
    VK_CHECK(d.BindImageMemory(dev, slot.mvImage, slot.mvMemory, 0));
    viewCI.image  = slot.mvImage;
    viewCI.format = VK_FORMAT_R16G16_SFLOAT;
    VK_CHECK(d.CreateImageView(dev, &viewCI, nullptr, &slot.mvView));

    // ── Sync primitives ──────────────────────────────────────────────────────
    VkSemaphoreCreateInfo semCI = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VK_CHECK(d.CreateSemaphore(dev, &semCI, nullptr, &slot.readySem));

    VkFenceCreateInfo fenceCI = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(d.CreateFence(dev, &fenceCI, nullptr, &slot.fence));

    // ── Command buffer ───────────────────────────────────────────────────────
    VkCommandBufferAllocateInfo cbAI = {};
    cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAI.commandPool        = m_cmdPool;
    cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = 1;
    VK_CHECK(d.AllocateCommandBuffers(m_ctx->device, &cbAI, &slot.cmd));

    slot.inFlight = false;
    slot.valid    = true;
    return VK_SUCCESS;
}

// ─── FreeSynthSlot ───────────────────────────────────────────────────────────
void FrameGenerator::FreeSynthSlot(SynthFrame& slot) {
    VkDevice dev = m_ctx->device;
    const DeviceDispatch& d = m_ctx->dispatch;

    if (slot.cmd)      d.FreeCommandBuffers(dev, m_cmdPool, 1, &slot.cmd);
    if (slot.fence)    d.DestroyFence(dev, slot.fence, nullptr);
    if (slot.readySem) d.DestroySemaphore(dev, slot.readySem, nullptr);
    if (slot.mvView)   d.DestroyImageView(dev, slot.mvView, nullptr);
    if (slot.mvImage)  d.DestroyImage(dev, slot.mvImage, nullptr);
    if (slot.mvMemory) d.FreeMemory(dev, slot.mvMemory, nullptr);
    if (slot.view)     d.DestroyImageView(dev, slot.view, nullptr);
    if (slot.image)    d.DestroyImage(dev, slot.image, nullptr);
    if (slot.memory)   d.FreeMemory(dev, slot.memory, nullptr);
    slot = {};
}

// ─── CaptureSwapchainImage ───────────────────────────────────────────────────
VkResult FrameGenerator::CaptureSwapchainImage(
        VkCommandBuffer cmd, VkImage swapchainImage, CapturedFrame& dst)
{
    const DeviceDispatch& d = m_ctx->dispatch;

    // Transition swapchain image: PRESENT_SRC → TRANSFER_SRC
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = swapchainImage;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        d.CmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Transition dst image: UNDEFINED → TRANSFER_DST
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = dst.image;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask       = 0;
        barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        d.CmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Copy
    VkImageCopy region = {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent         = { m_config.width, m_config.height, 1 };
    d.CmdCopyImage(cmd,
        swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst.image,      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);

    // Transition dst: TRANSFER_DST → SHADER_READ_ONLY
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = dst.image;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        d.CmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Restore swapchain image: TRANSFER_SRC → PRESENT_SRC
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = swapchainImage;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
        d.CmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    dst.valid = true;
    return VK_SUCCESS;
}

// ─── OnPresent ───────────────────────────────────────────────────────────────
VkResult FrameGenerator::OnPresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    m_scheduler.OnRealPresent();

    const DeviceDispatch& d = m_ctx->dispatch;
    VkDevice dev = m_ctx->device;

    // Find the swapchain image index being presented
    uint32_t imageIndex = UINT32_MAX;
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        if (pPresentInfo->pSwapchains[i] == m_swapchain) {
            imageIndex = pPresentInfo->pImageIndices[i];
            break;
        }
    }
    if (imageIndex == UINT32_MAX) {
        return d.QueuePresentKHR(queue, pPresentInfo);
    }

    // swapchainImage retrieval is handled by SwapchainManager in the full
    // present-chain implementation (TODO: blit path). No-op here.
    {
        std::lock_guard<std::mutex> lk(g_deviceLock);
        (void)imageIndex; // used in the full blit path
    }

    // On the first real frame, we don't have a previous frame yet — just pass through
    int synthCount = (int)m_config.multiplier - 1;
    if (!m_realFrames[m_prevIdx].valid) {
        // Capture current frame into "prev" slot and pass through
        VkCommandBuffer captureCmd;
        VkCommandBufferAllocateInfo cbAI = {};
        cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbAI.commandPool        = m_cmdPool;
        cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount = 1;
        if (d.AllocateCommandBuffers(dev, &cbAI, &captureCmd) == VK_SUCCESS) {
            VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            d.BeginCommandBuffer(captureCmd, &begin);
            // swapchainImage would be obtained from SwapchainManager — abbreviated here
            d.EndCommandBuffer(captureCmd);
            d.FreeCommandBuffers(dev, m_cmdPool, 1, &captureCmd);
        }
        m_realFrames[m_prevIdx].valid = true;
        return d.QueuePresentKHR(queue, pPresentInfo);
    }

    // ── Synthesis phase ───────────────────────────────────────────────────────
    auto synthStart = std::chrono::steady_clock::now();

    for (int si = 0; si < synthCount; si++) {
        SynthFrame& slot = m_synthFrames[si];

        // Wait for previous use of this slot to finish
        if (slot.inFlight) {
            d.WaitForFences(dev, 1, &slot.fence, VK_TRUE, 2000000000ULL);
            d.ResetFences(dev, 1, &slot.fence);
            slot.inFlight = false;
        }

        float t = PresentScheduler::InterpolationT((int)m_config.multiplier, si);

        // Record synthesis command buffer
        d.ResetCommandBuffer(slot.cmd, 0);
        VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        d.BeginCommandBuffer(slot.cmd, &begin);

        // Run optical flow → motion vectors
        m_opticalFlow.ComputeMotionVectors(
            slot.cmd,
            m_realFrames[m_prevIdx].view,
            m_realFrames[m_currIdx].view,
            slot.mvImage, slot.mvView, t);

        // Synthesise interpolated frame
        m_opticalFlow.SynthesizeFrame(
            slot.cmd,
            m_realFrames[m_prevIdx].view,
            m_realFrames[m_currIdx].view,
            slot.mvView,
            slot.image, slot.view,
            t,
            m_config.ui_mask == VK_TRUE,
            m_config.stereo_aware == VK_TRUE);

        d.EndCommandBuffer(slot.cmd);

        // Submit synthesis to compute queue
        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = &slot.cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &slot.readySem;
        d.QueueSubmit(m_ctx->computeQueue, 1, &submitInfo, slot.fence);
        slot.inFlight = true;
    }

    auto synthEnd = std::chrono::steady_clock::now();
    m_stats.synthesis_ms = std::chrono::duration<float, std::milli>(
        synthEnd - synthStart).count();

    // ── Present synthetic frames ──────────────────────────────────────────────
    // Present each synthesised frame as a blit into the swapchain
    // In production we'd acquire additional swapchain images. Here we blit
    // each synth result over the current swapchain image and re-present.
    // (Full implementation would use a dedicated present queue and ring buffer.)
    for (int si = 0; si < synthCount; si++) {
        SynthFrame& slot = m_synthFrames[si];
        // TODO (full blit path): blit slot.image → acquired swapchain image,
        // wait on slot.readySem, then QueuePresentKHR for each synthetic frame.
        (void)slot;
    }

    // ── Present real frame ────────────────────────────────────────────────────
    VkResult result = d.QueuePresentKHR(queue, pPresentInfo);

    // ── Update stats ──────────────────────────────────────────────────────────
    m_frameCount++;
    m_stats.frames_generated += synthCount;
    if (m_scheduler.IsWarm()) {
        float realInterval = (float)m_scheduler.GetRealIntervalUs() / 1000.0f;
        m_stats.real_fps    = 1000.0f / realInterval;
        m_stats.output_fps  = m_stats.real_fps * (float)m_config.multiplier;
    }

    // ── Rotate ring buffer ────────────────────────────────────────────────────
    std::swap(m_prevIdx, m_currIdx);
    m_realFrames[m_currIdx].valid = false;

    return result;
}
