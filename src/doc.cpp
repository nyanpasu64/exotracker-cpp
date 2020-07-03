#include "doc.h"

#include <cmath>  // pow

#ifdef UNITTEST
#include <doctest.h>
#endif

namespace doc {

inline namespace tuning {
    constexpr double NOTES_PER_OCTAVE = 12.;

    FrequenciesOwned equal_temperament(
        ChromaticInt root_chromatic, FreqDouble root_frequency
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

Document Document::clone() const {
    return *this;
}

Document::Document(const DocumentCopy & other) : DocumentCopy(other) {}

Document::Document(DocumentCopy && other) : DocumentCopy(std::move(other)) {}

#ifdef UNITTEST
TEST_CASE("equal_temperament()") {
    FrequenciesOwned freqs = equal_temperament();
    CHECK(freqs[69] == 440.);
    CHECK(256 < freqs[60]);
    CHECK(freqs[60] < 512);
}
#endif

}
