#pragma once

// Do not include synth_common.h, since synth_common.h includes this file.
#include "audio/event_queue.h"
#include "sequencer_driver_common.h"
#include "util/copy_move.h"
#include "util/release_assert.h"

#include <vector>

namespace audio::synth::music_driver {

using event_queue::ClockT;

/// Unused at the moment. Possibly related to RegisterWriteQueue?
struct TimeRef {
    DISABLE_COPY_MOVE(TimeRef)
    ClockT time;
};

/// An integer which should only take on values within a specific range.
/// This is purely for documentation. No compile-time or runtime checking is performed.
template<int begin, int end, typename T>
using Range = T;

using Address = uint16_t;
using Byte = uint8_t;

struct RegisterWrite {
    Address address;
    Byte value;
};

/// Maybe just inline the methods, don't move to .cpp?
class RegisterWriteQueue {
public:
    struct RelativeRegisterWrite {
        RegisterWrite write;
        ClockT time_before;
    };

    // fields
private:
    std::vector<RelativeRegisterWrite> vec;

    struct WriteState {
        ClockT accum_dtime = 0;
        bool pending() const {
            return accum_dtime != 0;
        }
    } input;

    struct ReadState {
        ClockT prev_time = 0;
        size_t index = 0;
        bool pending() const {
            return prev_time != 0 || index != 0;
        }
    } output;

    // impl
public:
    DISABLE_COPY_MOVE(RegisterWriteQueue)

    RegisterWriteQueue() : input{}, output{} {
        vec.reserve(4 * 1024);
    }

    void clear() {
        vec.clear();
        input = {};
        output = {};
    }

    // Called by OverallDriver’s member drivers.

    // Is this a usable API? I don't know.
    // I think music_driver::TimeRef will make it easier to use.
    void add_time(ClockT dtime) {
        input.accum_dtime += dtime;
    }

    void push_write(RegisterWrite val) {
        assert(!output.pending());
        RelativeRegisterWrite relative{.write=val, .time_before=input.accum_dtime};
        input.accum_dtime = 0;

        vec.push_back(relative);
    }

    // Called by Synth.

    /// Returns a nullable pointer to a RelativeRegisterWrite.
    RelativeRegisterWrite * peek_mut() {  // -> &'Self mut RelativeRegisterWrite
        assert(!input.pending());

        if (output.index < vec.size()) {
            return &vec[output.index];
        }

        return nullptr;
    }

    RegisterWrite pop() {
        assert(!input.pending());

        release_assert(output.index < vec.size());
        RelativeRegisterWrite out = vec[output.index++];
        assert(out.time_before == 0);
        return out.write;
    }

    size_t num_unread() {
        return vec.size() - output.index;
    }
};

using sequencer_driver::EventsRef;

// end namespace
}
