// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <array>
#include <vector>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"

namespace Service::AC::FakeAP {

/// Number of fake access points returned by ScanAPs.
constexpr std::size_t NumAccessPoints = 15;

/// Interval after which the fake access point list is regenerated.
constexpr u64 RegenerateIntervalSeconds = 60;

/// One access point entry as returned by AC:ScanAPs (0x34 bytes).
struct APEntry {
    u32_le ssid_length;
    std::array<char, 0x20> ssid;
    std::array<u8, 6> mac_address;
    INSERT_PADDING_BYTES(2);
    u16_le signal_strength; ///< Percent, 0-100
    u8 signal_strength_summary; ///< 0-3
    u8 channel;
    u16_le security_type;
    INSERT_PADDING_BYTES(2);
};
static_assert(sizeof(APEntry) == 0x34, "APEntry has incorrect size");

/**
 * Returns the current fake access point list, regenerating it if it is older than
 * RegenerateIntervalSeconds.
 */
const std::vector<APEntry>& GetAccessPoints();

} // namespace Service::AC::FakeAP
