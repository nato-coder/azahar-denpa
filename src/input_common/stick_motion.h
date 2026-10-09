// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "core/frontend/input.h"

namespace InputCommon {

/**
 * Motion device driven by an analog stick. While the stick is held, the emulated 3DS turns at a
 * speed proportional to the deflection: left/right turns around the vertical axis and up/down
 * looks up or down. Releasing the stick keeps the current orientation.
 */
class StickMotion : public Input::Factory<Input::MotionDevice> {
public:
    /**
     * Creates a motion device driven by an analog stick
     * @param params contains parameters for creating the device:
     *     - "analog": serialized analog device params; defaults to the current profile's C-Stick
     *     - "update_period": update period in milliseconds
     *     - "max_rate": turning speed in degrees per second at full deflection
     *     - "invert_x", "invert_y": reverse the turning direction of each axis
     */
    std::unique_ptr<Input::MotionDevice> Create(const Common::ParamPackage& params) override;
};

} // namespace InputCommon
