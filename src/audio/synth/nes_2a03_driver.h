// Only include in synth/nes_2a03.cpp, not .h.

#pragma once

#include "music_driver_common.h"
#include "envelope.h"
#include "sequencer.h"
#include "chip_kinds.h"

#include "util/copy_move.h"
#include "util/macros.h"

#include <cpp11-on-multicore/bitfield.h>

namespace audio::synth::nes_2a03_driver {

using namespace doc::tuning;
using namespace music_driver;
using chip_kinds::Apu1ChannelID;

// Pulse 1/2 driver

/// Convert [byte][bit]Bit indexing to [bit]Bit.
constexpr static int BITS_PER_BYTE = 8;
#define BYTE_BIT(byte, bit) ((byte) * BITS_PER_BYTE + (bit))
#define BYTE(byte) (BITS_PER_BYTE * (byte))

class Apu1PulseDriver {
    using PulseNum = Range<0, 2, uint16_t>;

    PulseNum /*const*/ _pulse_num;
    Address /*const*/ _base_address;
    TuningRef _tuning_table;

    bool _first_tick_occurred = false;

#define Apu1PulseDriver_FOREACH_RAW(X, APPL) \
    X(APPL(volume)); \
    X(APPL(arpeggio)); \
    X(APPL(wave_index));

#define ITER_NAME(name)  _ ## name ## _iter
#define ID(name)  name

#define DEFINE_ITERATOR(name) \
    envelope::EnvelopeIterator<decltype(doc::Instrument::name)> ITER_NAME(name)

    Apu1PulseDriver_FOREACH_RAW(DEFINE_ITERATOR, ID)

#define Apu1PulseDriver_FOREACH(X) \
    Apu1PulseDriver_FOREACH_RAW(X, ITER_NAME)

    doc::Note _prev_note;

public:
    // Reading a ADD_BITFIELD_MEMBER does not sign-extend it.
    // The read value can only be negative
    // if the ADD_BITFIELD_MEMBER has the same length as the storage type
    // (AKA the type returned by member accesses).
    BEGIN_BITFIELD_TYPE(Apu1Reg, int32_t)
        // Access bit fields.
    //  ADD_BITFIELD_MEMBER(memberName,     offset,         bits)
        ADD_BITFIELD_MEMBER(volume,         BYTE(0) + 0,    4)
        ADD_BITFIELD_MEMBER(const_vol,      BYTE(0) + 4,    1)
        ADD_BITFIELD_MEMBER(length_halt,    BYTE(0) + 5,    1)
        ADD_BITFIELD_MEMBER(duty,           BYTE(0) + 6,    2)

        // Period: clock/cycle = (period_reg + 1) * 16
        ADD_BITFIELD_MEMBER(period_reg,     BYTE(2) + 0,    BYTE(1) + 3)
        ADD_BITFIELD_MEMBER(length,         BYTE(3) + 3,    5)

        // Access raw bytes in endian-independent fashion.
        constexpr static Address BYTES = 4;
        ADD_BITFIELD_ARRAY(bytes, /*offset*/ 0, /*bits*/ 8, /*numItems*/ BYTES)
    END_BITFIELD_TYPE()

    static constexpr int MAX_VOLUME = decltype(Apu1Reg::volume)::Maximum;
    static_assert (MAX_VOLUME == 15, "huh?");
    static constexpr int MAX_PERIOD = decltype(Apu1Reg::period_reg)::Maximum;

private:
    Apu1Reg _prev_state = 0;
    Apu1Reg _next_state = 0;

    // impl
private:
    explicit DEFAULT_COPY(Apu1PulseDriver)
    DEFAULT_MOVE(Apu1PulseDriver)

public:
    Apu1PulseDriver(PulseNum pulse_num, TuningRef tuning_table) noexcept :
        _pulse_num(pulse_num),
        _base_address(Address(0x4000 + 0x4 * pulse_num)),
        _tuning_table(tuning_table),
        _volume_iter(&doc::Instrument::volume, MAX_VOLUME),
        _arpeggio_iter(&doc::Instrument::arpeggio, 0),
        _wave_index_iter(&doc::Instrument::wave_index, 0),
        _prev_note(0)
    {}

    void stop_playback(RegisterWriteQueue &/*mut*/ register_writes);

    // TODO add a $4015 reference parameter,
    // so after Apu1PulseDriver writes to channels,
    // Apu1Driver can toggle hardware envelopes.
    void tick(
        doc::Document const & document,
        sequencer::EventsRef events,
        RegisterWriteQueue &/*out*/ register_writes
    );
};

TuningOwned make_tuning_table(
    FrequenciesRef const frequencies,  // cycle/s
    ClockT const clocks_per_second  // clock/s
);

class Apu1Driver {
    // Apu1PulseDriver references _tuning_table, so disable moving this.
    DISABLE_COPY_MOVE(Apu1Driver)

    using ChannelID = Apu1ChannelID;

TEST_PUBLIC:
    ClockT const _clocks_per_sec;
    TuningOwned _tuning_table;

    Apu1PulseDriver _pulse1_driver;
    Apu1PulseDriver _pulse2_driver;

public:

    // TODO Apu1PulseDriver doesn't hold reference to _tuning_table,
    // but is passed one on each tick.
    Apu1Driver(ClockT clocks_per_sec, FrequenciesRef frequencies)
        : _clocks_per_sec(clocks_per_sec)
        , _tuning_table(make_tuning_table(frequencies, clocks_per_sec))
        , _pulse1_driver{0, TuningRef{_tuning_table}}
        , _pulse2_driver{1, TuningRef{_tuning_table}}
    {}

    void recompute_tuning(FrequenciesRef frequencies) {
        _tuning_table = make_tuning_table(frequencies, _clocks_per_sec);
    }

    void stop_playback(RegisterWriteQueue &/*mut*/ register_writes) {
        _pulse1_driver.stop_playback(/*mut*/ register_writes);
        _pulse2_driver.stop_playback(/*mut*/ register_writes);
    }

    void driver_tick(
        doc::Document const & document,
        EnumMap<ChannelID, sequencer::EventsRef> const & channel_events,
        RegisterWriteQueue &/*out*/ register_writes
    ) {
        _pulse1_driver.tick(
            document, channel_events[ChannelID::Pulse1], /*mut*/ register_writes
        );
        _pulse2_driver.tick(
            document, channel_events[ChannelID::Pulse2], /*mut*/ register_writes
        );

        // TODO write $4015 to register_writes, if I ever add envelope functionality.
    }
};


// namespace
}
