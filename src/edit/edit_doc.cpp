#include "edit_doc.h"
#include "edit_impl.h"
#include "edit/modified.h"
#include "doc.h"
#include "util/typeid_cast.h"
#include "util/release_assert.h"

#include <variant>
#include <utility>  // std::swap

namespace edit::edit_doc {

using edit_impl::make_command;

/// type GetFieldMut<T> = fn(&mut Document) -> &mut T;
template<typename T>
using GetFieldMut = T & (*)(doc::Document &);


template<typename T, GetFieldMut<T> get_field_mut, ModifiedFlags modified>
class Setter {
    T _value;

public:
    static constexpr ModifiedFlags _modified = modified;

    Setter(T new_value)
        : _value(new_value)
    {}

    void apply_swap(doc::Document & document) {
        std::swap(get_field_mut(document), _value);
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        using SelfEditCommand = edit_impl::ImplEditCommand<Setter>;

        // TODO freeze commands to prevent coalescing, upon subsequent edits.
        // Currently, if you undo to after a spinbox edit, and spin it again,
        // the previous undo state is destroyed!

        return typeid(prev) == typeid(SelfEditCommand);
    }
};

/// I wanted to turn this function into a local-variable lambda,
/// but in GCC, local-variable lambdas cannot be used as
/// non-type template parameters (Setter<GetFieldMut<T>>).
/// Context: https://docs.google.com/document/d/1D5lKC6eFOv4fptVkUsk8DgeboFSBigSQWef8-rqCzgw/edit
///
/// Honestly I'm disappointed you can't pass a "path" (document => document.foo.bar)
/// as a first-class template parameter.
/// Though Rust lambdas are quite concise (|d| &mut d.foo.bar).
static double & get_tempo_mut(doc::Document & document) {
    return document.sequencer_options.target_tempo;
};

EditBox set_tempo(double tempo) {
    return make_command(Setter<double, get_tempo_mut, ModifiedFlags::TargetTempo> (
        tempo
    ));
}

using doc::Document;
using doc::SequencerOptions;

class SetSequencerOptions {
    SequencerOptions _value;

public:
    ModifiedFlags _modified;

    SetSequencerOptions(SequencerOptions new_value, ModifiedFlags modified)
        : _value(new_value)
        , _modified(modified)
    {}

    void apply_swap(doc::Document & document) {
        std::swap(document.sequencer_options, _value);
    }

    bool can_coalesce(BaseEditCommand &) const {
        return false;
    }
};

EditBox set_sequencer_options(
    Document const& orig_doc, SequencerOptions options
) {
    ModifiedInt flags = 0;
    {
        auto const& orig_options = orig_doc.sequencer_options;

        if (orig_options.target_tempo != options.target_tempo) {
            flags |= ModifiedFlags::TargetTempo;
        }
        if (orig_options.spc_timer_period != options.spc_timer_period) {
            flags |= ModifiedFlags::SpcTimerPeriod;
        }
        if (orig_options.ticks_per_beat != options.ticks_per_beat) {
            flags |= ModifiedFlags::TicksPerBeat;
        }
    }

    return make_command(SetSequencerOptions(options, ModifiedFlags{flags}));
}


// # Timeline operations.

using namespace doc;

struct EditRow {
    doc::GridIndex _grid;
    std::optional<TimelineRow> _edit;

    /// If the command holds a row to be inserted, reserve memory for each cell.
    /// Once a cell gets swapped into the document,
    /// adding blocks must not allocate memory, to prevent blocking the audio thread.
    EditRow(doc::GridIndex grid, std::optional<TimelineRow> edit)
        : _grid(grid)
        , _edit(std::move(edit))
    {
        if (_edit) {
            for (auto & channel_cells : _edit->chip_channel_cells) {
                for (auto & cell : channel_cells) {
                    cell._raw_blocks.reserve(doc::MAX_BLOCKS_PER_CELL);
                }
            }
        }
    }

    // Do *not* replace with default-derived copy/move constructors.
    // They fail to call _raw_blocks.reserve()!
    EditRow(EditRow const& other) : EditRow(other._grid, other._edit) {}

    EditRow(EditRow && other) noexcept
        : EditRow(other._grid, std::move(other._edit))
    {}

    void apply_swap(doc::Document & document) {
        assert(document.timeline.capacity() >= MAX_TIMELINE_FRAMES);

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

    bool can_coalesce(BaseEditCommand &) const {
        return false;
    }

    constexpr static ModifiedFlags _modified = ModifiedFlags::TimelineRows;
};

// Exported via headers.

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

    return make_command(EditRow(
        grid_pos,
        TimelineRow{nbeats, std::move(chip_channel_cells)}
    ));
}

EditBox remove_timeline_row(doc::GridIndex grid_pos) {
    return make_command(EditRow(grid_pos, {}));
}

EditBox clone_timeline_row(doc::Document const& document, doc::GridIndex grid_pos) {
    return make_command(EditRow(
        grid_pos + 1, TimelineRow(document.timeline[grid_pos])
    ));
}

// # Set grid length.

struct SetGridLength {
    doc::GridIndex _grid;
    doc::BeatFraction _row_nbeats;

    void apply_swap(doc::Document & document) {
        std::swap(document.timeline[_grid].nbeats, _row_nbeats);
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        using ImplPatternEdit = edit_impl::ImplEditCommand<SetGridLength>;

        // Is it really a good idea to coalesce spinbox changes?
        // If you undo to after a spinbox edit, and spin it again,
        // the previous undo state is destroyed!

        if (auto p = typeid_cast<ImplPatternEdit *>(&prev)) {
            SetGridLength & prev = *p;
            return prev._grid == _grid;
        }

        return false;
    }

    constexpr static ModifiedFlags _modified = ModifiedFlags::TimelineRows;
};

EditBox set_grid_length(doc::GridIndex grid_pos, doc::BeatFraction nbeats) {
    return make_command(SetGridLength{grid_pos, nbeats});
}

// # Move timeline rows.

struct MoveGridDown {
    doc::GridIndex _grid;

    void apply_swap(doc::Document & document) {
        std::swap(document.timeline[_grid], document.timeline[_grid + 1]);
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        return false;
    }

    constexpr static ModifiedFlags _modified = ModifiedFlags::TimelineRows;
};

EditBox move_grid_up(doc::GridIndex grid_pos) {
    return make_command(MoveGridDown{grid_pos - 1});
}

EditBox move_grid_down(doc::GridIndex grid_pos) {
    return make_command(MoveGridDown{grid_pos});
}

}
