# LSFG-VK — Vulkan Layer для генерации кадров (Android ARM64)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](../LICENSE)
[![Platform: Android ARM64](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)]()
[![Vulkan: 1.1](https://img.shields.io/badge/Vulkan-1.1-red.svg)]()
[![Version: 1.0.0](https://img.shields.io/badge/Version-1.0.0-orange.svg)]()

> 🇷🇺 Русская документация | 🇬🇧 [English README](../README.md)

---

Vulkan Implicit Layer для **генерации промежуточных кадров (Frame Interpolation)** на Android ARM64.
Аналог LSFG-VK для Winlator/WinNative — прозрачно встраивается между DXVK/Wine и драйвером GPU,
перехватывая `vkQueuePresentKHR` для синтеза промежуточных кадров через GPU compute shaders.

---

## Возможности

| Функция | Описание |
|---|---|
| **2× / 3× / 4×** | Множитель генерации кадров |
| **Optical Flow** | Иерархический Lucas-Kanade (4-уровневая пирамида) |
| **UI Mask** | Защита HUD/текста от артефактов (Sobel + насыщенность) |
| **VR Stereo** | Стерео-aware поток для VRStereo.fx (Winlator VR-режим) |
| **Adreno 7xx** | Оптимизировано под Snapdragon 8 Gen 1/2/3 |
| **Mali-G7xx** | Tile-based friendly compute dispatch |

---

## Архитектура

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

Слой перехватывает `vkCreateSwapchainKHR`, `vkQueuePresentKHR` и `vkDestroySwapchainKHR`.
Для каждого реального кадра приложения синтезирует `(multiplier − 1)` промежуточных кадров
и презентует их перед реальным кадром.

---

## Структура проекта

```
lsfg/
├── CMakeLists.txt              # Система сборки (Android NDK)
├── compile_shaders.sh          # Компиляция GLSL → SPIR-V
├── cmake/
│   └── SpvToHeader.cmake       # SPIR-V → C++ header
├── include/
│   └── lsfg_vk.h               # Публичный API (типы, переменные окружения)
├── layer/
│   ├── layer.h / .cpp          # Точка входа, dispatch tables
│   ├── swapchain_manager.h/.cpp # Перехват swapchain
│   ├── frame_generator.h/.cpp  # Оркестратор генерации кадров
│   ├── optical_flow.h/.cpp     # GPU движок оптического потока
│   └── present_scheduler.h/.cpp # Планировщик презентации
├── shaders/
│   ├── optical_flow_pyramid.comp  # Gaussian pyramid
│   ├── motion_vectors.comp        # Lucas-Kanade MV
│   ├── optical_flow_warp.comp     # Bidirectional warp
│   ├── optical_flow_blend.comp    # Blend + dither
│   └── ui_mask.comp               # UI mask детекция
├── manifest/
│   └── VkLayer_lsfg_frame_generation.json
├── docs/
│   └── README_RU.md            # Эта документация
└── android/
    ├── build.gradle
    ├── settings.gradle
    └── app/
        └── build.gradle
```

---

## Сборка

### Требования

| Инструмент | Версия |
|---|---|
| Android NDK | r26+ (`26.1.10909125`) |
| CMake | 3.22+ |
| `glslangValidator` | Vulkan SDK или NDK shader-tools |
| Компилятор C++ | C++17 |

### Шаг 1: Компиляция шейдеров

```bash
# Установить glslangValidator
apt install glslang-tools       # Debian/Ubuntu
# или скачать Vulkan SDK: https://vulkan.lunarg.com/

chmod +x compile_shaders.sh
./compile_shaders.sh
```

### Шаг 2: Сборка через Android Studio / Gradle

```bash
cd android
./gradlew assembleRelease
# Готовый .so в: output/arm64-v8a/libVkLayer_lsfg_frame_generation.so
```

### Шаг 3: Сборка через CMake напрямую

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

## Установка в Winlator

### Метод 1: jniLibs (рекомендуется)

1. Скопируйте `libVkLayer_lsfg_frame_generation.so` в `jniLibs/arm64-v8a/` Winlator APK.
2. В настройках контейнера Winlator установите переменные окружения:

```bash
VK_INSTANCE_LAYERS=VK_LAYER_LSFG_frame_generation
VK_LAYER_PATH=/data/data/com.winlator/lib  # путь к jniLibs
LSFG_MULTIPLIER=2                           # 2, 3 или 4
LSFG_UI_MASK=1
LSFG_STEREO_AWARE=1                         # для VR-режима
```

3. Перезапустите контейнер.

### Метод 2: /data/local/debug/vulkan/ (ADB debug)

```bash
adb root
adb shell mkdir -p /data/local/debug/vulkan
adb push libVkLayer_lsfg_frame_generation.so /data/local/debug/vulkan/
adb push VkLayer_lsfg_frame_generation.json  /data/local/debug/vulkan/
```

---

## Настройка (переменные окружения)

| Переменная | Значение | По умолчанию | Описание |
|---|---|---|---|
| `LSFG_MULTIPLIER` | `2`, `3`, `4` | `2` | Множитель кадров |
| `LSFG_UI_MASK` | `0`, `1` | `1` | Защита UI от артефактов |
| `LSFG_STEREO_AWARE` | `0`, `1` | `1` | VRStereo.fx поддержка |
| `LSFG_DEBUG` | `1` | — | Verbose логирование в logcat |
| `LSFG_DISABLE` | `1` | — | Полное отключение слоя |

---

## VR-режим (VRStereo.fx)

При `LSFG_STEREO_AWARE=1` слой определяет стерео-кадры:
- **Левый глаз:** `x ∈ [0, W/2]`
- **Правый глаз:** `x ∈ [W/2, W]`

Оптический поток вычисляется **независимо для каждого глаза**, что исключает перетекание векторов движения между глазами.

---

## Алгоритм оптического потока

| Этап | Описание |
|---|---|
| **1. Pyramid** | 4-уровневая Gaussian pyramid (разрешение ÷2 на каждом уровне) |
| **2. Motion Vectors** | Иерархический Lucas-Kanade, coarse-to-fine, соседство 3×3, 5 итераций/уровень |
| **3. Warp** | Бидиректциональный варпинг: prev @ uv-t·mv, curr @ uv+(1-t)·mv |
| **4. Blend** | Линейная смесь + Bayer 4×4 dither (борьба с 8-bit banding) |
| **5. UI Mask** | Sobel + HSV saturation детекция → сохранение оригинала в UI зонах |

---

## Справочник API

### Публичные типы (include/lsfg_vk.h)

#### LsfgMultiplier
```c
typedef enum LsfgMultiplier {
    LSFG_MULTIPLIER_2X = 2,
    LSFG_MULTIPLIER_3X = 3,
    LSFG_MULTIPLIER_4X = 4,
} LsfgMultiplier;
```

#### LsfgSwapchainConfig
Конфигурация на swapchain, считывается из env-переменных при создании swapchain:
```c
typedef struct LsfgSwapchainConfig {
    LsfgMultiplier  multiplier;   // Множитель генерации кадров
    VkBool32        ui_mask;      // Защита UI-регионов от артефактов
    VkBool32        stereo_aware; // Разделение стерео-кадров для VRStereo.fx
    uint32_t        width;        // Ширина swapchain изображения
    uint32_t        height;       // Высота swapchain изображения
    VkFormat        format;       // Формат swapchain изображения
} LsfgSwapchainConfig;
```

#### LsfgStats
Статистика времени выполнения (для debug overlay):
```c
typedef struct LsfgStats {
    float    real_fps;          // Измеренный FPS приложения
    float    output_fps;        // Эффективный FPS после интерполяции
    float    optical_flow_ms;   // Время вычисления оптического потока (мс)
    float    synthesis_ms;      // Время синтеза кадра (мс)
    uint64_t frames_generated;  // Всего синтезированных кадров с запуска
    uint64_t frames_dropped;    // Кадры, пропущенные из-за бюджета задержки
} LsfgStats;
```

### Внутренние компоненты

#### FrameGenerator
Один экземпляр на swapchain. Владеет оптическим потоком, кольцевыми буферами,
пайплайном синтеза и планировщиком презентации.

```cpp
class FrameGenerator {
public:
    VkResult Init(DeviceContext* ctx, VkSwapchainKHR swapchain,
                  const LsfgSwapchainConfig& config,
                  const std::vector<VkImage>& swapchainImages);

    // Вызывается на каждый реальный present. Запускает синтез.
    VkResult OnPresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

    const LsfgStats& GetStats() const;
};
```

#### OpticalFlowEngine
GPU-движок оптического потока на compute shaders:

```cpp
class OpticalFlowEngine {
public:
    VkResult Init(DeviceContext* ctx, uint32_t width, uint32_t height, VkFormat format);

    // Вычислить векторы движения между двумя кадрами
    void ComputeMotionVectors(VkCommandBuffer cmd,
                              VkImageView prevFrame, VkImageView currFrame,
                              VkImage mvImage, VkImageView mvView, float t);

    // Синтезировать интерполированный кадр
    void SynthesizeFrame(VkCommandBuffer cmd,
                         VkImageView prevFrame, VkImageView currFrame,
                         VkImageView mvView,
                         VkImage dstImage, VkImageView dstView,
                         float t, bool useUiMask, bool stereoAware);
};
```

---

## TODO / Известные ограничения

- [ ] Полная интеграция present chain (blit синтетического кадра в swapchain image)
- [ ] Descriptor set кэш (сейчас пересоздаётся per-frame)
- [ ] Поддержка HDR форматов (R16G16B16A16_SFLOAT)
- [ ] Профилировщик (Snapdragon Profiler markers)
- [ ] Тест на Mali-G710 (Samsung Exynos)

---

## Лицензия

MIT License — совместима с AMD FidelityFX, DXVK, Vulkan Loader.

Полный текст: [LICENSE](../LICENSE)

---

## Участие в разработке

Pull request'ы приветствуются. Убедитесь что:
- Код компилируется без ошибок с флагами `-Wall -Wextra`
- Шейдеры пересобираются через `./compile_shaders.sh`
- Новые функции описаны в `docs/`
