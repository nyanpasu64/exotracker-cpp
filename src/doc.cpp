#include "doc.h"
#include "chip_kinds.h"
#include "util/enum_map.h"
#include "util/enumerate.h"
#include "util/release_assert.h"

#include <cmath>  // pow

#ifdef UNITTEST
#include <doctest.h>
#endif

namespace doc {

inline namespace tuning {
    FrequenciesOwned equal_temperament(
        ChromaticInt root_chromatic, FreqDouble root_frequency
    ) {
        FrequenciesOwned out;
        out.resize(CHROMATIC_COUNT);

        for (size_t i = 0; i < CHROMATIC_COUNT; i++) {
            auto semitone_offset = (ptrdiff_t)i - (ptrdiff_t)root_chromatic;
            double freq_ratio = pow(2, (double)semitone_offset / NOTES_PER_OCTAVE);
            out[i] = freq_ratio * root_frequency;
        }
        return out;
    }
}

chip_common::ChannelIndex DocumentCopy::chip_index_to_nchan(
    chip_common::ChipIndex chip
) const {
    release_assert(chip < chips.size());
    auto chip_kind = (size_t) chips[chip];

    release_assert(chip_kind < (size_t) ChipKind::COUNT);
    return chip_common::CHIP_TO_NCHAN[chip_kind];
}

Document Document::clone() const {
    return (DocumentCopy const&) *this;
}

void post_init(Document & document) {
    // Set tables to the correct length.
    document.instruments.v.resize(MAX_INSTRUMENTS);

    // We could assert instead of resizing,
    // but that makes it more difficult for unit tests to construct test documents.
    document.frequency_table.resize(CHROMATIC_COUNT);

    release_assert_equal(document.chip_channel_settings.size(), document.chips.size());
    for (ChipIndex chip = 0; chip < document.chips.size(); chip++) {
        release_assert_equal(
            document.chip_channel_settings[chip].size(),
            document.chip_index_to_nchan(chip));
    }

    // Reserve 256 elements to ensure that insert/delete is bounded-time.
    document.timeline.reserve(MAX_GRID_CELLS);

    // Reserve 32 elements to ensure that adding blocks is bounded-time.
    for (auto & row : document.timeline) {
        release_assert_equal(row.chip_channel_cells.size(), document.chips.size());

        for (
            auto const& [chip, chan_cells]
            : enumerate<ChipIndex>(row.chip_channel_cells)
        ) {
            release_assert_equal(chan_cells.size(), document.chip_index_to_nchan(chip));
            for (auto & cell : chan_cells) {
                cell._raw_blocks.reserve(MAX_BLOCKS_PER_CELL);
            }
        }
    }
}

Document::Document(const DocumentCopy & other) : DocumentCopy(other) {
    post_init(*this);
}

Document::Document(DocumentCopy && other) : DocumentCopy(std::move(other)) {
    post_init(*this);
}

#ifdef UNITTEST
TEST_CASE("equal_temperament()") {
    FrequenciesOwned freqs = equal_temperament();
    CHECK(freqs[69] == 440.);
    CHECK(256 < freqs[60]);
    CHECK(freqs[60] < 512);
}
#endif

}
