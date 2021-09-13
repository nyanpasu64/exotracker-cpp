#define main_window_INTERNAL public
#include "main_window.h"
#include "gui/pattern_editor.h"
#include "gui/timeline_editor.h"
#include "gui/instrument_dialog.h"
#include "gui/instrument_list.h"
#include "gui/move_cursor.h"
#include "gui/tempo_dialog.h"
#include "lib/layout_macros.h"
#include "gui_common.h"
#include "cmd_queue.h"
#include "edit/edit_doc.h"
#include "serialize.h"
#include "util/defer.h"
#include "util/math.h"
#include "util/release_assert.h"
#include "util/reverse.h"
#include "util/unwrap.h"

#include <fmt/core.h>
#include <rtaudio/RtAudio.h>
#include <verdigris/wobjectimpl.h>

// Widgets
#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSpinBox>
#include "gui/lib/icon_toolbar.h"
#include <QToolButton>
// Layouts
#include <QBoxLayout>
#include <QFormLayout>
// Other
#include <QAction>
#include <QDebug>
#include <QErrorMessage>
#include <QFileDialog>
#include <QFlags>
#include <QGuiApplication>
#include <QIcon>
#include <QPointer>
#include <QScreen>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <algorithm>  // std::min/max, std::sort
#include <chrono>
#include <iostream>
#include <optional>
#include <exception>  // std::uncaught_exceptions
#include <stdexcept>  // logic_error

namespace gui::main_window {

using std::unique_ptr;
using std::make_unique;

using gui::lib::icon_toolbar::IconToolBar;
using gui::lib::icon_toolbar::enable_button_borders;
using gui::pattern_editor::PatternEditor;
using gui::pattern_editor::StepDirection;
using gui::timeline_editor::TimelineEditor;
using gui::instrument_list::InstrumentList;
using doc::BeatFraction;
using util::math::ceildiv;
using util::math::frac_floor;
using util::reverse::reverse;
namespace edit_doc = edit::edit_doc;


// # impl RawSelection

RawSelection::RawSelection(Cursor cursor, int rows_per_beat)
    : _begin(cursor)
    , _end(cursor)
    , _bottom_padding(BeatFraction{1, rows_per_beat})
{}

Selection RawSelection::get_select() const {
    auto [left, right] = std::minmax(_begin.x, _end.x);
    auto [top, bottom] = std::minmax(_begin.y, _end.y);
    // you can't mutate bottom, because C++ unpacking is half-baked.

    Selection out {
        .left = left,
        .right = right,
        .top = top,
        .bottom = bottom,
    };

    out.bottom.beat += _bottom_padding;
    return out;
}

void RawSelection::set_end(Cursor end) {
    _end = end;
    _mode = SelectionMode::Normal;
}

void RawSelection::toggle_padding(int rows_per_beat) {
    if (_bottom_padding == 0) {
        // 1 row * beats/row
        _bottom_padding = BeatFraction{1, rows_per_beat};
    } else {
        _bottom_padding = 0;
    }
}

void RawSelection::select_all(
    doc::Document const& document,
    ColumnToNumSubcol col_to_nsubcol,
    int rows_per_beat
) {
    using doc::GridIndex;
    using cursor::ColumnIndex;

    Selection select = get_select();
    GridIndex top_seq = select.top.grid;
    GridIndex bottom_seq = select.bottom.grid;

    // Unconditionally enable padding below bottom of selection.
    _bottom_padding = BeatFraction{1, rows_per_beat};

    auto select_block = [this, &document, col_to_nsubcol, top_seq, bottom_seq] (
        ColumnIndex left_col, ColumnIndex right_col
    ) {
        release_assert(left_col < col_to_nsubcol.size());
        release_assert(right_col < col_to_nsubcol.size());

        _begin.x = CursorX{left_col, 0};
        _begin.y = GridAndBeat{top_seq, 0};

        _end.x = CursorX{right_col, col_to_nsubcol[right_col] - 1};
        _end.y = GridAndBeat{
            bottom_seq, document.timeline[bottom_seq].nbeats - _bottom_padding
        };
    };

    if (_mode == SelectionMode::Normal) {
        // Select all grid cells and channels the current selection occupies.
        _orig_left = select.left.column;
        _orig_right = select.right.column;

        select_block(_orig_left, _orig_right);
        _mode = SelectionMode::SelectChannels;

    } else if (_mode == SelectionMode::SelectChannels) {
        // Select all grid cells the current selection occupies,
        // and all channels unconditionally.
        select_block(0, ColumnIndex(col_to_nsubcol.size() - 1));
        _mode = SelectionMode::SelectAll;

    } else if (_mode == SelectionMode::SelectAll) {
        // Select all grid cells and channels the original selection occupied.
        select_block(_orig_left, _orig_right);
        _mode = SelectionMode::SelectChannels;
    }
}


// # impl CursorAndSelection

cursor::Cursor const& CursorAndSelection::get() const {
    return _cursor;
}

cursor::Cursor const& CursorAndSelection::operator*() const {
    return _cursor;
}

cursor::Cursor const* CursorAndSelection::operator->() const {
    return &_cursor;
}

Cursor & CursorAndSelection::get_mut() {
    return _cursor;
}

void CursorAndSelection::set(Cursor cursor) {
    _cursor = cursor;
    if (_select) {
        _select->set_end(_cursor);
    }
}

void CursorAndSelection::set_x(CursorX x) {
    _cursor.x = x;
    if (_select) {
        _select->set_end(_cursor);
    }
}

void CursorAndSelection::set_y(GridAndBeat y) {
    _cursor.y = y;
    if (_select) {
        _select->set_end(_cursor);
    }
}

std::optional<RawSelection> CursorAndSelection::raw_select() const {
    return _select;
}

std::optional<RawSelection> & CursorAndSelection::raw_select_mut() {
    return _select;
}

std::optional<Selection> CursorAndSelection::get_select() const {
    if (_select) {
        return _select->get_select();
    }
    return {};
}

void CursorAndSelection::enable_select(int rows_per_beat) {
    if (!_select) {
        _select = RawSelection(_cursor, rows_per_beat);
    }
}

void CursorAndSelection::clear_select() {
    if (_select) {
        _select = {};
    }
}

void setup_error_dialog(QErrorMessage & dialog) {
    static constexpr int W = 640;
    static constexpr int H = 360;
    dialog.resize(W, H);
    dialog.setModal(true);
}

// # impl MainWindow

W_OBJECT_IMPL(MainWindow)

static MainWindow * instance;

MainWindow::MainWindow(doc::Document document, QWidget *parent)
    // I kinda regret using the same name for namespace "history" and member variable "history".
    // But it's only a problem since C++ lacks pervasive `self`.
    : QMainWindow(parent)
    , _state(std::move(document))
{}

namespace {

template<typename SpinBox>
class WheelSpinBoxT : public SpinBox {
public:
    explicit WheelSpinBoxT(QWidget * parent = nullptr)
        : SpinBox(parent)
    {
        // Prevent mouse scrolling from focusing the spinbox.
        SpinBox::setFocusPolicy(Qt::StrongFocus);
    }

    void stepBy(int steps) override {
        SpinBox::stepBy(steps);

        // Prevent mouse scrolling from permanently selecting the spinbox.
        if (!SpinBox::hasFocus()) {
            SpinBox::lineEdit()->deselect();
        }
    }
};

using WheelSpinBox = WheelSpinBoxT<QSpinBox>;
using WheelDoubleSpinBox = WheelSpinBoxT<QDoubleSpinBox>;

// # MainWindow components

using cmd_queue::CommandQueue;
using cmd_queue::AudioCommand;

struct MainWindowUi : MainWindow {
    using MainWindow::MainWindow;
    // Use raw pointers since QObjects automatically destroy children.

    // File menu
    QAction * _open;
    QAction * _save_as;
    QAction * _exit;

    // Edit menu
    QAction * _overflow_paste;
    QAction * _key_repeat;

    // View menu
    QAction * _follow_playback;
    QAction * _compact_view;

    // Panels
    TimelineEditor * _timeline_editor;

    struct Timeline {
        QAction * add_row;
        QAction * remove_row;

        QAction * move_up;
        QAction * move_down;

        QAction * clone_row;
    } _timeline;

    InstrumentList * _instrument_list;

    PatternEditor * _pattern_editor;

    // Control panel
    // Per-song ephemeral state
    WheelSpinBox * _zoom_level;

    // Song options
    QPushButton * _edit_tempo;  // TODO non-modal?
    WheelDoubleSpinBox * _tempo;
    WheelSpinBox * _beats_per_measure;
    QComboBox * _end_action;
    WheelSpinBox * _end_jump_to;

    // TODO rework settings GUI
    WheelSpinBox * _length_beats;

    // Global state (editing)
    WheelSpinBox * _octave;

    // Step
    WheelSpinBox * _step;
    QComboBox * _step_direction;
    QCheckBox * _step_to_event;

    /// Output: _pattern_editor.
    void setup_widgets() {

        auto main = this;

        // Menu
        {main__m();
            {m__m(tr("&File"));
                _open = m->addAction(tr("&Open"));
                _save_as = m->addAction(tr("Save &As"));
                m->addSeparator();
                _exit = m->addAction(tr("E&xit"));
            }

            {m__m(tr("&Edit"));
                {m__check(tr("&Overflow paste"));
                    _overflow_paste = a;
                    a->setChecked(true);
                    a->setEnabled(false);
                }
                {m__check(tr("&Key repetition"));
                    _key_repeat = a;
                    a->setEnabled(false);
                }
            }

            {m__m(tr("&View"));
                {m__check(tr("&Follow playback"));
                    _follow_playback = a;
                    a->setChecked(true);
                    /* TODO finish implementing:
                    - if cursor != play, draw play position separately
                    - if cursor != play, don't move cursor upon beginning playback
                    - when setting cursor == play, snap to playback position instead of
                      waiting for next row
                    */
                }
                {m__check(tr("&Compact view"));
                    _compact_view = a;
                    a->setEnabled(false);
                }
            }
        }

        // Toolbar
        {main__tb(IconToolBar);
            tb->setFloatable(false);
            tb->setAllowedAreas(Qt::TopToolBarArea);

            // TODO toolbar?
            // TODO add zoom checkbox
        }

        // Central widget
        {main__central_c_l(QWidget, QVBoxLayout);
            l->setContentsMargins(0, 0, 0, 0);

            // Top dock area. TODO make panels draggable and rearrangeable
            {l__c_l(QWidget, QHBoxLayout);
                c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

                // Timeline editor panel.
                timeline_editor_panel(l);

                // Control panel.
                control_panel(l);

                // Instrument list panel.
                instrument_list_panel(l);
            }

            // Main body is the pattern editor.
            pattern_editor_panel(l);
        }
    }

    static constexpr int MAX_ZOOM_LEVEL = 64;

    void timeline_editor_panel(QBoxLayout * l) {
        {l__c_l(QGroupBox, QVBoxLayout)
            c->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
            c->setTitle(tr("Timeline"));
            {l__w_factory(TimelineEditor::make(this))
                _timeline_editor = w;
            }
            {l__w(IconToolBar)
                _timeline.add_row = w->add_icon_action(
                    tr("Add Timeline Entry"), "document-new"
                );
                _timeline.remove_row = w->add_icon_action(
                    tr("Delete Timeline Entry"), "edit-delete"
                );
                _timeline.move_up = w->add_icon_action(
                    tr("Move Row Up"), "go-up"
                );
                _timeline.move_down = w->add_icon_action(
                    tr("Move Row Down"), "go-down"
                );
                _timeline.clone_row = w->add_icon_action(
                    tr("Clone Row"), "edit-copy"
                );
                enable_button_borders(w);
            }
        }
    }

    void control_panel(QBoxLayout * l) { {  // needed to allow shadowing
        l__c_l(QWidget, QHBoxLayout);
        c->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        l->setContentsMargins(0, 0, 0, 0);

        // Song options.
        {l__l(QVBoxLayout);

            // Song settings
            {l__c_form(QGroupBox, QFormLayout);
                c->setTitle(tr("Song"));

                {form__left_right(
                    QPushButton(tr("Tempo..."), this), WheelDoubleSpinBox
                );
                    _edit_tempo = left;
                    _tempo = right;
                    right->setRange(doc::MIN_TEMPO, doc::MAX_TEMPO);
                }

                // Purely cosmetic, no downside to large values.
                form->addRow(
                    tr("Beats/measure"),
                    [this] {
                        auto w = _beats_per_measure = new WheelSpinBox;
                        w->setRange(1, (int) doc::MAX_BEATS_PER_FRAME);
                        w->setEnabled(false);
                        return w;
                    }()
                );

                // Song end selector.
                {form__l(QHBoxLayout);
                    l->addWidget(new QLabel(tr("End")));

                    {l__w(QComboBox);
                        _end_action = w;
                        w->setEnabled(false);
                        w->addItem(tr("Stop"));
                        w->addItem(tr("Jump to"));
                    }

                    {l__w(WheelSpinBox);
                        _end_jump_to = w;
                        w->setEnabled(false);
                        // Must point to a valid timeline index.
                        // TODO adjust range within [0..current timeline size).
                    }
                }
            }

            // TODO rework settings GUI
            {l__c_form(QGroupBox, QFormLayout);
                c->setTitle(tr("Timeline item"));

                form->addRow(
                    new QLabel(tr("Length (beats)")),
                    [this] {
                        auto w = _length_beats = new WheelSpinBox;
                        w->setRange(1, (int) doc::MAX_BEATS_PER_FRAME);
                        w->setValue(16);
                        return w;
                    }()
                );
            }
            l->addStretch();
        }

        // Pattern editing.
        {l__l(QVBoxLayout);
            {l__c_form(QGroupBox, QFormLayout);
                c->setTitle(tr("Note entry"));

                {form__label_w(tr("Octave"), WheelSpinBox);
                    _octave = w;

                    int gui_bottom_octave =
                        get_app().options().note_names.gui_bottom_octave;
                    int peak_octave = (doc::CHROMATIC_COUNT - 1) / doc::NOTES_PER_OCTAVE;
                    w->setRange(gui_bottom_octave, gui_bottom_octave + peak_octave);
                }

                {form__label_w(tr("Zoom"), WheelSpinBox);
                    _zoom_level = w;
                    w->setRange(1, MAX_ZOOM_LEVEL);
                }
            }
            {l__c_form(QGroupBox, QFormLayout);
                c->setTitle(tr("Step"));
                {form__label_w(tr("Rows"), WheelSpinBox);
                    _step = w;
                    w->setRange(0, 256);
                }

                {form__w(QComboBox);
                    _step_direction = w;

                    auto push = [&w] (StepDirection step, QString item) {
                        assert(w->count() == (int) step);
                        w->addItem(item);
                    };
                    push(StepDirection::Down, tr("Down"));
                    push(StepDirection::RightDigits, tr("Right (digits)"));
                    push(StepDirection::RightEffect, tr("Right (effect)"));
                    push(StepDirection::Right, tr("Right"));
                    assert(w->count() == (int) StepDirection::COUNT);
                }

                {form__w(QCheckBox(tr("Snap to event")));
                    _step_to_event = w;
                }
            }
            l->addStretch();
        }
    } }

    void instrument_list_panel(QBoxLayout * l) {
        {l__w_factory(InstrumentList::make(this))
            _instrument_list = w;
            w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        }
    }

    void pattern_editor_panel(QBoxLayout * l) {
        {l__c_l(QFrame, QVBoxLayout);
            c->setFrameStyle(int(QFrame::StyledPanel) | QFrame::Sunken);

            c->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
            l->setContentsMargins(0, 0, 0, 0);
            {l__w(PatternEditor(this));
                w->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
                _pattern_editor = w;
            }
        }
    }
};

class AudioComponent {
    // GUI/audio communication.
    AudioState _audio_state = AudioState::Stopped;
    CommandQueue _command_queue{};

    // Audio.
    RtAudio _rt{};
    unsigned int _curr_audio_device{};

    // Points to History and CommandQueue, must be listed after them.
    std::optional<AudioThreadHandle> _audio_handle;

// impl
public:
    AudioComponent() = default;

    AudioState audio_state() const {
        return _audio_state;
    }

// # Command queue.
private:
    /// Return a command to be sent to the audio thread.
    /// It ignores the command's contents,
    /// but monitors its "next" pointer for new commands.
    AudioCommand * stub_command() {
        return _command_queue.begin();
    }

    void gc_command_queue() {
        // Every time GUI pushes an event, it moves _command_queue.end().
        // Once the audio thread is done processing events,
        // the GUI thread's next call to gc_command_queue()
        // will advance _command_queue.begin().
        // To run code once after the audio thread catches up on events,
        // check if we drain 1+ event, then end with an empty queue.

        auto & audio_handle = _audio_handle.value();

        if (_command_queue.begin() != _command_queue.end()) {
            auto x = audio_handle.seen_command();
            while (_command_queue.begin() != x) {
                _command_queue.pop();
            }
            if (_command_queue.begin() == _command_queue.end()) {
                // once GUI sees audio caught up on commands, it must see audio's new time.
                if (_audio_state == AudioState::Starting) {
                    _audio_state = AudioState::PlayHasStarted;
                }
            }
        }
    }

// # Lifecycle transitions.
private:
    /// Output: _curr_audio_device.
    void scan_devices() {
        std::cout << "Devices {\n";

        using fmt::print;

        // Determine the number of devices available
        unsigned int n_devices = _rt.getDeviceCount();

        // Scan through devices for various capabilities
        for (unsigned int i = 0; i < n_devices; i++) {
            print("    {}: ", i);

            RtAudio::DeviceInfo info = _rt.getDeviceInfo(i);
            if (info.probed == true) {
                print(
                    "name={}, rate={}, out_nchan={}\n",
                    info.name,
                    info.preferredSampleRate,
                    info.outputChannels
                );
            } else {
                print("probe failed\n");
            }
        }

        std::cout << "}\n";
        fflush(stdout);

        if (n_devices == 0) {
            std::cout << "No devices available\n";
            return;
        }

        print("Default device index: {}\n", _rt.getDefaultOutputDevice());
        fflush(stdout);

        _curr_audio_device = _rt.getDefaultOutputDevice();
    }

public:
    /// Output: _audio_handle.
    void setup_audio(StateComponent const& state) {
        // TODO should this be handled by the constructor?
        // Initializes _curr_audio_device.
        scan_devices();

        release_assert(_command_queue.is_empty());

        // Begin playing audio. Destroying this variable makes audio stop.
        _audio_handle = AudioThreadHandle::make(
            _rt, _curr_audio_device, state.document().clone(), stub_command()
        );
    }

    void restart_audio_thread(StateComponent const& state) {
        // Only one stream can be running at a time.
        // The lifetimes of the old and new audio thread must not overlap.
        // So destroy the old before constructing the new.
        _audio_handle = {};  // joins on audio thread

        _audio_state = AudioState::Stopped;
        _command_queue.clear();

        _audio_handle = AudioThreadHandle::make(
            _rt, _curr_audio_device, state.document().clone(), stub_command()
        );
    }

// # Play/pause commands.
public:
    MaybeSequencerTime maybe_seq_time() {
        // not const because of gc_command_queue().

        MaybeSequencerTime maybe_seq_time{};

        if (_audio_handle.has_value()) {
            auto & audio_handle = _audio_handle.value();

            gc_command_queue();

            if (audio_state() == AudioState::PlayHasStarted) {
                maybe_seq_time = audio_handle.play_time();
            }
        }

        return maybe_seq_time;
    }

    void play_pause(StateTransaction & tx) {
        if (_audio_handle.has_value()) {
            gc_command_queue();

            if (_audio_state == AudioState::Stopped) {
                auto cursor = tx.state().cursor().y;
                cursor.beat = 0;
                play_from(tx, cursor);
            } else {
                stop_play();
            }
        }
    }

    void play_from_row(StateTransaction & tx) {
        if (_audio_handle.has_value()) {
            gc_command_queue();

            if (_audio_state == AudioState::Stopped) {
                play_from(tx, {});
            } else {
                stop_play();
            }
        }
    }

private:
    void play_from(StateTransaction & tx, std::optional<GridAndBeat> time) {
        auto start_time = time.value_or(tx.state().cursor().y);
        _command_queue.push(cmd_queue::PlayFrom{start_time});
        _audio_state = AudioState::Starting;

        if (time) {
            // Move cursor to right spot, while waiting for audio thread to respond.
            tx.cursor_mut().set_y(*time);
        }
    }

    void stop_play() {
        _command_queue.push(cmd_queue::StopPlayback{});
        _audio_state = AudioState::Stopped;
    }

// # Document edit commands.
private:
    void send_edit(AudioComponent & audio, edit::EditBox command) {
        if (audio._audio_handle.has_value()) {
            gc_command_queue();
            _command_queue.push(std::move(command));
        }
    }

public:
    void push_edit(
        StateTransaction & tx, edit::EditBox command, MoveCursor cursor_move
    ) {
        send_edit(*this, command->clone_for_audio(tx.state().document()));

        edit::MaybeCursor before_cursor;
        edit::MaybeCursor after_cursor;
        edit::Cursor const here = tx.state().cursor();

        auto p = &cursor_move;
        if (std::get_if<MoveCursor_::IgnoreCursor>(p)) {
            before_cursor = std::nullopt;
            after_cursor = std::nullopt;
        } else
        if (auto move_from = std::get_if<MoveCursor_::MoveFrom>(p)) {
            before_cursor = move_from->before_or_here.value_or(here);
            after_cursor = move_from->after_or_here.value_or(here);
        }

        tx.history_mut().push(edit::CursorEdit{
            std::move(command), before_cursor, after_cursor
        });

        if (after_cursor) {
            tx.cursor_mut().set(*after_cursor);
        }
    }

    bool undo(StateTransaction & tx) {
        if (auto cursor_edit = tx.history().get_undo()) {
            send_edit(*this, std::move(cursor_edit->edit));
            if (cursor_edit->before_cursor) {
                tx.cursor_mut().set(*cursor_edit->before_cursor);
            }
            tx.history_mut().undo();
            return true;
        }
        return false;
    }

    bool redo(StateTransaction & tx) {
        if (auto cursor_edit = tx.history().get_redo()) {
            send_edit(*this, std::move(cursor_edit->edit));
            if (cursor_edit->after_cursor) {
                tx.cursor_mut().set(*cursor_edit->after_cursor);
            }
            tx.history_mut().redo();
            return true;
        }
        return false;
    }
};

}  // anonymous namespace

using tempo_dialog::TempoDialog;
using instrument_dialog::InstrumentDialog;

// module-private
class MainWindowImpl : public MainWindowUi {
     W_OBJECT(MainWindowImpl)
public:
    // GUI widgets are defined in MainWindowUi.
    // These are non-widget utilities.
    QScreen * _screen;
    QTimer _gui_refresh_timer;
    QErrorMessage _error_dialog{this};
    QPointer<InstrumentDialog> _maybe_instr_dialog;

    // Global playback shortcuts.
    // TODO implement global configuration system with "reloaded" signal.
    // When user changes shortcuts, reassign shortcut keybinds.

    // QShortcut is only a shortcut. QAction can be bound to menus and buttons too.
    // Editor actions:
    QAction _play_pause;
    QAction _play_from_row;
    QAction _undo;
    QAction _redo;

    // Zoom actions:
    QAction _zoom_out;
    QAction _zoom_in;
    QAction _zoom_out_half;
    QAction _zoom_in_half;
    QAction _zoom_out_triplet;
    QAction _zoom_in_triplet;

    // Global actions:
    QAction _restart_audio;

    AudioComponent _audio;

    // utility methods
    doc::Document const& get_document() const {
        return _state.history().get_document();
    }

    // impl MainWindow

    std::optional<StateTransaction> edit_state() final {
        return StateTransaction::make(this);
    }

    StateTransaction edit_unwrap() final {
        return unwrap(edit_state());
    }

    /// Called after edit/undo/redo, which are capable of deleting the timeline row
    /// we're currently in.
    void clamp_cursor(StateTransaction & tx) {
        doc::Document const& document = get_document();

        auto cursor_y = _state.cursor().y;
        auto const orig = cursor_y;
        auto ngrid = doc::GridIndex(document.timeline.size());

        if (cursor_y.grid >= ngrid) {
            cursor_y.grid = ngrid - 1;

            BeatFraction nbeats = document.timeline[cursor_y.grid].nbeats;
            cursor_y.beat = nbeats;
        }

        BeatFraction nbeats = document.timeline[cursor_y.grid].nbeats;

        // If cursor is out of bounds, move to last row in pattern.
        if (cursor_y.beat >= nbeats) {
            auto rows_per_beat = _pattern_editor->zoom_level();

            BeatFraction rows = nbeats * rows_per_beat;
            int prev_row = util::math::frac_prev(rows);
            cursor_y.beat = BeatFraction{prev_row, rows_per_beat};
        }

        if (cursor_y != orig) {
            tx.cursor_mut().set_y(cursor_y);
        }
    }

    void push_edit(
        StateTransaction & tx, edit::EditBox command, MoveCursor cursor_move
    ) {
        _audio.push_edit(tx, std::move(command), cursor_move);
        clamp_cursor(tx);
    }

    void show_instr_dialog() override {
        if (!_maybe_instr_dialog) {
            _maybe_instr_dialog = InstrumentDialog::make(this);
            _maybe_instr_dialog->show();
        } else {
            _maybe_instr_dialog->activateWindow();
        }
    }

    // private methods
    MainWindowImpl(doc::Document document, QWidget * parent)
        : MainWindowUi(std::move(document), parent)
        , _error_dialog(this)
    {
        // Setup GUI.
        setup_widgets();  // Output: _pattern_editor.
        setup_error_dialog(_error_dialog);

        _pattern_editor->set_history(_state.document_getter());
        _timeline_editor->set_history(_state.document_getter());
        _instrument_list->reload_state();

        // Hook up refresh timer.
        connect(
            &_gui_refresh_timer, &QTimer::timeout,
            this, [this] () {
                auto maybe_seq_time = _audio.maybe_seq_time();
                if (!maybe_seq_time) return;
                auto const seq_time = *maybe_seq_time;

                // Update cursor to sequencer position (from audio thread).

                GridAndBeat play_time =
                    [seq_time, rows_per_beat = _zoom_level->value()]
                {
                    GridAndBeat play_time{seq_time.grid, seq_time.beats};

                    // Find row.
                    for (int curr_row = rows_per_beat - 1; curr_row >= 0; curr_row--) {
                        auto curr_ticks = curr_row / doc::BeatFraction{rows_per_beat}
                            * seq_time.curr_ticks_per_beat;

                        if (doc::round_to_int(curr_ticks) <= seq_time.ticks) {
                            play_time.beat += BeatFraction{curr_row, rows_per_beat};
                            break;
                        }
                    }
                    return play_time;
                }();

                // Optionally set cursor to match play time.
                if (_follow_playback->isChecked() && _state.cursor().y != play_time) {
                    edit_unwrap().cursor_mut().set_y(play_time);
                }

                // TODO if audio is playing, and cursor is detached from playback point,
                // render playback point separately and redraw audio/timeline editor.
            }
        );

        setup_screen();
        // TODO setup_screen() when primaryScreen changed
        // TODO setup_timer() when refreshRate changed

        _audio.setup_audio(_state);

        // Last thing.
        on_startup(get_app().options());
        // TODO reload_shortcuts() when shortcut keybinds changed
    }

    void setup_screen() {
        _screen = QGuiApplication::primaryScreen();
        setup_timer();
    }
    // W_SLOT(setup_screen)

    void setup_timer() {
        // floor div
        auto refresh_ms = int(1000 / _screen->refreshRate());
        _gui_refresh_timer.setInterval(refresh_ms);
        // calling twice will restart timer.
        _gui_refresh_timer.start();
    }
    // W_SLOT(setup_timer)

    void on_startup(config::Options const& options) {
        // Upon application startup, pattern editor panel is focused.
        _pattern_editor->setFocus();

        // TODO look into unifying with reload_shortcuts().
        _open->setShortcuts(QKeySequence::Open);
        connect(_open, &QAction::triggered, this, &MainWindowImpl::on_open);

        _save_as->setShortcuts(QKeySequence::SaveAs);
        connect(_save_as, &QAction::triggered, this, &MainWindowImpl::on_save_as);

        // TODO _exit->setShortcuts(QKeySequence::Quit);
        connect(_exit, &QAction::triggered, this, &QWidget::close);

        auto connect_spin = [](QSpinBox * spin, auto * target, auto func) {
            connect(
                spin, qOverload<int>(&QSpinBox::valueChanged),
                target, func,
                Qt::UniqueConnection
            );
        };

        auto connect_dspin = [](QDoubleSpinBox * spin, auto * target, auto func) {
            connect(
                spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                target, func,
                Qt::UniqueConnection
            );
        };

        auto connect_check = [](QCheckBox * check, auto * target, auto func) {
            // QCheckBox::clicked is not emitted when state is programmatically changed.
            // idk which is better.
            connect(
                check, &QCheckBox::toggled,
                target, func,
                Qt::UniqueConnection
            );
        };

        auto connect_combo = [](QComboBox * combo, auto * target, auto func) {
            connect(
                combo, qOverload<int>(&QComboBox::currentIndexChanged),
                target, func,
                Qt::UniqueConnection
            );
        };

        // Previously, BIND_SPIN(name) would use _##NAME to synthesize the field _name.
        // However, this means searching for _name won't find the usage,
        // making it hard to navigate the code.
        // So supply the name twice, once like a _field and once like a method.
        #define BIND_SPIN(FIELD, METHOD) \
            FIELD->setValue(_pattern_editor->METHOD()); \
            connect_spin( \
                FIELD, _pattern_editor, &PatternEditor::set_##METHOD \
            );

        #define BIND_DSPIN(FIELD, METHOD) \
            FIELD->setValue(_pattern_editor->METHOD()); \
            connect_dspin( \
                FIELD, _pattern_editor, &PatternEditor::set_##METHOD \
            );

        #define BIND_CHECK(FIELD, METHOD) \
            FIELD->setChecked(_pattern_editor->METHOD()); \
            connect_check( \
                FIELD, _pattern_editor, &PatternEditor::set_##METHOD \
            );

        #define BIND_COMBO(FIELD, METHOD) \
            FIELD->setCurrentIndex((int) _pattern_editor->METHOD()); \
            connect_combo( \
                FIELD, _pattern_editor, &PatternEditor::set_##METHOD##_int \
            );

        connect(_edit_tempo, &QPushButton::clicked, this, [this]() {
            TempoDialog::make(_state.document_getter(), this)->exec();
        });

        // _tempo obtains its value through StateTransaction.
        connect_dspin(_tempo, this, [this] (double tempo) {
            debug_unwrap(edit_state(), [&](auto & tx) {
                tx.push_edit(edit_doc::set_tempo(tempo), MoveCursor_::IGNORE_CURSOR);
            });
        });

        connect_spin(_length_beats, this, [this] (int grid_length_beats) {
            debug_unwrap(edit_state(), [&](auto & tx) {
                tx.push_edit(
                    edit_doc::set_grid_length(_state.cursor().y.grid, grid_length_beats),
                    MoveCursor_::IGNORE_CURSOR
                );
            });
        });

        // Bind octave field.
        {
            auto gui_bottom_octave = [] () {
                return get_app().options().note_names.gui_bottom_octave;
            };

            // Visual octave: add offset.
            _octave->setValue(_pattern_editor->octave() + gui_bottom_octave());

            // MIDI octave: subtract offset.
            connect_spin(
                _octave,
                _pattern_editor,
                [this, gui_bottom_octave] (int octave) {
                    _pattern_editor->set_octave(octave - gui_bottom_octave());
                }
            );
        }

        BIND_SPIN(_zoom_level, zoom_level)

        BIND_SPIN(_step, step)
        BIND_COMBO(_step_direction, step_direction);
        BIND_CHECK(_step_to_event, step_to_event);

        // Connect timeline editor toolbar.
        auto connect_action = [this] (QAction & action, auto /*copied*/ func) {
            connect(&action, &QAction::triggered, this, func);
        };
        connect_action(*_timeline.add_row, &MainWindowImpl::add_timeline_row);
        connect_action(*_timeline.remove_row, &MainWindowImpl::remove_timeline_row);
        connect_action(*_timeline.move_up, &MainWindowImpl::move_grid_up);
        connect_action(*_timeline.move_down, &MainWindowImpl::move_grid_down);
        connect_action(*_timeline.clone_row, &MainWindowImpl::clone_timeline_row);

        // Bind keyboard shortcuts, and (for the time being) connect to functions.
        reload_shortcuts();

        // Initialize GUI state.
        edit_unwrap().update_all();
    }

    void on_open() {
        using serialize::ErrorType;

        // TODO save recent dirs, using SQLite or QSettings
        auto path = QFileDialog::getOpenFileName(
            this,
            tr("Open File"),
            QString(),
            tr("ExoTracker modules (*.etm);;All files (*)"));

        if (path.isEmpty()) {
            return;
        }

        // TODO prompt if document dirty.

        auto result = serialize::load_from_path(path.toUtf8());
        if (result.v) {
            // If document loaded successfully, load it into the program.
            auto & [document, metadata] = *result.v;

            // Replace the GUI state with the new file.
            // Hopefully I didn't miss anything.
            {
                StateTransaction tx = edit_unwrap();
                // Probably redundant, but do it just to be safe.
                tx.update_all();

                tx.cursor_mut() = {};

                // This *technically* doesn't result in the audio thread accessing freed memory,
                // since this only overwrites _state._history
                // and the audio thread only reads from _command_queue.
                //
                // However this is still easy to get wrong,
                // since the GUI is operating on the new document
                // and the audio thread is still operating on the old one.
                // If you fail to reload the audio thread with the new document
                // (_audio.restart_audio_thread()),
                // you end up in an inconsistent state upon editing or playback.
                tx.set_document(std::move(document));
                tx.set_instrument(0);
            }

            _zoom_level->setValue(metadata.zoom_level);

            // Restart the audio thread with the new document.
            _audio.restart_audio_thread(_state);
        } else {
            // Document failed to load. There should be an error message explaining why.
            assert(!result.errors.empty());
        }

        // Show warnings or errors.
        if (!result.v || !result.errors.empty()) {
            QTextDocument document;
            auto cursor = QTextCursor(&document);
            cursor.beginEditBlock();

            if (result.v) {
                cursor.insertText(tr("File loaded with warnings:"));
            } else {
                cursor.insertText(tr("Failed to load file:"));
            }

            // https://stackoverflow.com/a/51864380
            QTextList* list = nullptr;
            QTextBlockFormat non_list_format = cursor.blockFormat();
            for (auto const& err : result.errors) {
                if (!list) {
                    // create list with 1 item
                    list = cursor.insertList(QTextListFormat::ListDisc);
                } else {
                    // append item to list
                    cursor.insertBlock();
                }

                QString line = QLatin1String("%1: %2")
                    .arg((err.type == ErrorType::Error) ? tr("Error") : tr("Warning"))
                    .arg(QString::fromStdString(err.description));
                cursor.insertText(line);
            }
            // This ends the list.
            cursor.insertBlock();
            cursor.setBlockFormat(non_list_format);

            _error_dialog.showMessage(document.toHtml());
        }
    }

    void on_save_as() {
        using serialize::Metadata;

        // TODO save recent dirs, using SQLite or QSettings
        auto path = QFileDialog::getSaveFileName(
            this,
            tr("Open File"),
            QString(),
            tr("ExoTracker modules (*.etm);;All files (*)"));

        if (path.isEmpty()) {
            return;
        }

        auto result = serialize::save_to_path(
            get_document(),
            Metadata {
                .zoom_level = (uint16_t) _zoom_level->value(),
            },
            path.toUtf8());

        if (result) {
            QTextDocument document;
            auto cursor = QTextCursor(&document);

            cursor.insertText(tr("Failed to save file:\n"));
            cursor.insertText(QString::fromStdString(*result));
            _error_dialog.showMessage(document.toHtml());
        }
    }

    /// Compute the fixed zoom sequence, consisting of powers of 2
    /// and an optional factor of 3.
    static std::vector<int> calc_zoom_levels() {
        std::vector<int> zoom_levels;

        // Add regular zoom levels.
        for (int i = 1; i <= MAX_ZOOM_LEVEL; i *= 2) {
            zoom_levels.push_back(i);
        }
        // Add triplet zoom levels.
        for (int i = 3; i <= MAX_ZOOM_LEVEL; i *= 2) {
            zoom_levels.push_back(i);
        }
        // Sort in increasing order.
        std::sort(zoom_levels.begin(), zoom_levels.end());

        return zoom_levels;
    }

    std::vector<int> _zoom_levels = calc_zoom_levels();

    /// Clears existing bindings and rebinds shortcuts.
    /// Can be called multiple times.
    void reload_shortcuts() {
        auto & shortcuts = get_app().options().global_keys;

        // This function is only for binding shortcut keys.
        // Do not connect toolbar/menu actions here, but in on_startup() instead.
        // For the time being, connecting shortcut actions is allowed,
        // but most of these actions will have toolbar/menu entries in the future.

        auto bind_editor_action = [this] (QAction & action) {
            action.setShortcutContext(Qt::WidgetWithChildrenShortcut);

            // "A QWidget should only have one of each action and adding an action
            // it already has will not cause the same action to be in the widget twice."
            _pattern_editor->addAction(&action);
        };

        auto connect_action = [this] (QAction & action, auto /*copied*/ func) {
            connect(&action, &QAction::triggered, this, func, Qt::UniqueConnection);
        };

        #define BIND_FROM_CONFIG(NAME) \
            _##NAME.setShortcut(QKeySequence{shortcuts.NAME}); \
            bind_editor_action(_##NAME)

        BIND_FROM_CONFIG(play_pause);
        connect_action(_play_pause, [this] () {
            auto tx = edit_unwrap();
            _audio.play_pause(tx);
        });

        BIND_FROM_CONFIG(play_from_row);
        connect_action(_play_from_row, [this] () {
            auto tx = edit_unwrap();
            _audio.play_from_row(tx);
        });

        _undo.setShortcuts(QKeySequence::Undo);
        bind_editor_action(_undo);
        connect_action(_undo, &MainWindowImpl::undo);

        _redo.setShortcuts(QKeySequence::Redo);
        bind_editor_action(_redo);
        connect_action(_redo, &MainWindowImpl::redo);

        // TODO maybe these shortcuts should be inactive when order editor is focused
        BIND_FROM_CONFIG(zoom_out);
        connect_action(_zoom_out, [this] () {
            int const curr_zoom = _zoom_level->value();

            // Pick the next smaller zoom level in the fixed zoom sequence.
            for (int const new_zoom : reverse(_zoom_levels)) {
                if (new_zoom < curr_zoom) {
                    _zoom_level->setValue(new_zoom);
                    return;
                }
            }
            // If we're already at minimum zoom, don't change zoom level.
        });

        BIND_FROM_CONFIG(zoom_in);
        connect_action(_zoom_in, [this] () {
            int const curr_zoom = _zoom_level->value();

            // Pick the next larger zoom level in the fixed zoom sequence.
            for (int const new_zoom : _zoom_levels) {
                if (new_zoom > curr_zoom) {
                    _zoom_level->setValue(new_zoom);
                    return;
                }
            }
            // If we're already at maximum zoom, don't change zoom level.
        });

        BIND_FROM_CONFIG(zoom_out_half);
        connect_action(_zoom_out_half, [this] () {
            // Halve zoom, rounded down. QSpinBox will clamp minimum to 1.
            _zoom_level->setValue(_zoom_level->value() / 2);
        });

        BIND_FROM_CONFIG(zoom_in_half);
        connect_action(_zoom_in_half, [this] () {
            // Double zoom. QSpinBox will truncate to maximum value.
            _zoom_level->setValue(_zoom_level->value() * 2);
        });

        BIND_FROM_CONFIG(zoom_out_triplet);
        connect_action(_zoom_out_triplet, [this] () {
            // Multiply zoom by 2/3, rounded down. QSpinBox will clamp minimum to 1.
            _zoom_level->setValue(_zoom_level->value() * 2 / 3);
        });

        BIND_FROM_CONFIG(zoom_in_triplet);
        connect_action(_zoom_in_triplet, [this] () {
            // Multiply zoom by 3/2, rounded up.
            // If we rounded down, zooming 1 would result in 1, which is bad.
            // QSpinBox will truncate to maximum value.
            //
            // Rounding up has the nice property that zoom_in_triplet() followed by
            // zoom_out_triplet() always produces the value we started with
            // (assuming no truncation).
            _zoom_level->setValue(ceildiv(_zoom_level->value() * 3, 2));
        });

        _restart_audio.setShortcut(QKeySequence{Qt::Key_F12});
        _restart_audio.setShortcutContext(Qt::ShortcutContext::ApplicationShortcut);
        this->addAction(&_restart_audio);
        connect_action(_restart_audio, [this] () {
            _audio.restart_audio_thread(_state);
        });
    }

    // # Mutation methods, called when QAction are triggered.

    void undo() {
        auto tx = edit_unwrap();
        if (_audio.undo(tx)) {
            clamp_cursor(tx);
        }
    }

    void redo() {
        auto tx = edit_unwrap();
        if (_audio.redo(tx)) {
            clamp_cursor(tx);
        }
    }

    void add_timeline_row() {
        auto & document = get_document();
        if (document.timeline.size() >= doc::MAX_TIMELINE_FRAMES) {
            return;
        }

        auto old_grid = _state.cursor().y.grid;

        // Don't use this for undo.
        // If you do, "add, undo, add" will differ from "add".
        Cursor new_cursor = {
            .x = _state.cursor().x,
            .y = {
                .grid = old_grid + 1,
                .beat = 0,
            },
        };

        auto tx = edit_unwrap();
        tx.push_edit(
            edit_doc::add_timeline_row(document, old_grid + 1, _length_beats->value()),
            move_to(new_cursor));
    }

    void remove_timeline_row() {
        auto old_grid = _state.cursor().y.grid;

        // The resulting cursor is invalid if you delete the last row.
        // clamp_cursor() will fix it.
        Cursor new_cursor = {
            .x = _state.cursor().x,
            .y = {
                .grid = old_grid,
                .beat = 0,
            },
        };

        auto & document = get_document();
        if (document.timeline.size() <= 1) {
            return;
        }

        auto tx = edit_unwrap();
        tx.push_edit(
            edit_doc::remove_timeline_row(_state.cursor().y.grid), move_to(new_cursor)
        );
    }

    void move_grid_up() {
        auto const& cursor = _state.cursor();
        if (cursor.y.grid.v > 0) {
            auto up = cursor;
            up.y.grid--;

            auto tx = edit_unwrap();
            tx.push_edit(edit_doc::move_grid_up(_state.cursor().y.grid), move_to(up));
        }
    }

    void move_grid_down() {
        auto const& cursor = _state.cursor();
        auto & document = get_document();
        if (cursor.y.grid + 1 < document.timeline.size()) {
            auto down = cursor;
            down.y.grid++;

            auto tx = edit_unwrap();
            tx.push_edit(
                edit_doc::move_grid_down(_state.cursor().y.grid), move_to(down)
            );
        }
    }

    void clone_timeline_row() {
        auto & document = get_document();
        if (document.timeline.size() >= doc::MAX_TIMELINE_FRAMES) {
            return;
        }

        auto tx = edit_unwrap();

        // Right now the clone button keeps the cursor position.
        // Should it move the cursor down by 1 pattern, into the clone?
        // Or down to the beat 0 of the clone?
        tx.push_edit(
            edit_doc::clone_timeline_row(document, _state.cursor().y.grid),
            move_to_here());
    }
};
W_OBJECT_IMPL(MainWindowImpl)


// # GUI state mutation tracking (StateTransaction):

StateTransaction::StateTransaction(MainWindowImpl *win)
    : _win(win)
    , _uncaught_exceptions(std::uncaught_exceptions())
{
    assert(!win->_state._during_update);
    state_mut()._during_update = true;
}

std::optional<StateTransaction> StateTransaction::make(MainWindowImpl *win) {
    if (win->_state._during_update) {
        return {};
    }
    return StateTransaction(win);
}

StateTransaction::StateTransaction(StateTransaction && other) noexcept
    : _win(other._win)
    , _uncaught_exceptions(other._uncaught_exceptions)
    , _queued_updates(other._queued_updates)
{
    other._win = nullptr;
}

StateTransaction::~StateTransaction() noexcept(false) {
    if (_win == nullptr) {
        return;
    }
    auto & state = state_mut();
    defer {
        state._during_update = false;
    };

    // If unwinding, skip updating the GUI; we don't want to do work during unwinding.
    if (std::uncaught_exceptions() != _uncaught_exceptions) {
        return;
    }

    auto e = _queued_updates;
    using E = StateUpdateFlag;

    // PatternEditor depends on _history and _cursor.
    if (e & (E::DocumentEdited | E::CursorMoved)) {
        _win->_pattern_editor->update();
    }

    // TimelineEditor depends on _history and _cursor.
    if (e & E::DocumentEdited) {
        // TODO find a less hacky way to update item count
        _win->_timeline_editor->set_history(state.document_getter());
    } else if (e & E::CursorMoved) {
        _win->_timeline_editor->update_cursor();
    }

    // InstrumentList depends on _history and _instrument.
    if (e & E::DocumentEdited) {
        _win->_instrument_list->reload_state();
    } else if (e & E::InstrumentSwitched) {
        _win->_instrument_list->update_selection();
    }

    // Synchronizing InstrumentDialog with the document and active instrument
    // is tricky.
    // https://docs.google.com/document/d/1xSXmtB4-9Wa11Bo9jWp3cpMvIWSbl6DNN3gojpW-NhE/edit#heading=h.68ro9bhgsp2w
    if (_win->_maybe_instr_dialog) {
        if (e & E::InstrumentDeleted) {
            // closes dialog, nulls out pointer later on.
            _win->_maybe_instr_dialog->close();
        } else if (e & (E::DocumentEdited | E::InstrumentSwitched)) {
            // may close dialog and null out pointer later on.
            _win->_maybe_instr_dialog->reload_state(e & E::InstrumentSwitched);
        }
    }

    doc::Document const& doc = state.document();

    if (e & E::DocumentEdited) {
        auto b = QSignalBlocker(_win->_tempo);
        _win->_tempo->setValue(doc.sequencer_options.target_tempo);
    }

    if (e & (E::DocumentEdited | E::CursorMoved)) {
        auto b = QSignalBlocker(_win->_length_beats);
        auto nbeats = frac_floor(doc.timeline[state.cursor().y.grid].nbeats);
        _win->_length_beats->setValue(nbeats);
    }
}

StateComponent const& StateTransaction::state() const {
    return _win->_state;
}

StateComponent & StateTransaction::state_mut() {
    return _win->_state;
}

using E = StateUpdateFlag;

history::History & StateTransaction::history_mut() {
    _queued_updates |= E::DocumentEdited;
    return state_mut()._history;
}

void StateTransaction::push_edit(edit::EditBox command, MoveCursor cursor_move) {
    _win->push_edit(*this, std::move(command), cursor_move);
}

void StateTransaction::instrument_deleted() {
    _queued_updates |= E::InstrumentDeleted;
}

void StateTransaction::set_document(doc::Document document) {
    state_mut()._history = History(std::move(document));
    _queued_updates |= E::DocumentReplaced | E::DocumentEdited;
}

CursorAndSelection & StateTransaction::cursor_mut() {
    _queued_updates |= E::CursorMoved;
    return state_mut()._cursor;
}

void StateTransaction::set_instrument(int instrument) {
    _queued_updates |= E::InstrumentSwitched;
    state_mut()._instrument = instrument;
}

// public
std::unique_ptr<MainWindow> MainWindow::make(doc::Document document, QWidget * parent) {
    if (instance) {
        throw std::logic_error("Tried to create two MainWindow instances");
    }
    auto out = make_unique<MainWindowImpl>(std::move(document), parent);
    instance = &*out;
    return out;
}

MainWindow & MainWindow::get_instance() {
    if (instance) {
        return *instance;
    } else {
        throw std::logic_error("Tried to get instance when none was present");
    }
}


MainWindow::~MainWindow() {
    instance = nullptr;
}

// namespace
}
