*Note that this document may not be fully up-to-date.*

## Code architecture

I have a [Google Drive folder of design notes](https://drive.google.com/drive/u/0/folders/15A1Td92HofO7KQ62QtuEDSmd4X1KKPAZ).

Code is stored in src/, dependencies in 3rdparty/.

Classes have member variables prefixed with a single underscore, like `_var`, to distinguish from locals.

## Build notes

### Windows

MSVC, Clang, and MinGW are supported. Clang compiles main.cpp very slowly because of CLI11 command-line handling. clang-cl is not supported because it doesn't understand all MSVC flags. Clang with GCC ABI works as of 2021-04, but is not regularly tested.

If you want an optimized build with .pdb debug symbols, compile under MSVC or Clang under Release (or RelWithDebInfo) configuration.

### Does RelWithDebugInfo slow down the binary?

On Windows, I compared compiler flags (compile_commands.json) between Release and RelWithDebugInfo. I found that enabling RelWithDebugInfo decreases optimization along with enabling debugging:

- MSVC: `/O2 /Ob2` to `/O2 /Ob1 -Zi`
    - binary increases from 881KB to 1165KB.
- Clang: `-O3` to `-O2 -g`
    - binary increases from 615KB to 1510KB.
- MinGW GCC: `-O3` to `-O3` (no change to optimization)
    - binary increases from 2MB to >20MB from symbols.

I modified CMakeLists.txt to enable release-build .pdb generation for both Clang and MSVC.

### Windows C++ runtime

Linking to static MSVCRT only increases binary size by around 200KB, and prevents "missing DLL" errors from users. However it interferes with windeployqt and causes problems when allocating/freeing memory in different modules.

Install the VS2019 redistributable at https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads#section-2.

### MinGW runtime

exotracker does not link to the MinGW runtime statically. Enabling it removes the need for DLLs, but increases EXE file size by around 900KB. In any case, MinGW binaries are larger even after stripping (904KB) and require larger MinGW Qt DLLs, so are unsuitable for dev builds or releases (regardless of static/dynamic runtime).

## Code style

I am following some Rust-inspired rules in the codebase.

- Polymorphic classes are non-copyable. Maybe non-polymorphic classes (not value structs) too.
- Mutable aliasing is disallowed, except when mandated by third-party libraries (Blip_Synth points to Blip_Buffer, nsfplay APU2 points to APU1).
- Self-referential structs are disallowed, except when mandated by third-party libraries (blip_buffer and rtaudio), in which case the copy and move constructors are disabled (or move constructor is manually fixed, in the case of Blip_Buffer).
- Inheritance and storing data in base classes is discouraged.

In audio code, "out parameters" are common since they don't require allocations. Heap allocations can take a long time, pool allocations (I don't know much) may be faster, writing into an existing buffer is free.

In function signatures, out parameters (mutable references primarily written to, not read from) are placed at the end of the argument list. This follows the [Google Style Guide](https://google.github.io/styleguide/cppguide.html#Output_Parameters).

## GUI settings and sync

There are two types of persistent settings, saved to disk. (Neither has been implemented yet.)

- Options are set in the options dialog. Changes from other program instances are not picked up when you open the options dialog, but when you explicitly press the "synchronize settings" button. Changes are written to disk/registry when the dialog is applied or closed.
- Persistent state items (cursor follows playback, overflow paste) are set in the main window, and written to disk when the program is closed.

Both types of settings are stored in the application rather than the window. That means they're shared among all open tracker windows in a given process.

If multiple processes are running at once (for example to open multiple app versions at the same time), they will each have their own copy of settings. I plan to handle this better than FamiTracker, where the last window to close saves settings, but still not as seamlessly as multiple windows in a single process. Instead of overwriting the entire config file each time, I will query the registry or a single config file, possibly using QSettings.

## GUI app/window objects

I plan to add an option to open multiple windows linked to a single process, sharing settings. This will change the behavior of keys like Ctrl+N, which will create a new module instead of erasing the open one. Trying to open multiple instances (even of different versions) will instead spawn a new window in the running process.

`class GuiApp : public QApplication` stores program-wide variables.

- Application settings and keybindings, edited through the settings menu.
- Recent files list.
- All persistent tracker-window state (like cursor-follow) is stored here. If each window had its own settings, we don't know which settings to save when closing the program.

`class MainWindow : public QMainWindow` stores variables specific to each window (holding a single tracker module).

- Cursor position
- Should cursor step distance be owned by MainWindow (which shows it) or PatternEditorPanel (which uses it)?
- Audio thread handles (RtAudio and synth) will be stored in MainWindow at first. If concurrent RtAudio instances don't work properly, I'll move it to GuiApp and only allow one module to play at a time.

Multi-tab support is not planned at the moment, since the primary purpose of multiple instances is to copy-paste data between modules. If I want to later hack in tabs, each tab could have its own MainWindow instance, but acting as a sub-widget in an actual container window.

## How are repaints triggered?

Propagating app updates in an imperative program is difficult. After experimenting with several complex ad-hoc repaint systems, I settled on a centralized `StateTransaction` type which is the only way to perform window/document mutations, tracks which GUI elements must be updated, and does so in `~StateTransaction()`.

I have a historical overview of past attempted approaches in a [Google doc](https://docs.google.com/document/d/1KYM8WJCIQc-U-7vUBlLcLzZOnvNdjpgpVQ1CDqz9QxA). This document is quite messy though.

## GUI drawing

QPainter has a highly imperative/stateful drawing API (`setPen()` and `setBrush()`).

QPainter's coordinates work as follows:

- The origin lies at the top left, +x points right, and +y points down.

- (0, 0) lies at the center of a pixel, not at fenceposts. I think this was a mistake, for the following reasons:

  - If you create a `QRect` from (0, 0) to (16, 16), drawing it takes up 17 pixels on-screen. QRect's size-based constructor sets the bottom-right endpoint by subtracting 1 from horizontal and vertical size.

  - `QPainter::fillRect(QPoint{0, 0}, QPoint{16, 16})` takes up 17 pixels on-screen.

  - `QPainter::fillRect(QPoint{0, 0}, QSize{16, 16})` takes up 16 pixels on-screen.

  - https://bugreports.qt.io/browse/QTBUG-38642

    > When you draw an outline at 10,10 20x20 the [outline] will span a rectangle which is [positioned] at 9.5, 9.5 spanning 21x21 pixels (assuming 1px wide pens). The interior of that rectangle is [positioned] at 10.5,10.5 19x19.

      This is so confusing.

  - https://bugreports.qt.io/browse/QTBUG-38653

- The Qt website pretends that QRect is defined in terms of grid intersections rather than pixels, leading to a confusing explanation. https://doc.qt.io/qt-5/qrect.html#coordinates

  - > We recommend that you use x() + width() and y() + height() to find the true bottom-right corner, and avoid right() and bottom(). Another solution is to use QRectF: The QRectF class defines a rectangle in the plane using floating point accuracy for coordinates, and the QRectF::right() and QRectF::bottom() functions do return the right and bottom coordinates.

I instead chose a mental model where coordinates lie at fenceposts/gridlines between pixels. When drawing an axis-aligned line 1 or more pixels thick, you can pick which side it lies on. I haven't figured out how to handle lines "centered" on pixels, or non-axis-aligned lines yet.

This makes it easier to allocate parts of the screen to draw channels with no overlaps or gaps between them. I can say that the first channel takes up horizontal region [0, 128] (where the endpoints lie between pixels, so inclusive/exclusive doesn't matter). This region includes pixel centers [0, 128) (including 0 but not 128).

As a result, I have defined several functions to implement my drawing model in terms of QPainter.

## Document/undo architecture

MainWindow and OverallSynth each keep their own copy of the document. Both threads can access their copy of the document without mutexes or locking of any kind. MainWindow doesn't store a document directly, but instead owns a `History` which stores undo/redo state.

Whenever the user edits the document, both copies need to be edited in sync. To achieve this, all document mutations are reified as "command objects", or subclasses of `edit::BaseEditCommand`. This exposes a *very* simple interface, summarized below:

```cpp
class BaseEditCommand {
public:
    virtual void apply_swap(doc::Document & document) = 0;
};
using EditBox = std::unique_ptr<BaseEditCommand>;
```

Every time the user performs an edit action, the program (GUI thread) calls a "constructor function" which takes a `Document const&` and parameters describing user input, and returning a `EditBox`.

The returned `EditBox` is copied. One copy is sent from the GUI to audio thread through a lock-free queue, where OverallSynth calls `apply_swap()` to apply the edit to the audio document. The other copy is kept by the GUI thread, which calls `apply_swap()` on the GUI document, then pushes the command onto the undo stack.

`apply_swap()` doesn't mutate the document directly, but instead swaps the command's contents with part of the document (like a pattern vector). This was designed to satisfy two requirements: the ability to undo/redo on the GUI thread, and wait-free operation on the audio thread.

### Undo/redo (GUI thread)

Once you apply a change on a document, you need to be able to revert the change. You can call apply_swap() repeatedly on the same document to repeatedly undo/redo the same action. Why does this work? After applying a `BaseEditCommand`, it holds the relevant parts of the document state from before the edit. If you call `apply_swap()` again, it acts as an undo operation, restoring the old version of the document and storing the new version. You can call it again to act as a redo operation.

### Wait-free operation (audio thread)

When the audio thread applies an edit, the function call cannot allocate or deallocate memory. I achieve this by swapping part of the document with the edit command. For example, I can replace an entire document pattern (a `std::vector` of events belonging to one channel) with a new version from the `EditBox`. The old pattern is placed into the `EditBox` instead of being deallocated (which is unbounded-time).

Once the audio thread processes a command, it cannot deallocate the command object itself, which may own vectors and other heap-allocated data. To achieve this, the audio thread's doesn't receive ownership over commands, only mutable references. The audio thread signals to the GUI thread which commands it's finished processing, so the GUI thread can destroy them.

### Examples (not all implemented in tracker yet)

An "insert note" function picks a single pattern in the document and creates a copy. It inserts a note in the proper spot in the copy, and returns an `EditBox` owning a `BaseEditCommand` subclass containing the edited pattern copy.

When you call `edit_box->apply_swap(document)`, the edit command swaps part of `edit_box` with part of `document`.

----

One useful property is that an "insert instrument" or "delete note/instrument/etc." function can return the exact same `BaseEditCommand` subclass (`ImplEditCommand<PatternEdit>`), and you only have to implement the subclass once. Each subclass doesn't care about the operation performed, only the portion of the document mutated (in this case, it stores a single pattern and its location).

----

An "add order entry at location" function returns an `EditBox` owning a different `BaseEditCommand` subclass, holding an `optional<order entry>` (containing a vector of owned patterns) and an `OrderEntryIndex = uint8_t`.

When you call `edit_box->apply_swap(document)`, since the `optional<order entry>` holds a value, the entry gets inserted into the order at the right index (which has a reserved capacity of 256 entries to avoid reallocation) and replaced with std::nullopt (which is harder in C++ than Rust).

When you call `edit_box->apply_swap(document)` a second time, since the `optional<order entry>` is empty, an entry gets removed from the order and placed into `edit_box`. This undoes the original insertion.

----

The "remove order entry at location" function can returns the same `BaseEditCommand` subclass, only initialized to be empty instead of holding a value. Then `apply_swap()` will remove and save the corresponding entry from the document.

## Pattern reuse philosophy

In FamiTracker, the list of valid patterns is not exposed to the user. You can often dredge up old patterns by manually changing pattern IDs in the frame editor. To get rid of these, you can ask FT to "Remove unused patterns". I have never had a use for dredging up old patterns, but apparently other people use this feature.

My goal in exploring alternative approaches is not file-size efficiency (deleting unused patterns when saving to disk), but searching for a mental model where patterns are (by default) used in only one place, and multiple uses of a single pattern is explicitly specified. My goals are clear warning signs when editing a pattern used in multiple places, and clear indications of a reused pattern's purpose (a name) and all usages.

## Timeline

The frame/order editor is replaced with a timeline editor, and its functionality is changed significantly.

The pattern grid structure from existing trackers is carried over (under the name of timeline rows and grid cells). Each timeline row has its own length which can vary between rows (like OpenMPT, unlike FamiTracker). Each timeline row holds one timeline cell (or grid cell) per channel. However, unlike patterns, timeline cells do not contain events directly, but through several layers of indirection.

A timeline cell can hold zero or more blocks, which carry a start and end time (in integer beats) and a pattern. These blocks have nonzero length, do not overlap in time, occur in increasing time order, and lie between 0 and the timeline cell's length (the last block's end time can take on a special value corresponding to "end of cell")[1].

Each block contains a single pattern, consisting of a list of events and an optional loop duration (in integer beats). The pattern starts playing when absolute time reaches the block's start time, and stops playing when absolute time reaches the block's end time. If the loop duration is set, whenever relative time (within the pattern) reaches the loop duration, playback jumps back to the pattern's begin. A block can cut off a pattern's events early when time reaches the block's end time (either the pattern's initial play or during a loop). However a block cannot start playback partway into a pattern (no plans to add support yet).

Eventually, patterns can be reused in multiple blocks at different times (and possibly different channels).

[1] If a user shrinks a timeline entry, it may cause an timed-end block to end past the cell, or an "end of cell" block to have a size ≤ 0. To prevent this from breaking the GUI or sequencer, `TimelineCellIter` clamps block end times to the end of the cell, and skips blocks beginning at or past the end of the cell. However, out-of-bounds events and blocks are still stored in the document. I have not decided how to indicate these to the user.

### Motivation

The timeline system is intended to allow treating the program like FamiStudio or a tracker, with timestamps encoded relative to current pattern/frame begin, and reuse at pattern-level granularity. If you try to enter a note/volume/effect in a region without a block in place, a block is automatically created in the current channel, filling all empty space available (up to an entire grid cell) (not implemented yet).

It is also intended to have a similar degree of flexibility as a DAW like Reaper (fine-grained block splitting and looping). The tradeoff is that because global timestamps are relative to grid cell begin, blocks are not allowed to cross grid cell boundaries (otherwise it would be painful to convert between block/pattern-relative and global timestamps).

### Implementation

The timeline code is implemented in `doc/timeline.h`. I added several helper classes.

`TimelineCellRef` and `TimelineCellRefMut` store a reference to a timeline cell, and the owning timeline row's length.

`TimelineChannelRef` and `TimelineChannelRefMut` store a reference to a `Timeline` (all grid cells, all channels), and a chip and channel value. Timelines are currently stored as `[grid] [chip, channel] TimelineCell` to make adding/removing grid cells easy. But `TimelineChannelRef` can be indexed `[grid] TimelineCell`, to simplify code (like sequencers and cursor movement) that interacts with multiple grid/timeline cells, but only one channel.

#### TimelineCellIter(Ref)

I added classes `TimelineCellIter` and `TimelineCellIterRef` to step through a timeline cell's blocks, and loop each block's patterns for as long as it's playing. These classes (which act like coroutines/generators) are constructed with a `TimelineCell` and its duration, and yield `PatternRef` objects until exhausted.

For each block in the cell, `TimelineCellIter(Ref)` will yield a `PatternRef` with the block's pattern either once (if the pattern doesn't loop), or once for each time the pattern loops within the block. The `PatternRef` stores the time the pattern plays within the grid cell, and a span (pointer, size) to the events that should be played (excluding all events past the block's end time, but currently not excluding events at the beginning).

To add support for starting playback partway through a pattern, a `PatternRef` would have to store a timestamp to subtract from all events when calculating the absolute time (relative to the grid cell) the events play at.

## Audio architecture

Design notes at https://docs.google.com/document/d/17g5wqgpUPWItvHCY-0eCaqZSNdVKwouYoGbu-RTAjfo .

There is only 1 audio thread, spawned by RtAudio and periodically running our callback. This is how OpenMPT works as well.

The audio system (`OverallSynth`) is driven by sound synthesis callbacks (operates on a pull model). Every time RtAudio calls the audio callback which calls `OverallSynth::synthesize_overall()`, `OverallSynth` calls `SpcResampler::resample()` (a wrapper around libsamplerate) to resample SNES-rate audio (oversampled to emulate the DAC and analog filter) to the host rate. `SpcResampler` repeatedly calls `OverallSynth::synthesize_tick_oversampled()` which advances one timer/engine tick at a time, until libsamplerate has enough input to generate the number of samples that RtAudio wants. The remaining input hangs around and is consumed in the next callback.

By contrast, FamiTracker's synth thread pushes to a queue with backpressure.

Alternatives to this design:

- exotracker's NES/Blip_Buffer implementation ran `OverallSynth` for partial ticks. That approach was both more complex to implement (required suspending emulation/synthesis midway through a timer tick), and requires asking the resampler for how many input samples are necessary to generate N output samples (which is impossible in resamplers aside from Blip_Buffer). The only "advantage" was the ability to process events midway through a tick, but I ended up not doing so because it could violate driver invariants (my code assumes that all events, like notes previews and settings changes, occur on a timer tick).
- Synthesizing audio in a separate "synth thread", and splitting into fixed-size chunks queued up and read by the "output thread", is unacceptable since it generates latency. (This is how FamiTracker works.) Even with a length-0 queue, the synth thread can run 1 audio block ahead of the output thread.

### RtAudio audio output

AudioThreadHandle sends audio to computer speakers, and is intended for GUI mode with concurrent editing of the document during playback. The GUI and audio thread don't share a document or use atomics/mutexes to synchronize access, but instead the audio thread owns its own copy of the document, and receives mutation commands from the GUI (search this doc for `BaseEditCommand` and `AudioCommand`).

I believe libopenmpt does not talk directly to an output device, but merely exposes a callback api with no knowledge of locks or an audio library (here, RtAudio). (OpenMPT allows simple edits to patterns without locks! Complex edits require locking though.) libopenmpt can be called via ffmpeg or foobar2000, which have their own non-speaker output mechanisms. IDK how OpenMPT's synth gets informed of structural changes to the document's tick rate or list of patterns or instruments, so it knows to invalidate state.

### Switching from PortAudio to RtAudio

In PortAudio, there's 1 RAII object to initialize PortAudio (`AutoSystem`), one object representing the session (`System`), one object per stream (`Stream`).

In RtAudio, the `RtAudio` object is both the interface to a list of devices, and stream connected to one device.

Example of RtAudio usage in OpenMPT: https://github.com/OpenMPT/openmpt/blob/07f8c29b37f2bf9696e2f5ffb6eef0152e7fd4cf/sounddev/SoundDeviceRtAudio.cpp#L139

### RtAudio mono issues

On some platforms (Windows WASAPI), sending a mono audio stream only sends sound to the left ear. In this situation, RtAudio does not upmix mono to stereo. Bug report at https://github.com/thestk/rtaudio/issues/243 .

RtAudio:

- On Windows (I think it's WASAPI but the device name doesn't say which API was used), a mono stream only plays in the left speaker.
- On Linux PulseAudio, a mono stream plays in both speakers.

PortAudio:

- On all platforms (Windows DirectSound, WASAPI, Linux ALSA), a mono stream plays in both speakers.
- In portaudio/src/hostapi/wasapi/pa_win_wasapi.c, `GetMonoToStereoMixer()` gets called whenever I open a WASAPI output stream in mono, and `PaWasapiSubStream` `monoMixer` gets called whenever I write to the stream. This means that portaudio has code to broadcast mono to stereo.

~~I have worked around this issue by upmixing mono to stereo myself in `OutputCallback::rtaudio_callback()`.~~ exotracker-snes outputs stereo audio natively, so this issue no longer affects us.

### Audio components

`OverallSynth` owns a list of sound chips: `vector<ChipIndex -> unique_ptr<ChipInstance subclass>>`. Which chips are loaded, and in what order, is determined by the current document's properties. Each `ChipInstance` subclass can appear more than once, allowing you to use the same chip multiple times (not possible on 0CC-FamiTracker).

Each `ChipInstance` handles the channels/drivers/synthesis internally, so `OverallSynth` doesn't need to know how many channels each chip has. This reduces the potential for logic errors and broken invariants (different parts of the program disagreeing about chip/channel layout).

`ChipInstance` subclasses (sound chip objects) are defined in `synth/nes_2a03.cpp` etc. The header (`synth/nes_2a03.h` etc.) exposes factory functions returning `unique_ptr<ChipInstance>` base-class pointers. All methods except `ChipInstance::run_chip_for()` are pure virtual (implemented in subclasses).

Data flows from doc.h (document) -> sequencer.h (notes each tick) -> nes_2a03_driver.h (register writes each tick) -> nes_2a03.h (sound).

----

Each `ChipInstance` subclass `(Chip)Instance` has an associated `(Chip)ChannelID` enum (or enum class), also found as `(Chip)Instance::ChannelID`, specifying which channels that chip has. `(Chip)Instance` owns a sequencer (`ChipSequencer<(Chip)Instance::ChannelID>`), driver `(Chip)Driver`, and sound chip emulator (from nsfplay).

Each sound chip module (`ChipInstance` subclass) can be run for a specific period of time (nclock: `ClockT`). The sound chip's synthesis callback (`ChipInstance::run_chip_for()`) is called by `OverallSynth`, and alternates running the synthesizer to generate audio for a duration in clock cycles (`ChipInstance::synth_run_clocks()`), and handling externally-imposed ticks or internally-timed register writes. On every tick (60 ticks/second), the synthesis callback returns, `OverallSynth` calls `ChipInstance::driver_tick()`, `ChipInstance`'s sound driver fetches event data from the sequencer and determines what registers to write to the chip (for new notes, instruments, vibrato, etc), and `OverallSynth` calls the synthesis callback again. On every register write, the synthesis callback writes to the registers of its sound chip (`ChipInstance::synth_write_memory()`) before resuming synthesis.

Audio generated from separate emulated sound chips is mixed linearly (addition or weighted average). Each sound chip returns premixed audio from all channels, instead of returning each channel's audio level. (Why? I use nsfplay to emulate sound chips; each nsfplay chip returns premixed audio from all channels. Also many Famicom chips mix their channels using nonlinear or time-division mixing.) Each sound chip can output audio by either writing to `OverallSynth._nes_blip` (type: `Blip_Buffer`), or writing to an temporary buffer passed into the sound chip callback.

Sound chip emulation (audio synthesis) will *not* be implemented in my future NSF driver (since it's handled by hardware or the NSF player/emulator).

----

The sequencer (`ChipSequencer<ChannelID>` holding many `ChannelSequencer`) is implemented in C++, and converts pattern data from "events placed at beats" into "events placed at ticks". This is a complex task because events are located at beat fractions, not entries in a fixed array (which other trackers use). However I have come up with a sequencer design implemented in C++, that should be portable to low-powered CPUs without multiplication and division units.

At a given tempo, all beats are assumed to be the same length, an integer number of ticks. In both NSF export and PC preview, exotracker converts all note positions from beat fractions into two parts, a non-negative integer part and a "fraction less than 1" part. During NSF export, each fractional part, or possibly (beat fraction, tick offset) tuple, will be converted to a unique integer index. During playback, the sequencer uses these integers to index into the current tempo's "timing table" (precomputed for each tempo during NSF export), determining the note's position within the current beat.

In the remainder of this section, I ignore NSF export and focus on PC playback. Each `ChannelSequencer` remembers its position in a document, but not what document it's playing (except as a performance optimization). Every call to `ChannelSequencer` will pass the latest document available as an argument (which changes every time the user edits the document's patterns or global options). Editing is not implemented yet, though.

`ChipSequencer<ChannelID>` is defined in `audio/synth/sequencer.h`. It owns `EnumMap<ChannelID, ChannelSequencer>` (one `ChannelSequencer` for every channel the current chip has).

----

Each chip's sound driver (`(Chip)Driver`) is called once per tick. It handles events from the sequencer, as well as volume envelopes and vibrato. It generates register writes once per tick (possibly with delays between channels). It will be implemented separately in C++ and NSF export.

Each `(Chip)Instance` subclass owns a `(Chip)Driver` owning several `(Chip)(Channel)Driver`, neither of which inherits from global interfaces. Drivers are only exposed to their owning instance, not to `OverallSynth` or beyond.

My current API provides no way for drivers to *read* from sound chips' address spaces. Channel drivers can use custom APIs to tell their owning chip driver how to handle chip-wide register writes: after all channel drivers are done running (and generating register writes), the chip driver knows the desired state of each channel and can generate more register writes. (For example, the 5B chip has a single 6-bit register telling the chip to enable/disable tone/noise for each of the 3 channels.)

j0CC handles this differently. CAPU (hardware chip synth) has a Read() method, but it's completely unused. The 5B uses static globals, rather than a ChipDriver class, to write chip-wide registers.

## GUI/audio communication

- struct SeekTo

  - messages: `vector<(chip id, channel id, volume/instr/effect state)>`
  - time: beat fraction

- struct AudioCommand

  - seek_or_stop: `optional<SeekTo>`
  - next: `atomic<AudioCommand*>` = nullptr

- impl AudioCommand

  - explicit AudioCommand(seek_or_stop: `optional<SeekTo>`)

    - ...

  - explicit AudioCommand(AudioCommand const &) = default

  - AudioCommand(AudioCommand &&) = default

  - REMOVED METHOD (clashes with field next):

    - AudioCommand * next() const

      - return next.load(acquire)

- class CommandQueue

  - Not exposed to audio thread. Audio thread is initialized with `AudioCommand *` and iterates via `next()`.
  - AudioCommand * _begin (non-null?)
  - AudioCommand * _end (non-null, may be equal)

- impl CommandQueue

  - priv fn init()
    - _begin = _end = new AudioCommand{{}}
  - priv fn destroy_all()
    - *Only run this when there are no live readers left.*
    - if (_begin == nullptr)  // moved-from state?
      - return
    - while (auto next = _begin->next.load(relaxed))
      - auto destroy = _begin
      - _begin = next
      - delete destroy
    - release_assert(_begin == _end)
    - delete _begin
    - // set _begin = _end = nullptr?
  - pub fn ~CommandQueue()
    - destroy_all()
  - pub fn clear()
    - destroy_all()
    - init()
  - pub AudioCommand const * begin()
    - Not thread-safe. The return value is sent to audio thread.
    - return _begin
  - pub AudioCommand const * end()
    - Not thread-safe.
    - return _end
  - pub fn push(AudioCommand elem)
    - auto new_end = new AudioCommand(std::move(elem))
    - _end->next.store(new_end, release)
      - Paired with synthesize_overall() load(acquire).
    - _end = new_end
  - pub fn pop()  // no return value because exception safety or something? what is exception safety help
    - release_assert(_begin != _end)
    - auto next = _begin->next.load(relaxed)
    - release_assert(next)
    - auto destroy = _begin
    - _begin = next
    - delete destroy

- gui

  - _playback_state: enum AudioState
    - Stopped
    - Starting
    - PlayHasStarted
  - _audio_queue: CommandQueue

- impl gui

  - fn clean_done() -> void
    - auto x = audio.seen_command()
    - while list.begin() != x
      - list.pop()
    - if (list.begin() == list.end())
      - // once GUI sees audio caught up on commands, it must see audio's new time.
      - if (_playback_state == Starting)
        - _playback_state = PlayHasStarted
  - fn start_stop
    - if _playback_state == Stopped
      - (eventually recall channel state and current speed)
      - _audio_queue.push(AudioCommand{cursor time})
      - _playback_state = Starting
    - else
      - _audio_queue.push(AudioCommand{{}})
      - _playback_state = Stopped
  - fn update
    - if _playback_state == PlayHasStarted
      - write cursor pos = audio.seq_time() if has_value()
  - fn on_arrow_key
    - if _playback_state != Stopped
      - early return
    - scroll i guess

- audio

  - _seen_command: `atomic<AudioCommand *>`
  - _seq_time: `atomic<maybe timestamp>` (value always present, ignored by GUI when GUI thinks not playing)

- impl audio

  - seen_command()  // Called on GUI thread
    - The audio thread ignores this command since it has already handled it.
    - We only look at the next pointer.
    - This wastes one command worth of memory, but not a big deal
    - return _seen_command.load(acquire)  // once GUI sees we've caught up on commands, it must see our new time.
  - seq_time()  // Called on GUI thread
    - return _seq_time.load(seq_cst)  // ehh... minimize latency 👌
  - new(AudioCommand *) -> audio  // Called on GUI thread
    - _seen_command.store(^, release)
    - _seq_time.store(suitable initial value, relaxed)
  - synthesize_overall()
    - auto seq_time = _seq_time.load(relaxed)
    - auto const orig_seq_time = seq_time
    - // Handle all commands we haven't seen yet.
    - auto cmd = _seen_command.load(relaxed)
    - auto const orig_cmd = cmd
    - while (auto next = cmd->next.load(acquire))
      - Paired with CommandQueue::push() store(release).
      - Handle command (*next). If we seek, set seq_time = nullopt.
      - cmd = next
    - (... synthesize audio. If a tick occurs, overwrite seq_time.)
    - if (seq_time != orig_seq_time)
      - _seq_time.store(seq_time, seq_cst)  // ehh... minimize latency 👌
    - if (cmd != orig_cmd)
      - _seen_command.store(cmd, release)  // once GUI sees we've caught up on commands, it must see our new time.

### Do we need memory barriers between constructing the synth and accessing it?

No.

On Linux, RtAudio uses pthread to create the audio thread.

- https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap04.html#tag_04_12
- https://stackoverflow.com/questions/24137964
- pthread_create acts as a memory barrier, so the synth object (as initialized by the main thread) will be fully seen by the audio thread.

On Windows, RtAudio uses CreateThread to create the audio thread.

- https://stackoverflow.com/questions/12363211
- > All writes prior to CreateThread are visible to the new thread.
- Apparently this is a de-facto standard.

On Mac... I have no clue.

## Rules for avoiding circular header inclusion

C++ headers malfunction when there is a `#include` cycle. I have designed some rules to prevent inclusion cycles (ensure topological sortability).

Abstract files (in order from common to derived). Each file can include all files listed above.

- ../*_common.h
- ./general_common.h
  - External libraries: none
- ./specific_common.h
  - External libraries: specific-related libraries
- ./specific/nested_common.h, if used
- ./specific/nested.h
- ./specific.h (public interface of . folder)
- ./general.h, if used
- ../app.cpp will interface mostly with ./specific.h (a public interface). I should try C++ modules.

----

Concrete files in the "audio" folder (in order from common to derived). Each file can include all files listed above.

- audio_common.h (general audio types and functions, like Amplitude = s16)
- output.h
  - Depends on output-related libraries (RtAudio).
- Application code depends on output.h.

and

- audio_common.h
- synth_common.h
  - Depends on synth-related libraries (blip_buf).
- synth/Nes2A03Synth.h
  - class audio::synth::Nes2A03Synth
- synth.h
- Application code depends on synth.h.

(There is an inconsistency where I place audio/audio_common.h within audio/, but audio/synth.h outside audio/synth/. This inconsistency is unfortunate, but I do not plan to change this.)

----

Includes will be ordered from most coupled to least coupled. This ensures that tightly coupled headers don't depend on incidental dependencies.

## Dependencies (3rdparty)

Dependencies may originate from Git repositories (either release/tag, or master), or release tarballs.

To import dependencies from Git repositories:

```sh
git subrepo clone <remote-url> [<subdir>]
```

This will clone the full repository. If you want partial repository clones (either excluding files, or only cloning subdirectories), I haven't figured out the best approach yet.

*Older discussion about Git dependencies:*

`git subtree` is useful in theory. In practice, it squashes the remote repo into a single commit, then performs a subtree merge (the remote repo becomes a subdir in exotracker's filesystem).

The issue is that rebasing and reapplying the squashed commit will write that commit into / instead of /3rdparty/repo/, whether I use plain rebase or `git rebase --rebase-merges`. Since [Git 2.24.0 (commit 917a319)](https://github.com/git/git/commit/917a319ea59c130a14cff7656537ba14f593568b), you can specify `git rebase --rebase-merges --strategy subtree`, which works, but is a complex and ugly incantation.

My new preference is to just squash the subtree merge commit out of existence, which simplifies the commit graph too. I lose the ability to `git subtree pull`, but that's unimportant since the upstreams I'm using barely move at all (except for gsl and immer).
