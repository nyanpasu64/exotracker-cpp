#include "audio/make_blip_buffer.h"

#include <doctest.h>
#include <gsl/span>
#include <Blip_Buffer/Blip_Buffer.h>

// buffer to read samples into
static int const buf_size = 10000;
static blip_amplitude_t out_buffer[buf_size];

static int const SAMPLES_PER_SEC = 48000;
static int const CPU_CLK_PER_S = 100'000;
static double const VOLUME = 1.0;
static int const BLIP_RANGE = 16;

/// Based off
/// https://github.com/eriser/blip-buffer/blob/4e55118d026ef38d5eee4cd7ec170726196bc41b/demo/buffering.cpp#L28-L33
TEST_CASE("Simple demo of blip_buffer") {
    Blip_Buffer blip = audio::make_blip_buffer(SAMPLES_PER_SEC, CPU_CLK_PER_S);

    // The actual output value (assuming no DC removal) is around
    // (amplitude / range) * volume * 65536.
    MyBlipSynth synth{blip, VOLUME, BLIP_RANGE};

    // Writes to blip.
    // update(time, value). Each synth's times must be in sorted order.
    synth.update(0, 10);
    synth.update(10, 0);
    synth.update(20, 10);

    // Required before calling blip.read_samples(). Otherwise you will read 0 samples.
    blip.end_frame(30);

    // Writes to samples[0 : <=buf_size].
    int count = blip.read_samples(out_buffer, buf_size);
    CHECK(count > 0);

    // Assert that the result is not silent.
    gsl::span<blip_amplitude_t> out_span{out_buffer, count};

    bool not_silent = false;
    for (auto & amplitude : out_span) {
        if (amplitude != 0) {
            not_silent = true;
            break;
        }
    }
    CHECK_UNARY(not_silent);
}

/**
As long as blip_buffer's cycle/sec is higher than audio's sample/sec,
there will be no risk of accidentally reading more samples into your audio write-buffer
than needed.

Still, pass a size limit to `blip.read_samples(samples, buf_size)`.
*/
TEST_CASE("Counting cycles to ensure we get a "
          "perfectly predictable number of samples out of blip_buffer") {
    Blip_Buffer blip = audio::make_blip_buffer(SAMPLES_PER_SEC, CPU_CLK_PER_S);

    int const samples_wanted = 1000;
    int cycles_needed = blip.count_clocks(samples_wanted);

    blip.end_frame(cycles_needed);
    CHECK(blip.samples_avail() == samples_wanted);

    // Writes to samples[0 : <=buf_size].
    int count = blip.read_samples(out_buffer, buf_size);
    CHECK(count == samples_wanted);
}
