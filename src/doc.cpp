#include "doc.h"
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

Document Document::clone() const {
    return *this;
}

void post_init(Document & document) {
    // Set tables to the correct length.
    document.instruments.v.resize(MAX_INSTRUMENTS);
    document.frequency_table.resize(CHROMATIC_COUNT);

    // Reserve 256 elements to ensure that insert/delete is bounded-time.
    document.sequence.reserve(MAX_SEQUENCE_LEN);
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
