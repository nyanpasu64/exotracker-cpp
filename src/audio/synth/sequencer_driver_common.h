#pragma once

#include "doc.h"

#include <gsl/span>

namespace audio::synth::sequencer_driver {

using EventsRef = gsl::span<doc::RowEvent const>;

}