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
        Chromatic root_chromatic, FreqDouble root_frequency
    ) {
        FrequenciesOwned out;

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

static void post_init(Document & document) {
    // Reserve 1024 elements to ensure that adding blocks is bounded-time.
    auto & sequence = document.sequence;
    release_assert_equal(sequence.size(), document.chips.size());

    for (auto const& [chip, chan_tracks] : enumerate<ChipIndex>(sequence)) {
        release_assert_equal(chan_tracks.size(), document.chip_index_to_nchan(chip));
        for (SequenceTrack & track : chan_tracks) {
            track.blocks.reserve(MAX_BLOCKS_PER_TRACK);
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
