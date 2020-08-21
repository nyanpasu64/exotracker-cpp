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

enum class CellOperation {
    Add,
    Remove,
};

struct EditRowInner {
    doc::GridIndex _grid;
    CellOperation _oper;
    doc::GridCell _nbeats;
    doc::ChipChannelTo<doc::TimelineCell> _chip_channel_cells;

    void apply_swap(doc::Document & document) {
        auto ncell = document.grid_cells.size();

        auto nchip = (ChipIndex) document.chips.size();
        release_assert_equal(document.chip_channel_timelines.size(), nchip);
        release_assert_equal(_chip_channel_cells.size(), nchip);

        for (ChipIndex chip = 0; chip < nchip; chip++) {
            auto nchan = document.chip_index_to_nchan(chip);
            auto & channel_timelines = document.chip_channel_timelines[chip];
            auto & channel_cells = _chip_channel_cells[chip];

            release_assert_equal(channel_timelines.size(), nchan);
            release_assert_equal(channel_cells.size(), nchan);

            for (ChannelIndex chan = 0; chan < nchan; chan++) {
                auto & timeline = channel_timelines[chan];
                auto & cell = channel_cells[chan];

                release_assert_equal(timeline.size(), ncell);
                if (_oper == CellOperation::Add) {
                    assert(cell._raw_blocks.capacity() >= doc::MAX_BLOCKS_PER_CELL);
                    timeline.insert(timeline.begin() + _grid, std::move(cell));
                } else {
                    assert(cell._raw_blocks.capacity() == 0);
                    assert(
                        timeline[_grid]._raw_blocks.capacity()
                        >= doc::MAX_BLOCKS_PER_CELL
                    );
                    cell = std::move(timeline[_grid]);
                    timeline.erase(timeline.begin() + _grid);
                }
            }
        }

        if (_oper == CellOperation::Add) {
            document.grid_cells.insert(document.grid_cells.begin() + _grid, _nbeats);
            _oper = CellOperation::Remove;

        } else {
            _nbeats = document.grid_cells[_grid];
            document.grid_cells.erase(document.grid_cells.begin() + _grid);
            _oper = CellOperation::Add;
        }
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        (void) prev;
        return false;
    }

    constexpr static ModifiedFlags _modified = ModifiedFlags::GridCells;
};

/// If the command holds a row to be inserted, reserve memory for each cell.
/// Once a cell gets swapped into the document, adding blocks must not allocate memory,
/// to prevent blocking the audio thread.
struct EditRow : EditRowInner {
    EditRow(EditRowInner cell) : EditRowInner(std::move(cell)) {
        if (_oper == CellOperation::Add) {
            for (auto & channel_cells : _chip_channel_cells) {
                for (auto & cell : channel_cells) {
                    cell._raw_blocks.reserve(doc::MAX_BLOCKS_PER_CELL);
                    assert(cell._raw_blocks.capacity() >= doc::MAX_BLOCKS_PER_CELL);
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
        CellOperation::Add,
        {nbeats},
        chip_channel_cells,
    });
}

}
