## Code architecture

I have a [Google Drive folder of design notes](https://drive.google.com/drive/u/0/folders/15A1Td92HofO7KQ62QtuEDSmd4X1KKPAZ).

Code is stored in src/, dependencies in 3rdparty/.

Classes have member variables prefixed with a single underscore, like `_var`, to distinguish from locals. Not all classes follow this convention yet (but they should eventually).

src/gui/history.h has `gui::history::History`. There is only 1 instance, and it owns tracker pattern state. The pattern editor and audio thread read from `History`.

## Code style

I am following some Rust-inspired rules in the codebase.

- Polymorphic classes are non-copyable. Maybe non-polymorphic classes (not value structs) too.
- Mutable aliasing is disallowed, except when mandated by third-party libraries (Blip_Synth points to Blip_Buffer, nsfplay APU2 points to APU1).
- Self-referential structs are disallowed, except when mandated by third-party libraries (blip_buffer and rtaudio), in which case the copy and move constructors are disabled (or move constructor is manually fixed, in the case of Blip_Buffer).
- Inheritance and storing data in base classes is discouraged.

In audio code, "out parameters" are common since they don't require allocations. Heap allocations can take a long time, pool allocations (I don't know much) may be faster, writing into an existing buffer is free.

In function signatures, out parameters (mutable references primarily written to, not read from) are placed at the end of the argument list. This follows the [Google Style Guide](https://google.github.io/styleguide/cppguide.html#Output_Parameters).

## GUI settings and sync

There are two types of persistent settings, saved to disk.

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

## Document architecture

The History class stores two copies of the current document, each located behind its own mutex. This operates analogously to double-buffered displays. Each can be designated as the front or back document. The "front document" is shown to the GUI and read by the audio thread, and never modified. The "back document" is not shown to the GUI or read by the audio thread, but mutated in response to user input.

The interesting part is in `DocumentStore::history_apply_change()`. When the user edits the document, the program runs the edit function on the back document, swaps the roles of the documents (page flip), then runs the edit function on the new back document (which the audio thread may still be holding its mutex, so the GUI thread may block). This results in O(2*edit size) edit time instead of O(entire document) per edit. One risk of this design is that the two copies may desync; one way that may happen is if `command.redo()` is not deterministic.

Each document is protected by a pseudo-rwlock (backed by a mutex) for two threads. The GUI does not acquire a lock when reading, but acquires a lock (exclusive reference) when writing. The audio thread acquires a lock when reading from the document; it cannot safely write, since the GUI can read simultaneously without locking the mutex.

Since Document is mutable, it should almost never be copied (except when initializing the double-buffer), since a deep copy would be very slow. As a result, you need to explicitly call `Document.clone()` when you make a copy. By contrast, copying an immutable persistent data structure (my previous design) is merely a pointer copy and atomic refcount increase.

## Document format (unfinished, subject to change)

In FamiTracker, the list of valid patterns is not exposed to the user. You can often dredge up old patterns by manually changing pattern IDs in the sequence table. You can ask FT to "Remove unused patterns".

In Exotracker, PatternID will be an opaque identifier not visible to the user. Non-shared patterns are shown on the GUI as a dash. Shared pattern are shown on the GUI as the SequenceIndex of its first usage. This will make "dredging up old patterns" impossible.

I have never had a use for FT's design. However apparently other people use this feature.

----

File-size efficiency (deleting unused patterns when saving to disk) was never my goal. The mental model is that patterns belong to frames, aka sequence entries.

There's no reason i couldn't implement both ft-style "sequence to pattern mapping" and filesystem-style "pattern IDs are hidden from the user" GUIs, on the same data format. And "delete unused patterns" would be a toggle option.

## Audio architecture

Design notes at https://docs.google.com/document/d/17g5wqgpUPWItvHCY-0eCaqZSNdVKwouYoGbu-RTAjfo .

There is only 1 audio thread, spawned by RtAudio and periodically running our callback. This is how OpenMPT works as well.

The audio system (`OverallSynth`) is driven by sound synthesis callbacks (operates on a pull model). Every time RtAudio calls the audio callback which calls `OverallSynth.synthesize_overall()`, OverallSynth synthesizes a fixed number of samples, using `EventQueue` to know when to trigger new ticks (frame or vblank).

By contrast, FamiTracker's synth thread pushes to a queue with backpressure.

Alternatives to this design:

- Synthesizing 1 tick of audio at a time from the callback thread is also an acceptable option.
- Synthesizing audio in a separate "synth thread", and splitting into fixed-size chunks queued up and read by the "output thread", is unacceptable since it generates latency. (This is how FamiTracker works.) Even with a length-0 queue, the synth thread can run 1 audio block ahead of the output thread.

### RtAudio audio output

AudioThreadHandle sends audio to computer speakers, and is intended for GUI mode with concurrent editing of the document during playback. AudioThreadHandle uses doc::GetDocument to handles audio/GUI locking and sends a raw `Document const &` to OverallSynth.

In the absence of concurrent editing, you can use OverallSynth directly and avoid locking and unlocking std::mutex.

This has precedent: I believe libopenmpt does not talk directly to an output device, but merely exposes a callback api with no knowledge of locks or an audio library (here, RtAudio). (OpenMPT allows simple edits to patterns without locks! Complex edits require locking though.) libopenmpt can be called via ffmpeg or foobar2000, which have their own non-speaker output mechanisms.

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

I have worked around this issue by upmixing mono to stereo myself in `OutputCallback::rtaudio_callback()`.

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
