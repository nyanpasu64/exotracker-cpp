#include "main_window.h"
#include "gui/pattern_editor/pattern_editor_panel.h"
#include "gui/timeline_editor.h"
#include "gui/move_cursor.h"
#include "lib/layout_macros.h"
#include "gui_common.h"
#include "cmd_queue.h"
#include "edit/edit_doc.h"
#include "util/math.h"
#include "util/release_assert.h"
#include "util/reverse.h"

#include <fmt/core.h>
#include <rtaudio/RtAudio.h>
#include <verdigris/wobjectimpl.h>

// Widgets
#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include "gui/lib/icon_toolbar.h"
#include <QToolButton>
// Layouts
#include <QBoxLayout>
#include <QFormLayout>
// Other
#include <QAction>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QTimer>

#include <algorithm>  // std::min/max, std::sort
#include <chrono>
#include <functional>  // reference_wrapper
#include <iostream>
#include <optional>
#include <stdexcept>  // logic_error

namespace gui::main_window {

using std::unique_ptr;
using std::make_unique;

using gui::pattern_editor::PatternEditorPanel;
using gui::timeline_editor::TimelineEditor;
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

W_OBJECT_IMPL(CursorAndSelection)

cursor::Cursor const& CursorAndSelection::get() const {
    return _cursor;
}

cursor::Cursor const& CursorAndSelection::operator*() const {
    return _cursor;
}

cursor::Cursor const* CursorAndSelection::operator->() const {
    return &_cursor;
}

Cursor & CursorAndSelection::get_internal() {
    return _cursor;
}

void CursorAndSelection::set_internal(Cursor cursor) {
    _cursor = cursor;
    if (_select) {
        _select->set_end(_cursor);
    }
}

void CursorAndSelection::set(Cursor cursor) {
    set_internal(cursor);
    reset_digit();
    emit cursor_moved();
}

void CursorAndSelection::set_x(CursorX x) {
    _cursor.x = x;
    if (_select) {
        _select->set_end(_cursor);
    }
    reset_digit();
    emit cursor_moved();
}

void CursorAndSelection::set_y(GridAndBeat y) {
    _cursor.y = y;
    if (_select) {
        _select->set_end(_cursor);
    }
    reset_digit();
    emit cursor_moved();
}

int CursorAndSelection::digit_index() const {
    return _digit;
}

int CursorAndSelection::advance_digit() {
    ++_digit;
    // Technically we should emit cursor_moved(),
    // but advance_digit() is only called after/by another function which also emits it,
    // and we want to avoid unnecessary double redraws.

    return _digit;
}

void CursorAndSelection::reset_digit() {
    _digit = 0;
    // Technically we should emit cursor_moved(),
    // but reset_digit() is only called after/by another function which also emits it,
    // and we want to avoid unnecessary double redraws.
}

std::optional<RawSelection> CursorAndSelection::raw_select() const {
    return _select;
}

std::optional<RawSelection> & CursorAndSelection::raw_select_mut() {
    // We emit the signal before _select is mutated.
    // But the listener is connected via a Qt::QueuedConnection,
    // so it only gets invoked once the mutating function exits to the event loop.
    emit cursor_moved();
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
        emit cursor_moved();
    }
}

void CursorAndSelection::clear_select() {
    if (_select) {
        _select = {};
        emit cursor_moved();
    }
    // TODO reset digit?
}


// # impl MainWindow

W_OBJECT_IMPL(MainWindow)

static MainWindow * instance;

MainWindow::MainWindow(QWidget *parent) :
    // I kinda regret using the same name for namespace "history" and member variable "history".
    // But it's only a problem since C++ lacks pervasive `self`.
    QMainWindow(parent)
{}


// # MainWindow components

using cmd_queue::CommandQueue;
using cmd_queue::AudioCommand;

constexpr int MAX_BEATS_PER_PATTERN = 256;

struct MainWindowUi : MainWindow {
    using MainWindow::MainWindow;

    // Use raw pointers since QObjects automatically destroy children.

    TimelineEditor * _timeline_editor;

    struct Timeline {
        QAction * add_row;
        QAction * remove_row;

        QAction * move_up;
        QAction * move_down;

        QAction * clone_row;
    } _timeline;

    // Global state (view)
    QCheckBox * _follow_playback;
    QCheckBox * _compact_view;

    // Per-song ephemeral state
    QSpinBox * _zoom_level;

    // Song options
    QSpinBox * _ticks_per_beat;
    QSpinBox * _beats_per_measure;
    QComboBox * _end_action;
    QSpinBox * _end_jump_to;

    // TODO rework settings GUI
    QSpinBox * _length_beats;

    // Global state (editing)
    QSpinBox * _octave;
    QSpinBox * _step;
    QCheckBox * _step_to_event;
    QCheckBox * _overflow_paste;
    QCheckBox * _key_repeat;

    PatternEditorPanel * _pattern_editor_panel;

    /// Output: _pattern_editor_panel.
    void setup_widgets() {

        auto main = this;

        // TODO move to main or GuiApp.
        IconToolBar::setup_icon_theme();

        {main__tb(IconToolBar(false));  // No button borders
            tb->setFloatable(false);

            // View options.
            tb->addWidget([this] {
                auto w = _follow_playback = new QCheckBox;
                w->setChecked(true);
                w->setEnabled(false);
                w->setText(tr("Follow playback"));
                return w;
            }());

            tb->addWidget([this] {
                auto w = _compact_view = new QCheckBox;
                w->setEnabled(false);
                w->setText(tr("Compact view"));
                return w;
            }());

            // TODO add zoom checkbox
        }

        {main__central_c_l(QWidget, QVBoxLayout);
            l->setContentsMargins(0, 0, 0, 0);

            // Top panel.
            setup_panel(l);

            // Pattern view.
            {l__c_l(QFrame, QVBoxLayout);
                c->setFrameStyle(int(QFrame::StyledPanel) | QFrame::Sunken);

                c->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
                l->setContentsMargins(0, 0, 0, 0);
                {l__w(PatternEditorPanel(this));
                    w->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
                    _pattern_editor_panel = w;
                }
            }
        }
    }

    static constexpr int MAX_ZOOM_LEVEL = 64;

    void setup_panel(QBoxLayout * l) { {  // needed to allow shadowing
        l__c_l(QWidget, QHBoxLayout);
        c->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        // Timeline editor.
        {l__c_l(QGroupBox, QVBoxLayout)
            c->setTitle(tr("Timeline"));
            {l__w_factory(TimelineEditor::make(this))
                _timeline_editor = w;
                // w->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
            }
            {l__w(IconToolBar(true))  // Show button borders.
                _timeline.add_row = w->add_icon_action(
                    tr("Add Timeline Row"), "document-new"
                );
                _timeline.remove_row = w->add_icon_action(
                    tr("Delete Timeline Row"), "edit-delete"
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
            }
        }

        // Song options.
        {l__l(QVBoxLayout);

            // Song settings
            {l__c_form(QGroupBox, QFormLayout);
                c->setTitle(tr("Song"));

                // Large values may cause issues on 8-bit processors.
                form->addRow(
                    tr("Ticks/beat"),
                    [this] {
                        auto w = _ticks_per_beat = new QSpinBox;
                        w->setRange(1, doc::MAX_TICKS_PER_BEAT);
                        return w;
                    }()
                );

                // Purely cosmetic, no downside to large values.
                form->addRow(
                    tr("Beats/measure"),
                    [this] {
                        auto w = _beats_per_measure = new QSpinBox;
                        w->setRange(1, MAX_BEATS_PER_PATTERN);
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

                    {l__w(QSpinBox);
                        _end_jump_to = w;
                        w->setEnabled(false);
                        w->setRange(0, doc::MAX_GRID_CELLS - 1);
                    }
                }
            }

            // TODO rework settings GUI
            {l__c_form(QGroupBox, QFormLayout);
                c->setTitle(tr("Timeline row"));

                form->addRow(
                    new QLabel(tr("Length (beats)")),
                    [this] {
                        auto w = _length_beats = new QSpinBox;
                        w->setRange(1, MAX_BEATS_PER_PATTERN);
                        w->setValue(16);
                        return w;
                    }()
                );
            }
        }

        // Pattern editing.
        {l__c_form(QGroupBox, QFormLayout);
            c->setTitle(tr("Note entry"));

            {form__label_w(tr("Octave"), QSpinBox);
                _octave = w;

                int gui_bottom_octave =
                    get_app().options().note_names.gui_bottom_octave;
                int peak_octave = ceildiv(
                    doc::CHROMATIC_COUNT - 1, doc::NOTES_PER_OCTAVE
                );
                w->setRange(gui_bottom_octave, gui_bottom_octave + peak_octave);
            }

            {form__label_w(tr("Step"), QSpinBox);
                _step = w;
                w->setRange(0, 256);
            }

            {form__w(QCheckBox(tr("Step to event")));
                _step_to_event = w;
            }

            {form__w(QCheckBox(tr("Overflow paste")));
                _overflow_paste = w;
                w->setEnabled(false);
            }

            {form__w(QCheckBox(tr("Key repetition")));
                _key_repeat = w;
                w->setEnabled(false);
            }

            {form__label_w(tr("Zoom"), QSpinBox);
                _zoom_level = w;
                w->setRange(1, MAX_ZOOM_LEVEL);
            }
        }
    } }
};

using gui::history::History;

class AudioComponent {
    // GUI/audio communication.
    AudioState _audio_state = AudioState::Stopped;
    CommandQueue _command_queue{};

    // fields
    History _history;

    // Audio.
    RtAudio _rt{};
    unsigned int _curr_audio_device{};

    // Points to History and CommandQueue, must be listed after them.
    std::optional<AudioThreadHandle> _audio_handle;

    // impl
public:
    AudioComponent(doc::Document document) : _history(std::move(document)) {
    }

    AudioState audio_state() const {
        return _audio_state;
    }

    History & history() {
        return _history;
    }

    doc::Document const& get_document() const {
        return _history.get_document();
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
    void setup_audio() {
        // TODO should this be handled by the constructor?
        // Initializes _curr_audio_device.
        scan_devices();

        release_assert(_command_queue.is_empty());

        // Begin playing audio. Destroying this variable makes audio stop.
        _audio_handle = AudioThreadHandle::make(
            _rt, _curr_audio_device, get_document().clone(), stub_command()
        );
    }

    void restart_audio_thread() {
        // Only one stream can be running at a time.
        // The lifetimes of the old and new audio thread must not overlap.
        // So destroy the old before constructing the new.
        _audio_handle = {};  // joins on audio thread

        _audio_state = AudioState::Stopped;
        _command_queue.clear();

        _audio_handle = AudioThreadHandle::make(
            _rt, _curr_audio_device, get_document().clone(), stub_command()
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

    void play_pause(StateComponent & state) {
        if (_audio_handle.has_value()) {
            gc_command_queue();

            if (_audio_state == AudioState::Stopped) {
                auto cursor = state._cursor->y;
                cursor.beat = 0;
                play_from(state, cursor);
            } else {
                stop_play();
            }
        }
    }

    void play_from_row(StateComponent & state) {
        if (_audio_handle.has_value()) {
            gc_command_queue();

            if (_audio_state == AudioState::Stopped) {
                play_from(state, state._cursor->y);
            } else {
                stop_play();
            }
        }
    }

private:
    void play_from(StateComponent & state, GridAndBeat time) {
        _command_queue.push(cmd_queue::SeekTo{time});
        _audio_state = AudioState::Starting;

        // Move cursor to right spot, while waiting for audio thread to respond.
        state._cursor.set_y(time);
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
        StateComponent & state, edit::EditBox command, MoveCursor cursor_move
    ) {
        send_edit(*this, command->box_clone());

        edit::MaybeCursor before_cursor;
        edit::MaybeCursor after_cursor;
        edit::Cursor const here = *state._cursor;

        auto p = &cursor_move;
        if (std::get_if<MoveCursor_::NotPatternEdit>(p)) {
            before_cursor = {};
            after_cursor = {};
        } else
        if (std::get_if<MoveCursor_::AdvanceDigit>(p)) {
            before_cursor = here;
            after_cursor = here;
        } else
        if (auto move_from = std::get_if<MoveCursor_::MoveFrom>(p)) {
            before_cursor = move_from->before_or_here.value_or(here);
            after_cursor = move_from->after_or_here.value_or(here);
        }

        _history.push(edit::CursorEdit{
            std::move(command), before_cursor, after_cursor
        });

        if (after_cursor) {
            // Does not emit cursor_moved()!
            state._cursor.set_internal(*after_cursor);
        }

        if (std::get_if<MoveCursor_::AdvanceDigit>(p)) {
            state._cursor.advance_digit();
        } else {
            state._cursor.reset_digit();
        }
    }

    bool undo(StateComponent & state) {
        if (auto cursor_edit = _history.get_undo()) {
            send_edit(*this, std::move(cursor_edit->edit));
            if (cursor_edit->before_cursor) {
                state._cursor.set(*cursor_edit->before_cursor);
            }
            _history.undo();
            return true;
        }
        return false;
    }

    bool redo(StateComponent & state) {
        if (auto cursor_edit = _history.get_redo()) {
            send_edit(*this, std::move(cursor_edit->edit));
            if (cursor_edit->after_cursor) {
                state._cursor.set(*cursor_edit->after_cursor);
            }
            _history.redo();
            return true;
        }
        return false;
    }
};


// module-private
class MainWindowImpl : public MainWindowUi {
     W_OBJECT(MainWindowImpl)
public:
    // GUI widgets/etc.
    QScreen * _screen;
    QTimer _gui_refresh_timer;

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

    // impl MainWindow
    void _() override {}

    doc::Document const& get_document() const {
        return _audio.get_document();
    }

    AudioState audio_state() const override {
        return _audio.audio_state();
    }

    /// Called after edit/undo/redo, which are capable of deleting the timeline row
    /// we're currently in.
    ///
    /// You should call update_widgets() afterwards.
    void clamp_cursor() {
        doc::Document const& document = get_document();

        auto cursor_y = _cursor->y;
        auto ngrid = doc::GridIndex(document.timeline.size());

        if (cursor_y.grid >= ngrid) {
            cursor_y.grid = ngrid - 1;

            BeatFraction nbeats = document.timeline[cursor_y.grid].nbeats;
            cursor_y.beat = nbeats;
        }

        BeatFraction nbeats = document.timeline[cursor_y.grid].nbeats;

        // If cursor is out of bounds, move to last row in pattern.
        if (cursor_y.beat >= nbeats) {
            auto rows_per_beat = _pattern_editor_panel->zoom_level();

            BeatFraction rows = nbeats * rows_per_beat;
            int prev_row = util::math::frac_prev(rows);
            cursor_y.beat = BeatFraction{prev_row, rows_per_beat};
        }

        // Does NOT emit cursor_moved().
        _cursor.get_internal().y = cursor_y;
    }

    void push_edit(edit::EditBox command, MoveCursor cursor_move) override {
        // Never emits cursor_moved().
        _audio.push_edit(*this, std::move(command), cursor_move);
        clamp_cursor();

        // So run this instead, which is equivalent
        // (except immediate instead of queued).
        update_widgets();
    }

    // private methods
    MainWindowImpl(doc::Document document, QWidget * parent)
        : MainWindowUi(parent)
        , _audio(std::move(document))
    {
        // Setup GUI.
        setup_widgets();  // Output: _pattern_editor_panel.
        _pattern_editor_panel->set_history(_audio.history());
        _timeline_editor->set_history(_audio.history());

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
                if (_follow_playback->isChecked()) {
                    if (_cursor->y != play_time) {
                        _cursor.set_y(play_time);
                    }
                }

                // TODO write to _play_time field (even if cursor doesn't follow
                // playback), and redraw audio/timeline editor.
            }
        );

        // When the cursor moves, redraw all editors.
        // Qt::QueuedConnection has "transactional" semantics;
        // the callback will only run once the mutating function returns
        // to the event loop, preventing intermediate states.
        connect(
            &_cursor,
            &CursorAndSelection::cursor_moved,
            this,
            &MainWindowImpl::update_widgets,
            Qt::QueuedConnection
        );

        setup_screen();
        // TODO setup_screen() when primaryScreen changed
        // TODO setup_timer() when refreshRate changed

        _audio.setup_audio();

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
        _pattern_editor_panel->setFocus();

        auto pattern_setter = [this] (auto method) {
            return std::bind_front(method, _pattern_editor_panel);
        };

        auto connect_spin = [](QSpinBox * spin, auto target, auto func) {
            connect(
                spin,
                qOverload<int>(&QSpinBox::valueChanged),
                target,
                func,
                Qt::UniqueConnection
            );
        };

        auto connect_check = [](QCheckBox * check, auto target, auto func) {
            // QCheckBox::clicked is not emitted when state is programmatically changed.
            // idk which is better.
            connect(
                check,
                &QCheckBox::toggled,
                target,
                func,
                Qt::UniqueConnection
            );
        };

        // Previously, BIND_SPIN(name) would use _##NAME to synthesize the field _name.
        // However, this means searching for _name won't find the usage,
        // making it hard to navigate the code.
        // So supply the name twice, once like a _field and once like a method.
        #define BIND_SPIN(FIELD, METHOD) \
            FIELD->setValue(_pattern_editor_panel->METHOD()); \
            connect_spin( \
                FIELD, \
                _pattern_editor_panel, \
                pattern_setter(&PatternEditorPanel::set_##METHOD) \
            );

        #define BIND_CHECK(FIELD, METHOD) \
            FIELD->setChecked(_pattern_editor_panel->METHOD()); \
            connect_check( \
                FIELD, \
                _pattern_editor_panel, \
                pattern_setter(&PatternEditorPanel::set_##METHOD) \
            );

        BIND_SPIN(_zoom_level, zoom_level)

        auto gui_bottom_octave = [] () {
            return get_app().options().note_names.gui_bottom_octave;
        };

        // Visual octave: add offset.
        _octave->setValue(_pattern_editor_panel->octave() + gui_bottom_octave());

        // MIDI octave: subtract offset.
        connect_spin(
            _octave,
            _pattern_editor_panel,
            [this, gui_bottom_octave] (int octave) {
                _pattern_editor_panel->set_octave(octave - gui_bottom_octave());
            }
        );

        BIND_SPIN(_step, step)
        BIND_CHECK(_step_to_event, step_to_event);

        // _ticks_per_beat obtains its value through update_gui_from_doc().
        connect_spin(_ticks_per_beat, this, [this] (int ticks_per_beat) {
            push_edit(
                edit_doc::set_ticks_per_beat(ticks_per_beat),
                MoveCursor_::NotPatternEdit{}
            );
        });

        // TODO connect _length_beats to edit_doc::set_timeline_row_length()
        connect_spin(_length_beats, this, [this] (int grid_length_beats) {
            push_edit(
                edit_doc::set_grid_length(_cursor->y.grid, grid_length_beats),
                MoveCursor_::NotPatternEdit{}
            );
        });

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
        update_widgets();
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
            _pattern_editor_panel->addAction(&action);
        };

        auto connect_action = [this] (QAction & action, auto /*copied*/ func) {
            connect(&action, &QAction::triggered, this, func, Qt::UniqueConnection);
        };

        #define BIND_FROM_CONFIG(NAME) \
            _##NAME.setShortcut(QKeySequence{shortcuts.NAME}); \
            bind_editor_action(_##NAME)

        BIND_FROM_CONFIG(play_pause);
        connect_action(_play_pause, [this] () {
            _audio.play_pause(*this);
            update_widgets();
        });

        BIND_FROM_CONFIG(play_from_row);
        connect_action(_play_from_row, [this] () {
            _audio.play_from_row(*this);
            update_widgets();
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
            _audio.restart_audio_thread();
        });
    }

    // # Updating GUI from document

    /// To avoid tricky stale-GUI bugs,
    /// update the whole UI on any change to cursor or document.
    ///
    /// (how does reactivity work lol, and how do i get it)
    void update_widgets() {
        _pattern_editor_panel->update();  // depends on _cursor and _history

        // TODO find a less hacky way to update item count
        _timeline_editor->set_history(_audio.history());
        _timeline_editor->update_cursor();

        doc::Document const& doc = get_document();

        {auto b = QSignalBlocker(_ticks_per_beat);
            _ticks_per_beat->setValue(doc.sequencer_options.ticks_per_beat);
        }

        {auto b = QSignalBlocker(_length_beats);
            auto nbeats = frac_floor(doc.timeline[_cursor->y.grid].nbeats);
            _length_beats->setValue(nbeats);
        }
    }

    // # Mutation methods, called when QAction are triggered.

    void undo() {
        if (_audio.undo(*this)) {
            clamp_cursor();
            update_widgets();
        }
    }

    void redo() {
        if (_audio.redo(*this)) {
            clamp_cursor();
            update_widgets();
        }
    }

    void add_timeline_row() {
        auto & document = get_document();
        if (document.timeline.size() >= doc::MAX_GRID_CELLS) {
            return;
        }

        // Don't use this for undo.
        // If you do, "add, undo, add" will differ from "add".
        Cursor new_cursor = {
            .x = _cursor->x,
            .y = {
                .grid = _cursor->y.grid + 1,
                .beat = 0,
            },
        };

        push_edit(
            edit_doc::add_timeline_row(
                document, _cursor->y.grid + 1, _length_beats->value()
            ),
            move_to(new_cursor)
        );
    }

    void remove_timeline_row() {
        // The resulting cursor is invalid if you delete the last row.
        // clamp_cursor() will fix it.
        Cursor new_cursor = {
            .x = _cursor->x,
            .y = {
                .grid = _cursor->y.grid,
                .beat = 0,
            },
        };

        auto & document = get_document();
        if (document.timeline.size() <= 1) {
            return;
        }

        push_edit(edit_doc::remove_timeline_row(_cursor->y.grid), move_to(new_cursor));
    }

    void move_grid_up() {
        if (_cursor->y.grid.v > 0) {
            auto up = *_cursor;
            up.y.grid--;
            push_edit(edit_doc::move_grid_up(_cursor->y.grid), move_to(up));
        }
    }

    void move_grid_down() {
        auto & document = get_document();
        if (_cursor->y.grid + 1 < document.timeline.size()) {
            auto down = *_cursor;
            down.y.grid++;
            push_edit(edit_doc::move_grid_down(_cursor->y.grid), move_to(down));
        }
    }

    void clone_timeline_row() {
        auto & document = get_document();
        if (document.timeline.size() >= doc::MAX_GRID_CELLS) {
            return;
        }

        // Right now the clone button keeps the cursor position.
        // Should it move the cursor down by 1 pattern, into the clone?
        // Or down to the beat 0 of the clone?
        push_edit(
            edit_doc::clone_timeline_row(document, _cursor->y.grid), keep_cursor()
        );
    }
};

W_OBJECT_IMPL(MainWindowImpl)

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
