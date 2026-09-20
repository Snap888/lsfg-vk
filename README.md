# LSFG-VK — Vulkan Frame Generation Layer for Android ARM64

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: Android ARM64](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)]()
[![Vulkan: 1.1](https://img.shields.io/badge/Vulkan-1.1-red.svg)]()
[![Version: 1.0.0](https://img.shields.io/badge/Version-1.0.0-orange.svg)]()

> 🇷🇺 [Документация на русском](docs/README_RU.md) | 🇬🇧 English documentation below

---

A Vulkan Implicit Layer for **Frame Generation / Frame Interpolation** on Android ARM64.
Analogous to LSFG-VK for Winlator/WinNative — sits transparently between DXVK/Wine and the GPU driver, intercepting `vkQueuePresentKHR` to synthesise intermediate frames using GPU compute shaders.

---

## Features

| Feature | Description |
|---|---|
| **2× / 3× / 4×** | Frame generation multiplier |
| **Optical Flow** | Hierarchical Lucas-Kanade (4-level pyramid) |
| **UI Mask** | HUD/text protection from artefacts (Sobel + saturation) |
| **VR Stereo** | Stereo-aware flow for VRStereo.fx (Winlator VR mode) |
| **Adreno 7xx** | Optimised for Snapdragon 8 Gen 1/2/3 |
| **Mali-G7xx** | Tile-based friendly compute dispatch |

---

## Architecture

```
[DXVK/Wine App]
      │ vkQueuePresentKHR
      ▼
┌─────────────────────────────────────┐
│  VK_LAYER_LSFG_frame_generation     │
│                                     │
│  ┌─────────────┐  ┌──────────────┐  │
│  │SwapchainMgr │  │PresentSched  │  │
│  └──────┬──────┘  └──────┬───────┘  │
│         │                │          │
│  ┌──────▼──────────────────────┐    │
│  │       FrameGenerator        │    │
│  │  ┌───────────────────────┐  │    │
│  │  │   OpticalFlowEngine   │  │    │
│  │  │  pyramid → LK → warp  │  │    │
│  │  │  → blend → ui_mask    │  │    │
│  │  └───────────────────────┘  │    │
│  └─────────────────────────────┘    │
└─────────────────────────────────────┘
      │ vkQueuePresentKHR (×multiplier)
      ▼
[Android GPU Driver]
```

The layer hooks `vkCreateSwapchainKHR`, `vkQueuePresentKHR`, and `vkDestroySwapchainKHR`. For each real application frame it synthesises `(multiplier − 1)` intermediate frames and presents them before the real frame.

---

## Project Structure

```
lsfg/
├── CMakeLists.txt              # Build system (Android NDK)
├── compile_shaders.sh          # GLSL → SPIR-V compilation script
├── cmake/
│   └── SpvToHeader.cmake       # SPIR-V → C++ header embedding
├── include/
│   └── lsfg_vk.h               # Public API (types, env-var docs)
├── layer/
│   ├── layer.h / .cpp          # Entry point, Vulkan dispatch tables
│   ├── swapchain_manager.h/.cpp # Swapchain interception
│   ├── frame_generator.h/.cpp  # Frame generation orchestrator
│   ├── optical_flow.h/.cpp     # GPU optical flow engine
│   └── present_scheduler.h/.cpp # Present timing scheduler
├── shaders/
│   ├── optical_flow_pyramid.comp  # Gaussian pyramid downscale
│   ├── motion_vectors.comp        # Lucas-Kanade motion vectors
│   ├── optical_flow_warp.comp     # Bidirectional warp
│   ├── optical_flow_blend.comp    # Blend + Bayer dither
│   └── ui_mask.comp               # UI mask detection
├── manifest/
│   └── VkLayer_lsfg_frame_generation.json
├── docs/
│   └── README_RU.md            # Russian documentation
└── android/
    ├── build.gradle
    ├── settings.gradle
    └── app/
        └── build.gradle
```

---

## Build

### Requirements

| Tool | Version |
|---|---|
| Android NDK | r26+ (`26.1.10909125`) |
| CMake | 3.22+ |
| `glslangValidator` | Vulkan SDK or NDK shader-tools |
| C++ Compiler | C++17 |

### Step 1: Compile shaders

```bash
# Install glslangValidator
apt install glslang-tools          # Debian/Ubuntu
# or download Vulkan SDK: https://vulkan.lunarg.com/

chmod +x compile_shaders.sh
./compile_shaders.sh
```

### Step 2: Build via Android Studio / Gradle

```bash
cd android
./gradlew assembleRelease
# Output: output/arm64-v8a/libVkLayer_lsfg_frame_generation.so
```

### Step 3: Build via CMake (command line)

```bash
export ANDROID_NDK=/path/to/ndk
mkdir build && cd build
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## Installation in Winlator

### Method 1: jniLibs (recommended)

1. Copy `libVkLayer_lsfg_frame_generation.so` into `jniLibs/arm64-v8a/` of the Winlator APK.
2. In the Winlator container settings, set the following environment variables:

```bash
VK_INSTANCE_LAYERS=VK_LAYER_LSFG_frame_generation
VK_LAYER_PATH=/data/data/com.winlator/lib   # path to jniLibs
LSFG_MULTIPLIER=2                            # 2, 3 or 4
LSFG_UI_MASK=1
LSFG_STEREO_AWARE=1                          # for VR mode
```

3. Restart the container.

### Method 2: /data/local/debug/vulkan/ (ADB debug)

```bash
adb root
adb shell mkdir -p /data/local/debug/vulkan
adb push libVkLayer_lsfg_frame_generation.so /data/local/debug/vulkan/
adb push VkLayer_lsfg_frame_generation.json  /data/local/debug/vulkan/
```

---

## Configuration (Environment Variables)

| Variable | Values | Default | Description |
|---|---|---|---|
| `LSFG_MULTIPLIER` | `2`, `3`, `4` | `2` | Frame generation multiplier |
| `LSFG_UI_MASK` | `0`, `1` | `1` | UI region protection from artefacts |
| `LSFG_STEREO_AWARE` | `0`, `1` | `1` | VRStereo.fx support |
| `LSFG_DEBUG` | `1` | — | Verbose logcat logging |
| `LSFG_DISABLE` | `1` | — | Completely disable the layer |

---

## VR Mode (VRStereo.fx)

When `LSFG_STEREO_AWARE=1`, the layer detects stereo frames (left eye: `x∈[0, W/2]`, right eye: `x∈[W/2, W]`) and computes optical flow independently for each eye, preventing motion-vector bleed between eyes.

---

## Optical Flow Algorithm

| Stage | Description |
|---|---|
| **1. Pyramid** | 4-level Gaussian pyramid (resolution ÷2 per level) |
| **2. Motion Vectors** | Hierarchical Lucas-Kanade, coarse-to-fine, 3×3 neighbourhood, 5 iterations/level |
| **3. Warp** | Bidirectional warp: prev @ uv-t·mv, curr @ uv+(1-t)·mv |
| **4. Blend** | Linear mix + 4×4 Bayer dither (suppresses 8-bit banding) |
| **5. UI Mask** | Sobel edge + HSV saturation detection, preserve original in UI zones |

---

## API Reference

### Public Types (include/lsfg_vk.h)

#### LsfgMultiplier
```c
typedef enum LsfgMultiplier {
    LSFG_MULTIPLIER_2X = 2,
    LSFG_MULTIPLIER_3X = 3,
    LSFG_MULTIPLIER_4X = 4,
} LsfgMultiplier;
```

#### LsfgSwapchainConfig
```c
typedef struct LsfgSwapchainConfig {
    LsfgMultiplier  multiplier;   // Frame generation multiplier
    VkBool32        ui_mask;      // Protect UI regions from artefacts
    VkBool32        stereo_aware; // Split stereo frames for VRStereo.fx
    uint32_t        width;        // Swapchain image width
    uint32_t        height;       // Swapchain image height
    VkFormat        format;       // Swapchain image format
} LsfgSwapchainConfig;
```

#### LsfgStats
Runtime statistics exposed for a debug overlay:
```c
typedef struct LsfgStats {
    float    real_fps;          // Measured application FPS
    float    output_fps;        // Effective output FPS after interpolation
    float    optical_flow_ms;   // Optical flow compute time (ms)
    float    synthesis_ms;      // Frame synthesis time (ms)
    uint64_t frames_generated;  // Total synthesised frames since start
    uint64_t frames_dropped;    // Frames dropped due to latency budget
} LsfgStats;
```

---

## Known Limitations / TODO

- [ ] Full present-chain integration (blit synthetic frame into swapchain image)
- [ ] Descriptor set cache (currently recreated per-frame)
- [ ] HDR format support (R16G16B16A16_SFLOAT)
- [ ] Snapdragon Profiler markers
- [ ] Mali-G710 (Samsung Exynos) validation

---

## License

MIT License — compatible with AMD FidelityFX, DXVK, and the Vulkan Loader.

See [LICENSE](LICENSE) for the full text.

---

## Contributing

Pull requests are welcome. Please make sure:
- Code compiles cleanly with `-Wall -Wextra`
- Shaders re-compile with `./compile_shaders.sh`
- New features are covered by a brief description in `docs/`
