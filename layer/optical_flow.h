// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// layer/optical_flow.h — GPU optical flow engine (compute shaders)

#pragma once

#include "layer.h"
#include <vector>
#include <array>

// ─── Constants ────────────────────────────────────────────────────────────────
static constexpr uint32_t PYRAMID_LEVELS   = 4;
static constexpr uint32_t BLOCK_SIZE       = 8;   // LK block size in pixels
static constexpr uint32_t MAX_ITERATIONS   = 5;   // LK iterations per level

// ─── Push-constant layouts (must match shaders) ───────────────────────────────
struct PyramidPushConst {
    uint32_t src_width;
    uint32_t src_height;
    uint32_t dst_width;
    uint32_t dst_height;
};

struct MotionVectorPushConst {
    uint32_t width;
    uint32_t height;
    uint32_t level;         // pyramid level (0 = finest)
    float    t;             // interpolation time [0,1]
};

struct WarpPushConst {
    uint32_t width;
    uint32_t height;
    float    t;             // interpolation time [0,1]
    uint32_t stereo_aware;  // 1 = split L/R eye
};

struct BlendPushConst {
    uint32_t width;
    uint32_t height;
    float    t;
    uint32_t use_ui_mask;
};

// ─── Per-level pyramid resource ───────────────────────────────────────────────
struct PyramidLevel {
    VkImage     image     = VK_NULL_HANDLE;
    VkImageView view      = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t    width     = 0;
    uint32_t    height    = 0;
};

// ─── OpticalFlowEngine ───────────────────────────────────────────────────────
class OpticalFlowEngine {
public:
    OpticalFlowEngine() = default;
    ~OpticalFlowEngine() { Destroy(); }

    // Initialize pipelines and allocate persistent resources
    VkResult Init(DeviceContext* ctx, uint32_t width, uint32_t height, VkFormat format);
    void     Destroy();

    // Compute motion vectors between prevFrame and currFrame.
    // Writes motion vectors into mvImage (RG16_SFLOAT, same resolution as frames).
    // Records commands into cmd; caller owns submission.
    void ComputeMotionVectors(
        VkCommandBuffer cmd,
        VkImageView     prevFrame,
        VkImageView     currFrame,
        VkImage         mvImage,
        VkImageView     mvView,
        float           t);

    // Synthesize an interpolated frame.
    // Reads mvImage + prevFrame + currFrame, writes into dstImage.
    void SynthesizeFrame(
        VkCommandBuffer cmd,
        VkImageView     prevFrame,
        VkImageView     currFrame,
        VkImageView     mvView,
        VkImage         dstImage,
        VkImageView     dstView,
        float           t,
        bool            useUiMask,
        bool            stereoAware);

private:
    DeviceContext*  m_ctx      = nullptr;
    uint32_t        m_width    = 0;
    uint32_t        m_height   = 0;
    VkFormat        m_format   = VK_FORMAT_UNDEFINED;

    // ── Pyramid resources ────────────────────────────────────────────────────
    std::array<PyramidLevel, PYRAMID_LEVELS> m_prevPyramid;
    std::array<PyramidLevel, PYRAMID_LEVELS> m_currPyramid;

    // ── Intermediate MV buffer (each level) ──────────────────────────────────
    std::array<PyramidLevel, PYRAMID_LEVELS> m_mvPyramid;

    // ── UI mask image ────────────────────────────────────────────────────────
    VkImage        m_uiMaskImage  = VK_NULL_HANDLE;
    VkImageView    m_uiMaskView   = VK_NULL_HANDLE;
    VkDeviceMemory m_uiMaskMem    = VK_NULL_HANDLE;

    // ── Sampler ──────────────────────────────────────────────────────────────
    VkSampler      m_linearSampler = VK_NULL_HANDLE;
    VkSampler      m_nearestSampler= VK_NULL_HANDLE;

    // ── Descriptor layouts ───────────────────────────────────────────────────
    VkDescriptorPool       m_descPool       = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_pyramidLayout  = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_mvLayout       = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_warpLayout     = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_blendLayout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_maskLayout     = VK_NULL_HANDLE;

    // ── Pipelines ────────────────────────────────────────────────────────────
    VkPipeline       m_pyramidPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pyramidPipeLayout = VK_NULL_HANDLE;
    VkPipeline       m_mvPipeline      = VK_NULL_HANDLE;
    VkPipelineLayout m_mvPipeLayout    = VK_NULL_HANDLE;
    VkPipeline       m_warpPipeline    = VK_NULL_HANDLE;
    VkPipelineLayout m_warpPipeLayout  = VK_NULL_HANDLE;
    VkPipeline       m_blendPipeline   = VK_NULL_HANDLE;
    VkPipelineLayout m_blendPipeLayout = VK_NULL_HANDLE;
    VkPipeline       m_maskPipeline    = VK_NULL_HANDLE;
    VkPipelineLayout m_maskPipeLayout  = VK_NULL_HANDLE;

    // ── Helpers ──────────────────────────────────────────────────────────────
    VkResult CreatePyramidResources();
    VkResult CreatePipelines();
    VkResult CreateDescriptorPool();
    VkResult CreateSamplers();

    VkResult AllocatePyramidLevel(PyramidLevel& level,
                                  uint32_t w, uint32_t h,
                                  VkFormat fmt,
                                  VkImageUsageFlags usage);
    void     FreePyramidLevel(PyramidLevel& level);

    VkResult CreateComputePipeline(
        const uint32_t*  spvCode,
        size_t           spvSize,
        VkDescriptorSetLayout setLayout,
        uint32_t         pushConstSize,
        VkPipelineLayout* outLayout,
        VkPipeline*      outPipeline);

    void ImageBarrier(VkCommandBuffer cmd,
                      VkImage         image,
                      VkImageLayout   oldLayout,
                      VkImageLayout   newLayout,
                      VkAccessFlags   srcAccess,
                      VkAccessFlags   dstAccess,
                      VkPipelineStageFlags srcStage,
                      VkPipelineStageFlags dstStage);
};
