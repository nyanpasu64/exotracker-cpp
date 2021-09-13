#include "edit_instr_list.h"
#include "edit_impl.h"
#include "modified.h"
#include "doc.h"
#include "util/compare.h"
#include "util/expr.h"
#include "util/release_assert.h"
#include "util/typeid_cast.h"

#include <limits>
#include <optional>
#include <utility>  // std::move

namespace edit::edit_instr_list {

using namespace doc;
using namespace edit_impl;

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

    static constexpr ModifiedFlags _modified = ModifiedFlags::InstrumentsEdited;
    using Impl = ImplEditCommand<AddRemoveInstrument, Override::None>;
};

static std::optional<InstrumentIndex> get_empty_idx(
    Instruments const& instruments, size_t begin_idx
) {
    for (size_t i = begin_idx; i < MAX_INSTRUMENTS; i++) {
        if (!instruments[i]) {
            return (InstrumentIndex) i;
        }
    }
    return {};
}

static Instrument new_instrument() {
    // Translating "New Instrument" is non-trivial since this file doesn't link
    // to Qt. See https://gitlab.com/exotracker/exotracker-cpp/-/issues/91.
    return Instrument {
        .name = "New Instrument",
        .keysplit = {
            InstrumentPatch {},
        },
    };
}

std::tuple<MaybeEditBox, InstrumentIndex> try_add_instrument(
    Document const& doc, InstrumentIndex begin_idx
) {
    auto maybe_empty_idx = get_empty_idx(doc.instruments, begin_idx);
    if (!maybe_empty_idx) {
        return {nullptr, 0};
    }
    InstrumentIndex empty_idx = *maybe_empty_idx;

    return {
        make_command(AddRemoveInstrument {empty_idx, new_instrument()}),
        empty_idx,
    };
}

std::tuple<MaybeEditBox, InstrumentIndex> try_clone_instrument(
    Document const& doc, InstrumentIndex old_idx, InstrumentIndex begin_idx
) {
    static_assert(
        MAX_INSTRUMENTS == 256,
        "Must add bounds check when decreasing instrument limit");

    if (!doc.instruments[old_idx]) {
        return {nullptr, 0};
    }

    auto maybe_empty_idx = get_empty_idx(doc.instruments, begin_idx);
    if (!maybe_empty_idx) {
        return {nullptr, 0};
    }
    InstrumentIndex empty_idx = *maybe_empty_idx;

    return {
        // make a copy of doc.instruments[old_idx]
        make_command(AddRemoveInstrument {empty_idx, doc.instruments[old_idx]}),
        empty_idx,
    };
}

std::tuple<MaybeEditBox, InstrumentIndex> try_remove_instrument(
    Document const& doc, InstrumentIndex instr_idx
) {
    static_assert(
        MAX_INSTRUMENTS == 256,
        "Must add bounds check when decreasing instrument limit");

    if (!doc.instruments[instr_idx]) {
        return {nullptr, 0};
    }

    InstrumentIndex begin_idx = EXPR(
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
        begin_idx,
    };
}

struct RenamePath {
    InstrumentIndex instr_idx;

    DEFAULT_EQUALABLE(RenamePath)
};

struct RenameInstrument {
    RenamePath path;
    std::string name;

    void apply_swap(Document & doc) {
        release_assert(doc.instruments[path.instr_idx].has_value());
        std::swap(doc.instruments[path.instr_idx]->name, name);
    }

    using Impl = ImplEditCommand<RenameInstrument, Override::CanMerge>;
    bool can_merge(BaseEditCommand & prev) const {
        if (auto * p = typeid_cast<Impl *>(&prev)) {
            return p->path == path;
        }
        return false;
    }

    // ModifiedFlags is currently only used by the audio thread,
    // and renaming instruments doesn't affect the audio thread.
    static constexpr ModifiedFlags _modified = (ModifiedFlags) 0;
};

MaybeEditBox try_rename_instrument(
    Document const& doc, InstrumentIndex instr_idx, std::string new_name
) {
    static_assert(
        MAX_INSTRUMENTS == 256,
        "Must add bounds check when decreasing instrument limit");

    if (!doc.instruments[instr_idx]) {
        return nullptr;
    }
    return make_command(RenameInstrument{{instr_idx}, std::move(new_name)});
}


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

    using Impl = ImplEditCommand<SwapInstruments, Override::None>;

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

    using Impl = ImplEditCommand<SwapInstrumentsCached, Override::None>;
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
