// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// Copyright (c) 2024 LSFG-VK Contributors
//
// Public API header — included by the layer and optionally by tools/tests.

#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── Version ─────────────────────────────────────────────────────────────────
#define LSFG_VK_VERSION_MAJOR 1
#define LSFG_VK_VERSION_MINOR 0
#define LSFG_VK_VERSION_PATCH 0

// ─── Environment variable controls ───────────────────────────────────────────
// LSFG_MULTIPLIER   = 2 | 3 | 4          (default: 2)
// LSFG_DEBUG        = 1                  (enable verbose logging)
// LSFG_DISABLE      = 1                  (disable the layer entirely)
// LSFG_UI_MASK      = 0 | 1              (default: 1 — protect UI regions)
// LSFG_STEREO_AWARE = 0 | 1              (default: 1 — VRStereo.fx support)

// ─── Frame generation multiplier ─────────────────────────────────────────────
typedef enum LsfgMultiplier {
    LSFG_MULTIPLIER_2X = 2,
    LSFG_MULTIPLIER_3X = 3,
    LSFG_MULTIPLIER_4X = 4,
} LsfgMultiplier;

// ─── Per-swapchain configuration (read from env at swapchain creation) ────────
typedef struct LsfgSwapchainConfig {
    LsfgMultiplier  multiplier;     ///< Frame generation multiplier
    VkBool32        ui_mask;        ///< Protect UI regions from artefacts
    VkBool32        stereo_aware;   ///< Split stereo frames for VRStereo.fx
    uint32_t        width;          ///< Swapchain image width
    uint32_t        height;         ///< Swapchain image height
    VkFormat        format;         ///< Swapchain image format
} LsfgSwapchainConfig;

// ─── Runtime statistics (exposed for debug overlay) ──────────────────────────
typedef struct LsfgStats {
    float    real_fps;              ///< Measured application FPS
    float    output_fps;            ///< Effective output FPS after interpolation
    float    optical_flow_ms;       ///< Optical flow compute time (ms)
    float    synthesis_ms;          ///< Frame synthesis time (ms)
    uint64_t frames_generated;      ///< Total synthesised frames since start
    uint64_t frames_dropped;        ///< Frames dropped due to latency budget
} LsfgStats;

#ifdef __cplusplus
}
#endif
