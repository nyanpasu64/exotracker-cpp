#include "main_window.h"
#include "gui/pattern_editor/pattern_editor_panel.h"
#include "gui/move_cursor.h"
#include "lib/layout_macros.h"
#include "gui_common.h"
#include "cmd_queue.h"
#include "edit/edit_doc.h"
#include "util/release_assert.h"
#include "util/math.h"

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
#include <QToolBar>
// Layouts
#include <QBoxLayout>
#include <QFormLayout>
// Other
#include <QAction>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <algorithm>  // std::min/max
#include <chrono>
#include <functional>  // reference_wrapper
#include <iostream>
#include <optional>
#include <stdexcept>  // logic_error

namespace gui::main_window {

using std::unique_ptr;
using std::make_unique;

using gui::pattern_editor::PatternEditorPanel;
using doc::BeatFraction;
using util::math::ceildiv;
using edit::edit_doc::set_ticks_per_beat;


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
    using doc::SeqEntryIndex;
    using cursor::ColumnIndex;

    Selection select = get_select();
    SeqEntryIndex top_seq = select.top.seq_entry_index;
    SeqEntryIndex bottom_seq = select.bottom.seq_entry_index;

    // Unconditionally enable padding below bottom of selection.
    _bottom_padding = BeatFraction{1, rows_per_beat};

    auto select_block = [this, &document, col_to_nsubcol, top_seq, bottom_seq] (
        ColumnIndex left_col, ColumnIndex right_col
    ) {
        release_assert(left_col < col_to_nsubcol.size());
        release_assert(right_col < col_to_nsubcol.size());

        _begin.x = CursorX{left_col, 0};
        _begin.y = PatternAndBeat{top_seq, 0};

        _end.x = CursorX{right_col, col_to_nsubcol[right_col] - 1};
        _end.y = PatternAndBeat{
            bottom_seq, document.sequence[bottom_seq].nbeats - _bottom_padding
        };
    };

    if (_mode == SelectionMode::Normal) {
        // Select all sequence entries and channels the current selection occupies.
        _orig_left = select.left.column;
        _orig_right = select.right.column;

        select_block(_orig_left, _orig_right);
        _mode = SelectionMode::SelectChannels;

    } else if (_mode == SelectionMode::SelectChannels) {
        // Select all sequence entries the current selection occupies,
        // and all channels unconditionally.
        select_block(0, ColumnIndex(col_to_nsubcol.size() - 1));
        _mode = SelectionMode::SelectAll;

    } else if (_mode == SelectionMode::SelectAll) {
        // Select all sequence entries and channels the original selection occupied.
        select_block(_orig_left, _orig_right);
        _mode = SelectionMode::SelectChannels;
    }
}

cursor::Cursor const& CursorAndSelection::get() const {
    return _cursor;
}

cursor::Cursor const& CursorAndSelection::operator*() const {
    return _cursor;
}

cursor::Cursor const* CursorAndSelection::operator->() const {
    return &_cursor;
}

void CursorAndSelection::set(Cursor cursor) {
    _cursor = cursor;
    if (_select) {
        _select->set_end(_cursor);
    }
    reset_digit();
}

void CursorAndSelection::set_x(CursorX x) {
    _cursor.x = x;
    if (_select) {
        _select->set_end(_cursor);
    }
    reset_digit();
}

void CursorAndSelection::set_y(PatternAndBeat y) {
    _cursor.y = y;
    if (_select) {
        _select->set_end(_cursor);
    }
    reset_digit();
}

int CursorAndSelection::digit_index() const {
    return _digit;
}

int CursorAndSelection::advance_digit() {
    return ++_digit;
}

void CursorAndSelection::reset_digit() {
    _digit = 0;
}

std::optional<RawSelection> CursorAndSelection::raw_select() {
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
    _select = {};
    // TODO reset digit?
}


W_OBJECT_IMPL(MainWindow)

static MainWindow * instance;

MainWindow::MainWindow(QWidget *parent) :
    // I kinda regret using the same name for namespace "history" and member variable "history".
    // But it's only a problem since C++ lacks pervasive `self`.
    QMainWindow(parent)
{}

using cmd_queue::CommandQueue;
using cmd_queue::AudioCommand;

constexpr int MAX_BEATS_PER_PATTERN = 256;

struct MainWindowUi : MainWindow {
    using MainWindow::MainWindow;

    // Use raw pointers since QObjects automatically destroy children.

    // Global state (view)
    QCheckBox * _follow_playback;
    QCheckBox * _compact_view;

    // Per-song ephemeral state
    QSpinBox * _rows_per_beat;

    // Song options
    QSpinBox * _ticks_per_beat;
    QSpinBox * _beats_per_measure;
    QComboBox * _end_action;
    QSpinBox * _end_jump_to;

    // Order entry settings
    QSpinBox * _length_beats;

    // Global state (editing)
    QSpinBox * _octave;
    QSpinBox * _step;
    QCheckBox * _overflow_paste;
    QCheckBox * _key_repeat;

    PatternEditorPanel * _pattern_editor_panel;

    /// Output: _pattern_editor_panel.
    void setup_widgets() {

        auto main = this;

        {main__tb(QToolBar);
            tb->setFloatable(false);

            // View options.
            tb->addWidget([this] {
                auto w = _follow_playback = new QCheckBox;
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
            l->setMargin(0);

            // Top panel.
            setup_panel(l);

            // Pattern view.
            {l__c_l(QFrame, QVBoxLayout);
                c->setFrameStyle(int(QFrame::StyledPanel) | QFrame::Sunken);

                c->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
                l->setMargin(0);
                {l__w(PatternEditorPanel(this));
                    w->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
                    _pattern_editor_panel = w;
                }
            }
        }
    }

    void setup_panel(QBoxLayout * l) { {  // needed to allow shadowing
        l__c_l(QWidget, QHBoxLayout);
        c->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        struct OrderEditor : QLabel {
            using QLabel::QLabel;
            QSize sizeHint() const override {
                return QSize{256, 0};
            }
        };

        // Order editor.
        {l__w(OrderEditor(tr("pretend there's an\norder editor here")));
            w->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
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
                        w->setRange(0, doc::MAX_SEQUENCE_LEN - 1);
                    }
                }
            }

            // Order entry settings
            {l__c_form(QGroupBox, QFormLayout);
                c->setTitle(tr("Order entry"));

                form->addRow(
                    new QLabel(tr("Pattern length")),
                    [this] {
                        auto w = _length_beats = new QSpinBox;
                        w->setRange(1, MAX_BEATS_PER_PATTERN);
                        w->setEnabled(false);
                        return w;
                    }()
                );
            }
        }

        // Pattern editing.
        {l__c_form(QGroupBox, QFormLayout);
            c->setTitle(tr("Pattern editing"));

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

            {form__w(QCheckBox(tr("Overflow paste")));
                _overflow_paste = w;
                w->setEnabled(false);
            }

            {form__w(QCheckBox(tr("Key repetition")));
                _key_repeat = w;
                w->setEnabled(false);
            }

            {form__label_w(tr("Zoom"), QSpinBox);
                _rows_per_beat = w;
                w->setRange(1, 64);
            }
        }
    } }
};

// module-private
class MainWindowImpl : public MainWindowUi {
     W_OBJECT(MainWindowImpl)
public:
    // fields
    gui::history::History _history;

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

    // Global actions:
    QAction _restart_audio;

    /// This API is a bit too broad for my liking, but whatever.
    class AudioComponent {
        // GUI/audio communication.
        AudioState _audio_state = AudioState::Stopped;
        CommandQueue _command_queue;

    public:
        bool is_empty() const {
            return _command_queue.begin() == _command_queue.end();
        }

        /// Return a command to be sent to the audio thread.
        /// It ignores the command's contents,
        /// but monitors its "next" pointer for new commands.
        AudioCommand * stub_command() {
            return _command_queue.begin();
        }

        AudioState audio_state() const {
            return _audio_state;
        }

        void reset() {
            _command_queue.clear();
            _audio_state = AudioState::Stopped;
        }

        void play_pause(MainWindowImpl & win) {
            if (win._audio_handle.has_value()) {
                gc_command_queue(win._audio_handle.value());

                if (_audio_state == AudioState::Stopped) {
                    auto cursor = win._cursor->y;
                    cursor.beat = 0;
                    play_from(win, cursor);
                } else {
                    stop_play(win);
                }
            }
        }

        void play_from_row(MainWindowImpl & win) {
            if (win._audio_handle.has_value()) {
                gc_command_queue(win._audio_handle.value());

                if (_audio_state == AudioState::Stopped) {
                    play_from(win, win._cursor->y);
                } else {
                    stop_play(win);
                }
            }
        }

        void send_edit(MainWindowImpl & win, edit::EditBox command) {
            if (win._audio_handle.has_value()) {
                gc_command_queue(win._audio_handle.value());
                _command_queue.push(std::move(command));
            }
        }

    private:
        void play_from(MainWindowImpl & win, PatternAndBeat time) {
            _command_queue.push(cmd_queue::SeekTo{time});
            _audio_state = AudioState::Starting;

            // Move cursor to right spot, while waiting for audio thread to respond.
            win._cursor.set_y(time);
        }

        void stop_play([[maybe_unused]] MainWindowImpl & win) {
            _command_queue.push(cmd_queue::StopPlayback{});
            _audio_state = AudioState::Stopped;
        }

    public:
        void gc_command_queue(AudioThreadHandle & audio_handle) {
            // Every time GUI pushes an event, it moves _command_queue.end().
            // Once the audio thread is done processing events,
            // the GUI thread's next call to gc_command_queue()
            // will advance _command_queue.begin().
            // To run code once after the audio thread catches up on events,
            // check if we drain 1+ event, then end with an empty queue.

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
    };

    AudioComponent _audio_component;

    // Audio.
    RtAudio _rt;
    unsigned int _curr_audio_device;

    // Points to _history and _audio_component, must be listed after them.
    std::optional<AudioThreadHandle> _audio_handle;

    // Used to compute GUI redraw FPS. Currently unused.
    using Clock = std::chrono::steady_clock;
    Clock::time_point _prev_time;

    // impl MainWindow
    void _() override {}

    void on_startup(
        config::Options const& options, doc::Document const& document
    ) {
        // Upon application startup, pattern editor panel is focused.
        _pattern_editor_panel->setFocus();

        auto connect_spin = [&](QSpinBox * spin, auto target, auto func) {
            connect(
                spin,
                qOverload<int>(&QSpinBox::valueChanged),
                target,
                func,
                Qt::UniqueConnection
            );
        };

        auto pattern_setter = [this] (auto method) {
            return std::bind_front(method, _pattern_editor_panel);
        };

        #define BIND_SPIN(KEY) \
            _##KEY->setValue(_pattern_editor_panel->KEY()); \
            connect_spin( \
                _##KEY, \
                _pattern_editor_panel, \
                pattern_setter(&PatternEditorPanel::set_##KEY) \
            );

        BIND_SPIN(rows_per_beat)

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

        BIND_SPIN(step)

        // _ticks_per_beat obtains its value through update_gui_from_doc().
        connect_spin(_ticks_per_beat, this, [this] (int ticks_per_beat) {
            push_edit(set_ticks_per_beat(ticks_per_beat), {}, false);
        });

        update_gui_from_doc(document);
    }

    void update_gui_from_doc(doc::Document const& document) {
        {
            auto b = QSignalBlocker(_ticks_per_beat);
            _ticks_per_beat->setValue(document.sequencer_options.ticks_per_beat);
        }
    }

    std::optional<AudioThreadHandle> const & audio_handle() const override {
        return _audio_handle;
    }

    AudioState audio_state() const override {
        return _audio_component.audio_state();
    }

    void push_edit(
        edit::EditBox command,
        std::optional<Cursor> maybe_cursor,
        bool advance_digit = false
    ) override {
        _audio_component.send_edit(*this, command->box_clone());

        Cursor old_cursor = *_cursor;

        if (maybe_cursor) {
            _cursor.set(*maybe_cursor);
        }

        _history.push(edit::CursorEdit{std::move(command), old_cursor, *_cursor});
        if (advance_digit) {
            _cursor.advance_digit();
        } else {
            _cursor.reset_digit();
        }
    }

    void restart_audio_thread() override {
        // Only one stream can be running at a time.
        // The lifetimes of the old and new audio thread must not overlap.
        // So destroy the old before constructing the new.
        _audio_handle = {};
        _audio_component.reset();
        _audio_handle = AudioThreadHandle::make(
            _rt,
            _curr_audio_device,
            _history.get_document().clone(),
            _audio_component.stub_command()
        );
    }

    // private methods
    MainWindowImpl(doc::Document document, QWidget * parent)
        : MainWindowUi(parent)
        , _history{std::move(document)}
        , _rt{}
        , _audio_handle{}
    {
        // Setup GUI.
        setup_widgets();  // Output: _pattern_editor_panel.
        _pattern_editor_panel->set_history(_history);

        // Hook up refresh timer.
        connect(
            &_gui_refresh_timer, &QTimer::timeout,
            this, [this] () {
                MaybeSequencerTime maybe_seq_time{};

                if (_audio_handle.has_value()) {
                    auto & audio_handle = _audio_handle.value();

                    _audio_component.gc_command_queue(audio_handle);

                    if (_audio_component.audio_state() == AudioState::PlayHasStarted) {
                        maybe_seq_time = audio_handle.play_time();
                    }
                }

                emit gui_refresh(maybe_seq_time);
            }
        );
        setup_screen();
        // TODO setup_screen() when primaryScreen changed
        // TODO setup_timer() when refreshRate changed

        setup_audio();

        reload_shortcuts();
        // TODO reload_shortcuts() when shortcut keybinds changed

        // Last thing.
        on_startup(get_app().options(), _history.get_document());
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

    /// Output: _audio_device.
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

    /// Output: _audio_handle.
    void setup_audio() {
        // Initializes _audio_device.
        scan_devices();

        release_assert(_audio_component.is_empty());

        // Begin playing audio. Destroying this variable makes audio stop.
        _audio_handle = AudioThreadHandle::make(
            _rt,
            _curr_audio_device,
            _history.get_document().clone(),
            _audio_component.stub_command()
        );
    }

    void undo() {
        if (auto cursor_edit = _history.get_undo()) {
            _audio_component.send_edit(*this, std::move(cursor_edit->edit));
            _cursor.set(cursor_edit->before_cursor);
            _history.undo();
            update_gui_from_doc(_history.get_document());
            _pattern_editor_panel->update();  // depends on _cursor and _history
        }
    }

    void redo() {
        if (auto cursor_edit = _history.get_redo()) {
            _audio_component.send_edit(*this, std::move(cursor_edit->edit));
            _cursor.set(cursor_edit->after_cursor);
            _history.redo();
            update_gui_from_doc(_history.get_document());
            _pattern_editor_panel->update();  // depends on _cursor and _history
        }
    }

    /// Clears existing bindings and rebinds shortcuts.
    /// Can be called multiple times.
    void reload_shortcuts() {
        auto & shortcuts = get_app().options().global_keys;

        auto bind_editor_action = [this] (QAction & action) {
            action.setShortcutContext(Qt::WidgetWithChildrenShortcut);

            // "A QWidget should only have one of each action and adding an action
            // it already has will not cause the same action to be in the widget twice."
            _pattern_editor_panel->addAction(&action);
        };

        auto connect_action = [this] (QAction & action, auto /*copied*/ func) {
            connect(&action, &QAction::triggered, this, func, Qt::UniqueConnection);
        };

        _play_pause.setShortcut(QKeySequence{shortcuts.play_pause});
        bind_editor_action(_play_pause);
        connect_action(_play_pause, [this] () {
            _audio_component.play_pause(*this);
            _pattern_editor_panel->update();  // this->update() works too.
        });

        _play_from_row.setShortcut(QKeySequence{shortcuts.play_from_row});
        bind_editor_action(_play_from_row);
        connect_action(_play_from_row, [this] () {
            _audio_component.play_from_row(*this);
            _pattern_editor_panel->update();
        });

        _undo.setShortcuts(QKeySequence::Undo);
        bind_editor_action(_undo);
        connect_action(_undo, &MainWindowImpl::undo);

        _redo.setShortcuts(QKeySequence::Redo);
        bind_editor_action(_redo);
        connect_action(_redo, &MainWindowImpl::redo);

        _restart_audio.setShortcut(QKeySequence{Qt::Key_F12});
        _restart_audio.setShortcutContext(Qt::ShortcutContext::ApplicationShortcut);
        this->addAction(&_restart_audio);
        connect_action(_restart_audio, &MainWindow::restart_audio_thread);
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
