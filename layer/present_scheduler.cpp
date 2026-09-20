// SPDX-License-Identifier: MIT
// LSFG-VK — Vulkan Frame Generation Layer for Android ARM64
// layer/present_scheduler.cpp

#include "present_scheduler.h"
#include <algorithm>
#include <numeric>

void PresentScheduler::OnRealPresent() {
    auto now = std::chrono::steady_clock::now();

    if (m_sampleCount > 0) {
        uint64_t deltaUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now - m_lastPresent).count();

        // Clamp to sane range (4ms – 200ms) to ignore stalls
        deltaUs = std::max<uint64_t>(4000,  deltaUs);
        deltaUs = std::min<uint64_t>(200000, deltaUs);

        m_history[m_historyHead] = deltaUs;
        m_historyHead = (m_historyHead + 1) % HISTORY_SIZE;
        m_sampleCount = std::min(m_sampleCount + 1, HISTORY_SIZE);

        // Compute EMA over last min(sampleCount, HISTORY_SIZE) samples
        int count = std::min(m_sampleCount, HISTORY_SIZE);
        uint64_t sum = 0;
        for (int i = 0; i < count; i++) {
            int idx = (m_historyHead - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
            sum += m_history[idx];
        }
        m_realIntervalUs = sum / (uint64_t)count;
    } else {
        m_sampleCount = 1;
    }

    m_lastPresent = now;
}

uint64_t PresentScheduler::GetSynthDelay(int multiplier, int synthIdx) const {
    // Distribute synthetic frames evenly within the real frame interval.
    // synthIdx 0 → first synth frame inserted before the real one.
    // e.g. multiplier=2, synthIdx=0 → delay = interval/2 before real
    if (multiplier <= 1) return 0;
    uint64_t slot = m_realIntervalUs / (uint64_t)multiplier;
    return slot * (uint64_t)(synthIdx + 1);
}

float PresentScheduler::InterpolationT(int multiplier, int synthIdx) {
    // t ∈ (0, 1) between the previous and current real frame.
    // For 2x: t = 0.5
    // For 3x: t = 0.333, 0.667
    // For 4x: t = 0.25, 0.5, 0.75
    return (float)(synthIdx + 1) / (float)multiplier;
}
