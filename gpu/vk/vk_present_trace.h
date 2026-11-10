// Bounded change-only presentation diagnostics; caller serializes access with present_image_mu.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace mkwvk {

struct PresentGeometrySample {
    std::array<uint32_t, 2> source{}, content{}, output{};
    std::array<uint32_t, 4> destination{};
    std::array<int, 4> crop{};
    bool crop_supplied = false, sampled_view = false;
    double aspect = 0;
    uint32_t aspect_state = 0; // 0 unavailable, 1 invalid, 2 positive finite
    uint32_t kind = 0, route = 0; // kind: game/made/startup; route: direct/scaled/clear/HDR conversion
    int filter = 0, sharpness = 0, framegen = 0;

    bool operator==(const PresentGeometrySample&) const = default;

    void host_aspect(bool available, double value) {
        const bool valid = available && value > 0 && std::isfinite(value);
        aspect_state = available ? (valid ? 2 : 1) : 0;
        aspect = valid ? value : 0; // NaN must not make an unchanged sample different every frame.
    }
};

enum class PresentTraceDecision { Quiet, Emit, Limit };

class PresentGeometryTrace {
public:
    static constexpr uint32_t kLimit = 256;

    PresentTraceDecision note(const PresentGeometrySample& sample) {
        const uint32_t kind = sample.kind < last_.size() ? sample.kind : 0;
        auto key = sample;
        // Requests accompany effective changes; slider-only changes must not consume the geometry budget.
        key.filter = key.sharpness = key.framegen = 0;
        if (seen_[kind] && key == last_[kind]) return PresentTraceDecision::Quiet;
        last_[kind] = key;
        seen_[kind] = true;
        if (emitted_ < kLimit) {
            ++emitted_;
            return PresentTraceDecision::Emit;
        }
        if (!limited_) {
            limited_ = true;
            return PresentTraceDecision::Limit;
        }
        return PresentTraceDecision::Quiet;
    }

private:
    std::array<PresentGeometrySample, 3> last_{};
    std::array<bool, 3> seen_{};
    uint32_t emitted_ = 0;
    bool limited_ = false;
};

} // namespace mkwvk
