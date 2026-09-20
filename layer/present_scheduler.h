// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// layer/present_scheduler.h — Timing and pacing of synthesised frames

#pragma once

#include <chrono>
#include <cstdint>
#include <array>

// ─── PresentScheduler ─────────────────────────────────────────────────────────
//
// Measures the real frame interval and distributes synthetic frames evenly
// so that the display receives them at approximately equal intervals.
//
class PresentScheduler {
public:
    PresentScheduler() = default;

    // Call when the application submits a real frame
    void OnRealPresent();

    // Returns the estimated real frame interval (microseconds)
    uint64_t GetRealIntervalUs() const { return m_realIntervalUs; }

    // Returns the recommended delay between presents (microseconds)
    // for a given multiplier and synthetic frame index.
    // synthIdx: 0 = first synthetic frame, multiplier-2 = last.
    uint64_t GetSynthDelay(int multiplier, int synthIdx) const;

    // Returns true if we have enough timing history to make predictions.
    bool IsWarm() const { return m_sampleCount >= WARM_UP_FRAMES; }

    // Returns the interpolation time t ∈ (0, 1) for synthIdx in a multiplier.
    static float InterpolationT(int multiplier, int synthIdx);

private:
    static constexpr int WARM_UP_FRAMES  = 5;
    static constexpr int HISTORY_SIZE    = 16;

    std::array<uint64_t, HISTORY_SIZE> m_history = {};
    int      m_historyHead    = 0;
    int      m_sampleCount    = 0;
    uint64_t m_realIntervalUs = 16667;  // default: 60 Hz

    std::chrono::steady_clock::time_point m_lastPresent;
};
