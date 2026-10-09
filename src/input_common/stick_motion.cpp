// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <tuple>
#include "common/math_util.h"
#include "common/quaternion.h"
#include "common/settings.h"
#include "common/vector_math.h"
#include "input_common/stick_motion.h"

namespace InputCommon {

namespace {

constexpr float MaxPitchDegrees = 80.0f;
// Longest time step taken into account, so a long pause does not cause a big jump
constexpr float MaxStepSeconds = 0.1f;

// The orientation is advanced whenever the status is polled (on the thread that polls the
// motion sensors), so the analog device is only read from that thread. This matters for
// frontends such as libretro, whose input callbacks are only valid on the emulation thread.
class StickMotionDevice final : public Input::MotionDevice {
public:
    StickMotionDevice(std::unique_ptr<Input::AnalogDevice> analog_, float max_rate_,
                      bool invert_x_, bool invert_y_)
        : analog(std::move(analog_)), max_rate(max_rate_), invert_x(invert_x_),
          invert_y(invert_y_) {}

    std::tuple<Common::Vec3<float>, Common::Vec3<float>> GetStatus() const override {
        std::lock_guard guard{mutex};

        const auto now = std::chrono::steady_clock::now();
        const float dt =
            last_update ? std::min(std::chrono::duration<float>(now - *last_update).count(),
                                   MaxStepSeconds)
                        : 0.0f;
        last_update = now;

        const auto old_q = q;
        if (dt > 0.0f && analog) {
            const auto [x, y] = analog->GetStatus();
            const float step = Common::PI / 180.0f * max_rate * dt;
            const float max_pitch = Common::PI / 180.0f * MaxPitchDegrees;
            yaw += (invert_x ? x : -x) * step;
            pitch = std::clamp(pitch + (invert_y ? -y : y) * step, -max_pitch, max_pitch);

            // Turn around the world's vertical axis, then look up/down around the 3DS's own
            // horizontal axis so the horizon stays level.
            q = Common::MakeQuaternion(Common::MakeVec(0.0f, 1.0f, 0.0f), yaw) *
                Common::MakeQuaternion(Common::MakeVec(1.0f, 0.0f, 0.0f), pitch);
        }

        // Same conversion as the mouse motion emulation (motion_emu.cpp)
        const auto inv_q = q.Inverse();
        auto gravity = Common::MakeVec(0.0f, -1.0f, 0.0f);
        auto angular_rate = Common::MakeVec(0.0f, 0.0f, 0.0f);
        if (dt > 0.0f) {
            angular_rate = ((q - old_q) * inv_q).xyz * 2;
            angular_rate *= 1.0f / dt / Common::PI * 180.0f;
        }
        gravity = Common::QuaternionRotate(inv_q, gravity);
        angular_rate = Common::QuaternionRotate(inv_q, angular_rate);
        return {gravity, angular_rate};
    }

private:
    std::unique_ptr<Input::AnalogDevice> analog;
    const float max_rate;
    const bool invert_x;
    const bool invert_y;

    mutable std::mutex mutex;
    mutable std::optional<std::chrono::steady_clock::time_point> last_update;
    mutable float yaw = 0.0f;
    mutable float pitch = 0.0f;
    mutable Common::Quaternion<float> q = Common::MakeQuaternion(Common::Vec3<float>(), 0);
};

} // Anonymous namespace

std::unique_ptr<Input::MotionDevice> StickMotion::Create(const Common::ParamPackage& params) {
    const std::string analog_params = params.Get(
        "analog", Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CStick]);
    return std::make_unique<StickMotionDevice>(
        Input::CreateDevice<Input::AnalogDevice>(analog_params), params.Get("max_rate", 90.0f),
        params.Get("invert_x", 0) != 0, params.Get("invert_y", 0) != 0);
}

} // namespace InputCommon
