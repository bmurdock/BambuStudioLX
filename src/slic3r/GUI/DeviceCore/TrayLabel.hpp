#pragma once

#include "DevDefs.h"
#include <algorithm>
#include <optional>
#include <string>

namespace Slic3r { namespace GUI {

// Printer tray identifiers also appear in serialized inventory. Formatting
// them must not require a running wx application.
inline std::string format_tray_label(int tray_id, std::optional<int> extruder_count = std::nullopt)
{
    if (tray_id < 0) return {};
    if (tray_id == VIRTUAL_TRAY_MAIN_ID || tray_id == VIRTUAL_TRAY_DEPUTY_ID) {
        if (extruder_count.value_or(-1) == 2)
            return tray_id == VIRTUAL_TRAY_MAIN_ID ? "Ext-R" : "Ext-L";
        return "Ext";
    }
    if (tray_id >= 128 * 4) {
        const int index = (tray_id - 128 * 4) / 4;
        if (index >= 26) return {};
        return std::string(1, static_cast<char>('A' + index));
    }
    if (tray_id >= 0x80 && tray_id <= 0x87)
        return "HT-" + std::string(1, static_cast<char>('A' + tray_id - 128));
    if (tray_id >= 24 && tray_id <= 27)
        return "Q" + std::to_string(tray_id - 24 + 1);
    return std::string(1, static_cast<char>('A' + std::min(tray_id / 4, 25))) +
           std::to_string(tray_id % 4 + 1);
}

}} // namespace Slic3r::GUI
