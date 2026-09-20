// SPDX-License-Identifier: MIT
// FrameGenConfig.kt — Конфигурация генерации кадров для WinNative

package com.winlator.cmod.runtime.display.framegen

import android.os.Parcelable
import kotlinx.parcelize.Parcelize

/**
 * Множитель кадров Frame Generation.
 * Значения соответствуют LSFG_MULTIPLIER env var.
 */
enum class FrameGenMultiplier(val value: Int, val displayName: String) {
    X2(2, "2×  (~2x FPS)"),
    X3(3, "3×  (~3x FPS)"),
    X4(4, "4×  (~4x FPS)");

    companion object {
        fun fromValue(v: Int): FrameGenMultiplier =
            entries.firstOrNull { it.value == v } ?: X2
    }
}

/**
 * Полная конфигурация генерации кадров.
 * Сохраняется в SharedPreferences контейнера и передаётся в LsfgVkLayer.buildEnvironment().
 */
@Parcelize
data class FrameGenConfig(
    /** Включить генерацию кадров */
    val enabled: Boolean = false,

    /** Множитель (2x, 3x, 4x) */
    val multiplier: FrameGenMultiplier = FrameGenMultiplier.X2,

    /** Защита UI/HUD регионов от артефактов (рекомендуется включить) */
    val uiMask: Boolean = true,

    /**
     * Stereo-aware оптический поток для VRStereo.fx.
     * При включении вычисляет поток независимо для левого и правого глаза.
     */
    val stereoAware: Boolean = true,

    /** Verbose логирование в logcat (для отладки) */
    val debugLogging: Boolean = false,
) : Parcelable {

    companion object {
        /** Ключи SharedPreferences */
        const val PREF_KEY_ENABLED      = "lsfg_enabled"
        const val PREF_KEY_MULTIPLIER   = "lsfg_multiplier"
        const val PREF_KEY_UI_MASK      = "lsfg_ui_mask"
        const val PREF_KEY_STEREO_AWARE = "lsfg_stereo_aware"
        const val PREF_KEY_DEBUG        = "lsfg_debug"

        /** Восстановление из SharedPreferences */
        fun fromPrefs(prefs: android.content.SharedPreferences): FrameGenConfig =
            FrameGenConfig(
                enabled      = prefs.getBoolean(PREF_KEY_ENABLED, false),
                multiplier   = FrameGenMultiplier.fromValue(
                                   prefs.getInt(PREF_KEY_MULTIPLIER, 2)),
                uiMask       = prefs.getBoolean(PREF_KEY_UI_MASK, true),
                stereoAware  = prefs.getBoolean(PREF_KEY_STEREO_AWARE, true),
                debugLogging = prefs.getBoolean(PREF_KEY_DEBUG, false),
            )
    }

    /** Сохранение в SharedPreferences */
    fun saveToPrefs(prefs: android.content.SharedPreferences) {
        prefs.edit()
            .putBoolean(PREF_KEY_ENABLED,      enabled)
            .putInt    (PREF_KEY_MULTIPLIER,   multiplier.value)
            .putBoolean(PREF_KEY_UI_MASK,      uiMask)
            .putBoolean(PREF_KEY_STEREO_AWARE, stereoAware)
            .putBoolean(PREF_KEY_DEBUG,        debugLogging)
            .apply()
    }
}
