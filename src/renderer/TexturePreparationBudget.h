#pragma once
#include <algorithm>
#include <cstdint>

// Render-thread-local credit. Preserve preparation throughput when animation
// is capped, while bounding work after a pause or a long render stall.
class TexturePreparationBudget {
  public:
    static constexpr std::int64_t MiB = 1024 * 1024;
    void beginFrame(std::int64_t now, double expectedPeriodNs = 0) {
        frameNs_ = frameBytes_ = 0;
        frameCapNs_ = 2000000;
        frameCapBytes_ = 4 * MiB;
        if (last_ >= 0) {
            const auto elapsed = std::clamp<std::int64_t>(now - last_, 0, 1000000000);
            const double cpuRate = expectedPeriodNs > 0 ? std::max(.12, 2000000 / expectedPeriodNs) : .12;
            const double uploadRate = expectedPeriodNs > 0 ?
                std::max(240.0 * MiB / 1e9, 4.0 * MiB / expectedPeriodNs) : 240.0 * MiB / 1e9;
            cpuNs_ = std::min(8000000.0, cpuNs_ + elapsed * cpuRate);
            uploadBytes_ = std::min(16.0 * MiB, uploadBytes_ + elapsed * uploadRate);
            if (elapsed > 0) {
                averagePeriodNs_ = averagePeriodNs_ > 0 ? .75 * averagePeriodNs_ + .25 * elapsed : elapsed;
                // Extra preparation is useful only when software pacing leaves
                // headroom. Spending it while already behind hurts smoothness.
                if (expectedPeriodNs > 0 && averagePeriodNs_ <= expectedPeriodNs * 1.1) {
                    frameCapNs_ = std::clamp(.12 * expectedPeriodNs, 2000000.0, 8000000.0);
                    frameCapBytes_ = std::clamp(240.0 * MiB / 1e9 * expectedPeriodNs, 4.0 * MiB, 16.0 * MiB);
                }
            }
        }
        last_ = std::max(last_, now);
    }
    bool permits(std::int64_t bytes) const {
        return cpuNs_ > 0 && frameNs_ < frameCapNs_ && bytes <= uploadBytes_ && bytes <= frameCapBytes_ - frameBytes_;
    }
    void chargeTime(std::int64_t ns) { const auto cost = std::max<std::int64_t>(0, ns); cpuNs_ -= cost; frameNs_ += cost; }
    void chargeUpload(std::int64_t bytes) { const auto cost = std::max<std::int64_t>(0, bytes); uploadBytes_ -= cost; frameBytes_ += cost; }
  private:
    std::int64_t last_{-1};
    double cpuNs_{2000000}, uploadBytes_{4.0 * MiB};
    double averagePeriodNs_{}, frameNs_{}, frameBytes_{};
    double frameCapNs_{2000000}, frameCapBytes_{4.0 * MiB};
};
