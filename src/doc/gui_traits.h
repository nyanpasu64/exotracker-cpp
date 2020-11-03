#pragma once

/// This namespace contains functions used to render Document instances.
/// It is not included and re-exported by doc.h,
/// to prevent most code from recompiling when new functions are added.
///
/// The functions in this file are NOT guaranteed to be safe to call
/// at static initialization time.
/// Some functions may happen to be safe, but this is not guaranteed going forwards.

#include "doc.h"
#include "chip_common.h"
#include <string_view>

#include <cstdint>

namespace doc::gui_traits {

using chip_common::ChipIndex;
using chip_common::ChannelIndex;

using ChipChannelToVolumeDigits = uint8_t const* const*;

/// [k: ChipKind] [CHIP_TO_NCHAN[k]] number of digits.
extern const ChipChannelToVolumeDigits CHIP_CHANNEL_TO_VOLUME_DIGITS;

[[nodiscard]] uint8_t get_volume_digits(
    Document const& doc, ChipIndex chip, ChannelIndex channel
);

[[nodiscard]] bool is_noise(
    Document const& doc, ChipIndex chip, ChannelIndex channel
);

[[nodiscard]] char const* channel_name(
    Document const& doc, ChipIndex chip, ChannelIndex channel
);

}
