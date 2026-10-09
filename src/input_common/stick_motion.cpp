// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <tuple>
#include "common/math_util.h"
#include "common/quaternion.h"
#include "common/settings.h"
#include "common/thread.h"
#include "common/vector_math.h"
#include "input_common/stick_motion.h"

namespace InputCommon {

namespace {

constexpr float MaxPitchDegrees = 80.0f;

class StickMotionDevice final : public Input::MotionDevice {
public:
    StickMotionDevice(std::unique_ptr<Input::AnalogDevice> analog_, int update_millisecond_,
                      float max_rate_, bool invert_x_, bool invert_y_)
        : analog(std::move(analog_)), update_millisecond(update_millisecond_),
          max_rate(max_rate_), invert_x(invert_x_), invert_y(invert_y_),
          thread(&StickMotionDevice::UpdateThread, this) {}

    ~StickMotionDevice() override {
        shutdown_event.Set();
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::tuple<Common::Vec3<float>, Common::Vec3<float>> GetStatus() const override {
        std::lock_guard guard{status_mutex};
        return status;
    }

private:
    void UpdateThread() {
        const auto update_duration = std::chrono::milliseconds(update_millisecond);
        const float step = Common::PI / 180.0f * max_rate * update_millisecond / 1000.0f;
        const float max_pitch = Common::PI / 180.0f * MaxPitchDegrees;

        float yaw = 0.0f;
        float pitch = 0.0f;
        Common::Quaternion<float> q = Common::MakeQuaternion(Common::Vec3<float>(), 0);
        Common::Quaternion<float> old_q;
        auto update_time = std::chrono::steady_clock::now();

        while (!shutdown_event.WaitUntil(update_time)) {
            update_time += update_duration;
            old_q = q;

            auto [x, y] = analog ? analog->GetStatus() : std::tuple<float, float>{0.0f, 0.0f};
            yaw += (invert_x ? x : -x) * step;
            pitch = std::clamp(pitch + (invert_y ? -y : y) * step, -max_pitch, max_pitch);

            // Turn around the world's vertical axis, then look up/down around the 3DS's own
            // horizontal axis so the horizon stays level.
            q = Common::MakeQuaternion(Common::MakeVec(0.0f, 1.0f, 0.0f), yaw) *
                Common::MakeQuaternion(Common::MakeVec(1.0f, 0.0f, 0.0f), pitch);

            // Same conversion as the mouse motion emulation (motion_emu.cpp)
            const auto inv_q = q.Inverse();
            auto gravity = Common::MakeVec(0.0f, -1.0f, 0.0f);
            auto angular_rate = ((q - old_q) * inv_q).xyz * 2;
            angular_rate *= 1000.0f / update_millisecond / Common::PI * 180.0f;
            gravity = Common::QuaternionRotate(inv_q, gravity);
            angular_rate = Common::QuaternionRotate(inv_q, angular_rate);

            std::lock_guard guard{status_mutex};
            status = std::make_tuple(gravity, angular_rate);
        }
    }

    std::unique_ptr<Input::AnalogDevice> analog;
    const int update_millisecond;
    const float max_rate;
    const bool invert_x;
    const bool invert_y;

    mutable std::mutex status_mutex;
    std::tuple<Common::Vec3<float>, Common::Vec3<float>> status{
        Common::MakeVec(0.0f, -1.0f, 0.0f), Common::MakeVec(0.0f, 0.0f, 0.0f)};

    Common::Event shutdown_event;
    std::thread thread;
};

} // Anonymous namespace

std::unique_ptr<Input::MotionDevice> StickMotion::Create(const Common::ParamPackage& params) {
    const std::string analog_params = params.Get(
        "analog", Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CStick]);
    return std::make_unique<StickMotionDevice>(
        Input::CreateDevice<Input::AnalogDevice>(analog_params), params.Get("update_period", 16),
        params.Get("max_rate", 90.0f), params.Get("invert_x", 0) != 0,
        params.Get("invert_y", 0) != 0);
}

} // namespace InputCommon
