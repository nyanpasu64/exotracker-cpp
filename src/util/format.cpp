#include "format.h"

#include <fmt/core.h>

std::string format_frac(doc::timed_events::BeatFraction frac) {
    return fmt::format(
        "{} {}/{}",
        frac.numerator() / frac.denominator(),
        frac.numerator() % frac.denominator(),
        frac.denominator()
    );
}
