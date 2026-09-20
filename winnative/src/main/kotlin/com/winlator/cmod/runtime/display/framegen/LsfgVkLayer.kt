// SPDX-License-Identifier: MIT
// LsfgVkLayer.kt — Kotlin обёртка для управления LSFG-VK Vulkan слоем в WinNative
//
// Этот класс управляет активацией VK_LAYER_LSFG_frame_generation через
// переменные окружения, которые передаются в Wine-контейнер WinNative.

package com.winlator.cmod.runtime.display.framegen

import android.content.Context
import android.util.Log

/**
 * Управляет состоянием и конфигурацией LSFG-VK Vulkan слоя.
 * Слой загружается как implicit layer через libVkLayer_lsfg_frame_generation.so,
 * помещённый в jniLibs/arm64-v8a WinNative APK.
 */
object LsfgVkLayer {

    private const val TAG = "LsfgVkLayer"

    /** Имя слоя для VK_INSTANCE_LAYERS */
    const val LAYER_NAME = "VK_LAYER_LSFG_frame_generation"

    /** Имя .so файла в jniLibs */
    const val LIBRARY_NAME = "VkLayer_lsfg_frame_generation"

    // ─── Переменные окружения для Wine-контейнера ────────────────────────────

    /** Список переменных окружения для активации слоя */
    fun buildEnvironment(config: FrameGenConfig): Map<String, String> {
        if (!config.enabled) {
            return mapOf("LSFG_DISABLE" to "1")
        }

        return buildMap {
            put("VK_INSTANCE_LAYERS", LAYER_NAME)
            put("LSFG_MULTIPLIER",    config.multiplier.value.toString())
            put("LSFG_UI_MASK",       if (config.uiMask) "1" else "0")
            put("LSFG_STEREO_AWARE",  if (config.stereoAware) "1" else "0")
            if (config.debugLogging) put("LSFG_DEBUG", "1")
        }
    }

    /**
     * Проверяет, доступен ли .so файл слоя в этом APK.
     * Вызывается при старте контейнера.
     */
    fun isAvailable(context: Context): Boolean {
        return try {
            System.loadLibrary(LIBRARY_NAME)
            Log.i(TAG, "LSFG-VK layer loaded successfully")
            true
        } catch (e: UnsatisfiedLinkError) {
            Log.w(TAG, "LSFG-VK layer not found: ${e.message}")
            false
        }
    }

    /**
     * Возвращает путь к .so файлу внутри APK для передачи в VK_LAYER_PATH.
     * На Android слой загружается автоматически через system linker,
     * поэтому VK_LAYER_PATH обычно не нужен если .so в jniLibs.
     */
    fun getLayerPath(context: Context): String {
        return context.applicationInfo.nativeLibraryDir
    }
}
