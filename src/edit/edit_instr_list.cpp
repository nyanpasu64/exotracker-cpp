#include "edit_instr_list.h"
#include "edit_impl.h"
#include "modified.h"
#include "doc.h"
#include "util/release_assert.h"

#include <limits>
#include <utility>  // std::move

namespace edit::edit_instr_list {

using namespace doc;
using edit_impl::make_command;

static void timeline_swap_instruments(
    Timeline & timeline, InstrumentIndex a, InstrumentIndex b
) {
    for (TimelineRow & frame : timeline) {
        for (auto & channel_cells : frame.chip_channel_cells) {
            for (TimelineCell & cell : channel_cells) {
                for (TimelineBlock & block : cell._raw_blocks) {
                    for (auto & ev : block.pattern.events) {
                        if (ev.v.instr == a) {
                            ev.v.instr = b;
                        } else if (ev.v.instr == b) {
                            ev.v.instr = a;
                        }
                    }
                }
            }
        }
    }
}

struct SwapInstruments {
    InstrumentIndex a;
    InstrumentIndex b;

    void apply_swap(Document & doc) {
        if (a == b) {
            return;
        }

        // Not currently necessary to assert that a and b < MAX_INSTRUMENTS
        // because MAX_INSTRUMENTS is 256.
        static_assert(
            std::numeric_limits<InstrumentIndex>::max() < MAX_INSTRUMENTS,
            "TODO add bounds checks");

        std::swap(doc.instruments[a], doc.instruments[b]);
        timeline_swap_instruments(doc.timeline, a, b);

        // TODO tell synth that instruments swapped?
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        return false;
    }

    static constexpr ModifiedFlags _modified = ModifiedFlags::InstrumentsEdited;
};

EditBox swap_instruments(InstrumentIndex a, InstrumentIndex b) {
    return make_command(SwapInstruments{a, b});
}

struct SwapInstrumentsCached {
    InstrumentIndex a;
    InstrumentIndex b;
    Timeline timeline;

    void apply_swap(Document & doc) {
        if (a == b) {
            return;
        }

        static_assert(
            std::numeric_limits<InstrumentIndex>::max() < MAX_INSTRUMENTS,
            "TODO add bounds checks");

        std::swap(doc.instruments[a], doc.instruments[b]);
        std::swap(doc.timeline, timeline);

        // TODO tell synth that instruments swapped?
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        return false;
    }

    static constexpr ModifiedFlags _modified = ModifiedFlags::InstrumentsEdited;
};

EditBox swap_instruments_cached(
    Document const& doc, InstrumentIndex a, InstrumentIndex b
) {
    Timeline timeline = doc.timeline;  // Make a copy
    timeline_swap_instruments(timeline, a, b);
    return make_command(SwapInstrumentsCached{a, b, std::move(timeline)});
}

}
