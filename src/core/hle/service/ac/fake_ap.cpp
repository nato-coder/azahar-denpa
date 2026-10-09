// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <fmt/format.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/hle/service/ac/fake_ap.h"

namespace Service::AC::FakeAP {

namespace {

constexpr u16 SecurityOpen = 0x0000;
constexpr u16 SecurityWPA_AES = 0x0303;
constexpr u16 SecurityWPA2_TKIP = 0x0404;
constexpr u16 SecurityWPA2_AES = 0x0505;

std::vector<APEntry> access_points;
std::optional<std::chrono::steady_clock::time_point> last_generated;

u8 StrengthSummary(u16 strength) {
    if (strength >= 50)
        return 3;
    if (strength >= 30)
        return 2;
    if (strength >= 15)
        return 1;
    return 0;
}

std::string MakeSSID(std::mt19937& rng) {
    std::uniform_int_distribution<u32> hex16(0, 0xFFFF);
    std::uniform_int_distribution<u32> hex24(0, 0xFFFFFF);
    std::uniform_int_distribution<int> pattern(0, 7);
    switch (pattern(rng)) {
    case 0:
        return fmt::format("Buffalo-G-{:04X}", hex16(rng));
    case 1:
        return fmt::format("aterm-{:06x}-g", hex24(rng));
    case 2:
        return fmt::format("elecom2g-{:06x}", hex24(rng));
    case 3:
        return fmt::format("TP-Link_{:04X}", hex16(rng));
    case 4:
        return fmt::format("HUMAX-{:05X}", hex24(rng) & 0xFFFFF);
    case 5:
        return fmt::format("SPWN_N3_{:06x}", hex24(rng));
    case 6:
        return fmt::format("ASUS_{:02X}_2G", hex16(rng) & 0xFF);
    default:
        return fmt::format("Wi-Fi-{:04X}", hex16(rng));
    }
}

void Generate() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<u32> byte(0, 0xFF);
    std::uniform_int_distribution<u32> strength(5, 90);
    std::uniform_int_distribution<u32> channel(1, 13);
    std::discrete_distribution<int> security({70, 15, 10, 5});
    constexpr std::array<u16, 4> security_types{SecurityWPA2_AES, SecurityWPA2_TKIP,
                                                SecurityWPA_AES, SecurityOpen};

    access_points.clear();
    std::set<std::array<u8, 6>> used_macs;
    while (access_points.size() < NumAccessPoints) {
        APEntry entry{};

        for (auto& b : entry.mac_address) {
            b = static_cast<u8>(byte(rng));
        }
        entry.mac_address[0] &= 0xFE; // Unicast address
        if (!used_macs.insert(entry.mac_address).second) {
            continue;
        }

        const std::string ssid = MakeSSID(rng);
        const std::size_t ssid_length = std::min(ssid.size(), entry.ssid.size());
        std::memcpy(entry.ssid.data(), ssid.data(), ssid_length);
        entry.ssid_length = static_cast<u32>(ssid_length);

        const u16 signal_strength = static_cast<u16>(strength(rng));
        entry.signal_strength = signal_strength;
        entry.signal_strength_summary = StrengthSummary(signal_strength);
        entry.channel = static_cast<u8>(channel(rng));
        entry.security_type = security_types[security(rng)];

        access_points.push_back(entry);
    }

    // Real hardware returns the strongest access points first.
    std::sort(access_points.begin(), access_points.end(),
              [](const APEntry& a, const APEntry& b) {
                  return static_cast<u16>(a.signal_strength) > static_cast<u16>(b.signal_strength);
              });

    LOG_INFO(Service_AC, "Generated {} fake access points", access_points.size());
}

/**
 * Loads access points from <sysdata>/fake_ap.bin if it exists. The file holds raw ScanAPs
 * entries (0x34 bytes each); reading stops at the first entry whose SSID length is zero.
 */
bool LoadFromFile() {
    const std::string path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + "fake_ap.bin";
    FileUtil::IOFile file(path, "rb");
    if (!file.IsOpen()) {
        return false;
    }

    std::vector<APEntry> entries(file.GetSize() / sizeof(APEntry));
    entries.resize(file.ReadArray(entries.data(), entries.size()));
    const auto end = std::find_if(entries.begin(), entries.end(),
                                  [](const APEntry& e) { return e.ssid_length == 0; });
    entries.erase(end, entries.end());

    access_points = std::move(entries);
    last_generated.reset(); // Regenerate once the file is removed
    LOG_INFO(Service_AC, "Loaded {} access points from {}", access_points.size(), path);
    return true;
}

} // Anonymous namespace

const std::vector<APEntry>& GetAccessPoints() {
    if (LoadFromFile()) {
        return access_points;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!last_generated ||
        now - *last_generated >= std::chrono::seconds(RegenerateIntervalSeconds)) {
        Generate();
        last_generated = now;
    }
    return access_points;
}

} // namespace Service::AC::FakeAP
