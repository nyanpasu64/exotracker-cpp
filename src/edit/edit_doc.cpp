#include "edit_doc.h"
#include "edit_impl.h"
#include "edit/modified.h"
#include "doc.h"
#include "util/typeid_cast.h"
#include "util/release_assert.h"

#include <variant>

namespace edit::edit_doc {

using edit_impl::make_command;

template<typename T>
using GetMutBare = T & (*)(doc::Document &);


template<typename T>
struct Setter {
    GetMutBare<T> _field;
    int _value;

    ModifiedFlags _modified;

    void apply_swap(doc::Document & document) {
        std::swap(_field(document), _value);
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        using ImplPatternEdit = edit_impl::ImplEditCommand<Setter>;

        // Is it really a good idea to coalesce spinbox changes?
        // If you undo to after a spinbox edit, and spin it again,
        // the previous undo state is destroyed!

        if (auto p = typeid_cast<ImplPatternEdit *>(&prev)) {
            Setter & prev = *p;
            return prev._field == _field;
        }

        return false;
    }
};

doc::TickT & mut_ticks_per_beat(doc::Document & document) {
    return document.sequencer_options.ticks_per_beat;
}

EditBox set_ticks_per_beat(int ticks_per_beat) {
    return make_command(Setter<doc::TickT> {
        mut_ticks_per_beat,
        ticks_per_beat,
        ModifiedFlags::Tempo,
    });
}

// # Timeline operations.

using namespace doc;

struct EditRowInner {
    doc::GridIndex _grid;
    std::optional<TimelineRow> _edit;

    void apply_swap(doc::Document & document) {

        auto validate_reserved = [] (TimelineRow & row) {
            for (auto & channel_cells : row.chip_channel_cells) {
                for (auto & cell : channel_cells) {
                    (void) cell;
                    assert(cell._raw_blocks.capacity() >= doc::MAX_BLOCKS_PER_CELL);
                }
            }
        };

        auto validate_empty = [] (TimelineRow & row) {
            (void) row;
            assert(row.chip_channel_cells.capacity() == 0);
        };

        if (_edit) {
            validate_reserved(*_edit);
            document.timeline.insert(document.timeline.begin() + _grid, std::move(*_edit));
            validate_empty(*_edit);
            _edit = std::nullopt;

        } else {
            _edit = {std::move(document.timeline[_grid])};
            validate_reserved(*_edit);
            validate_empty(document.timeline[_grid]);
            document.timeline.erase(document.timeline.begin() + _grid);
        }
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        (void) prev;
        return false;
    }

    constexpr static ModifiedFlags _modified = ModifiedFlags::TimelineRows;
};

/// If the command holds a row to be inserted, reserve memory for each cell.
/// Once a cell gets swapped into the document, adding blocks must not allocate memory,
/// to prevent blocking the audio thread.
struct EditRow : EditRowInner {
    EditRow(EditRowInner cell) : EditRowInner(std::move(cell)) {
        if (_edit) {
            for (auto & channel_cells : _edit->chip_channel_cells) {
                for (auto & cell : channel_cells) {
                    cell._raw_blocks.reserve(doc::MAX_BLOCKS_PER_CELL);
                }
            }
        }
    }

    EditRow(EditRow const& other) : EditRow((EditRowInner const&) other) {}

    EditRow(EditRow && other) : EditRow((EditRowInner &&) other) {}
};

EditBox add_timeline_row(
    doc::Document const& document, doc::GridIndex grid_pos, doc::BeatFraction nbeats
) {
    ChipChannelTo<TimelineCell> chip_channel_cells;
    auto nchip = (ChipIndex) document.chips.size();
    for (ChipIndex chip = 0; chip < nchip; chip++) {

        ChannelTo<TimelineCell> channel_cells;
        auto nchan = document.chip_index_to_nchan(chip);
        for (ChannelIndex chan = 0; chan < nchan; chan++) {
            channel_cells.push_back(TimelineCell{});
        }

        chip_channel_cells.push_back(std::move(channel_cells));
    }

    return make_command<EditRow>(EditRowInner{
        grid_pos,
        TimelineRow{nbeats, std::move(chip_channel_cells)},
    });
}

EditBox remove_timeline_row(doc::GridIndex grid_pos) {
    return make_command<EditRow>(EditRowInner{grid_pos, {}});
}

}
