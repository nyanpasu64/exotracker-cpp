#include "edit_instr_list.h"
#include "edit_impl.h"
#include "modified.h"
#include "doc.h"
#include "util/release_assert.h"
#include "util/expr.h"

#include <limits>
#include <optional>
#include <utility>  // std::move

namespace edit::edit_instr_list {

using namespace doc;
using edit_impl::make_command;

struct AddRemoveInstrument {
    InstrumentIndex index;
    std::optional<Instrument> instr;

    void apply_swap(Document & doc) {
        if (instr.has_value()) {
            release_assert(!doc.instruments[index].has_value());
        } else {
            release_assert(doc.instruments[index].has_value());
        }
        std::swap(instr, doc.instruments[index]);
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        return false;
    }

    static constexpr ModifiedFlags _modified = ModifiedFlags::InstrumentsEdited;
};

static std::optional<InstrumentIndex> get_empty_idx(Instruments const& instruments) {
    for (size_t i = 0; i < MAX_INSTRUMENTS; i++) {
        if (!instruments[i]) {
            return (InstrumentIndex) i;
        }
    }
    return {};
}

static Instrument new_instrument() {
    return doc::Instrument {
        .name = "New Instrument",
        .keysplit = {
            InstrumentPatch {},
        },
    };
}

std::tuple<MaybeEditBox, InstrumentIndex> try_add_instrument(Document const& doc) {
    auto maybe_empty_idx = get_empty_idx(doc.instruments);
    if (!maybe_empty_idx) {
        return {nullptr, 0};
    }
    InstrumentIndex empty_idx = *maybe_empty_idx;

    return {
        make_command(AddRemoveInstrument {empty_idx, new_instrument()}),
        empty_idx,
    };
}

MaybeEditBox try_insert_instrument(Document const& doc, InstrumentIndex instr_idx) {
    if (doc.instruments[instr_idx]) {
        return nullptr;
    }
    return make_command(AddRemoveInstrument {instr_idx, new_instrument()});
}

std::tuple<MaybeEditBox, InstrumentIndex> try_remove_instrument(
    Document const& doc, InstrumentIndex instr_idx
) {
    if (!doc.instruments[instr_idx]) {
        return {nullptr, 0};
    }

    InstrumentIndex new_idx = EXPR(
        // Find the next filled instrument slot.
        for (size_t i = instr_idx + 1; i < MAX_INSTRUMENTS; i++) {
            if (doc.instruments[i].has_value()) {
                return (InstrumentIndex) i;
            }
        }
        // We're removing the last instrument. Find the new last instrument.
        for (size_t i = instr_idx; i--; ) {
            if (doc.instruments[i].has_value()) {
                return (InstrumentIndex) i;
            }
        }
        // There are no instruments left. Keep the instrument as-is.
        // (This differs from FamiTracker which sets the new instrument to 0.)
        return instr_idx;
    );

    return {
        make_command(AddRemoveInstrument {instr_idx, {}}),
        new_idx,
    };
}

std::tuple<MaybeEditBox, InstrumentIndex> try_clone_instrument(
    Document const& doc, InstrumentIndex instr_idx
) {
    if (!doc.instruments[instr_idx]) {
        return {nullptr, 0};
    }

    auto maybe_empty_idx = get_empty_idx(doc.instruments);
    if (!maybe_empty_idx) {
        return {nullptr, 0};
    }
    InstrumentIndex empty_idx = *maybe_empty_idx;

    return {
        // make a copy of doc.instruments[instr_idx]
        make_command(AddRemoveInstrument {empty_idx, doc.instruments[instr_idx]}),
        empty_idx,
    };
}


static void timeline_swap_instruments(
    doc::Timeline & timeline, InstrumentIndex a, InstrumentIndex b
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

        // Not currently necessary to assert that a and b < doc::MAX_INSTRUMENTS
        // because doc::MAX_INSTRUMENTS is 256.
        static_assert(
            std::numeric_limits<InstrumentIndex>::max() < doc::MAX_INSTRUMENTS,
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
            std::numeric_limits<InstrumentIndex>::max() < doc::MAX_INSTRUMENTS,
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
    doc::Document const& doc, doc::InstrumentIndex a, doc::InstrumentIndex b
) {
    Timeline timeline = doc.timeline;  // Make a copy
    timeline_swap_instruments(timeline, a, b);
    return make_command(SwapInstrumentsCached{a, b, std::move(timeline)});
}

}
